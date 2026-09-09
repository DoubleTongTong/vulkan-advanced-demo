#include "VulkanImmediateCommands.h"

#include "VulkanContext.h"
#include "VulkanUtils.h"

#include <limits>
#include <stdexcept>

VulkanImmediateCommands::SubmitHandle::SubmitHandle(uint64_t value)
    : bufferIndex(static_cast<uint32_t>(value & 0xffffffffu)),
      submitId(static_cast<uint32_t>(value >> 32u)) {
}

bool VulkanImmediateCommands::SubmitHandle::empty() const {
    return submitId == 0;
}

uint64_t VulkanImmediateCommands::SubmitHandle::value() const {
    return (static_cast<uint64_t>(submitId) << 32u) + bufferIndex;
}

VulkanImmediateCommands::VulkanImmediateCommands(const VulkanContext& context, const char* debugName)
    : context_(context), debugName_(debugName ? debugName : "") {
    queue_ = context_.graphicsQueue();

    createCommandPool();
    createCommandBuffers();
}

VulkanImmediateCommands::~VulkanImmediateCommands() {
    waitAll();

    for (const CommandBuffer& commandBuffer : commandBuffers_) {
        if (commandBuffer.fence) {
            vkDestroyFence(context_.device(), commandBuffer.fence, nullptr);
        }

        if (commandBuffer.semaphore) {
            vkDestroySemaphore(context_.device(), commandBuffer.semaphore, nullptr);
        }
    }

    if (commandPool_) {
        vkDestroyCommandPool(context_.device(), commandPool_, nullptr);
    }
}

const VulkanImmediateCommands::CommandBuffer& VulkanImmediateCommands::acquire() {
    while (availableCommandBufferCount_ == 0) {
        purge();

        if (availableCommandBufferCount_ == 0) {
            wait(lastSubmitHandle_);
        }
    }

    CommandBuffer* current = nullptr;
    for (CommandBuffer& commandBuffer : commandBuffers_) {
        if (commandBuffer.commandBuffer == VK_NULL_HANDLE) {
            current = &commandBuffer;
            break;
        }
    }

    if (!current) {
        throw std::runtime_error("No available Vulkan command buffer.");
    }

    current->handle.submitId = submitCounter_;
    current->commandBuffer = current->allocatedCommandBuffer;
    current->isEncoding = true;
    --availableCommandBufferCount_;

    const VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    vulkan_utils::checkVk(
        vkBeginCommandBuffer(current->commandBuffer, &beginInfo),
        "vkBeginCommandBuffer");

    return *current;
}

VulkanImmediateCommands::SubmitHandle VulkanImmediateCommands::submit(const CommandBuffer& commandBuffer) {
    vulkan_utils::checkVk(vkEndCommandBuffer(commandBuffer.commandBuffer), "vkEndCommandBuffer");

    VkSemaphoreSubmitInfo waitSemaphores[2]{};
    uint32_t waitSemaphoreCount = 0;

    if (pendingWaitSemaphore_.semaphore) {
        waitSemaphores[waitSemaphoreCount++] = pendingWaitSemaphore_;
    }

    if (lastSubmitSemaphore_.semaphore) {
        waitSemaphores[waitSemaphoreCount++] = lastSubmitSemaphore_;
    }

    VkSemaphoreSubmitInfo signalSemaphores[2]{
        {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = commandBuffer.semaphore,
            .value = commandBuffer.handle.submitId,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        },
        {},
    };
    uint32_t signalSemaphoreCount = 1;

    if (pendingSignalSemaphore_.semaphore) {
        signalSemaphores[signalSemaphoreCount++] = pendingSignalSemaphore_;
    }

    const VkCommandBufferSubmitInfo commandBufferInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = commandBuffer.commandBuffer,
    };

    const VkSubmitInfo2 submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = waitSemaphoreCount,
        .pWaitSemaphoreInfos = waitSemaphores,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commandBufferInfo,
        .signalSemaphoreInfoCount = signalSemaphoreCount,
        .pSignalSemaphoreInfos = signalSemaphores,
    };

    vulkan_utils::checkVk(
        vkQueueSubmit2(queue_, 1, &submitInfo, commandBuffer.fence),
        "vkQueueSubmit2");

    lastSubmitHandle_ = commandBuffer.handle;
    lastSubmitSemaphore_ = signalSemaphores[0];

    pendingWaitSemaphore_.semaphore = VK_NULL_HANDLE;
    pendingSignalSemaphore_.semaphore = VK_NULL_HANDLE;

    CommandBuffer& mutableCommandBuffer = const_cast<CommandBuffer&>(commandBuffer);
    mutableCommandBuffer.isEncoding = false;

    ++submitCounter_;
    if (submitCounter_ == 0) {
        ++submitCounter_;
    }

    return lastSubmitHandle_;
}

void VulkanImmediateCommands::setSubmitWaitSemaphore(VkSemaphore semaphore) {
    pendingWaitSemaphore_ = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = semaphore,
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    };
}

void VulkanImmediateCommands::setSubmitSignalSemaphore(VkSemaphore semaphore, uint64_t signalValue) {
    pendingSignalSemaphore_ = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = semaphore,
        .value = signalValue,
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    };
}

VkSemaphore VulkanImmediateCommands::acquireLastSubmitSemaphore() {
    const VkSemaphore semaphore = lastSubmitSemaphore_.semaphore;
    lastSubmitSemaphore_.semaphore = VK_NULL_HANDLE;
    return semaphore;
}

VulkanImmediateCommands::SubmitHandle VulkanImmediateCommands::getLastSubmitHandle() const {
    return lastSubmitHandle_;
}

bool VulkanImmediateCommands::isReady(SubmitHandle handle) const {
    if (handle.empty() || handle.bufferIndex >= commandBuffers_.size()) {
        return true;
    }

    const CommandBuffer& commandBuffer = commandBuffers_[handle.bufferIndex];
    if (commandBuffer.commandBuffer == VK_NULL_HANDLE) {
        return true;
    }

    if (commandBuffer.handle.submitId != handle.submitId) {
        return true;
    }

    return vkWaitForFences(context_.device(), 1, &commandBuffer.fence, VK_TRUE, 0) == VK_SUCCESS;
}

void VulkanImmediateCommands::wait(SubmitHandle handle) {
    if (handle.empty()) {
        vulkan_utils::checkVk(vkDeviceWaitIdle(context_.device()), "vkDeviceWaitIdle");
        return;
    }

    if (isReady(handle)) {
        return;
    }

    CommandBuffer& commandBuffer = commandBuffers_[handle.bufferIndex];
    if (commandBuffer.isEncoding) {
        throw std::runtime_error("Cannot wait for a Vulkan command buffer that is still encoding.");
    }

    vulkan_utils::checkVk(
        vkWaitForFences(
            context_.device(),
            1,
            &commandBuffer.fence,
            VK_TRUE,
            std::numeric_limits<uint64_t>::max()),
        "vkWaitForFences");

    purge();
}

void VulkanImmediateCommands::waitAll() {
    VkFence fences[MaxCommandBuffers]{};
    uint32_t fenceCount = 0;

    for (const CommandBuffer& commandBuffer : commandBuffers_) {
        if (commandBuffer.commandBuffer != VK_NULL_HANDLE && !commandBuffer.isEncoding) {
            fences[fenceCount++] = commandBuffer.fence;
        }
    }

    if (fenceCount > 0) {
        vulkan_utils::checkVk(
            vkWaitForFences(
                context_.device(),
                fenceCount,
                fences,
                VK_TRUE,
                std::numeric_limits<uint64_t>::max()),
            "vkWaitForFences");
    }

    purge();
}

void VulkanImmediateCommands::purge() {
    for (CommandBuffer& commandBuffer : commandBuffers_) {
        if (commandBuffer.commandBuffer == VK_NULL_HANDLE || commandBuffer.isEncoding) {
            continue;
        }

        const VkResult result = vkWaitForFences(context_.device(), 1, &commandBuffer.fence, VK_TRUE, 0);
        if (result == VK_TIMEOUT) {
            continue;
        }

        vulkan_utils::checkVk(result, "vkWaitForFences");
        vulkan_utils::checkVk(
            vkResetCommandBuffer(commandBuffer.commandBuffer, 0),
            "vkResetCommandBuffer");
        vulkan_utils::checkVk(
            vkResetFences(context_.device(), 1, &commandBuffer.fence),
            "vkResetFences");

        commandBuffer.commandBuffer = VK_NULL_HANDLE;
        ++availableCommandBufferCount_;
    }
}

void VulkanImmediateCommands::createCommandPool() {
    const VkCommandPoolCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        // RESET 允许单独重置 command buffer；TRANSIENT 表示这些命令通常录完就提交。
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = context_.graphicsQueueFamilyIndex(),
    };

    vulkan_utils::checkVk(
        vkCreateCommandPool(context_.device(), &createInfo, nullptr, &commandPool_),
        "vkCreateCommandPool");

    context_.setDebugObjectName(
        VK_OBJECT_TYPE_COMMAND_POOL,
        reinterpret_cast<uint64_t>(commandPool_),
        debugName_);
}

void VulkanImmediateCommands::createCommandBuffers() {
    const VkCommandBufferAllocateInfo allocateInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    for (uint32_t i = 0; i < MaxCommandBuffers; ++i) {
        CommandBuffer& commandBuffer = commandBuffers_[i];
        commandBuffer.handle.bufferIndex = i;
        commandBuffer.fence = context_.createFence(false, "Immediate command fence");
        commandBuffer.semaphore = context_.createTimelineSemaphore(0, "Immediate command timeline semaphore");

        vulkan_utils::checkVk(
            vkAllocateCommandBuffers(context_.device(), &allocateInfo, &commandBuffer.allocatedCommandBuffer),
            "vkAllocateCommandBuffers");
    }
}
