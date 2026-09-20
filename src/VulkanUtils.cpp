#include "VulkanUtils.h"

#include <string>
#include <stdexcept>

namespace vulkan_utils {

void checkVk(VkResult result, const char* action) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(action) + " failed with VkResult " + std::to_string(result) + ".");
    }
}

void transitionImage(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageAspectFlags aspectMask,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkAccessFlags srcAccess,
    VkAccessFlags dstAccess,
    VkPipelineStageFlags srcStage,
    VkPipelineStageFlags dstStage,
    uint32_t baseArrayLayer,
    uint32_t layerCount) {
    const VkImageSubresourceRange subresourceRange{
        .aspectMask = aspectMask,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = baseArrayLayer,
        .layerCount = layerCount,
    };

    const VkImageMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = srcAccess,
        .dstAccessMask = dstAccess,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = subresourceRange,
    };

    vkCmdPipelineBarrier(
        commandBuffer,
        srcStage,
        dstStage,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier);
}

} // namespace vulkan_utils
