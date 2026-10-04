#include "FrameTimer.h"
#include "GlfwWindow.h"
#include "SceneCamera.h"
#include "VulkanContext.h"
#include "VulkanFrameSync.h"
#include "VulkanImmediateCommands.h"
#include "VulkanImGuiOverlay.h"
#include "VulkanProfiler.h"
#include "VulkanShaderModule.h"
#include "VulkanSwapchain.h"
#include "VulkanUtils.h"
#include "rendering/IRenderCommandRecorder.h"
#include "rendering/TessellatedDuckCommandRecorder.h"
#include "ui/FrameGraphPanel.h"
#include "ui/FpsPanel.h"
#include "ui/TessellationPanel.h"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>

int main() {
    try {
        APP_PROFILE_THREAD("Main thread");
        // 传入 1280x800 创建普通窗口；如果想铺满工作区，可以改成 0, 0。
        GlfwWindow window("GLFW Vulkan Demo", 1280, 800);

        // 先完成 Vulkan 最基础的上下文创建：instance、surface、device 和 graphics queue。
        VulkanContext vulkan(window.handle());

        const std::filesystem::path shaderDir = APP_TESSELLATED_DUCK_SHADER_DIR;
        const VulkanShaderModule vertexShader =
            VulkanShaderModule::fromFile(vulkan, shaderDir / "main.vert");
        const VulkanShaderModule tessellationControlShader =
            VulkanShaderModule::fromFile(vulkan, shaderDir / "main.tesc");
        const VulkanShaderModule tessellationEvaluationShader =
            VulkanShaderModule::fromFile(vulkan, shaderDir / "main.tese");
        const VulkanShaderModule geometryShader =
            VulkanShaderModule::fromFile(vulkan, shaderDir / "main.geom");
        const VulkanShaderModule fragmentShader =
            VulkanShaderModule::fromFile(vulkan, shaderDir / "main.frag");

        std::cout << "Created tessellation shader pipeline modules.\n";

        // Swapchain 管理一组可以呈现到窗口上的图像，后续渲染会围绕它展开。
        VulkanSwapchain swapchain(vulkan, window.width(), window.height());

        // ImmediateCommands 只负责命令缓冲的申请、提交和回收，不关心里面录制什么。
        VulkanImmediateCommands commands(vulkan, "Main immediate commands");
        // 从斜前方观察归一化后的鸭子；Home 可以随时恢复这个视角。
        SceneCamera sceneCamera(
            window.handle(),
            FirstPersonCamera::Settings{
                .position = {2.5f, 1.5f, 2.5f},
                .target = {0.0f, 0.0f, 0.0f},
                .maxSpeed = 4.0f,
            },
            SceneCamera::Lens{
                .verticalFieldOfViewDegrees = 45.0f,
                .nearPlane = 0.1f,
                .farPlane = 100.0f,
            });
        TessellatedDuckCommandRecorder tessellatedDuckRecorder(
            vulkan,
            swapchain,
            vertexShader,
            tessellationControlShader,
            tessellationEvaluationShader,
            geometryShader,
            fragmentShader,
            RUBBER_DUCK_SCENE,
            RUBBER_DUCK_TEXTURE);
        IRenderCommandRecorder& renderCommandRecorder = tessellatedDuckRecorder;
        FrameTimer frameTimer;
        VulkanImGuiOverlay imgui(vulkan, swapchain, window.handle());
        imgui.addPanel(std::make_unique<FpsPanel>(frameTimer));
        imgui.addPanel(std::make_unique<TessellationPanel>(tessellatedDuckRecorder));
        // imgui.addPanel(std::make_unique<FrameGraphPanel>(frameTimer));

        VulkanFrameSync frameSync(vulkan, static_cast<uint32_t>(swapchain.images().size()));

        while (!window.shouldClose()) {
            APP_PROFILE_FRAME();
            APP_PROFILE_SCOPE("Frame CPU");
            window.pollEvents();
            frameTimer.beginFrame();
            sceneCamera.update(
                frameTimer.deltaSeconds(),
                !imgui.wantsKeyboardInput(),
                !imgui.wantsMouseInput());

            const VulkanFrameSync::Frame frame = frameSync.currentFrame();
            if (!frame.submitHandle.empty()) {
                commands.wait(frame.submitHandle);
            }

            const VulkanSwapchain::AcquiredImage acquiredImage = swapchain.acquireNextImage(frame.imageAvailable);
            if (acquiredImage.result == VK_ERROR_OUT_OF_DATE_KHR) {
                frameTimer.finishFrame(false);
                continue;
            }

            if (!acquiredImage.shouldRender()) {
                vulkan_utils::checkVk(acquiredImage.result, "vkAcquireNextImageKHR");
            }

            const VkSemaphore renderFinished = frameSync.renderFinishedSemaphore(acquiredImage.imageIndex);
            commands.setSubmitWaitSemaphore(frame.imageAvailable);
            const VulkanImmediateCommands::CommandBuffer& commandBuffer = commands.acquire();
            const RenderView renderView = sceneCamera.makeRenderView(swapchain.extent());
            renderCommandRecorder.record({
                .commandBuffer = commandBuffer.commandBuffer,
                .imageIndex = acquiredImage.imageIndex,
                .frameIndex = frame.frameIndex,
                .view = renderView,
            });
            imgui.record(commandBuffer.commandBuffer, acquiredImage.imageIndex, frame.frameIndex);
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
            frameTimer.finishFrame(true);
        }

        commands.waitAll();
        vulkan_utils::checkVk(vkDeviceWaitIdle(vulkan.device()), "vkDeviceWaitIdle");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
