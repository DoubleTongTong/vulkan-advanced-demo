#include "rendering/InstancedMeshesCommandRecorder.h"

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

#include <cstdint>
#include <stdexcept>
#include <string>

namespace {

constexpr uint32_t MeshCount = 32 * 1024;
constexpr uint32_t ComputeLocalSize = 32;
constexpr uint32_t MaxBindlessTextures = 16;
constexpr const char* SamplersResource = "kSamplers";
constexpr const char* TexturesResource = "kTextures2D";
constexpr const char* InstancesResource = "kInstances";
constexpr const char* MatricesResource = "kMatrices";
constexpr const char* VerticesResource = "kVertices";

float random01(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float>(state) / static_cast<float>(UINT32_MAX);
}

VulkanBuffer makeInstances(const VulkanContext& context) {
    std::vector<glm::vec4> instances(MeshCount);
    uint32_t randomState = 0x87654321u;
    for (glm::vec4& instance : instances) {
        instance = {
            random01(randomState) * 1000.0f - 500.0f,
            random01(randomState) * 1000.0f - 500.0f,
            random01(randomState) * 1000.0f - 500.0f,
            random01(randomState) * 6.2831853f,
        };
    }

    return VulkanBuffer(
        context,
        {
            .usage = BufferUsage_Storage,
            .storage = BufferStorage::Device,
            .size = sizeof(glm::vec4) * instances.size(),
            .data = instances.data(),
            .debugName = "Instanced meshes positions and angles",
        });
}

std::vector<VulkanBuffer> makeMatrixBuffers(const VulkanContext& context) {
    std::vector<VulkanBuffer> buffers;
    buffers.reserve(VulkanFrameSync::MaxFramesInFlight);
    for (uint32_t index = 0; index < VulkanFrameSync::MaxFramesInFlight; ++index) {
        const std::string name = "Instanced mesh matrices " + std::to_string(index);
        buffers.emplace_back(
            context,
            BufferDesc{
                .usage = BufferUsage_Storage,
                .storage = BufferStorage::Device,
                .size = sizeof(glm::mat4) * MeshCount,
                .debugName = name.c_str(),
            });
    }
    return buffers;
}

} // namespace

InstancedMeshesCommandRecorder::InstancedMeshesCommandRecorder(
    const VulkanContext& context,
    const VulkanSwapchain& swapchain,
    const VulkanShaderModule& computeShader,
    const VulkanShaderModule& vertexShader,
    const VulkanShaderModule& fragmentShader,
    const std::filesystem::path& scenePath,
    const std::filesystem::path& texturePath)
    : context_(context),
      swapchain_(swapchain),
      texture_(context, ImageProcessor().loadRgba8(texturePath), "Instanced mesh base color"),
      mesh_(context, GeometryCache::loadOrConvert(scenePath)),
      instances_(makeInstances(context)),
      matrixBuffers_(makeMatrixBuffers(context)),
      textureDescriptors_(
          context,
          {
              .shaders = {&fragmentShader},
              .runtimeArrays = {{TexturesResource, MaxBindlessTextures}},
              .debugName = "Instanced mesh texture descriptors",
          }),
      startTime_(std::chrono::steady_clock::now()),
      depthAttachment_(context, swapchain.extent(), "Instanced mesh depth"),
      imageLayouts_(swapchain.images().size(), VK_IMAGE_LAYOUT_UNDEFINED) {
    if ((swapchain.imageUsage() & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
        throw std::runtime_error("Swapchain images do not support color attachment usage.");
    }

    textureDescriptors_.fillSamplers(SamplersResource, texture_.sampler());
    textureDescriptors_.writeTexture2D(TexturesResource, 0, texture_);

    // 每个在途帧独占一份模型矩阵，CPU 等待该 frame slot 后才会再次覆写。
    frameDescriptors_.reserve(matrixBuffers_.size());
    for (size_t index = 0; index < matrixBuffers_.size(); ++index) {
        auto descriptors = std::make_unique<VulkanDescriptorSet>(
            context_,
            DescriptorSetDesc{
                .shaders = {&computeShader, &vertexShader},
                .set = 1,
                .debugName = "Instanced mesh frame descriptors",
            });
        descriptors->writeStorageBuffer(InstancesResource, instances_);
        descriptors->writeStorageBuffer(MatricesResource, matrixBuffers_[index]);
        descriptors->writeStorageBuffer(VerticesResource, mesh_.vertexBuffer());
        frameDescriptors_.push_back(std::move(descriptors));
    }

    const std::vector<VkDescriptorSetLayout> layouts{
        textureDescriptors_.layout(),
        frameDescriptors_.front()->layout(),
    };
    computePipeline_ = std::make_unique<VulkanComputePipeline>(
        context_,
        ComputePipelineDesc{
            .shader = &computeShader,
            .descriptorSetLayouts = layouts,
            .debugName = "Instanced mesh matrix compute pipeline",
        });
    renderPipeline_ = std::make_unique<VulkanRenderPipeline>(
        context_,
        RenderPipelineDesc{
            .vertexShader = &vertexShader,
            .fragmentShader = &fragmentShader,
            // 顶点属性由 gl_VertexIndex 从 SSBO 拉取，没有传统 vertex input。
            .colorFormat = swapchain_.imageFormat(),
            .depthFormat = depthAttachment_.format(),
            .cullMode = VK_CULL_MODE_BACK_BIT,
            .depthTestEnabled = true,
            .depthWriteEnabled = true,
            .depthCompareOp = VK_COMPARE_OP_LESS,
            .descriptorSetLayouts = layouts,
            .debugName = "Instanced mesh render pipeline",
        });
}

void InstancedMeshesCommandRecorder::record(const RenderFrameContext& frame) {
    if (frame.frameIndex >= frameDescriptors_.size()) {
        throw std::out_of_range("Render frame index exceeds instanced mesh frame resources.");
    }

    const VkCommandBuffer commandBuffer = frame.commandBuffer;
    VulkanDescriptorSet& frameDescriptors = *frameDescriptors_[frame.frameIndex];
    VulkanBuffer& matrices = matrixBuffers_[frame.frameIndex];
    APP_PROFILE_FUNCTION();
    APP_PROFILE_GPU_ZONE(context_, commandBuffer, "Compute instanced meshes");

    // 第一阶段：每个 compute invocation 只为一个实例计算一次模型矩阵。
    computePipeline_->bind(commandBuffer);
    frameDescriptors.bindCompute(commandBuffer, computePipeline_->layout());
    struct ComputePushConstants {
        uint32_t instanceCount;
        float time;
    };
    const float seconds = std::chrono::duration<float>(
        std::chrono::steady_clock::now() - startTime_).count();
    const ComputePushConstants computeConstants{
        .instanceCount = MeshCount,
        .time = seconds,
    };
    vkCmdPushConstants(
        commandBuffer, computePipeline_->layout(), VK_SHADER_STAGE_COMPUTE_BIT,
        0, sizeof(ComputePushConstants), &computeConstants);
    vkCmdDispatch(commandBuffer, (MeshCount + ComputeLocalSize - 1) / ComputeLocalSize, 1, 1);

    // Compute 的写入必须对后续 vertex shader 可见，否则会读取未完成或旧的矩阵。
    const VkBufferMemoryBarrier matrixBarrier{
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = matrices.handle(),
        .offset = 0,
        .size = VK_WHOLE_SIZE,
    };
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        0,
        0, nullptr,
        1, &matrixBarrier,
        0, nullptr);

    const uint32_t imageIndex = frame.imageIndex;
    const VkImage image = swapchain_.images()[imageIndex];
    const VkExtent2D extent = swapchain_.extent();
    vulkan_utils::transitionImage(
        commandBuffer, image, VK_IMAGE_ASPECT_COLOR_BIT,
        imageLayouts_[imageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    depthAttachment_.transitionForWrite(commandBuffer);

    const VkClearValue clearColor{.color = {{1.0f, 1.0f, 1.0f, 1.0f}}};
    const VkRenderingAttachmentInfo colorAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = swapchain_.imageViews()[imageIndex],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = clearColor,
    };
    const VkRenderingAttachmentInfo depthAttachment = depthAttachment_.renderingInfo();
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

    renderPipeline_->bind(commandBuffer);
    textureDescriptors_.bind(commandBuffer, renderPipeline_->layout());
    frameDescriptors.bind(commandBuffer, renderPipeline_->layout());
    struct RenderPushConstants {
        glm::mat4 viewProjection;
        glm::vec4 meshCenterRadius;
    };
    const RenderPushConstants renderConstants{
        .viewProjection = frame.view.viewProjection,
        .meshCenterRadius = mesh_.centerRadius(),
    };
    vkCmdPushConstants(
        commandBuffer, renderPipeline_->layout(), VK_SHADER_STAGE_VERTEX_BIT,
        0, sizeof(RenderPushConstants), &renderConstants);
    mesh_.bindIndexBuffer(commandBuffer);
    vkCmdDrawIndexed(commandBuffer, mesh_.indexCount(), MeshCount, 0, 0, 0);
    vkCmdEndRendering(commandBuffer);

    vulkan_utils::transitionImage(
        commandBuffer, image, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    imageLayouts_[imageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
}
