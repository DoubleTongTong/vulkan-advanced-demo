#include "VulkanTextureCube.h"

#include "Image.h"
#include "VulkanContext.h"
#include "VulkanUtils.h"

#include <vk_mem_alloc.h>

#include <stdexcept>
#include <string>
#include <utility>

VulkanTextureCube::VulkanTextureCube(
    const VulkanContext& context,
    const CubeMapImage& image,
    const char* debugName)
    : context_(&context) {
    const size_t facePixelCount = static_cast<size_t>(image.faceSize) * image.faceSize;
    if (image.faceSize == 0 || image.pixels.size() != facePixelCount * 6u * 4u) {
        throw std::runtime_error("Cannot create a Vulkan cube texture from invalid face data.");
    }

    createImage(image.faceSize, debugName);

    // Cube map 在 Vulkan 中是一张有六个 array layer 的二维图像。
    const size_t faceByteSize = facePixelCount * 4u * sizeof(float);
    for (uint32_t face = 0; face < 6; ++face) {
        context_->uploadImage2D(
            image_,
            {image.faceSize, image.faceSize},
            image.pixels.data() + facePixelCount * 4u * face,
            faceByteSize,
            VK_IMAGE_LAYOUT_UNDEFINED,
            layout_,
            4u * sizeof(float),
            face);
    }

    createImageView(debugName);
    createSampler(debugName);
}

VulkanTextureCube::~VulkanTextureCube() {
    destroy();
}

VulkanTextureCube::VulkanTextureCube(VulkanTextureCube&& other) noexcept {
    *this = std::move(other);
}

VulkanTextureCube& VulkanTextureCube::operator=(VulkanTextureCube&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    destroy();
    context_ = other.context_;
    image_ = other.image_;
    imageView_ = other.imageView_;
    sampler_ = other.sampler_;
    allocation_ = other.allocation_;
    layout_ = other.layout_;

    other.context_ = nullptr;
    other.image_ = VK_NULL_HANDLE;
    other.imageView_ = VK_NULL_HANDLE;
    other.sampler_ = VK_NULL_HANDLE;
    other.allocation_ = nullptr;
    return *this;
}

VkImageView VulkanTextureCube::imageView() const {
    return imageView_;
}

VkSampler VulkanTextureCube::sampler() const {
    return sampler_;
}

VkImageLayout VulkanTextureCube::layout() const {
    return layout_;
}

void VulkanTextureCube::createImage(uint32_t faceSize, const char* debugName) {
    const VkImageCreateInfo imageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
        .extent = {faceSize, faceSize, 1},
        .mipLevels = 1,
        .arrayLayers = 6,
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
            context_->allocator(), &imageCreateInfo, &allocationCreateInfo,
            &image_, &allocation_, nullptr),
        "vmaCreateImage");
    context_->setDebugObjectName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(image_), debugName);
}

void VulkanTextureCube::createImageView(const char* debugName) {
    const VkImageViewCreateInfo viewCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image_,
        .viewType = VK_IMAGE_VIEW_TYPE_CUBE,
        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
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
            .layerCount = 6,
        },
    };

    vulkan_utils::checkVk(
        vkCreateImageView(context_->device(), &viewCreateInfo, nullptr, &imageView_),
        "vkCreateImageView");
    const std::string viewName = std::string(debugName ? debugName : "Cube texture") + " view";
    context_->setDebugObjectName(
        VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(imageView_), viewName.c_str());
}

void VulkanTextureCube::createSampler(const char* debugName) {
    const VkSamplerCreateInfo samplerCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 1.0f,
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    };

    vulkan_utils::checkVk(
        vkCreateSampler(context_->device(), &samplerCreateInfo, nullptr, &sampler_),
        "vkCreateSampler");
    const std::string samplerName = std::string(debugName ? debugName : "Cube texture") + " sampler";
    context_->setDebugObjectName(
        VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<uint64_t>(sampler_), samplerName.c_str());
}

void VulkanTextureCube::destroy() {
    if (!context_) {
        return;
    }
    if (sampler_) {
        vkDestroySampler(context_->device(), sampler_, nullptr);
    }
    if (imageView_) {
        vkDestroyImageView(context_->device(), imageView_, nullptr);
    }
    if (image_ && allocation_) {
        vmaDestroyImage(context_->allocator(), image_, allocation_);
    }

    context_ = nullptr;
    image_ = VK_NULL_HANDLE;
    imageView_ = VK_NULL_HANDLE;
    sampler_ = VK_NULL_HANDLE;
    allocation_ = nullptr;
}
