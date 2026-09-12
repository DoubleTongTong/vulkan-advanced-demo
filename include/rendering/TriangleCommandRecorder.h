#pragma once

#include "VulkanRenderPipeline.h"
#include "rendering/IRenderCommandRecorder.h"

#include <vulkan/vulkan.h>

#include <vector>

class VulkanContext;
class VulkanShaderModule;
class VulkanSwapchain;

class TriangleCommandRecorder final : public IRenderCommandRecorder {
public:
    TriangleCommandRecorder(
        const VulkanContext& context,
        const VulkanSwapchain& swapchain,
        const VulkanShaderModule& vertexShader,
        const VulkanShaderModule& fragmentShader);

    void record(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;

private:
    const VulkanSwapchain& swapchain_;
    VulkanRenderPipeline pipeline_;
    std::vector<VkImageLayout> imageLayouts_;
};
