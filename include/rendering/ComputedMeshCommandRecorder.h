#pragma once

#include "VulkanComputePipeline.h"
#include "VulkanDepthAttachment.h"
#include "VulkanDescriptorSet.h"
#include "VulkanRenderPipeline.h"
#include "VulkanStorageTexture2D.h"
#include "rendering/IRenderCommandRecorder.h"
#include "rendering/mesh/computed/VulkanComputedMesh.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <vector>

class VulkanContext;
class VulkanShaderModule;
class VulkanSwapchain;

struct TorusKnotParameters {
    uint32_t p = 1;
    uint32_t q = 1;

    bool operator==(const TorusKnotParameters&) const = default;
};

// Compute Shader 每帧生成环面结顶点和动态纹理，Graphics Pipeline 消费两者。
class ComputedMeshCommandRecorder final : public IRenderCommandRecorder {
public:
    ComputedMeshCommandRecorder(
        const VulkanContext& context,
        const VulkanSwapchain& swapchain,
        const VulkanShaderModule& meshComputeShader,
        const VulkanShaderModule& textureComputeShader,
        const VulkanShaderModule& vertexShader,
        const VulkanShaderModule& geometryShader,
        const VulkanShaderModule& fragmentShader);

    void record(const RenderFrameContext& frame) override;

    void requestKnot(TorusKnotParameters knot);
    const std::deque<TorusKnotParameters>& morphQueue() const;

    float animationSpeed() const;
    void setAnimationSpeed(float speed);
    bool useColoredMesh() const;
    void setUseColoredMesh(bool colored);
    float morphCoefficient() const;
    const VulkanStorageTexture2D& generatedTexture() const;

private:
    void advanceMorph(float deltaSeconds);

    static constexpr uint32_t NumU = 128;
    static constexpr uint32_t NumV = 32;
    static constexpr uint32_t MeshLocalSize = 64;
    static constexpr VkExtent2D TextureExtent{512, 512};

    const VulkanContext& context_;
    const VulkanSwapchain& swapchain_;
    VulkanComputedMesh mesh_;
    VulkanStorageTexture2D texture_;
    VulkanDescriptorSet meshWriteDescriptors_;
    VulkanDescriptorSet textureWriteDescriptors_;
    VulkanDescriptorSet textureReadDescriptors_;
    VulkanComputePipeline meshComputePipeline_;
    VulkanComputePipeline textureComputePipeline_;
    VulkanRenderPipeline renderPipeline_;
    VulkanDepthAttachment depthAttachment_;
    std::vector<VkImageLayout> imageLayouts_;
    std::deque<TorusKnotParameters> morphQueue_{{5, 8}, {5, 8}};
    float morphCoefficient_ = 0.0f;
    float animationSpeed_ = 1.0f;
    bool useColoredMesh_ = false;
    std::chrono::steady_clock::time_point startTime_;
    std::chrono::steady_clock::time_point previousFrameTime_;
};
