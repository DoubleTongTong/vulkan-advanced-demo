#include "VulkanSwapchain.h"

#include "VulkanContext.h"
#include "VulkanUtils.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

bool VulkanSwapchain::AcquiredImage::shouldRender() const {
    return result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR;
}

VulkanSwapchain::VulkanSwapchain(const VulkanContext& context, uint32_t width, uint32_t height)
    : context_(context) {
    create(width, height);
    loadImages();
}

VulkanSwapchain::~VulkanSwapchain() {
    if (swapchain_) {
        vkDestroySwapchainKHR(context_.device(), swapchain_, nullptr);
    }
}

VkSwapchainKHR VulkanSwapchain::handle() const {
    return swapchain_;
}

VkFormat VulkanSwapchain::imageFormat() const {
    return imageFormat_;
}

VkExtent2D VulkanSwapchain::extent() const {
    return extent_;
}

VkImageUsageFlags VulkanSwapchain::imageUsage() const {
    return imageUsage_;
}

const std::vector<VkImage>& VulkanSwapchain::images() const {
    return images_;
}

VulkanSwapchain::AcquiredImage VulkanSwapchain::acquireNextImage(VkSemaphore imageAvailable) const {
    AcquiredImage acquiredImage{};

    acquiredImage.result = vkAcquireNextImageKHR(
        context_.device(),
        swapchain_,
        std::numeric_limits<uint64_t>::max(),
        imageAvailable,
        VK_NULL_HANDLE,
        &acquiredImage.imageIndex);

    if (acquiredImage.shouldRender()) {
        acquiredImage.image = images_[acquiredImage.imageIndex];
    }

    return acquiredImage;
}

VkResult VulkanSwapchain::present(const AcquiredImage& image, VkSemaphore renderFinished) const {
    const VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = renderFinished ? 1u : 0u,
        .pWaitSemaphores = renderFinished ? &renderFinished : nullptr,
        .swapchainCount = 1,
        .pSwapchains = &swapchain_,
        .pImageIndices = &image.imageIndex,
    };

    return vkQueuePresentKHR(context_.graphicsQueue(), &presentInfo);
}

void VulkanSwapchain::create(uint32_t width, uint32_t height) {
    const VkSurfaceCapabilitiesKHR capabilities = querySurfaceCapabilities();
    const VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(querySurfaceFormats());
    const VkPresentModeKHR presentMode = choosePresentMode(queryPresentModes());

    imageFormat_ = surfaceFormat.format;
    extent_ = chooseExtent(capabilities, width, height);
    imageUsage_ = chooseImageUsage(surfaceFormat.format, capabilities);

    const bool opaqueSupported =
        (capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) != 0;
    const uint32_t queueFamilyIndex = context_.graphicsQueueFamilyIndex();

    const VkSwapchainCreateInfoKHR createInfo{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = context_.surface(),
        .minImageCount = chooseImageCount(capabilities),
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = extent_,
        .imageArrayLayers = 1,
        .imageUsage = imageUsage_,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 1,
        .pQueueFamilyIndices = &queueFamilyIndex,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = opaqueSupported ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        .presentMode = presentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };

    vulkan_utils::checkVk(vkCreateSwapchainKHR(context_.device(), &createInfo, nullptr, &swapchain_), "vkCreateSwapchainKHR");
    context_.setDebugObjectName(VK_OBJECT_TYPE_SWAPCHAIN_KHR, reinterpret_cast<uint64_t>(swapchain_), "Main swapchain");

    std::cout << "Swapchain image count: " << createInfo.minImageCount << '\n';
    std::cout << "Swapchain extent: " << extent_.width << "x" << extent_.height << '\n';
}

void VulkanSwapchain::loadImages() {
    uint32_t imageCount = 0;
    vulkan_utils::checkVk(vkGetSwapchainImagesKHR(context_.device(), swapchain_, &imageCount, nullptr), "vkGetSwapchainImagesKHR");

    images_.resize(imageCount);
    vulkan_utils::checkVk(vkGetSwapchainImagesKHR(context_.device(), swapchain_, &imageCount, images_.data()), "vkGetSwapchainImagesKHR");
}

VkSurfaceCapabilitiesKHR VulkanSwapchain::querySurfaceCapabilities() const {
    VkSurfaceCapabilitiesKHR capabilities{};
    vulkan_utils::checkVk(
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context_.physicalDevice(), context_.surface(), &capabilities),
        "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    return capabilities;
}

std::vector<VkSurfaceFormatKHR> VulkanSwapchain::querySurfaceFormats() const {
    uint32_t formatCount = 0;
    vulkan_utils::checkVk(
        vkGetPhysicalDeviceSurfaceFormatsKHR(context_.physicalDevice(), context_.surface(), &formatCount, nullptr),
        "vkGetPhysicalDeviceSurfaceFormatsKHR");

    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vulkan_utils::checkVk(
        vkGetPhysicalDeviceSurfaceFormatsKHR(context_.physicalDevice(), context_.surface(), &formatCount, formats.data()),
        "vkGetPhysicalDeviceSurfaceFormatsKHR");

    if (formats.empty()) {
        throw std::runtime_error("The selected Vulkan device does not expose surface formats.");
    }

    return formats;
}

std::vector<VkPresentModeKHR> VulkanSwapchain::queryPresentModes() const {
    uint32_t modeCount = 0;
    vulkan_utils::checkVk(
        vkGetPhysicalDeviceSurfacePresentModesKHR(context_.physicalDevice(), context_.surface(), &modeCount, nullptr),
        "vkGetPhysicalDeviceSurfacePresentModesKHR");

    std::vector<VkPresentModeKHR> modes(modeCount);
    vulkan_utils::checkVk(
        vkGetPhysicalDeviceSurfacePresentModesKHR(context_.physicalDevice(), context_.surface(), &modeCount, modes.data()),
        "vkGetPhysicalDeviceSurfacePresentModesKHR");

    if (modes.empty()) {
        throw std::runtime_error("The selected Vulkan device does not expose present modes.");
    }

    return modes;
}

VkSurfaceFormatKHR VulkanSwapchain::chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const {
    for (const VkSurfaceFormatKHR& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }

    for (const VkSurfaceFormatKHR& format : formats) {
        if (format.format == VK_FORMAT_R8G8B8A8_UNORM &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }

    return formats.front();
}

VkPresentModeKHR VulkanSwapchain::choosePresentMode(const std::vector<VkPresentModeKHR>& modes) const {
    if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != modes.end()) {
        return VK_PRESENT_MODE_MAILBOX_KHR;
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanSwapchain::chooseExtent(
    const VkSurfaceCapabilitiesKHR& capabilities,
    uint32_t width,
    uint32_t height) const {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    return {
        .width = std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
        .height = std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
    };
}

uint32_t VulkanSwapchain::chooseImageCount(const VkSurfaceCapabilitiesKHR& capabilities) const {
    const uint32_t desired = capabilities.minImageCount + 1;

    if (capabilities.maxImageCount > 0 && desired > capabilities.maxImageCount) {
        return capabilities.maxImageCount;
    }

    return desired;
}

VkImageUsageFlags VulkanSwapchain::chooseImageUsage(
    VkFormat format,
    const VkSurfaceCapabilitiesKHR& capabilities) const {
    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    // 额外用途必须先看 surface 是否支持，不能想加就加。
    if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0) {
        usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }

    if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0) {
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }

    VkFormatProperties formatProperties{};
    vkGetPhysicalDeviceFormatProperties(context_.physicalDevice(), format, &formatProperties);

    const bool storageImageSupportedBySurface =
        (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_STORAGE_BIT) != 0;
    const bool storageImageSupportedByFormat =
        (formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0;

    if (storageImageSupportedBySurface && storageImageSupportedByFormat) {
        usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    }

    return usage;
}
