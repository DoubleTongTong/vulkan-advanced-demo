#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

class VulkanContext;

class VulkanSwapchain {
public:
    VulkanSwapchain(const VulkanContext& context, uint32_t width, uint32_t height);
    ~VulkanSwapchain();

    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    VkSwapchainKHR handle() const;
    VkFormat imageFormat() const;
    VkExtent2D extent() const;
    const std::vector<VkImage>& images() const;

private:
    void create(uint32_t width, uint32_t height);
    void loadImages();

    VkSurfaceCapabilitiesKHR querySurfaceCapabilities() const;
    std::vector<VkSurfaceFormatKHR> querySurfaceFormats() const;
    std::vector<VkPresentModeKHR> queryPresentModes() const;

    VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const;
    VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes) const;
    VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities, uint32_t width, uint32_t height) const;
    uint32_t chooseImageCount(const VkSurfaceCapabilitiesKHR& capabilities) const;
    VkImageUsageFlags chooseImageUsage(VkFormat format, const VkSurfaceCapabilitiesKHR& capabilities) const;

    const VulkanContext& context_;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat imageFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    std::vector<VkImage> images_;
};
