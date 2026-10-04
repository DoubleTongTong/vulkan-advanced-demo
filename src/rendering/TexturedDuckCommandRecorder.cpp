#include "rendering/TexturedDuckCommandRecorder.h"

#include "ImageProcessor.h"
#include "mesh/ModelLoader.h"
#include "VulkanContext.h"
#include "VulkanProfiler.h"
#include "VulkanShaderModule.h"
#include "VulkanSwapchain.h"
#include "VulkanUtils.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <vk_mem_alloc.h>

#include <chrono>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace {

constexpr VkFormat DepthFormat = VK_FORMAT_D32_SFLOAT;
constexpr uint32_t MaxBindlessTextures = 16;
constexpr const char* SamplersResource = "kSamplers";
constexpr const char* TexturesResource = "kTextures2D";

glm::mat4 makeModelViewProjection(
    const RenderView& view,
    const float center[3],
    float radius) {
    using Clock = std::chrono::steady_clock;
    static const Clock::time_point startTime = Clock::now();

    const float seconds = std::chrono::duration<float>(Clock::now() - startTime).count();
    const float scale = 1.0f / radius;
    const float rotationAngle = glm::radians(35.0f) + seconds;
    const glm::mat4 model =
        glm::rotate(glm::mat4(1.0f), rotationAngle, glm::vec3(0.0f, 1.0f, 0.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(scale)) *
        glm::translate(glm::mat4(1.0f), glm::vec3(-center[0], -center[1], -center[2]));
    return view.viewProjection * model;
}

} // namespace

TexturedDuckCommandRecorder::TexturedDuckCommandRecorder(
    const VulkanContext& context,
    const VulkanSwapchain& swapchain,
    const VulkanShaderModule& vertexShader,
    const VulkanShaderModule& fragmentShader,
    const std::filesystem::path& scenePath,
    const std::filesystem::path& texturePath)
    : context_(context),
      swapchain_(swapchain),
      sceneData_(ModelLoader::loadFirstMesh(scenePath)),
      texture_(context, ImageProcessor().loadRgba8(texturePath), "Rubber duck base color texture"),
      textureDescriptors_(
          context,
          {
              .shaders = {&vertexShader, &fragmentShader},
              .runtimeArrays = {{TexturesResource, MaxBindlessTextures}},
              .debugName = "Textured duck bindless descriptors",
          }),
      vertexBuffer_(
          context,
          {
              .usage = BufferUsage_Vertex,
              .storage = BufferStorage::Device,
              .size = sceneData_.vertexBytes().size(),
              .data = sceneData_.vertexBytes().data(),
              .debugName = "Textured duck vertex buffer",
          }),
      indexBuffer_(
          context,
          {
              .usage = BufferUsage_Index,
              .storage = BufferStorage::Device,
              .size = sceneData_.indexBytes().size(),
              .data = sceneData_.indexBytes().data(),
              .debugName = "Textured duck index buffer",
          }),
      imageLayouts_(swapchain.images().size(), VK_IMAGE_LAYOUT_UNDEFINED) {
    const MeshBounds& bounds = sceneData_.bounds();
    std::copy_n(bounds.center, 3, meshCenter_);
    meshRadius_ = bounds.radius;
    indexCount_ = static_cast<uint32_t>(sceneData_.indices().size());

    if ((swapchain.imageUsage() & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
        throw std::runtime_error("Swapchain images do not support color attachment usage.");
    }

    textureDescriptors_.fillSamplers(SamplersResource, texture_.sampler());
    textureDescriptors_.writeTexture2D(TexturesResource, 0, texture_);
    createDepthAttachment();
    createPipeline(vertexShader, fragmentShader);
}

TexturedDuckCommandRecorder::~TexturedDuckCommandRecorder() {
    pipeline_.reset();
    destroyDepthAttachment();
}

void TexturedDuckCommandRecorder::createPipeline(
    const VulkanShaderModule& vertexShader,
    const VulkanShaderModule& fragmentShader) {
    pipeline_ = std::make_unique<VulkanRenderPipeline>(
        context_,
        RenderPipelineDesc{
            .vertexShader = &vertexShader,
            .fragmentShader = &fragmentShader,
            .vertexBindings = {
                {
                    .binding = 0,
                    .stride = sizeof(MeshVertex),
                    .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
                },
            },
            .vertexAttributes = {
                {
                    .location = 0,
                    .binding = 0,
                    .format = VK_FORMAT_R32G32B32_SFLOAT,
                    .offset = offsetof(MeshVertex, position),
                },
                {
                    .location = 1,
                    .binding = 0,
                    .format = VK_FORMAT_R32G32_SFLOAT,
                    .offset = offsetof(MeshVertex, uv),
                },
            },
            .colorFormat = swapchain_.imageFormat(),
            .depthFormat = DepthFormat,
            .cullMode = VK_CULL_MODE_BACK_BIT,
            .depthTestEnabled = true,
            .depthWriteEnabled = true,
            .descriptorSetLayouts = {textureDescriptors_.layout()},
            .debugName = "Textured duck pipeline",
        });
}

void TexturedDuckCommandRecorder::record(const RenderFrameContext& frame) {
    const VkCommandBuffer commandBuffer = frame.commandBuffer;
    const uint32_t imageIndex = frame.imageIndex;
    APP_PROFILE_FUNCTION();
    APP_PROFILE_GPU_ZONE(context_, commandBuffer, "Textured duck");
    const VkImage image = swapchain_.images()[imageIndex];
    const VkImageView imageView = swapchain_.imageViews()[imageIndex];
    const VkExtent2D extent = swapchain_.extent();

    vulkan_utils::transitionImage(
        commandBuffer,
        image,
        VK_IMAGE_ASPECT_COLOR_BIT,
        imageLayouts_[imageIndex],
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        0,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    vulkan_utils::transitionImage(
        commandBuffer,
        depthImage_,
        VK_IMAGE_ASPECT_DEPTH_BIT,
        depthLayout_,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        0,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);

    const VkClearValue clearColor{
        .color = {{0.45f, 0.45f, 0.45f, 1.0f}},
    };
    const VkClearValue clearDepth{
        .depthStencil = {1.0f, 0},
    };

    const VkRenderingAttachmentInfo colorAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = imageView,
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
        .renderArea = {
            .offset = {0, 0},
            .extent = extent,
        },
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
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    const VkRect2D scissor{
        .offset = {0, 0},
        .extent = extent,
    };
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    const VkBuffer vertexBuffers[] = {vertexBuffer_.handle()};
    const VkDeviceSize vertexOffsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, vertexOffsets);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer_.handle(), 0, VK_INDEX_TYPE_UINT32);

    pipeline_->bind(commandBuffer);
    textureDescriptors_.bind(commandBuffer, pipeline_->layout());

    struct PushConstants {
        glm::mat4 mvp;
        uint32_t textureId = 0;
    };
    const PushConstants pushConstants{
        .mvp = makeModelViewProjection(frame.view, meshCenter_, meshRadius_),
        .textureId = 0,
    };
    vkCmdPushConstants(
        commandBuffer,
        pipeline_->layout(),
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(PushConstants),
        &pushConstants);
    vkCmdDrawIndexed(commandBuffer, indexCount_, 1, 0, 0, 0);

    vkCmdEndRendering(commandBuffer);

    vulkan_utils::transitionImage(
        commandBuffer,
        image,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        0,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

    imageLayouts_[imageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    depthLayout_ = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
}

void TexturedDuckCommandRecorder::createDepthAttachment() {
    const VkExtent2D extent = swapchain_.extent();
    const VkImageCreateInfo imageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = DepthFormat,
        .extent = {
            .width = extent.width,
            .height = extent.height,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    const VmaAllocationCreateInfo allocationCreateInfo{
        .usage = VMA_MEMORY_USAGE_AUTO,
        .preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };

    vulkan_utils::checkVk(
        vmaCreateImage(
            context_.allocator(),
            &imageCreateInfo,
            &allocationCreateInfo,
            &depthImage_,
            &depthAllocation_,
            nullptr),
        "vmaCreateImage");
    context_.setDebugObjectName(
        VK_OBJECT_TYPE_IMAGE,
        reinterpret_cast<uint64_t>(depthImage_),
        "Textured duck depth image");

    const VkImageViewCreateInfo viewCreateInfo{
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
        vkCreateImageView(context_.device(), &viewCreateInfo, nullptr, &depthImageView_),
        "vkCreateImageView");
    context_.setDebugObjectName(
        VK_OBJECT_TYPE_IMAGE_VIEW,
        reinterpret_cast<uint64_t>(depthImageView_),
        "Textured duck depth image view");
}

void TexturedDuckCommandRecorder::destroyDepthAttachment() {
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
