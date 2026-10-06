#pragma once

#include "VulkanRenderPipeline.h"
#include "rendering/IRenderCommandRecorder.h"
#include "rendering/mesh/indirect/IndirectMesh.h"

#include <vulkan/vulkan.h>

#include <filesystem>
#include <memory>
#include <vector>

class VulkanContext;
class VulkanShaderModule;
class VulkanSwapchain;
struct VmaAllocation_T;
using VmaAllocation = VmaAllocation_T*;

// 使用一条 vkCmdDrawIndexedIndirect 绘制 Bistro 场景中的全部 Mesh。
class IndirectBistroCommandRecorder final : public IRenderCommandRecorder {
public:
    IndirectBistroCommandRecorder(
        const VulkanContext& context,
        const VulkanSwapchain& swapchain,
        const VulkanShaderModule& vertexShader,
        const VulkanShaderModule& geometryShader,
        const VulkanShaderModule& fragmentShader,
        const std::filesystem::path& scenePath);
    ~IndirectBistroCommandRecorder();

    void record(const RenderFrameContext& frame) override;

private:
    void createDepthAttachment();
    void destroyDepthAttachment();

    const VulkanContext& context_;
    const VulkanSwapchain& swapchain_;
    IndirectMesh mesh_;
    std::unique_ptr<VulkanRenderPipeline> pipeline_;
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkImageView depthImageView_ = VK_NULL_HANDLE;
    VmaAllocation depthAllocation_ = nullptr;
    VkImageLayout depthLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    std::vector<VkImageLayout> imageLayouts_;
};
