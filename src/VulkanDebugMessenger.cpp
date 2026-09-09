#include "VulkanDebugMessenger.h"

#include "VulkanUtils.h"

#include <iostream>

namespace {

VkBool32 VKAPI_CALL onVulkanDebugMessage(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void*) {
    if (severity < VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        return VK_FALSE;
    }

    const bool isError = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0;
    const bool isWarning = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0;

    std::ostream& output = isError || isWarning ? std::cerr : std::cout;
    output << (isError ? "Vulkan error: " : isWarning ? "Vulkan warning: " : "Vulkan info: ")
           << callbackData->pMessage << '\n';

    // 返回 VK_FALSE 表示这条消息只用于调试输出，不拦截 Vulkan 调用本身。
    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT makeCreateInfo() {
    return {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = onVulkanDebugMessage,
    };
}

} // namespace

VulkanDebugMessenger::VulkanDebugMessenger(VkInstance instance)
    : instance_(instance) {
    const auto createDebugMessenger =
        reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));

    if (!createDebugMessenger) {
        return;
    }

    const VkDebugUtilsMessengerCreateInfoEXT createInfo = makeCreateInfo();
    vulkan_utils::checkVk(
        createDebugMessenger(instance_, &createInfo, nullptr, &messenger_),
        "vkCreateDebugUtilsMessengerEXT");
}

VulkanDebugMessenger::~VulkanDebugMessenger() {
    if (!messenger_) {
        return;
    }

    const auto destroyDebugMessenger =
        reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));

    if (destroyDebugMessenger) {
        destroyDebugMessenger(instance_, messenger_, nullptr);
    }
}
