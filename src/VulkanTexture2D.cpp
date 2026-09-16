#include "VulkanTexture2D.h"

#include "Image.h"
#include "VulkanContext.h"
#include "VulkanUtils.h"

#include <vk_mem_alloc.h>

#include <stdexcept>
#include <utility>

VulkanTexture2D::VulkanTexture2D(
    const VulkanContext& context,
    const RgbaImage& image,
    const char* debugName)
    : context_(&context) {
    if (image.width == 0 || image.height == 0 || image.pixels.empty()) {
        throw std::runtime_error("Cannot create a Vulkan texture from an empty image.");
    }

    createImage(image, debugName);
    context_->uploadImage2D(
        image_,
        extent_,
        image.pixels.data(),
        image.pixels.size(),
        VK_IMAGE_LAYOUT_UNDEFINED,
        layout_,
        4);
    createImageView(debugName);
    createSampler(debugName);
}

VulkanTexture2D::~VulkanTexture2D() {
    destroy();
}

VulkanTexture2D::VulkanTexture2D(VulkanTexture2D&& other) noexcept {
    *this = std::move(other);
}

VulkanTexture2D& VulkanTexture2D::operator=(VulkanTexture2D&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    destroy();

    context_ = other.context_;
    image_ = other.image_;
    imageView_ = other.imageView_;
    sampler_ = other.sampler_;
    allocation_ = other.allocation_;
    extent_ = other.extent_;
    layout_ = other.layout_;

    other.context_ = nullptr;
    other.image_ = VK_NULL_HANDLE;
    other.imageView_ = VK_NULL_HANDLE;
    other.sampler_ = VK_NULL_HANDLE;
    other.allocation_ = nullptr;
    other.extent_ = {};

    return *this;
}

VkImageView VulkanTexture2D::imageView() const {
    return imageView_;
}

VkSampler VulkanTexture2D::sampler() const {
    return sampler_;
}

VkImageLayout VulkanTexture2D::layout() const {
    return layout_;
}

void VulkanTexture2D::createImage(const RgbaImage& image, const char* debugName) {
    extent_ = {
        .width = image.width,
        .height = image.height,
    };

    const VkImageCreateInfo imageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {
            .width = image.width,
            .height = image.height,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    const VmaAllocationCreateInfo allocationCreateInfo{
        .usage = VMA_MEMORY_USAGE_AUTO,
        .preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };

    vulkan_utils::checkVk(
        vmaCreateImage(
            context_->allocator(),
            &imageCreateInfo,
            &allocationCreateInfo,
            &image_,
            &allocation_,
            nullptr),
        "vmaCreateImage");
    context_->setDebugObjectName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(image_), debugName);
}

void VulkanTexture2D::createImageView(const char* debugName) {
    const VkImageViewCreateInfo viewCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image_,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .components = {
            .r = VK_COMPONENT_SWIZZLE_IDENTITY,
            .g = VK_COMPONENT_SWIZZLE_IDENTITY,
            .b = VK_COMPONENT_SWIZZLE_IDENTITY,
            .a = VK_COMPONENT_SWIZZLE_IDENTITY,
        },
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    vulkan_utils::checkVk(
        vkCreateImageView(context_->device(), &viewCreateInfo, nullptr, &imageView_),
        "vkCreateImageView");
    context_->setDebugObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(imageView_), debugName);
}

void VulkanTexture2D::createSampler(const char* debugName) {
    const VkSamplerCreateInfo samplerCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 1.0f,
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    };

    vulkan_utils::checkVk(
        vkCreateSampler(context_->device(), &samplerCreateInfo, nullptr, &sampler_),
        "vkCreateSampler");
    context_->setDebugObjectName(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<uint64_t>(sampler_), debugName);
}

void VulkanTexture2D::destroy() {
    if (!context_) {
        return;
    }

    if (sampler_) {
        vkDestroySampler(context_->device(), sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }

    if (imageView_) {
        vkDestroyImageView(context_->device(), imageView_, nullptr);
        imageView_ = VK_NULL_HANDLE;
    }

    if (image_ && allocation_) {
        vmaDestroyImage(context_->allocator(), image_, allocation_);
        image_ = VK_NULL_HANDLE;
        allocation_ = nullptr;
    }
}
