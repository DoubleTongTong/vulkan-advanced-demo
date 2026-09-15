#include "VulkanBuffer.h"

#include "VulkanContext.h"
#include "VulkanUtils.h"

#include <vk_mem_alloc.h>

#include <cstring>
#include <stdexcept>
#include <utility>

VulkanBuffer::VulkanBuffer(
    const VulkanContext& context,
    const BufferDesc& desc)
    : context_(&context) {
    if (desc.size == 0) {
        throw std::runtime_error("Cannot create an empty Vulkan buffer.");
    }

    create(desc);
    uploadInitialData(desc);
}

VulkanBuffer::~VulkanBuffer() {
    destroy();
}

VulkanBuffer::VulkanBuffer(VulkanBuffer&& other) noexcept {
    *this = std::move(other);
}

VulkanBuffer& VulkanBuffer::operator=(VulkanBuffer&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    destroy();

    context_ = other.context_;
    buffer_ = other.buffer_;
    allocation_ = other.allocation_;
    size_ = other.size_;
    usageFlags_ = other.usageFlags_;
    mappedPtr_ = other.mappedPtr_;

    other.context_ = nullptr;
    other.buffer_ = VK_NULL_HANDLE;
    other.allocation_ = nullptr;
    other.size_ = 0;
    other.usageFlags_ = 0;
    other.mappedPtr_ = nullptr;

    return *this;
}

VkBuffer VulkanBuffer::handle() const {
    return buffer_;
}

VkDeviceSize VulkanBuffer::size() const {
    return size_;
}

VkBufferUsageFlags VulkanBuffer::usageFlags() const {
    return usageFlags_;
}

bool VulkanBuffer::isMapped() const {
    return mappedPtr_ != nullptr;
}

void VulkanBuffer::bufferSubData(size_t offset, size_t byteSize, const void* data) {
    if (!mappedPtr_) {
        throw std::runtime_error("Cannot write directly into a non-mapped Vulkan buffer.");
    }

    if (offset + byteSize > size_) {
        throw std::runtime_error("Vulkan buffer write is out of range.");
    }

    if (data) {
        std::memcpy(static_cast<uint8_t*>(mappedPtr_) + offset, data, byteSize);
    } else {
        std::memset(static_cast<uint8_t*>(mappedPtr_) + offset, 0, byteSize);
    }

    flushMappedMemory(offset, byteSize);
}

void VulkanBuffer::flushMappedMemory(VkDeviceSize offset, VkDeviceSize byteSize) const {
    if (!mappedPtr_) {
        return;
    }

    vulkan_utils::checkVk(
        vmaFlushAllocation(context_->allocator(), allocation_, offset, byteSize),
        "vmaFlushAllocation");
}

void VulkanBuffer::create(const BufferDesc& desc) {
    size_ = desc.size;
    usageFlags_ = toVkUsageFlags(desc.usage, desc.storage);

    const VkBufferCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size_,
        .usage = usageFlags_,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo allocationCreateInfo{
        .usage = VMA_MEMORY_USAGE_AUTO,
    };

    if (desc.storage == BufferStorage::HostVisible) {
        allocationCreateInfo.flags =
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT;
        allocationCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    } else {
        allocationCreateInfo.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }

    VmaAllocationInfo allocationInfo{};
    vulkan_utils::checkVk(
        vmaCreateBuffer(
            context_->allocator(),
            &createInfo,
            &allocationCreateInfo,
            &buffer_,
            &allocation_,
            &allocationInfo),
        "vmaCreateBuffer");

    mappedPtr_ = allocationInfo.pMappedData;
    context_->setDebugObjectName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(buffer_), desc.debugName);
}

void VulkanBuffer::uploadInitialData(const BufferDesc& desc) {
    if (!desc.data) {
        return;
    }

    if (mappedPtr_) {
        bufferSubData(0, static_cast<size_t>(desc.size), desc.data);
        return;
    }

    context_->uploadBuffer(*this, 0, static_cast<size_t>(desc.size), desc.data);
}

void VulkanBuffer::destroy() {
    if (!context_) {
        return;
    }

    if (buffer_ && allocation_) {
        vmaDestroyBuffer(context_->allocator(), buffer_, allocation_);
        buffer_ = VK_NULL_HANDLE;
        allocation_ = nullptr;
        mappedPtr_ = nullptr;
    }
}

VkBufferUsageFlags VulkanBuffer::toVkUsageFlags(uint32_t usage, BufferStorage storage) const {
    VkBufferUsageFlags flags = 0;

    if (usage & BufferUsage_Index) {
        flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    }
    if (usage & BufferUsage_Vertex) {
        flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    }
    if (usage & BufferUsage_Uniform) {
        flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    }
    if (usage & BufferUsage_Storage) {
        flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    }
    if (usage & BufferUsage_TransferSrc) {
        flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    }
    if (usage & BufferUsage_TransferDst) {
        flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }

    if (storage == BufferStorage::Device) {
        flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }

    if (flags == 0) {
        throw std::runtime_error("Vulkan buffer usage flags cannot be empty.");
    }

    return flags;
}
