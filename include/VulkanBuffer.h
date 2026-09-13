#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>

class VulkanContext;
class VulkanImmediateCommands;
struct VmaAllocation_T;
using VmaAllocation = VmaAllocation_T*;

enum BufferUsageBits : uint32_t {
    BufferUsage_Index = 1u << 0u,
    BufferUsage_Vertex = 1u << 1u,
    BufferUsage_Uniform = 1u << 2u,
    BufferUsage_Storage = 1u << 3u,
    BufferUsage_TransferSrc = 1u << 4u,
    BufferUsage_TransferDst = 1u << 5u,
};

enum class BufferStorage {
    Device,
    HostVisible,
};

struct BufferDesc {
    uint32_t usage = 0;
    BufferStorage storage = BufferStorage::HostVisible;
    VkDeviceSize size = 0;
    const void* data = nullptr;
    const char* debugName = "Vulkan buffer";
};

class VulkanBuffer {
public:
    VulkanBuffer(
        const VulkanContext& context,
        const BufferDesc& desc,
        VulkanImmediateCommands* uploadCommands = nullptr);
    ~VulkanBuffer();

    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;

    VulkanBuffer(VulkanBuffer&& other) noexcept;
    VulkanBuffer& operator=(VulkanBuffer&& other) noexcept;

    VkBuffer handle() const;
    VkDeviceSize size() const;
    VkBufferUsageFlags usageFlags() const;
    bool isMapped() const;

    void bufferSubData(size_t offset, size_t byteSize, const void* data);
    void flushMappedMemory(VkDeviceSize offset, VkDeviceSize byteSize) const;

private:
    void create(const BufferDesc& desc);
    void uploadInitialData(const BufferDesc& desc, VulkanImmediateCommands* uploadCommands);
    void copyFrom(VulkanImmediateCommands& commands, const VulkanBuffer& source, VkDeviceSize byteSize);
    void destroy();

    VkBufferUsageFlags toVkUsageFlags(uint32_t usage, BufferStorage storage) const;

    const VulkanContext* context_ = nullptr;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = nullptr;
    VkDeviceSize size_ = 0;
    VkBufferUsageFlags usageFlags_ = 0;
    void* mappedPtr_ = nullptr;
};
