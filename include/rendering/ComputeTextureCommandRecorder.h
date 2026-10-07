#pragma once

#include "VulkanComputePipeline.h"
#include "VulkanDescriptorSet.h"
#include "VulkanRenderPipeline.h"
#include "VulkanStorageTexture2D.h"
#include "rendering/IRenderCommandRecorder.h"

#include <chrono>
#include <vector>

class VulkanContext;
class VulkanShaderModule;
class VulkanSwapchain;

// Compute Shader 每帧生成一张纹理，再用全屏三角形显示结果。
class ComputeTextureCommandRecorder final : public IRenderCommandRecorder {
public:
    ComputeTextureCommandRecorder(
        const VulkanContext& context,
        const VulkanSwapchain& swapchain,
        const VulkanShaderModule& computeShader,
        const VulkanShaderModule& vertexShader,
        const VulkanShaderModule& fragmentShader);

    void record(const RenderFrameContext& frame) override;

private:
    static constexpr uint32_t LocalSize = 16;

    const VulkanContext& context_;
    const VulkanSwapchain& swapchain_;
    VulkanStorageTexture2D texture_;
    VulkanDescriptorSet descriptors_;
    VulkanComputePipeline computePipeline_;
    VulkanRenderPipeline renderPipeline_;
    std::chrono::steady_clock::time_point startTime_;
    std::vector<VkImageLayout> imageLayouts_;
};
