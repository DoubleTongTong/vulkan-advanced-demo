#include "FrameTimer.h"
#include "GlfwWindow.h"
#include "CubeMapProcessor.h"
#include "ImageProcessor.h"
#include "SceneCamera.h"
#include "VulkanContext.h"
#include "VulkanFrameSync.h"
#include "VulkanImmediateCommands.h"
#include "VulkanImGuiOverlay.h"
#include "VulkanProfiler.h"
#include "VulkanShaderModule.h"
#include "VulkanSwapchain.h"
#include "VulkanUtils.h"
#include "VulkanTextureCube.h"
#include "rendering/BrdfLutCommandRecorder.h"
#include "rendering/EnvironmentMapFilterCommandRecorder.h"
#include "rendering/UnlitGltfCommandRecorder.h"
#include "rendering/IRenderCommandRecorder.h"
#include "ui/FrameGraphPanel.h"
#include "ui/FpsPanel.h"

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

        const std::filesystem::path shaderDir = APP_UNLIT_GLTF_SHADER_DIR;
        const VulkanShaderModule vertexShader =
            VulkanShaderModule::fromFile(vulkan, shaderDir / "main.vert");
        const VulkanShaderModule fragmentShader =
            VulkanShaderModule::fromFile(vulkan, shaderDir / "main.frag");

        std::cout << "Created glTF unlit shader modules.\n";

        // Swapchain 管理一组可以呈现到窗口上的图像，后续渲染会围绕它展开。
        VulkanSwapchain swapchain(vulkan, window.width(), window.height());

        // ImmediateCommands 只负责命令缓冲的申请、提交和回收，不关心里面录制什么。
        VulkanImmediateCommands commands(vulkan, "Main immediate commands");

        // BRDF LUT 是后续 PBR 使用的预计算资源，只需在启动阶段生成一次。
        {
            const VulkanShaderModule brdfLutShader = VulkanShaderModule::fromFile(
                vulkan,
                std::filesystem::path(APP_BRDF_LUT_SHADER_DIR) / "main.comp");
            const BrdfLutCommandRecorder brdfLutRecorder(
                vulkan,
                brdfLutShader,
                BrdfLutConfig{
                    .width = 256,
                    .height = 256,
                    .sampleCount = 1024,
                });

            const VulkanImmediateCommands::CommandBuffer& commandBuffer = commands.acquire();
            brdfLutRecorder.record(commandBuffer.commandBuffer);
            commands.wait(commands.submit(commandBuffer));
            brdfLutRecorder.saveKtx2(APP_BRDF_LUT_OUTPUT);
            std::cout << "Generated BRDF LUT: " << APP_BRDF_LUT_OUTPUT << '\n';
        }

        // 把同一张 HDR 环境分别卷积成漫反射、GGX 镜面和 Charlie Sheen 资源。
        {
            const RgbaFloatImage panorama =
                ImageProcessor().loadRgba32Float(PIAZZA_ENVIRONMENT);
            const CubeMapImage cubeMap =
                CubeMapProcessor().fromEquirectangular(panorama);
            const VulkanTextureCube environment(
                vulkan, cubeMap, "Piazza environment with mipmaps");
            const VulkanShaderModule filterShader = VulkanShaderModule::fromFile(
                vulkan,
                std::filesystem::path(APP_ENVIRONMENT_FILTER_SHADER_DIR) / "main.comp");

            const auto filterAndSave = [&vulkan, &commands, &environment, &filterShader](
                EnvironmentMapFilterConfig config,
                const std::filesystem::path& outputPath) {
                const EnvironmentMapFilterCommandRecorder recorder(
                    vulkan, filterShader, environment, config);
                const VulkanImmediateCommands::CommandBuffer& commandBuffer = commands.acquire();
                recorder.record(commandBuffer.commandBuffer);
                commands.wait(commands.submit(commandBuffer));
                recorder.saveKtx2(outputPath);
                recorder.saveEquirectangularHdrMipChain(
                    APP_FILTERED_ENVIRONMENT_HDR_DIR,
                    outputPath.stem().string());
                std::cout << "Generated filtered environment: " << outputPath << '\n';
            };

            constexpr uint32_t FilterSamples = 256;
            filterAndSave(
                {
                    .distribution = EnvironmentMapDistribution::Lambertian,
                    .faceSize = 64,
                    .mipLevels = 1,
                    .sampleCount = FilterSamples,
                },
                APP_IRRADIANCE_OUTPUT);
            filterAndSave(
                {
                    .distribution = EnvironmentMapDistribution::Ggx,
                    .faceSize = cubeMap.faceSize,
                    .mipLevels = environment.mipLevels(),
                    .sampleCount = FilterSamples,
                },
                APP_GGX_ENVIRONMENT_OUTPUT);
            filterAndSave(
                {
                    .distribution = EnvironmentMapDistribution::Charlie,
                    .faceSize = cubeMap.faceSize,
                    .mipLevels = environment.mipLevels(),
                    .sampleCount = FilterSamples,
                },
                APP_CHARLIE_ENVIRONMENT_OUTPUT);
        }

        SceneCamera sceneCamera(
            window.handle(),
            FirstPersonCamera::Settings{
                .position = {0.0f, 0.0f, 3.2f},
                .target = {0.0f, 0.0f, 0.0f},
                .maxSpeed = 2.0f,
            });
        UnlitGltfCommandRecorder unlitGltfRecorder(
            vulkan,
            swapchain,
            vertexShader,
            fragmentShader,
            DAMAGED_HELMET_SCENE);
        IRenderCommandRecorder& renderCommandRecorder = unlitGltfRecorder;
        FrameTimer frameTimer;
        VulkanImGuiOverlay imgui(vulkan, swapchain, window.handle());
        imgui.addPanel(std::make_unique<FpsPanel>(frameTimer));
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
