#pragma once

#include "VulkanImmediateCommands.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <vector>

class VulkanContext;

class VulkanFrameSync {
public:
    static constexpr uint32_t MaxFramesInFlight = 3;

    struct Frame {
        uint32_t frameIndex = 0;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VulkanImmediateCommands::SubmitHandle submitHandle;
    };

    VulkanFrameSync(const VulkanContext& context, uint32_t swapchainImageCount);
    ~VulkanFrameSync();

    VulkanFrameSync(const VulkanFrameSync&) = delete;
    VulkanFrameSync& operator=(const VulkanFrameSync&) = delete;

    Frame currentFrame() const;
    VkSemaphore renderFinishedSemaphore(uint32_t imageIndex) const;
    void markSubmitted(const Frame& frame, VulkanImmediateCommands::SubmitHandle submitHandle);
    void advanceFrame();

private:
    const VulkanContext& context_;
    std::array<VkSemaphore, MaxFramesInFlight> imageAvailableSemaphores_{};
    std::array<VulkanImmediateCommands::SubmitHandle, MaxFramesInFlight> frameSubmitHandles_{};
    std::vector<VkSemaphore> renderFinishedSemaphores_;
    uint32_t currentFrameIndex_ = 0;
};
