#pragma once

#include "ModelLoader.h"
#include "VulkanBuffer.h"
#include "VulkanRenderPipeline.h"
#include "rendering/IRenderCommandRecorder.h"

#include <vulkan/vulkan.h>

#include <filesystem>

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

    void record(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;

private:
    RubberDuckCommandRecorder(
        const VulkanContext& context,
        const VulkanSwapchain& swapchain,
        const VulkanShaderModule& vertexShader,
        const VulkanShaderModule& fragmentShader,
        ModelMesh&& mesh);

    void createDepthAttachment();
    void destroyDepthAttachment();

    const VulkanContext& context_;
    const VulkanSwapchain& swapchain_;
    VulkanBuffer vertexBuffer_;
    VulkanBuffer indexBuffer_;
    VulkanRenderPipeline solidPipeline_;
    VulkanRenderPipeline wireframePipeline_;
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkImageView depthImageView_ = VK_NULL_HANDLE;
    VmaAllocation depthAllocation_ = nullptr;
    VkImageLayout depthLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    float meshCenter_[3] = {};
    float meshRadius_ = 1.0f;
    uint32_t indexCount_ = 0;
    std::vector<VkImageLayout> imageLayouts_;
};
