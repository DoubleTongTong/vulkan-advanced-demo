#include "rendering/VertexPullingDuckCommandRecorder.h"

#include "ImageProcessor.h"
#include "mesh/GeometryCache.h"
#include "VulkanContext.h"
#include "VulkanProfiler.h"
#include "VulkanShaderModule.h"
#include "VulkanSwapchain.h"
#include "VulkanUtils.h"

#include <glm/ext/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <chrono>
#include <cstdint>
#include <stdexcept>

namespace {

constexpr uint32_t MaxBindlessTextures = 16;
constexpr const char* SamplersResource = "kSamplers";
constexpr const char* TexturesResource = "kTextures2D";

glm::mat4 makeModelViewProjection(
    const RenderView& view,
    const VertexPullingMesh& mesh) {
    using Clock = std::chrono::steady_clock;
    static const Clock::time_point startTime = Clock::now();

    const float seconds = std::chrono::duration<float>(Clock::now() - startTime).count();
    return view.viewProjection *
           mesh.normalizedModelMatrix(glm::radians(35.0f) + seconds);
}

} // namespace

VertexPullingDuckCommandRecorder::VertexPullingDuckCommandRecorder(
    const VulkanContext& context,
    const VulkanSwapchain& swapchain,
    const VulkanShaderModule& vertexShader,
    const VulkanShaderModule& fragmentShader,
    const std::filesystem::path& scenePath,
    const std::filesystem::path& texturePath)
    : context_(context),
      swapchain_(swapchain),
      texture_(context, ImageProcessor().loadRgba8(texturePath), "Vertex pulling duck base color texture"),
      mesh_(context, GeometryCache::loadOrConvert(scenePath)),
      vertexDescriptors_(
          context,
          {
              .shaders = {&vertexShader},
              .set = 1,
              .debugName = "Vertex pulling mesh descriptors",
          }),
      textureDescriptors_(
          context,
          {
              .shaders = {&fragmentShader},
              .runtimeArrays = {{TexturesResource, MaxBindlessTextures}},
              .debugName = "Vertex pulling duck texture descriptors",
          }),
      depthAttachment_(context, swapchain.extent(), "Vertex pulling duck depth"),
      imageLayouts_(swapchain.images().size(), VK_IMAGE_LAYOUT_UNDEFINED) {
    if ((swapchain.imageUsage() & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
        throw std::runtime_error("Swapchain images do not support color attachment usage.");
    }

    vertexDescriptors_.writeStorageBuffer("kVertices", mesh_.vertexStorageBuffer());
    textureDescriptors_.fillSamplers(SamplersResource, texture_.sampler());
    textureDescriptors_.writeTexture2D(TexturesResource, 0, texture_);
    pipeline_ = std::make_unique<VulkanRenderPipeline>(
        context_,
        RenderPipelineDesc{
            .vertexShader = &vertexShader,
            .fragmentShader = &fragmentShader,
            // PVP 在 shader 内读取 storage buffer，不声明传统 vertex input。
            .colorFormat = swapchain_.imageFormat(),
            .depthFormat = depthAttachment_.format(),
            .cullMode = VK_CULL_MODE_BACK_BIT,
            .depthTestEnabled = true,
            .depthWriteEnabled = true,
            .descriptorSetLayouts = {
                textureDescriptors_.layout(),
                vertexDescriptors_.layout(),
            },
            .debugName = "Vertex pulling duck pipeline",
        });
}

void VertexPullingDuckCommandRecorder::record(const RenderFrameContext& frame) {
    const VkCommandBuffer commandBuffer = frame.commandBuffer;
    const uint32_t imageIndex = frame.imageIndex;
    APP_PROFILE_FUNCTION();
    APP_PROFILE_GPU_ZONE(context_, commandBuffer, "Vertex pulling duck");

    const VkImage image = swapchain_.images()[imageIndex];
    const VkExtent2D extent = swapchain_.extent();
    vulkan_utils::transitionImage(
        commandBuffer, image, VK_IMAGE_ASPECT_COLOR_BIT,
        imageLayouts_[imageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    depthAttachment_.transitionForWrite(commandBuffer);

    const VkClearValue clearColor{.color = {{0.45f, 0.45f, 0.45f, 1.0f}}};
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

    pipeline_->bind(commandBuffer);
    textureDescriptors_.bind(commandBuffer, pipeline_->layout());
    vertexDescriptors_.bind(commandBuffer, pipeline_->layout());

    struct PushConstants {
        glm::mat4 mvp;
        uint32_t textureId = 0;
    };
    const PushConstants pushConstants{
        .mvp = makeModelViewProjection(frame.view, mesh_),
        .textureId = 0,
    };
    vkCmdPushConstants(
        commandBuffer, pipeline_->layout(),
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(PushConstants), &pushConstants);
    mesh_.bindIndexBuffer(commandBuffer);
    vkCmdDrawIndexed(commandBuffer, mesh_.indexCount(), 1, 0, 0, 0);
    vkCmdEndRendering(commandBuffer);

    vulkan_utils::transitionImage(
        commandBuffer, image, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    imageLayouts_[imageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
}
