#pragma once

#include "VulkanBuffer.h"
#include "VulkanComputePipeline.h"
#include "VulkanDescriptorSet.h"
#include "VulkanRenderPipeline.h"
#include "VulkanTexture2D.h"
#include "rendering/IRenderCommandRecorder.h"
#include "rendering/mesh/instanced/InstancedMesh.h"

#include <vulkan/vulkan.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <vector>

class VulkanContext;
class VulkanShaderModule;
class VulkanSwapchain;
struct VmaAllocation_T;
using VmaAllocation = VmaAllocation_T*;

// Compute shader 每帧计算模型矩阵，graphics pipeline 随后实例化绘制真实 glTF 网格。
class InstancedMeshesCommandRecorder final : public IRenderCommandRecorder {
public:
    InstancedMeshesCommandRecorder(
        const VulkanContext& context,
        const VulkanSwapchain& swapchain,
        const VulkanShaderModule& computeShader,
        const VulkanShaderModule& vertexShader,
        const VulkanShaderModule& fragmentShader,
        const std::filesystem::path& scenePath,
        const std::filesystem::path& texturePath);
    ~InstancedMeshesCommandRecorder();

    void record(const RenderFrameContext& frame) override;

private:
    void createDepthAttachment();
    void destroyDepthAttachment();

    const VulkanContext& context_;
    const VulkanSwapchain& swapchain_;
    VulkanTexture2D texture_;
    InstancedMesh mesh_;
    VulkanBuffer instances_;
    std::vector<VulkanBuffer> matrixBuffers_;
    VulkanDescriptorSet textureDescriptors_;
    std::vector<std::unique_ptr<VulkanDescriptorSet>> frameDescriptors_;
    std::unique_ptr<VulkanComputePipeline> computePipeline_;
    std::unique_ptr<VulkanRenderPipeline> renderPipeline_;
    std::chrono::steady_clock::time_point startTime_;
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkImageView depthImageView_ = VK_NULL_HANDLE;
    VmaAllocation depthAllocation_ = nullptr;
    VkImageLayout depthLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    std::vector<VkImageLayout> imageLayouts_;
};
