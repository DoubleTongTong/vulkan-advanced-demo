#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

struct GLFWwindow;

class VulkanContext {
public:
    explicit VulkanContext(GLFWwindow* window);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    VkInstance instance() const;
    VkSurfaceKHR surface() const;
    VkPhysicalDevice physicalDevice() const;
    VkDevice device() const;
    VkQueue graphicsQueue() const;
    uint32_t graphicsQueueFamilyIndex() const;

private:
    static constexpr uint32_t InvalidQueueFamily = UINT32_MAX;

    void createInstance();
    void createSurface(GLFWwindow* window);
    void pickPhysicalDevice();
    void createLogicalDevice();

    bool validationLayerAvailable() const;
    bool deviceSupportsRequiredExtensions(VkPhysicalDevice device) const;
    uint32_t findGraphicsPresentQueueFamily(VkPhysicalDevice device) const;
    int deviceScore(VkPhysicalDevice device) const;
    std::vector<const char*> requiredInstanceExtensions() const;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamilyIndex_ = InvalidQueueFamily;
    bool validationEnabled_ = false;
};
