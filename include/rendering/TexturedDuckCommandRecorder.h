#pragma once

#include "VulkanBuffer.h"
#include "VulkanDescriptorSet.h"
#include "VulkanDepthAttachment.h"
#include "VulkanRenderPipeline.h"
#include "VulkanTexture2D.h"
#include "mesh/MeshData.h"
#include "rendering/IRenderCommandRecorder.h"

#include <vulkan/vulkan.h>

#include <filesystem>
#include <memory>
#include <vector>

class VulkanContext;
class VulkanShaderModule;
class VulkanSwapchain;

class TexturedDuckCommandRecorder final : public IRenderCommandRecorder {
public:
    TexturedDuckCommandRecorder(
        const VulkanContext& context,
        const VulkanSwapchain& swapchain,
        const VulkanShaderModule& vertexShader,
        const VulkanShaderModule& fragmentShader,
        const std::filesystem::path& scenePath,
        const std::filesystem::path& texturePath);
    void record(const RenderFrameContext& frame) override;
    const VulkanTexture2D& texture() const { return texture_; }

private:
    void createPipeline(const VulkanShaderModule& vertexShader, const VulkanShaderModule& fragmentShader);

    const VulkanContext& context_;
    const VulkanSwapchain& swapchain_;
    MeshData sceneData_;
    VulkanTexture2D texture_;
    VulkanDescriptorSet textureDescriptors_;
    VulkanBuffer vertexBuffer_;
    VulkanBuffer indexBuffer_;
    std::unique_ptr<VulkanRenderPipeline> pipeline_;
    VulkanDepthAttachment depthAttachment_;
    float meshCenter_[3] = {};
    float meshRadius_ = 1.0f;
    uint32_t indexCount_ = 0;
    std::vector<VkImageLayout> imageLayouts_;
};
