#pragma once

#include "VulkanRenderPipeline.h"
#include "rendering/IRenderCommandRecorder.h"

#include <glm/vec3.hpp>
#include <vulkan/vulkan.h>

#include <vector>

class VulkanContext;
class VulkanShaderModule;
class VulkanSwapchain;

struct InfiniteGridSettings {
    glm::vec3 origin{0.0f};
    float extent = 100.0f;
    float cellSize = 0.025f;
    float minPixelsBetweenCells = 2.0f;
};

// 在 XZ 平面绘制跟随相机移动的程序化网格。
// 网格没有顶点缓冲，也没有纹理；几何和线条都由 Shader 直接生成。
class InfiniteGridCommandRecorder final : public IRenderCommandRecorder {
public:
    InfiniteGridCommandRecorder(
        const VulkanContext& context,
        const VulkanSwapchain& swapchain,
        const VulkanShaderModule& vertexShader,
        const VulkanShaderModule& fragmentShader,
        InfiniteGridSettings settings = {});

    void record(const RenderFrameContext& frame) override;

private:
    const VulkanContext& context_;
    const VulkanSwapchain& swapchain_;
    VulkanRenderPipeline pipeline_;
    InfiniteGridSettings settings_;
    std::vector<VkImageLayout> imageLayouts_;
};
