#include "VulkanKtxTexture.h"

#include "VulkanBuffer.h"
#include "VulkanContext.h"
#include "VulkanImmediateCommands.h"
#include "VulkanUtils.h"

#include <ktx.h>
#include <vk_mem_alloc.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

struct KtxDeleter {
    void operator()(ktxTexture2* texture) const {
        ktxTexture2_Destroy(texture);
    }
};

using KtxTexture = std::unique_ptr<ktxTexture2, KtxDeleter>;

void checkKtx(KTX_error_code result, const char* action) {
    if (result != KTX_SUCCESS) {
        throw std::runtime_error(std::string(action) + ": " + ktxErrorString(result));
    }
}

KtxTexture loadKtx(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path)) {
        throw std::runtime_error("KTX2 texture does not exist: " + path.string());
    }

    ktxTexture2* rawTexture = nullptr;
    checkKtx(
        ktxTexture2_CreateFromNamedFile(
            path.string().c_str(),
            KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
            &rawTexture),
        "Failed to load KTX2 texture");
    return KtxTexture(rawTexture);
}

size_t align4(size_t value) {
    return (value + 3u) & ~size_t(3u);
}

} // namespace

VulkanKtxTexture::VulkanKtxTexture(
    const VulkanContext& context,
    const std::filesystem::path& path,
    const char* debugName)
    : context_(&context) {
    const KtxTexture texture = loadKtx(path);
    if (texture->isCompressed || texture->numDimensions != 2 || texture->numLayers != 1 ||
        texture->baseDepth != 1 || (texture->numFaces != 1 && texture->numFaces != 6) ||
        texture->vkFormat == VK_FORMAT_UNDEFINED || texture->numLevels == 0) {
        throw std::runtime_error(
            "KTX2 texture must be an uncompressed 2D image or cubemap: " + path.string());
    }

    format_ = static_cast<VkFormat>(texture->vkFormat);
    mipLevels_ = texture->numLevels;
    isCube_ = texture->numFaces == 6;
    const uint32_t layerCount = isCube_ ? 6u : 1u;

    const VkImageCreateInfo imageInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags = isCube_ ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0u,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format_,
        .extent = {texture->baseWidth, texture->baseHeight, 1},
        .mipLevels = mipLevels_,
        .arrayLayers = layerCount,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    const VmaAllocationCreateInfo allocationInfo{
        .usage = VMA_MEMORY_USAGE_AUTO,
        .preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };
    vulkan_utils::checkVk(
        vmaCreateImage(
            context.allocator(),
            &imageInfo,
            &allocationInfo,
            &image_,
            &allocation_,
            nullptr),
        "vmaCreateImage");

    try {
        context.setDebugObjectName(
            VK_OBJECT_TYPE_IMAGE,
            reinterpret_cast<uint64_t>(image_),
            debugName);

        std::vector<uint8_t> uploadBytes;
        std::vector<VkBufferImageCopy> copyRegions;
        const uint8_t* sourceBytes = ktxTexture_GetData(ktxTexture(texture.get()));
        for (uint32_t mip = 0; mip < mipLevels_; ++mip) {
            const size_t imageSize = ktxTexture_GetImageSize(ktxTexture(texture.get()), mip);
            for (uint32_t face = 0; face < layerCount; ++face) {
                ktx_size_t sourceOffset = 0;
                checkKtx(
                    ktxTexture_GetImageOffset(
                        ktxTexture(texture.get()), mip, 0, face, &sourceOffset),
                    "Failed to find KTX2 image offset");
                const size_t destinationOffset = align4(uploadBytes.size());
                uploadBytes.resize(destinationOffset + imageSize);
                std::memcpy(
                    uploadBytes.data() + destinationOffset,
                    sourceBytes + sourceOffset,
                    imageSize);

                copyRegions.push_back({
                    .bufferOffset = destinationOffset,
                    .bufferRowLength = 0,
                    .bufferImageHeight = 0,
                    .imageSubresource = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .mipLevel = mip,
                        .baseArrayLayer = face,
                        .layerCount = 1,
                    },
                    .imageOffset = {0, 0, 0},
                    .imageExtent = {
                        .width = std::max(texture->baseWidth >> mip, 1u),
                        .height = std::max(texture->baseHeight >> mip, 1u),
                        .depth = 1,
                    },
                });
            }
        }

        const VulkanBuffer stagingBuffer(
            context,
            BufferDesc{
                .usage = BufferUsage_TransferSrc,
                .storage = BufferStorage::HostVisible,
                .size = uploadBytes.size(),
                .data = uploadBytes.data(),
                .debugName = "KTX2 texture staging buffer",
            });
        VulkanImmediateCommands commands(context, "KTX2 texture upload commands");
        const VulkanImmediateCommands::CommandBuffer& commandBuffer = commands.acquire();
        vulkan_utils::transitionImage(
            commandBuffer.commandBuffer,
            image_,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            0,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            layerCount,
            0,
            mipLevels_);
        vkCmdCopyBufferToImage(
            commandBuffer.commandBuffer,
            stagingBuffer.handle(),
            image_,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            static_cast<uint32_t>(copyRegions.size()),
            copyRegions.data());
        vulkan_utils::transitionImage(
            commandBuffer.commandBuffer,
            image_,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            layout_,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            layerCount,
            0,
            mipLevels_);
        commands.wait(commands.submit(commandBuffer));

        const VkImageViewCreateInfo viewInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image_,
            .viewType = isCube_ ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D,
            .format = format_,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = mipLevels_,
                .baseArrayLayer = 0,
                .layerCount = layerCount,
            },
        };
        vulkan_utils::checkVk(
            vkCreateImageView(context.device(), &viewInfo, nullptr, &imageView_),
            "vkCreateImageView");

        const VkSamplerCreateInfo samplerInfo{
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .maxLod = static_cast<float>(mipLevels_ - 1),
        };
        vulkan_utils::checkVk(
            vkCreateSampler(context.device(), &samplerInfo, nullptr, &sampler_),
            "vkCreateSampler");
    } catch (...) {
        destroy();
        throw;
    }
}

VulkanKtxTexture::~VulkanKtxTexture() {
    destroy();
}

VulkanKtxTexture::VulkanKtxTexture(VulkanKtxTexture&& other) noexcept {
    *this = std::move(other);
}

VulkanKtxTexture& VulkanKtxTexture::operator=(VulkanKtxTexture&& other) noexcept {
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
    format_ = other.format_;
    mipLevels_ = other.mipLevels_;
    isCube_ = other.isCube_;

    other.context_ = nullptr;
    other.image_ = VK_NULL_HANDLE;
    other.imageView_ = VK_NULL_HANDLE;
    other.sampler_ = VK_NULL_HANDLE;
    other.allocation_ = nullptr;
    other.mipLevels_ = 0;
    other.isCube_ = false;
    return *this;
}

VkImageView VulkanKtxTexture::imageView() const { return imageView_; }
VkSampler VulkanKtxTexture::sampler() const { return sampler_; }
VkImageLayout VulkanKtxTexture::layout() const { return layout_; }
VkFormat VulkanKtxTexture::format() const { return format_; }
uint32_t VulkanKtxTexture::mipLevels() const { return mipLevels_; }
bool VulkanKtxTexture::isCube() const { return isCube_; }

void VulkanKtxTexture::destroy() {
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
