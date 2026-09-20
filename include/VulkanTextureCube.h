#pragma once

#include <vulkan/vulkan.h>

class VulkanContext;
struct CubeMapImage;
struct VmaAllocation_T;
using VmaAllocation = VmaAllocation_T*;

class VulkanTextureCube {
public:
    VulkanTextureCube(const VulkanContext& context, const CubeMapImage& image, const char* debugName);
    ~VulkanTextureCube();

    VulkanTextureCube(const VulkanTextureCube&) = delete;
    VulkanTextureCube& operator=(const VulkanTextureCube&) = delete;

    VulkanTextureCube(VulkanTextureCube&& other) noexcept;
    VulkanTextureCube& operator=(VulkanTextureCube&& other) noexcept;

    VkImageView imageView() const;
    VkSampler sampler() const;
    VkImageLayout layout() const;

private:
    void createImage(uint32_t faceSize, const char* debugName);
    void createImageView(const char* debugName);
    void createSampler(const char* debugName);
    void destroy();

    const VulkanContext* context_ = nullptr;
    VkImage image_ = VK_NULL_HANDLE;
    VkImageView imageView_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = nullptr;
    VkImageLayout layout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
};
