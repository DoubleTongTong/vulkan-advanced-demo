#pragma once

#include "VulkanBuffer.h"
#include "VulkanDescriptorSet.h"
#include "VulkanDepthAttachment.h"
#include "VulkanRenderPipeline.h"
#include "VulkanTexture2D.h"
#include "rendering/IRenderCommandRecorder.h"
#include "rendering/mesh/vertex_pulling/VertexPullingMesh.h"

#include <vulkan/vulkan.h>

#include <filesystem>
#include <memory>
#include <vector>

class VulkanContext;
class VulkanShaderModule;
class VulkanSwapchain;

// 用 TCS/TES 自适应细分鸭子网格，再由 Geometry Shader 生成线框重心坐标。
class TessellatedDuckCommandRecorder final : public IRenderCommandRecorder {
public:
    TessellatedDuckCommandRecorder(
        const VulkanContext& context,
        const VulkanSwapchain& swapchain,
        const VulkanShaderModule& vertexShader,
        const VulkanShaderModule& tessellationControlShader,
        const VulkanShaderModule& tessellationEvaluationShader,
        const VulkanShaderModule& geometryShader,
        const VulkanShaderModule& fragmentShader,
        const std::filesystem::path& scenePath,
        const std::filesystem::path& texturePath);
    void record(const RenderFrameContext& frame) override;

    float tessellationScale() const;
    void setTessellationScale(float scale);

private:
    const VulkanContext& context_;
    const VulkanSwapchain& swapchain_;
    VulkanTexture2D texture_;
    VertexPullingMesh mesh_;
    std::vector<VulkanBuffer> frameDataBuffers_;
    VulkanDescriptorSet textureDescriptors_;
    std::vector<std::unique_ptr<VulkanDescriptorSet>> frameDescriptors_;
    std::unique_ptr<VulkanRenderPipeline> pipeline_;
    float tessellationScale_ = 1.0f;
    VulkanDepthAttachment depthAttachment_;
    std::vector<VkImageLayout> imageLayouts_;
};
