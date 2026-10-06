#pragma once

#include "VulkanBuffer.h"
#include "VulkanDescriptorSet.h"
#include "VulkanDepthAttachment.h"
#include "VulkanRenderPipeline.h"
#include "VulkanTexture2D.h"
#include "VulkanTextureCube.h"
#include "mesh/MeshData.h"
#include "rendering/IRenderCommandRecorder.h"
#include "rendering/canvas/LineCanvas3D.h"

#include <vulkan/vulkan.h>

#include <filesystem>
#include <memory>
#include <vector>

class VulkanContext;
class VulkanShaderModule;
class VulkanSwapchain;

class ReflectiveDuckCommandRecorder final : public IRenderCommandRecorder {
public:
    ReflectiveDuckCommandRecorder(
        const VulkanContext& context,
        const VulkanSwapchain& swapchain,
        const VulkanShaderModule& vertexShader,
        const VulkanShaderModule& fragmentShader,
        const VulkanShaderModule& skyVertexShader,
        const VulkanShaderModule& skyFragmentShader,
        const std::filesystem::path& scenePath,
        const std::filesystem::path& texturePath,
        const std::filesystem::path& environmentPath);
    void record(const RenderFrameContext& frame) override;

private:
    static CubeMapImage loadEnvironment(const std::filesystem::path& environmentPath);

    void createPipelines(
        const VulkanShaderModule& vertexShader,
        const VulkanShaderModule& fragmentShader,
        const VulkanShaderModule& skyVertexShader,
        const VulkanShaderModule& skyFragmentShader);
    void buildDebugCanvas();

    const VulkanContext& context_;
    const VulkanSwapchain& swapchain_;
    LineCanvas3D lineCanvas_;
    MeshData sceneData_;
    VulkanTexture2D texture_;
    VulkanTextureCube environment_;
    VulkanDescriptorSet textureDescriptors_;
    VulkanBuffer vertexBuffer_;
    VulkanBuffer indexBuffer_;
    std::unique_ptr<VulkanRenderPipeline> duckPipeline_;
    std::unique_ptr<VulkanRenderPipeline> skyPipeline_;
    VulkanDepthAttachment depthAttachment_;
    float meshCenter_[3] = {};
    float meshRadius_ = 1.0f;
    uint32_t indexCount_ = 0;
    std::vector<VkImageLayout> imageLayouts_;
};
