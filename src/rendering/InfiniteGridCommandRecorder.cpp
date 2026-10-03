#include "rendering/InfiniteGridCommandRecorder.h"

#include "VulkanContext.h"
#include "VulkanProfiler.h"
#include "VulkanShaderModule.h"
#include "VulkanSwapchain.h"
#include "VulkanUtils.h"

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <stdexcept>

InfiniteGridCommandRecorder::InfiniteGridCommandRecorder(
    const VulkanContext& context,
    const VulkanSwapchain& swapchain,
    const VulkanShaderModule& vertexShader,
    const VulkanShaderModule& fragmentShader,
    InfiniteGridSettings settings)
    : context_(context),
      swapchain_(swapchain),
      pipeline_(
          context,
          RenderPipelineDesc{
              .vertexShader = &vertexShader,
              .fragmentShader = &fragmentShader,
              .colorFormat = swapchain.imageFormat(),
              // 网格边缘依赖透明度渐隐，因此使用标准 SrcAlpha 混合。
              .blendEnabled = true,
              .debugName = "Infinite grid render pipeline",
          }),
      settings_(settings),
      imageLayouts_(swapchain.images().size(), VK_IMAGE_LAYOUT_UNDEFINED) {
    if ((swapchain.imageUsage() & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
        throw std::runtime_error("Swapchain images do not support color attachment usage.");
    }
    if (settings_.extent <= 0.0f ||
        settings_.cellSize <= 0.0f ||
        settings_.minPixelsBetweenCells <= 0.0f) {
        throw std::invalid_argument("Infinite grid settings must be positive.");
    }
}

void InfiniteGridCommandRecorder::record(const RenderFrameContext& frame) {
    APP_PROFILE_FUNCTION();
    APP_PROFILE_GPU_ZONE(context_, frame.commandBuffer, "Infinite grid");

    const VkCommandBuffer commandBuffer = frame.commandBuffer;
    const uint32_t imageIndex = frame.imageIndex;
    const VkImage image = swapchain_.images().at(imageIndex);
    const VkImageView imageView = swapchain_.imageViews().at(imageIndex);
    const VkExtent2D extent = swapchain_.extent();

    vulkan_utils::transitionImage(
        commandBuffer,
        image,
        VK_IMAGE_ASPECT_COLOR_BIT,
        imageLayouts_.at(imageIndex),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        0,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    const VkClearValue clearColor{.color = {{1.0f, 1.0f, 1.0f, 1.0f}}};
    const VkRenderingAttachmentInfo colorAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = imageView,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = clearColor,
    };
    const VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.offset = {0, 0}, .extent = extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
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

    pipeline_.bind(commandBuffer);

    // vec4 明确补齐 std430/Push Constant 对齐，避免 vec3 的尾部填充歧义。
    struct PushConstants {
        glm::mat4 viewProjection;
        glm::vec4 cameraPosition;
        glm::vec4 origin;
        // x: 半边长，y: 最小格宽，z: LOD 切换所需的最小像素间距。
        glm::vec4 gridParameters;
    };
    const PushConstants constants{
        .viewProjection = frame.view.viewProjection,
        .cameraPosition = glm::vec4(frame.view.cameraPosition, 1.0f),
        .origin = glm::vec4(settings_.origin, 1.0f),
        .gridParameters = {
            settings_.extent,
            settings_.cellSize,
            settings_.minPixelsBetweenCells,
            0.0f,
        },
    };
    vkCmdPushConstants(
        commandBuffer,
        pipeline_.layout(),
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(constants),
        &constants);

    // Vertex Shader 内置 6 个索引，直接生成覆盖网格区域的两个三角形。
    vkCmdDraw(commandBuffer, 6, 1, 0, 0);
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
}
