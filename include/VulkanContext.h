#pragma once

#include "VulkanDebugMessenger.h"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

class VulkanBuffer;
class VulkanStagingUploader;
struct GLFWwindow;
struct VmaAllocator_T;
using VmaAllocator = VmaAllocator_T*;

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
    VmaAllocator allocator() const;
    VkQueue graphicsQueue() const;
    uint32_t graphicsQueueFamilyIndex() const;
    VkSemaphore createSemaphore(const char* debugName = nullptr) const;
    VkSemaphore createTimelineSemaphore(uint64_t initialValue = 0, const char* debugName = nullptr) const;
    VkFence createFence(bool signaled = false, const char* debugName = nullptr) const;
    void setDebugObjectName(VkObjectType type, uint64_t handle, const char* name) const;
    void uploadBuffer(VulkanBuffer& destination, size_t dstOffset, size_t byteSize, const void* data) const;
    void uploadImage2D(
        VkImage image,
        VkExtent2D extent,
        const void* data,
        size_t byteSize,
        VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        VkImageLayout finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        size_t bytesPerPixel = 4) const;

private:
    static constexpr uint32_t InvalidQueueFamily = UINT32_MAX;

    void createInstance();
    void createSurface(GLFWwindow* window);
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createAllocator();
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
    VmaAllocator allocator_ = nullptr;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    PFN_vkSetDebugUtilsObjectNameEXT setDebugUtilsObjectName_ = nullptr;
    std::unique_ptr<VulkanStagingUploader> stagingUploader_;
    uint32_t graphicsQueueFamilyIndex_ = InvalidQueueFamily;
    bool validationEnabled_ = false;
    std::unique_ptr<VulkanDebugMessenger> debugMessenger_;
};
