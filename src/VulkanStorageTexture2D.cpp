#include "VulkanStorageTexture2D.h"

#include "VulkanContext.h"
#include "VulkanUtils.h"

#include <vk_mem_alloc.h>

#include <stdexcept>
#include <string>

VulkanStorageTexture2D::VulkanStorageTexture2D(
    const VulkanContext& context,
    VkExtent2D extent,
    const char* debugName)
    : context_(context), extent_(extent) {
    if (extent_.width == 0 || extent_.height == 0) {
        throw std::invalid_argument("Storage texture extent must not be empty.");
    }

    const VkImageCreateInfo imageInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {extent_.width, extent_.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
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
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        vulkan_utils::checkVk(
            vkCreateImageView(context_.device(), &viewInfo, nullptr, &imageView_),
            "vkCreateImageView");

        const VkSamplerCreateInfo samplerInfo{
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .maxLod = 0.0f,
        };
        vulkan_utils::checkVk(
            vkCreateSampler(context_.device(), &samplerInfo, nullptr, &sampler_),
            "vkCreateSampler");
    } catch (...) {
        destroy();
        throw;
    }

    const std::string name = debugName ? debugName : "Storage texture";
    context_.setDebugObjectName(
        VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(image_), name.c_str());
    context_.setDebugObjectName(
        VK_OBJECT_TYPE_IMAGE_VIEW,
        reinterpret_cast<uint64_t>(imageView_),
        (name + " view").c_str());
    context_.setDebugObjectName(
        VK_OBJECT_TYPE_SAMPLER,
        reinterpret_cast<uint64_t>(sampler_),
        (name + " sampler").c_str());
}

VulkanStorageTexture2D::~VulkanStorageTexture2D() {
    destroy();
}

VkImageView VulkanStorageTexture2D::imageView() const {
    return imageView_;
}

VkSampler VulkanStorageTexture2D::sampler() const {
    return sampler_;
}

VkExtent2D VulkanStorageTexture2D::extent() const {
    return extent_;
}

void VulkanStorageTexture2D::transitionForComputeWrite(VkCommandBuffer commandBuffer) {
    const bool firstUse = layout_ == VK_IMAGE_LAYOUT_UNDEFINED;
    vulkan_utils::transitionImage(
        commandBuffer,
        image_,
        VK_IMAGE_ASPECT_COLOR_BIT,
        layout_,
        VK_IMAGE_LAYOUT_GENERAL,
        firstUse ? 0 : VK_ACCESS_SHADER_READ_BIT,
        VK_ACCESS_SHADER_WRITE_BIT,
        firstUse ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    layout_ = VK_IMAGE_LAYOUT_GENERAL;
}

void VulkanStorageTexture2D::transitionForSampling(VkCommandBuffer commandBuffer) {
    if (layout_ != VK_IMAGE_LAYOUT_GENERAL) {
        throw std::logic_error("Storage texture must be compute-writable before sampling.");
    }
    vulkan_utils::transitionImage(
        commandBuffer,
        image_,
        VK_IMAGE_ASPECT_COLOR_BIT,
        layout_,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    layout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void VulkanStorageTexture2D::destroy() {
    if (sampler_) {
        vkDestroySampler(context_.device(), sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
    if (imageView_) {
        vkDestroyImageView(context_.device(), imageView_, nullptr);
        imageView_ = VK_NULL_HANDLE;
    }
    if (image_ && allocation_) {
        vmaDestroyImage(context_.allocator(), image_, allocation_);
        image_ = VK_NULL_HANDLE;
        allocation_ = nullptr;
    }
}
