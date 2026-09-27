#pragma once

#include "rendering/descriptors/IVulkanDescriptorSet.h"
#include <vulkan/vulkan.h>

class VulkanBuffer;
class VulkanContext;
class VulkanShaderModule;

// PVP 专用 set=1：将顶点拉取 mesh 的 storage buffer 绑定到 shader 的 kVertices。
class VertexPullingDescriptorSet final : public IVulkanDescriptorSet {
public:
    VertexPullingDescriptorSet(
        const VulkanContext& context,
        const VulkanShaderModule& vertexShader,
        const VulkanBuffer& vertexStorageBuffer);
    ~VertexPullingDescriptorSet();

    VertexPullingDescriptorSet(const VertexPullingDescriptorSet&) = delete;
    VertexPullingDescriptorSet& operator=(const VertexPullingDescriptorSet&) = delete;

    VertexPullingDescriptorSet(VertexPullingDescriptorSet&& other) noexcept;
    VertexPullingDescriptorSet& operator=(VertexPullingDescriptorSet&& other) noexcept;

    VkDescriptorSetLayout layout() const override;
    void bind(
        VkCommandBuffer commandBuffer,
        VkPipelineLayout pipelineLayout,
        uint32_t setIndex) const override;

private:
    void destroy();

    const VulkanContext* context_ = nullptr;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
};
