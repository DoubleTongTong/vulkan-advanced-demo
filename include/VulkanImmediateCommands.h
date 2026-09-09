#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>

class VulkanContext;

class VulkanImmediateCommands {
public:
    struct SubmitHandle {
        uint32_t bufferIndex = 0;
        uint32_t submitId = 0;

        SubmitHandle() = default;
        explicit SubmitHandle(uint64_t value);

        bool empty() const;
        uint64_t value() const;
    };

    struct CommandBuffer {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkCommandBuffer allocatedCommandBuffer = VK_NULL_HANDLE;
        SubmitHandle handle;
        VkFence fence = VK_NULL_HANDLE;
        VkSemaphore semaphore = VK_NULL_HANDLE;
        bool isEncoding = false;
    };

    static constexpr uint32_t MaxCommandBuffers = 64;

    explicit VulkanImmediateCommands(const VulkanContext& context, const char* debugName = "Immediate commands");
    ~VulkanImmediateCommands();

    VulkanImmediateCommands(const VulkanImmediateCommands&) = delete;
    VulkanImmediateCommands& operator=(const VulkanImmediateCommands&) = delete;

    const CommandBuffer& acquire();
    SubmitHandle submit(const CommandBuffer& commandBuffer);

    void setSubmitWaitSemaphore(VkSemaphore semaphore);
    void setSubmitSignalSemaphore(VkSemaphore semaphore, uint64_t signalValue = 0);
    VkSemaphore acquireLastSubmitSemaphore();

    SubmitHandle getLastSubmitHandle() const;
    bool isReady(SubmitHandle handle) const;
    void wait(SubmitHandle handle);
    void waitAll();

private:
    void purge();
    void createCommandPool();
    void createCommandBuffers();

    const VulkanContext& context_;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    const char* debugName_ = "";

    std::array<CommandBuffer, MaxCommandBuffers> commandBuffers_{};
    VkSemaphoreSubmitInfo pendingWaitSemaphore_{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    };
    VkSemaphoreSubmitInfo pendingSignalSemaphore_{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    };
    VkSemaphoreSubmitInfo lastSubmitSemaphore_{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    };

    SubmitHandle lastSubmitHandle_;
    uint32_t availableCommandBufferCount_ = MaxCommandBuffers;
    uint32_t submitCounter_ = 1;
};
