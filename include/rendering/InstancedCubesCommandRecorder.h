#pragma once

#include "VulkanBuffer.h"
#include "VulkanDescriptorSet.h"
#include "VulkanRenderPipeline.h"
#include "VulkanTexture2D.h"
#include "rendering/IRenderCommandRecorder.h"

#include <vulkan/vulkan.h>

#include <chrono>
#include <memory>
#include <vector>

class VulkanContext;
class VulkanShaderModule;
class VulkanSwapchain;
struct VmaAllocation_T;
using VmaAllocation = VmaAllocation_T*;

// 一次 draw call 绘制 1024 * 1024 个立方体。
// 顶点和 UV 在 shader 中生成，CPU 侧只保存每个实例的中心与初始角度。
class InstancedCubesCommandRecorder final : public IRenderCommandRecorder {
public:
    InstancedCubesCommandRecorder(
        const VulkanContext& context,
        const VulkanSwapchain& swapchain,
        const VulkanShaderModule& vertexShader,
        const VulkanShaderModule& fragmentShader);
    ~InstancedCubesCommandRecorder();

    void record(const RenderFrameContext& frame) override;

private:
    void createDepthAttachment();
    void destroyDepthAttachment();

    const VulkanContext& context_;
    const VulkanSwapchain& swapchain_;
    VulkanTexture2D texture_;
    VulkanBuffer instanceBuffer_;
    VulkanDescriptorSet instanceDescriptors_;
    VulkanDescriptorSet textureDescriptors_;
    std::unique_ptr<VulkanRenderPipeline> pipeline_;
    std::chrono::steady_clock::time_point startTime_;
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkImageView depthImageView_ = VK_NULL_HANDLE;
    VmaAllocation depthAllocation_ = nullptr;
    VkImageLayout depthLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    std::vector<VkImageLayout> imageLayouts_;
};
