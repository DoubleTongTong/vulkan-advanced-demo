#pragma once

#include "VulkanDescriptorSet.h"
#include "VulkanDepthAttachment.h"
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
    void record(const RenderFrameContext& frame) override;

private:
    const VulkanContext& context_;
    const VulkanSwapchain& swapchain_;
    VulkanTexture2D texture_;
    VertexPullingMesh mesh_;
    VulkanDescriptorSet vertexDescriptors_;
    VulkanDescriptorSet textureDescriptors_;
    std::unique_ptr<VulkanRenderPipeline> pipeline_;
    VulkanDepthAttachment depthAttachment_;
    std::vector<VkImageLayout> imageLayouts_;
};
