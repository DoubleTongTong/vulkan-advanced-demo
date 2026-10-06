#include "VulkanDepthAttachment.h"

#include "VulkanContext.h"
#include "VulkanUtils.h"

#include <vk_mem_alloc.h>

#include <stdexcept>
#include <string>

VulkanDepthAttachment::VulkanDepthAttachment(
    const VulkanContext& context,
    VkExtent2D extent,
    const char* debugName,
    VkFormat format)
    : context_(context), format_(format) {
    if (extent.width == 0 || extent.height == 0) {
        throw std::invalid_argument("Depth attachment extent must not be empty.");
    }
    if (format_ == VK_FORMAT_UNDEFINED) {
        throw std::invalid_argument("Depth attachment format must be defined.");
    }

    const VkImageCreateInfo imageInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format_,
        .extent = {extent.width, extent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    const VmaAllocationCreateInfo allocationInfo{
        .usage = VMA_MEMORY_USAGE_AUTO,
        .preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };

    try {
        vulkan_utils::checkVk(
            vmaCreateImage(
                context_.allocator(), &imageInfo, &allocationInfo,
                &image_, &allocation_, nullptr),
            "vmaCreateImage");

        const VkImageViewCreateInfo viewInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image_,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = format_,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        vulkan_utils::checkVk(
            vkCreateImageView(context_.device(), &viewInfo, nullptr, &view_),
            "vkCreateImageView");
    } catch (...) {
        destroy();
        throw;
    }

    const std::string name = debugName ? debugName : "Depth attachment";
    context_.setDebugObjectName(
        VK_OBJECT_TYPE_IMAGE,
        reinterpret_cast<uint64_t>(image_),
        (name + " image").c_str());
    context_.setDebugObjectName(
        VK_OBJECT_TYPE_IMAGE_VIEW,
        reinterpret_cast<uint64_t>(view_),
        (name + " view").c_str());
}

VulkanDepthAttachment::~VulkanDepthAttachment() {
    destroy();
}

VkFormat VulkanDepthAttachment::format() const {
    return format_;
}

VkImageView VulkanDepthAttachment::view() const {
    return view_;
}

void VulkanDepthAttachment::transitionForWrite(VkCommandBuffer commandBuffer) {
    const bool firstUse = layout_ == VK_IMAGE_LAYOUT_UNDEFINED;
    const VkPipelineStageFlags depthStages =
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

    vulkan_utils::transitionImage(
        commandBuffer,
        image_,
        VK_IMAGE_ASPECT_DEPTH_BIT,
        layout_,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        firstUse ? 0 : VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        firstUse ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : depthStages,
        depthStages);
    layout_ = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
}

VkRenderingAttachmentInfo VulkanDepthAttachment::renderingInfo(
    float clearDepth,
    uint32_t clearStencil) const {
    return {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = view_,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue = {.depthStencil = {clearDepth, clearStencil}},
    };
}

void VulkanDepthAttachment::destroy() {
    if (view_) {
        vkDestroyImageView(context_.device(), view_, nullptr);
        view_ = VK_NULL_HANDLE;
    }
    if (image_ && allocation_) {
        vmaDestroyImage(context_.allocator(), image_, allocation_);
        image_ = VK_NULL_HANDLE;
        allocation_ = nullptr;
    }
}
