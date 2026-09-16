#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

class VulkanContext;
class VulkanTexture2D;

// 管理一张 bindless 资源表：
// binding 0 保存 sampler table，binding 1 保存 sampled image runtime array。
class VulkanBindlessDescriptorSet {
public:
    VulkanBindlessDescriptorSet(
        const VulkanContext& context,
        uint32_t maxTextures,
        uint32_t maxSamplers,
        const char* debugName);
    ~VulkanBindlessDescriptorSet();

    VulkanBindlessDescriptorSet(const VulkanBindlessDescriptorSet&) = delete;
    VulkanBindlessDescriptorSet& operator=(const VulkanBindlessDescriptorSet&) = delete;

    VulkanBindlessDescriptorSet(VulkanBindlessDescriptorSet&& other) noexcept;
    VulkanBindlessDescriptorSet& operator=(VulkanBindlessDescriptorSet&& other) noexcept;

    VkDescriptorSetLayout layout() const;
    VkDescriptorSet set() const;
    uint32_t maxTextures() const;
    uint32_t maxSamplers() const;

    void fillSamplers(VkSampler sampler);
    void writeSampler(uint32_t samplerIndex, VkSampler sampler);
    void writeTexture2D(uint32_t textureIndex, const VulkanTexture2D& texture);
    void bind(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, uint32_t setIndex = 0) const;

private:
    void createLayout(const char* debugName);
    void createPoolAndSet(const char* debugName);
    void destroy();

    const VulkanContext* context_ = nullptr;
    uint32_t maxTextures_ = 0;
    uint32_t maxSamplers_ = 0;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
};
