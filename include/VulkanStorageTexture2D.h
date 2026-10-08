#pragma once

#include <vulkan/vulkan.h>

struct VmaAllocation_T;
using VmaAllocation = VmaAllocation_T*;

class VulkanContext;

// GPU 可写、Fragment Shader 可采样的二维纹理。
// 类内维护真实布局，Descriptor 则分别固定声明 GENERAL 和 SHADER_READ_ONLY。
class VulkanStorageTexture2D final {
public:
    VulkanStorageTexture2D(
        const VulkanContext& context,
        VkExtent2D extent,
        const char* debugName,
        VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    ~VulkanStorageTexture2D();

    VulkanStorageTexture2D(const VulkanStorageTexture2D&) = delete;
    VulkanStorageTexture2D& operator=(const VulkanStorageTexture2D&) = delete;

    VkImageView imageView() const;
    VkSampler sampler() const;
    VkExtent2D extent() const;

    void transitionForComputeWrite(VkCommandBuffer commandBuffer);
    void transitionForSampling(VkCommandBuffer commandBuffer);

private:
    void destroy();

    const VulkanContext& context_;
    VkImage image_ = VK_NULL_HANDLE;
    VkImageView imageView_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = nullptr;
    VkExtent2D extent_{};
    VkImageLayout layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
};
