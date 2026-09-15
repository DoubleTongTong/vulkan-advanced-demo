#include "VulkanStagingUploader.h"

#include "VulkanBuffer.h"
#include "VulkanContext.h"
#include "VulkanUtils.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>

VulkanStagingUploader::VulkanStagingUploader(const VulkanContext& context)
    : context_(context), commands_(context, "Upload immediate commands") {
}

VulkanStagingUploader::~VulkanStagingUploader() {
    // staging buffer 可能仍被 GPU copy 命令读取，释放前必须等上传队列清干净。
    commands_.waitAll();
}

void VulkanStagingUploader::uploadBuffer(
    VulkanBuffer& destination,
    size_t dstOffset,
    size_t byteSize,
    const void* data) {
    if (!data || byteSize == 0) {
        return;
    }

    if (dstOffset + byteSize > destination.size()) {
        throw std::runtime_error("Staging buffer upload is out of destination buffer range.");
    }

    if (destination.isMapped()) {
        // HostVisible buffer 本来就能被 CPU 写入，不需要再绕 staging。
        destination.bufferSubData(dstOffset, byteSize, data);
        return;
    }

    // Device-local buffer 不能直接写，先写入 CPU 可见 staging buffer，再由 GPU copy 过去。
    // staging buffer 有常驻上限；超大资源会拆成多次 copy，避免一次上传后永久占住大块内存。
    ensureStagingBuffer(static_cast<VkDeviceSize>(byteSize));

    size_t copiedBytes = 0;
    while (copiedBytes < byteSize) {
        const size_t chunkSize = std::min(
            byteSize - copiedBytes,
            static_cast<size_t>(stagingBuffer_->size()));
        stagingBuffer_->bufferSubData(0, chunkSize, static_cast<const uint8_t*>(data) + copiedBytes);

        const VulkanImmediateCommands::CommandBuffer& commandBuffer = commands_.acquire();
        const VkBufferCopy copyRegion{
            .srcOffset = 0,
            .dstOffset = static_cast<VkDeviceSize>(dstOffset + copiedBytes),
            .size = static_cast<VkDeviceSize>(chunkSize),
        };
        vkCmdCopyBuffer(commandBuffer.commandBuffer, stagingBuffer_->handle(), destination.handle(), 1, &copyRegion);

        // copy 写入后加一道屏障，让后续 vertex/index/shader 读取一定能看到最新内容。
        const VkBufferMemoryBarrier barrier{
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = destinationAccessMask(destination.usageFlags()),
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = destination.handle(),
            .offset = static_cast<VkDeviceSize>(dstOffset + copiedBytes),
            .size = static_cast<VkDeviceSize>(chunkSize),
        };
        vkCmdPipelineBarrier(
            commandBuffer.commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            destinationStageMask(destination.usageFlags()),
            0,
            0,
            nullptr,
            1,
            &barrier,
            0,
            nullptr);

        const VulkanImmediateCommands::SubmitHandle submitHandle = commands_.submit(commandBuffer);
        commands_.wait(submitHandle);

        copiedBytes += chunkSize;
    }
}

void VulkanStagingUploader::uploadImage2D(
    VkImage image,
    VkExtent2D extent,
    const void* data,
    size_t byteSize,
    VkImageLayout oldLayout,
    VkImageLayout finalLayout,
    size_t bytesPerPixel) {
    if (!image || !data || byteSize == 0) {
        return;
    }

    if (extent.width == 0 || extent.height == 0 || bytesPerPixel == 0) {
        throw std::runtime_error("Cannot upload an empty Vulkan image.");
    }

    const size_t rowSize = static_cast<size_t>(extent.width) * bytesPerPixel;
    const size_t expectedSize = rowSize * static_cast<size_t>(extent.height);
    if (byteSize < expectedSize) {
        throw std::runtime_error("Image upload data is smaller than the requested image extent.");
    }

    if (rowSize > static_cast<size_t>(MaxStagingBufferSize)) {
        throw std::runtime_error("A single image row is larger than the maximum staging buffer size.");
    }

    ensureStagingBuffer(static_cast<VkDeviceSize>(std::min(expectedSize, static_cast<size_t>(MaxStagingBufferSize))));

    uint32_t copiedRows = 0;
    while (copiedRows < extent.height) {
        const size_t rowsLeft = static_cast<size_t>(extent.height - copiedRows);
        const size_t rowsPerChunk = std::max<size_t>(1, static_cast<size_t>(stagingBuffer_->size()) / rowSize);
        const uint32_t chunkRows = static_cast<uint32_t>(std::min(rowsLeft, rowsPerChunk));
        const size_t chunkSize = rowSize * chunkRows;
        const bool isFirstChunk = copiedRows == 0;
        const bool isLastChunk = copiedRows + chunkRows == extent.height;

        stagingBuffer_->bufferSubData(
            0,
            chunkSize,
            static_cast<const uint8_t*>(data) + rowSize * copiedRows);

        const VulkanImmediateCommands::CommandBuffer& commandBuffer = commands_.acquire();
        if (isFirstChunk) {
            // 图片 copy 前必须进入 TRANSFER_DST_OPTIMAL，否则 Vulkan 不允许写入图像内存。
            vulkan_utils::transitionImage(
                commandBuffer.commandBuffer,
                image,
                VK_IMAGE_ASPECT_COLOR_BIT,
                oldLayout,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                0,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT);
        }

        const VkBufferImageCopy copyRegion{
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .imageOffset = {0, static_cast<int32_t>(copiedRows), 0},
            .imageExtent = {
                .width = extent.width,
                .height = chunkRows,
                .depth = 1,
            },
        };
        vkCmdCopyBufferToImage(
            commandBuffer.commandBuffer,
            stagingBuffer_->handle(),
            image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &copyRegion);

        if (isLastChunk) {
            // 上传完成后切到调用方希望的最终布局，常见情况是 shader read。
            vulkan_utils::transitionImage(
                commandBuffer.commandBuffer,
                image,
                VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                finalLayout,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        }

        const VulkanImmediateCommands::SubmitHandle submitHandle = commands_.submit(commandBuffer);
        commands_.wait(submitHandle);

        copiedRows += chunkRows;
    }
}

void VulkanStagingUploader::ensureStagingBuffer(VkDeviceSize byteSize) {
    if (stagingBuffer_ && stagingBuffer_->size() >= byteSize) {
        return;
    }

    // 扩容前先等旧提交结束，避免 GPU 还在读旧 staging buffer 时就释放它。
    commands_.waitAll();
    const VkDeviceSize stagingSize = std::min(
        std::max(byteSize, MinStagingBufferSize),
        MaxStagingBufferSize);
    stagingBuffer_ = std::make_unique<VulkanBuffer>(
        context_,
        BufferDesc{
            .usage = BufferUsage_TransferSrc,
            .storage = BufferStorage::HostVisible,
            .size = stagingSize,
            .debugName = "Reusable staging upload buffer",
        });
}

VkAccessFlags VulkanStagingUploader::destinationAccessMask(VkBufferUsageFlags usageFlags) const {
    VkAccessFlags accessMask = 0;

    if (usageFlags & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) {
        accessMask |= VK_ACCESS_INDEX_READ_BIT;
    }
    if (usageFlags & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) {
        accessMask |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    }
    if (usageFlags & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) {
        accessMask |= VK_ACCESS_UNIFORM_READ_BIT;
    }
    if (usageFlags & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) {
        accessMask |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    }
    if (usageFlags & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) {
        accessMask |= VK_ACCESS_TRANSFER_READ_BIT;
    }

    return accessMask ? accessMask : VK_ACCESS_MEMORY_READ_BIT;
}

VkPipelineStageFlags VulkanStagingUploader::destinationStageMask(VkBufferUsageFlags usageFlags) const {
    VkPipelineStageFlags stageMask = 0;

    if (usageFlags & (VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) {
        stageMask |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
    }
    if (usageFlags & (VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) {
        stageMask |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    if (usageFlags & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) {
        stageMask |= VK_PIPELINE_STAGE_TRANSFER_BIT;
    }

    return stageMask ? stageMask : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
}
