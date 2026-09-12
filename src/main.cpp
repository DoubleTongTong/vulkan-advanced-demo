#include "GlfwWindow.h"
#include "VulkanContext.h"
#include "VulkanFrameSync.h"
#include "VulkanImmediateCommands.h"
#include "VulkanShaderModule.h"
#include "VulkanSwapchain.h"
#include "VulkanUtils.h"
#include "rendering/IRenderCommandRecorder.h"
#include "rendering/TriangleCommandRecorder.h"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>

int main() {
    try {
        // 传入 1280x800 创建普通窗口；如果想铺满工作区，可以改成 0, 0。
        GlfwWindow window("GLFW Vulkan Demo", 1280, 800);

        // 先完成 Vulkan 最基础的上下文创建：instance、surface、device 和 graphics queue。
        VulkanContext vulkan(window.handle());

        const std::filesystem::path shaderDir = APP_SHADER_DIR;
        const VulkanShaderModule vertexShader =
            VulkanShaderModule::fromFile(vulkan, shaderDir / "main.vert");
        const VulkanShaderModule fragmentShader =
            VulkanShaderModule::fromFile(vulkan, shaderDir / "main.frag");

        std::cout << "Created shader modules. Vertex push constants: "
                  << vertexShader.pushConstantSize()
                  << " bytes, fragment push constants: "
                  << fragmentShader.pushConstantSize()
                  << " bytes.\n";

        // Swapchain 管理一组可以呈现到窗口上的图像，后续渲染会围绕它展开。
        VulkanSwapchain swapchain(vulkan, window.width(), window.height());

        TriangleCommandRecorder triangleRecorder(vulkan, swapchain, vertexShader, fragmentShader);
        IRenderCommandRecorder& renderCommandRecorder = triangleRecorder;

        // ImmediateCommands 只负责命令缓冲的申请、提交和回收，不关心里面录制什么。
        VulkanImmediateCommands commands(vulkan, "Main immediate commands");
        VulkanFrameSync frameSync(vulkan, static_cast<uint32_t>(swapchain.images().size()));

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
            renderCommandRecorder.record(commandBuffer.commandBuffer, acquiredImage.imageIndex);
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
