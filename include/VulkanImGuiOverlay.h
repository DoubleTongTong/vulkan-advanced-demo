#pragma once

#include "VulkanBuffer.h"
#include "VulkanFrameSync.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>

class VulkanBindlessDescriptorSet;
class VulkanContext;
class VulkanRenderPipeline;
class VulkanSwapchain;
class VulkanTexture2D;
struct GLFWwindow;

// 在已有场景上叠加 ImGui；每个 frame slot 单独保存顶点和索引缓冲。
class VulkanImGuiOverlay {
public:
    VulkanImGuiOverlay(
        const VulkanContext& context,
        const VulkanSwapchain& swapchain,
        GLFWwindow* window,
        const VulkanTexture2D& previewTexture);
    ~VulkanImGuiOverlay();

    VulkanImGuiOverlay(const VulkanImGuiOverlay&) = delete;
    VulkanImGuiOverlay& operator=(const VulkanImGuiOverlay&) = delete;

    void record(VkCommandBuffer commandBuffer, uint32_t imageIndex, uint32_t frameIndex);

private:
    struct FrameBuffers {
        std::unique_ptr<VulkanBuffer> vertices;
        std::unique_ptr<VulkanBuffer> indices;
    };

    void createFontTexture();
    void createPipeline();
    void uploadDrawData(uint32_t frameIndex);
    void bindDrawState(VkCommandBuffer commandBuffer, uint32_t frameIndex, float width, float height) const;

    const VulkanContext& context_;
    const VulkanSwapchain& swapchain_;
    std::unique_ptr<VulkanTexture2D> fontTexture_;
    std::unique_ptr<VulkanBindlessDescriptorSet> descriptors_;
    std::unique_ptr<VulkanRenderPipeline> pipeline_;
    std::array<FrameBuffers, VulkanFrameSync::MaxFramesInFlight> frameBuffers_{};
};
