#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

class VulkanContext;
struct RgbaImage;
struct VmaAllocation_T;
using VmaAllocation = VmaAllocation_T*;

class VulkanTexture2D {
public:
    VulkanTexture2D(const VulkanContext& context, const RgbaImage& image, const char* debugName);
    ~VulkanTexture2D();

    VulkanTexture2D(const VulkanTexture2D&) = delete;
    VulkanTexture2D& operator=(const VulkanTexture2D&) = delete;

    VulkanTexture2D(VulkanTexture2D&& other) noexcept;
    VulkanTexture2D& operator=(VulkanTexture2D&& other) noexcept;

    VkImageView imageView() const;
    VkSampler sampler() const;
    VkImageLayout layout() const;

private:
    void createImage(const RgbaImage& image, const char* debugName);
    void createImageView(const char* debugName);
    void createSampler(const char* debugName);
    void destroy();

    const VulkanContext* context_ = nullptr;
    VkImage image_ = VK_NULL_HANDLE;
    VkImageView imageView_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = nullptr;
    VkExtent2D extent_{};
    VkImageLayout layout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
};
