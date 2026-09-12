#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

class VulkanContext;

class VulkanSwapchain {
public:
    struct AcquiredImage {
        uint32_t imageIndex = 0;
        VkImage image = VK_NULL_HANDLE;
        VkResult result = VK_SUCCESS;

        bool shouldRender() const;
    };

    VulkanSwapchain(const VulkanContext& context, uint32_t width, uint32_t height);
    ~VulkanSwapchain();

    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    VkSwapchainKHR handle() const;
    VkFormat imageFormat() const;
    VkExtent2D extent() const;
    VkImageUsageFlags imageUsage() const;
    const std::vector<VkImage>& images() const;
    const std::vector<VkImageView>& imageViews() const;
    AcquiredImage acquireNextImage(VkSemaphore imageAvailable) const;
    VkResult present(const AcquiredImage& image, VkSemaphore renderFinished) const;

private:
    void create(uint32_t width, uint32_t height);
    void loadImages();
    void createImageViews();

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
    VkImageUsageFlags imageUsage_ = 0;
    VkExtent2D extent_{};
    std::vector<VkImage> images_;
    std::vector<VkImageView> imageViews_;
};
