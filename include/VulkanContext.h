#pragma once

#include "VulkanDebugMessenger.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
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
    VkSemaphore createSemaphore(const char* debugName = nullptr) const;
    VkSemaphore createTimelineSemaphore(uint64_t initialValue = 0, const char* debugName = nullptr) const;
    VkFence createFence(bool signaled = false, const char* debugName = nullptr) const;
    void setDebugObjectName(VkObjectType type, uint64_t handle, const char* name) const;

private:
    static constexpr uint32_t InvalidQueueFamily = UINT32_MAX;

    void createInstance();
    void createSurface(GLFWwindow* window);
    void pickPhysicalDevice();
    void createLogicalDevice();
    void loadDeviceDebugFunctions();

    bool validationLayerAvailable() const;
    bool instanceExtensionAvailable(const char* name) const;
    bool deviceSupportsRequiredExtensions(VkPhysicalDevice device) const;
    bool deviceSupportsRequiredFeatures(VkPhysicalDevice device) const;
    uint32_t findGraphicsPresentQueueFamily(VkPhysicalDevice device) const;
    int deviceScore(VkPhysicalDevice device) const;
    std::vector<const char*> requiredInstanceExtensions() const;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    PFN_vkSetDebugUtilsObjectNameEXT setDebugUtilsObjectName_ = nullptr;
    uint32_t graphicsQueueFamilyIndex_ = InvalidQueueFamily;
    bool validationEnabled_ = false;
    std::unique_ptr<VulkanDebugMessenger> debugMessenger_;
};
