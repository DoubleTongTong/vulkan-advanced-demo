#pragma once

#include <vulkan/vulkan.h>

struct VmaAllocation_T;
using VmaAllocation = VmaAllocation_T*;

class VulkanContext;

// 与一个渲染尺寸绑定的深度附件。类内统一维护 Image、VMA 分配、ImageView、
// 当前布局以及跨帧写后写同步，Recorder 只负责把它放进 RenderingInfo。
class VulkanDepthAttachment final {
public:
    static constexpr VkFormat DefaultFormat = VK_FORMAT_D32_SFLOAT;

    VulkanDepthAttachment(
        const VulkanContext& context,
        VkExtent2D extent,
        const char* debugName,
        VkFormat format = DefaultFormat);
    ~VulkanDepthAttachment();

    VulkanDepthAttachment(const VulkanDepthAttachment&) = delete;
    VulkanDepthAttachment& operator=(const VulkanDepthAttachment&) = delete;

    VkFormat format() const;
    VkImageView view() const;

    // 首次使用负责 UNDEFINED -> DEPTH_ATTACHMENT_OPTIMAL；后续使用建立前后帧
    // 深度写之间的执行与内存依赖。
    void transitionForWrite(VkCommandBuffer commandBuffer);

    VkRenderingAttachmentInfo renderingInfo(
        float clearDepth = 1.0f,
        uint32_t clearStencil = 0) const;

private:
    void destroy();

    const VulkanContext& context_;
    VkImage image_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = nullptr;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkImageLayout layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
};
