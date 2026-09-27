#pragma once

#include "rendering/descriptors/textures/TextureDescriptorSet.h"
#include "rendering/descriptors/vertex_pulling/VertexPullingDescriptorSet.h"
#include "rendering/mesh/vertex_pulling/VertexPullingMesh.h"
#include "VulkanRenderPipeline.h"
#include "VulkanTexture2D.h"
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

// 使用 gl_VertexIndex 从 storage buffer 拉取顶点属性的纹理化鸭子示例。
class VertexPullingDuckCommandRecorder final : public IRenderCommandRecorder {
public:
    VertexPullingDuckCommandRecorder(
        const VulkanContext& context,
        const VulkanSwapchain& swapchain,
        const VulkanShaderModule& vertexShader,
        const VulkanShaderModule& fragmentShader,
        const std::filesystem::path& scenePath,
        const std::filesystem::path& texturePath);
    ~VertexPullingDuckCommandRecorder();

    void record(const RenderFrameContext& frame) override;

private:
    void createDepthAttachment();
    void destroyDepthAttachment();

    const VulkanContext& context_;
    const VulkanSwapchain& swapchain_;
    VulkanTexture2D texture_;
    VertexPullingMesh mesh_;
    VertexPullingDescriptorSet vertexDescriptors_;
    TextureDescriptorSet textureDescriptors_;
    std::unique_ptr<VulkanRenderPipeline> pipeline_;
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkImageView depthImageView_ = VK_NULL_HANDLE;
    VmaAllocation depthAllocation_ = nullptr;
    VkImageLayout depthLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    std::vector<VkImageLayout> imageLayouts_;
};
