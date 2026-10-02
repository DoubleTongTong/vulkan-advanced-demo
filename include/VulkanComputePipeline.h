#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

class VulkanContext;
class VulkanShaderModule;

struct ComputePipelineDesc {
    const VulkanShaderModule* shader = nullptr;
    std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
    const char* debugName = "Compute pipeline";
};

// Compute pipeline 的轻量 RAII 封装：拥有 pipeline layout 与 pipeline。
class VulkanComputePipeline final {
public:
    VulkanComputePipeline(const VulkanContext& context, const ComputePipelineDesc& desc);
    ~VulkanComputePipeline();

    VulkanComputePipeline(const VulkanComputePipeline&) = delete;
    VulkanComputePipeline& operator=(const VulkanComputePipeline&) = delete;

    VulkanComputePipeline(VulkanComputePipeline&& other) noexcept;
    VulkanComputePipeline& operator=(VulkanComputePipeline&& other) noexcept;

    VkPipelineLayout layout() const;
    uint32_t pushConstantSize() const;
    void bind(VkCommandBuffer commandBuffer) const;

private:
    void destroy();

    const VulkanContext* context_ = nullptr;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    uint32_t pushConstantSize_ = 0;
};
