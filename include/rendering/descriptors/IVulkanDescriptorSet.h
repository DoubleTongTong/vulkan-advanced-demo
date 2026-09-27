#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

// 所有 descriptor set 的最小共同能力：提供 layout，并在指定 set 位置完成绑定。
class IVulkanDescriptorSet {
public:
    virtual ~IVulkanDescriptorSet() = default;

    virtual VkDescriptorSetLayout layout() const = 0;
    virtual void bind(
        VkCommandBuffer commandBuffer,
        VkPipelineLayout pipelineLayout,
        uint32_t setIndex) const = 0;
};
