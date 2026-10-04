#pragma once

#include "VulkanBuffer.h"
#include "VulkanRenderPipeline.h"
#include "mesh/MeshData.h"
#include "rendering/IRenderCommandRecorder.h"

#include <vulkan/vulkan.h>

#include <filesystem>
#include <memory>
#include <vector>

class VulkanContext;
class VulkanShaderModule;
class VulkanSwapchain;
struct VmaAllocation_T;
using VmaAllocation = VmaAllocation_T*;

class RubberDuckCommandRecorder final : public IRenderCommandRecorder {
public:
    RubberDuckCommandRecorder(
        const VulkanContext& context,
        const VulkanSwapchain& swapchain,
        const VulkanShaderModule& vertexShader,
        const VulkanShaderModule& fragmentShader,
        const std::filesystem::path& scenePath);
    ~RubberDuckCommandRecorder();

    void record(const RenderFrameContext& frame) override;

private:
    RubberDuckCommandRecorder(
        const VulkanContext& context,
        const VulkanSwapchain& swapchain,
        const VulkanShaderModule& vertexShader,
        const VulkanShaderModule& fragmentShader,
        MeshData&& mesh);

    void createIndexBuffer(const MeshData& mesh);
    void createDepthAttachment();
    void destroyDepthAttachment();

    const VulkanContext& context_;
    const VulkanSwapchain& swapchain_;
    VulkanBuffer vertexBuffer_;
    std::unique_ptr<VulkanBuffer> indexBuffer_;
    std::vector<VkDeviceSize> indexOffsets_;
    std::vector<uint32_t> indexCounts_;
    VulkanRenderPipeline solidPipeline_;
    VulkanRenderPipeline wireframePipeline_;
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkImageView depthImageView_ = VK_NULL_HANDLE;
    VmaAllocation depthAllocation_ = nullptr;
    VkImageLayout depthLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    float meshCenter_[3] = {};
    float meshRadius_ = 1.0f;
    std::vector<VkImageLayout> imageLayouts_;
};
