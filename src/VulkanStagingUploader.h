#pragma once

#include "VulkanImmediateCommands.h"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <memory>

class VulkanBuffer;
class VulkanContext;

class VulkanStagingUploader {
public:
    explicit VulkanStagingUploader(const VulkanContext& context);
    ~VulkanStagingUploader();

    VulkanStagingUploader(const VulkanStagingUploader&) = delete;
    VulkanStagingUploader& operator=(const VulkanStagingUploader&) = delete;

    void uploadBuffer(VulkanBuffer& destination, size_t dstOffset, size_t byteSize, const void* data);
    void uploadImage2D(
        VkImage image,
        VkExtent2D extent,
        const void* data,
        size_t byteSize,
        VkImageLayout oldLayout,
        VkImageLayout finalLayout,
        size_t bytesPerPixel);

private:
    static constexpr VkDeviceSize MinStagingBufferSize = 16ull * 1024ull * 1024ull;
    static constexpr VkDeviceSize MaxStagingBufferSize = 64ull * 1024ull * 1024ull;

    void ensureStagingBuffer(VkDeviceSize byteSize);
    VkAccessFlags destinationAccessMask(VkBufferUsageFlags usageFlags) const;
    VkPipelineStageFlags destinationStageMask(VkBufferUsageFlags usageFlags) const;

    const VulkanContext& context_;
    VulkanImmediateCommands commands_;
    std::unique_ptr<VulkanBuffer> stagingBuffer_;
};
