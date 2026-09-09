#include "GlfwWindow.h"
#include "VulkanContext.h"
#include "VulkanFrameSync.h"
#include "VulkanImmediateCommands.h"
#include "VulkanSwapchain.h"
#include "VulkanUtils.h"

#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void transitionImage(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkAccessFlags srcAccess,
    VkAccessFlags dstAccess,
    VkPipelineStageFlags srcStage,
    VkPipelineStageFlags dstStage) {
    const VkImageSubresourceRange colorRange{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };

    const VkImageMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = srcAccess,
        .dstAccessMask = dstAccess,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = colorRange,
    };

    vkCmdPipelineBarrier(
        commandBuffer,
        srcStage,
        dstStage,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier);
}

void recordClearSwapchainImage(
    VkCommandBuffer commandBuffer,
    const VulkanSwapchain& swapchain,
    uint32_t imageIndex,
    std::vector<VkImageLayout>& imageLayouts) {
    const VkImage image = swapchain.images()[imageIndex];

    transitionImage(
        commandBuffer,
        image,
        imageLayouts[imageIndex],
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT);

    const VkImageSubresourceRange colorRange{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };

    // 当前还没有 render pass 和 pipeline，先用 command buffer 清屏验证提交链路。
    const VkClearColorValue clearColor{{0.0f, 0.25f, 1.0f, 1.0f}};
    vkCmdClearColorImage(
        commandBuffer,
        image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        &clearColor,
        1,
        &colorRange);

    transitionImage(
        commandBuffer,
        image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        0,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

    imageLayouts[imageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
}

} // namespace

int main() {
    try {
        // 传入 1280x800 创建普通窗口；如果想铺满工作区，可以改成 0, 0。
        GlfwWindow window("GLFW Vulkan Demo", 1280, 800);

        // 先完成 Vulkan 最基础的上下文创建：instance、surface、device 和 graphics queue。
        VulkanContext vulkan(window.handle());

        // Swapchain 管理一组可以呈现到窗口上的图像，后续渲染会围绕它展开。
        VulkanSwapchain swapchain(vulkan, window.width(), window.height());

        if ((swapchain.imageUsage() & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0) {
            throw std::runtime_error("Swapchain images do not support transfer dst usage.");
        }

        // ImmediateCommands 只负责命令缓冲的申请、提交和回收，不关心里面录制什么。
        VulkanImmediateCommands commands(vulkan, "Main immediate commands");
        VulkanFrameSync frameSync(vulkan, static_cast<uint32_t>(swapchain.images().size()));
        std::vector<VkImageLayout> imageLayouts(swapchain.images().size(), VK_IMAGE_LAYOUT_UNDEFINED);

        while (!window.shouldClose()) {
            window.pollEvents();

            const VulkanFrameSync::Frame frame = frameSync.currentFrame();
            if (!frame.submitHandle.empty()) {
                commands.wait(frame.submitHandle);
            }

            const VulkanSwapchain::AcquiredImage acquiredImage = swapchain.acquireNextImage(frame.imageAvailable);
            if (acquiredImage.result == VK_ERROR_OUT_OF_DATE_KHR) {
                continue;
            }

            if (!acquiredImage.shouldRender()) {
                vulkan_utils::checkVk(acquiredImage.result, "vkAcquireNextImageKHR");
            }

            const VkSemaphore renderFinished = frameSync.renderFinishedSemaphore(acquiredImage.imageIndex);
            commands.setSubmitWaitSemaphore(frame.imageAvailable);
            const VulkanImmediateCommands::CommandBuffer& commandBuffer = commands.acquire();
            recordClearSwapchainImage(commandBuffer.commandBuffer, swapchain, acquiredImage.imageIndex, imageLayouts);
            commands.setSubmitSignalSemaphore(renderFinished);
            const VulkanImmediateCommands::SubmitHandle submitHandle = commands.submit(commandBuffer);
            frameSync.markSubmitted(frame, submitHandle);

            const VkResult presentResult = swapchain.present(acquiredImage, renderFinished);
            if (presentResult != VK_SUCCESS &&
                presentResult != VK_SUBOPTIMAL_KHR &&
                presentResult != VK_ERROR_OUT_OF_DATE_KHR) {
                vulkan_utils::checkVk(presentResult, "vkQueuePresentKHR");
            }

            frameSync.advanceFrame();
        }

        commands.waitAll();
        vulkan_utils::checkVk(vkDeviceWaitIdle(vulkan.device()), "vkDeviceWaitIdle");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
