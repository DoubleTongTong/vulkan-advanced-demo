#include "VulkanFrameSync.h"

#include "VulkanContext.h"

VulkanFrameSync::VulkanFrameSync(const VulkanContext& context, uint32_t swapchainImageCount)
    : context_(context) {
    for (VkSemaphore& semaphore : imageAvailableSemaphores_) {
        semaphore = context_.createSemaphore("Frame image available");
    }

    renderFinishedSemaphores_.reserve(swapchainImageCount);
    for (uint32_t i = 0; i < swapchainImageCount; ++i) {
        renderFinishedSemaphores_.push_back(context_.createSemaphore("Frame render finished"));
    }
}

VulkanFrameSync::~VulkanFrameSync() {
    for (VkSemaphore semaphore : renderFinishedSemaphores_) {
        if (semaphore) {
            vkDestroySemaphore(context_.device(), semaphore, nullptr);
        }
    }

    for (VkSemaphore semaphore : imageAvailableSemaphores_) {
        if (semaphore) {
            vkDestroySemaphore(context_.device(), semaphore, nullptr);
        }
    }
}

VulkanFrameSync::Frame VulkanFrameSync::currentFrame() const {
    return {
        .frameIndex = currentFrameIndex_,
        .imageAvailable = imageAvailableSemaphores_[currentFrameIndex_],
        .submitHandle = frameSubmitHandles_[currentFrameIndex_],
    };
}

VkSemaphore VulkanFrameSync::renderFinishedSemaphore(uint32_t imageIndex) const {
    return renderFinishedSemaphores_[imageIndex];
}

void VulkanFrameSync::markSubmitted(
    const Frame& frame,
    VulkanImmediateCommands::SubmitHandle submitHandle) {
    frameSubmitHandles_[frame.frameIndex] = submitHandle;
}

void VulkanFrameSync::advanceFrame() {
    currentFrameIndex_ = (currentFrameIndex_ + 1) % MaxFramesInFlight;
}
