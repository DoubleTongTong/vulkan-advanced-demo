#include "rendering/IndirectBistroCommandRecorder.h"

#include "VulkanContext.h"
#include "VulkanProfiler.h"
#include "VulkanShaderModule.h"
#include "VulkanSwapchain.h"
#include "VulkanUtils.h"
#include "mesh/GeometryCache.h"

#include <glm/mat4x4.hpp>

#include <cstddef>
#include <stdexcept>

IndirectBistroCommandRecorder::IndirectBistroCommandRecorder(
    const VulkanContext& context,
    const VulkanSwapchain& swapchain,
    const VulkanShaderModule& vertexShader,
    const VulkanShaderModule& geometryShader,
    const VulkanShaderModule& fragmentShader,
    const std::filesystem::path& scenePath)
    : context_(context),
      swapchain_(swapchain),
      mesh_(context, GeometryCache::loadOrConvert(scenePath)),
      depthAttachment_(context, swapchain.extent(), "Bistro indirect depth"),
      imageLayouts_(swapchain.images().size(), VK_IMAGE_LAYOUT_UNDEFINED) {
    if ((swapchain.imageUsage() & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
        throw std::runtime_error("Swapchain images do not support color attachment usage.");
    }

    pipeline_ = std::make_unique<VulkanRenderPipeline>(
        context_,
        RenderPipelineDesc{
            .vertexShader = &vertexShader,
            .geometryShader = &geometryShader,
            .fragmentShader = &fragmentShader,
            .vertexBindings = {{
                .binding = 0,
                .stride = sizeof(MeshVertex),
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
            }},
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
                    .format = VK_FORMAT_R32G32B32_SFLOAT,
                    .offset = offsetof(MeshVertex, normal),
                },
            },
            .colorFormat = swapchain_.imageFormat(),
            .depthFormat = depthAttachment_.format(),
            // Bistro 数据来自多个建模对象，暂时关闭剔除以避免源 winding 不一致造成缺面。
            .cullMode = VK_CULL_MODE_NONE,
            .depthTestEnabled = true,
            .depthWriteEnabled = true,
            .depthCompareOp = VK_COMPARE_OP_LESS,
            .debugName = "Bistro indirect render pipeline",
        });
}

void IndirectBistroCommandRecorder::record(const RenderFrameContext& frame) {
    APP_PROFILE_FUNCTION();
    APP_PROFILE_GPU_ZONE(context_, frame.commandBuffer, "Bistro indirect rendering");

    const VkCommandBuffer commandBuffer = frame.commandBuffer;
    const uint32_t imageIndex = frame.imageIndex;
    const VkImage image = swapchain_.images().at(imageIndex);
    const VkExtent2D extent = swapchain_.extent();

    vulkan_utils::transitionImage(
        commandBuffer, image, VK_IMAGE_ASPECT_COLOR_BIT,
        imageLayouts_.at(imageIndex), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    depthAttachment_.transitionForWrite(commandBuffer);

    const VkClearValue clearColor{.color = {{0.64f, 0.76f, 0.88f, 1.0f}}};
    const VkRenderingAttachmentInfo colorAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = swapchain_.imageViews().at(imageIndex),
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
    vkCmdPushConstants(
        commandBuffer,
        pipeline_->layout(),
        VK_SHADER_STAGE_VERTEX_BIT,
        0,
        sizeof(glm::mat4),
        &frame.view.viewProjection);
    mesh_.bindAndDraw(commandBuffer);
    vkCmdEndRendering(commandBuffer);

    vulkan_utils::transitionImage(
        commandBuffer, image, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    imageLayouts_[imageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
}
