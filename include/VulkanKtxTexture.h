#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>

class VulkanContext;
struct VmaAllocation_T;
using VmaAllocation = VmaAllocation_T*;

// 直接加载未压缩 KTX2 的 2D 或 Cubemap 纹理，并保留文件中的完整 mip 链。
// 当前用于读取离线生成的 BRDF LUT、Irradiance 和 GGX 环境贴图。
class VulkanKtxTexture final {
public:
    VulkanKtxTexture(
        const VulkanContext& context,
        const std::filesystem::path& path,
        const char* debugName);
    ~VulkanKtxTexture();

    VulkanKtxTexture(const VulkanKtxTexture&) = delete;
    VulkanKtxTexture& operator=(const VulkanKtxTexture&) = delete;

    VulkanKtxTexture(VulkanKtxTexture&& other) noexcept;
    VulkanKtxTexture& operator=(VulkanKtxTexture&& other) noexcept;

    VkImageView imageView() const;
    VkSampler sampler() const;
    VkImageLayout layout() const;
    VkFormat format() const;
    uint32_t mipLevels() const;
    bool isCube() const;

private:
    void destroy();

    const VulkanContext* context_ = nullptr;
    VkImage image_ = VK_NULL_HANDLE;
    VkImageView imageView_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = nullptr;
    VkImageLayout layout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    uint32_t mipLevels_ = 0;
    bool isCube_ = false;
};
