#pragma once

#include <vulkan/vulkan.h>

class VulkanDebugMessenger {
public:
    explicit VulkanDebugMessenger(VkInstance instance);
    ~VulkanDebugMessenger();

    VulkanDebugMessenger(const VulkanDebugMessenger&) = delete;
    VulkanDebugMessenger& operator=(const VulkanDebugMessenger&) = delete;

private:
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
};
