#include "rendering/TessellatedDuckCommandRecorder.h"

#include "ImageProcessor.h"
#include "VulkanContext.h"
#include "VulkanFrameSync.h"
#include "VulkanProfiler.h"
#include "VulkanShaderModule.h"
#include "VulkanSwapchain.h"
#include "VulkanUtils.h"
#include "mesh/GeometryCache.h"

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <vk_mem_alloc.h>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace {

constexpr VkFormat DepthFormat = VK_FORMAT_D32_SFLOAT;
constexpr uint32_t MaxBindlessTextures = 16;
constexpr const char* SamplersResource = "kSamplers";
constexpr const char* TexturesResource = "kTextures2D";
constexpr const char* FrameResource = "kFrame";
constexpr const char* VerticesResource = "kVertices";

// 每个在途帧独占一份，避免 CPU 更新时覆盖 GPU 仍在读取的数据。
struct FrameData {
    glm::mat4 model{1.0f};
    glm::mat4 viewProjection{1.0f};
    glm::vec4 cameraPosition{0.0f};
    // x 保存 UI 控制的细分缩放，其余分量留作后续扩展。
    glm::vec4 tessellationParameters{1.0f, 0.0f, 0.0f, 0.0f};
};

std::vector<VulkanBuffer> makeFrameDataBuffers(const VulkanContext& context) {
    std::vector<VulkanBuffer> buffers;
    buffers.reserve(VulkanFrameSync::MaxFramesInFlight);
    for (uint32_t index = 0; index < VulkanFrameSync::MaxFramesInFlight; ++index) {
        const std::string name = "Tessellation frame data " + std::to_string(index);
        buffers.emplace_back(
            context,
            BufferDesc{
                .usage = BufferUsage_Storage,
                .storage = BufferStorage::HostVisible,
                .size = sizeof(FrameData),
                .debugName = name.c_str(),
            });
    }
    return buffers;
}

} // namespace

TessellatedDuckCommandRecorder::TessellatedDuckCommandRecorder(
    const VulkanContext& context,
    const VulkanSwapchain& swapchain,
    const VulkanShaderModule& vertexShader,
    const VulkanShaderModule& tessellationControlShader,
    const VulkanShaderModule& tessellationEvaluationShader,
    const VulkanShaderModule& geometryShader,
    const VulkanShaderModule& fragmentShader,
    const std::filesystem::path& scenePath,
    const std::filesystem::path& texturePath)
    : context_(context),
      swapchain_(swapchain),
      texture_(context, ImageProcessor().loadRgba8(texturePath), "Tessellated duck base color"),
      mesh_(context, GeometryCache::loadOrConvert(scenePath)),
      frameDataBuffers_(makeFrameDataBuffers(context)),
      textureDescriptors_(
          context,
          DescriptorSetDesc{
              .shaders = {&fragmentShader},
              .runtimeArrays = {{TexturesResource, MaxBindlessTextures}},
              .debugName = "Tessellated duck texture descriptors",
          }),
      imageLayouts_(swapchain.images().size(), VK_IMAGE_LAYOUT_UNDEFINED) {
    if ((swapchain.imageUsage() & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
        throw std::runtime_error("Swapchain images do not support color attachment usage.");
    }

    textureDescriptors_.fillSamplers(SamplersResource, texture_.sampler());
    textureDescriptors_.writeTexture2D(TexturesResource, 0, texture_);

    frameDescriptors_.reserve(frameDataBuffers_.size());
    for (size_t index = 0; index < frameDataBuffers_.size(); ++index) {
        auto descriptors = std::make_unique<VulkanDescriptorSet>(
            context_,
            DescriptorSetDesc{
                .shaders = {&vertexShader, &tessellationControlShader},
                .set = 1,
                .debugName = "Tessellation frame descriptors",
            });
        descriptors->writeStorageBuffer(FrameResource, frameDataBuffers_[index]);
        descriptors->writeStorageBuffer(VerticesResource, mesh_.vertexStorageBuffer());
        frameDescriptors_.push_back(std::move(descriptors));
    }

    const std::vector<VkDescriptorSetLayout> layouts{
        textureDescriptors_.layout(),
        frameDescriptors_.front()->layout(),
    };
    pipeline_ = std::make_unique<VulkanRenderPipeline>(
        context_,
        RenderPipelineDesc{
            .vertexShader = &vertexShader,
            .tessellationControlShader = &tessellationControlShader,
            .tessellationEvaluationShader = &tessellationEvaluationShader,
            .geometryShader = &geometryShader,
            .fragmentShader = &fragmentShader,
            .colorFormat = swapchain_.imageFormat(),
            .depthFormat = DepthFormat,
            .topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST,
            .patchControlPoints = 3,
            .cullMode = VK_CULL_MODE_BACK_BIT,
            .depthTestEnabled = true,
            .depthWriteEnabled = true,
            .depthCompareOp = VK_COMPARE_OP_LESS,
            .descriptorSetLayouts = layouts,
            .debugName = "Tessellated duck render pipeline",
        });
    createDepthAttachment();
}

TessellatedDuckCommandRecorder::~TessellatedDuckCommandRecorder() {
    pipeline_.reset();
    destroyDepthAttachment();
}

void TessellatedDuckCommandRecorder::record(const RenderFrameContext& frame) {
    if (frame.frameIndex >= frameDataBuffers_.size()) {
        throw std::out_of_range("Render frame index exceeds tessellation frame resources.");
    }

    APP_PROFILE_FUNCTION();
    APP_PROFILE_GPU_ZONE(context_, frame.commandBuffer, "Tessellated duck");

    // Vertex shader 负责模型变换，TES 再对已经变换好的控制点做插值。
    const FrameData frameData{
        .model = mesh_.normalizedModelMatrix(0.0f),
        .viewProjection = frame.view.viewProjection,
        .cameraPosition = glm::vec4(frame.view.cameraPosition, 1.0f),
        .tessellationParameters = {tessellationScale_, 0.0f, 0.0f, 0.0f},
    };
    frameDataBuffers_[frame.frameIndex].bufferSubData(0, sizeof(frameData), &frameData);

    const VkCommandBuffer commandBuffer = frame.commandBuffer;
    const uint32_t imageIndex = frame.imageIndex;
    const VkImage image = swapchain_.images().at(imageIndex);
    const VkExtent2D extent = swapchain_.extent();
    vulkan_utils::transitionImage(
        commandBuffer, image, VK_IMAGE_ASPECT_COLOR_BIT,
        imageLayouts_.at(imageIndex), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    vulkan_utils::transitionImage(
        commandBuffer, depthImage_, VK_IMAGE_ASPECT_DEPTH_BIT,
        depthLayout_, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);

    const VkClearValue clearColor{.color = {{0.92f, 0.92f, 0.92f, 1.0f}}};
    const VkClearValue clearDepth{.depthStencil = {1.0f, 0}};
    const VkRenderingAttachmentInfo colorAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = swapchain_.imageViews().at(imageIndex),
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = clearColor,
    };
    const VkRenderingAttachmentInfo depthAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = depthImageView_,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue = clearDepth,
    };
    const VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.offset = {0, 0}, .extent = extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = &depthAttachment,
    };
    vkCmdBeginRendering(commandBuffer, &renderingInfo);

    const VkViewport viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(extent.width),
        .height = static_cast<float>(extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    const VkRect2D scissor{.offset = {0, 0}, .extent = extent};
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    pipeline_->bind(commandBuffer);
    textureDescriptors_.bind(commandBuffer, pipeline_->layout());
    frameDescriptors_[frame.frameIndex]->bind(commandBuffer, pipeline_->layout());
    mesh_.bindIndexBuffer(commandBuffer);
    vkCmdDrawIndexed(commandBuffer, mesh_.indexCount(), 1, 0, 0, 0);
    vkCmdEndRendering(commandBuffer);

    vulkan_utils::transitionImage(
        commandBuffer, image, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    imageLayouts_[imageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    depthLayout_ = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
}

float TessellatedDuckCommandRecorder::tessellationScale() const {
    return tessellationScale_;
}

void TessellatedDuckCommandRecorder::setTessellationScale(float scale) {
    tessellationScale_ = std::clamp(scale, 0.5f, 2.0f);
}

void TessellatedDuckCommandRecorder::createDepthAttachment() {
    const VkExtent2D extent = swapchain_.extent();
    const VkImageCreateInfo imageInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = DepthFormat,
        .extent = {extent.width, extent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    const VmaAllocationCreateInfo allocationInfo{
        .usage = VMA_MEMORY_USAGE_AUTO,
        .preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };

    try {
        vulkan_utils::checkVk(
            vmaCreateImage(
                context_.allocator(), &imageInfo, &allocationInfo,
                &depthImage_, &depthAllocation_, nullptr),
            "vmaCreateImage");
        const VkImageViewCreateInfo viewInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = depthImage_,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = DepthFormat,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        vulkan_utils::checkVk(
            vkCreateImageView(context_.device(), &viewInfo, nullptr, &depthImageView_),
            "vkCreateImageView");
    } catch (...) {
        destroyDepthAttachment();
        throw;
    }

    context_.setDebugObjectName(
        VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(depthImage_),
        "Tessellated duck depth image");
    context_.setDebugObjectName(
        VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(depthImageView_),
        "Tessellated duck depth image view");
}

void TessellatedDuckCommandRecorder::destroyDepthAttachment() {
    if (depthImageView_) {
        vkDestroyImageView(context_.device(), depthImageView_, nullptr);
        depthImageView_ = VK_NULL_HANDLE;
    }
    if (depthImage_ && depthAllocation_) {
        vmaDestroyImage(context_.allocator(), depthImage_, depthAllocation_);
        depthImage_ = VK_NULL_HANDLE;
        depthAllocation_ = nullptr;
    }
}
