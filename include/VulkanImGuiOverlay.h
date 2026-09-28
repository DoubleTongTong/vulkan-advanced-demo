#pragma once

#include "VulkanBuffer.h"
#include "VulkanFrameSync.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

class VulkanDescriptorSet;
class VulkanContext;
class VulkanRenderPipeline;
class VulkanSwapchain;
class VulkanTexture2D;
class IImGuiPanel;
struct GLFWwindow;

// 只负责 ImGui 的输入、GPU 资源与绘制；具体界面由 panel 描述。
class VulkanImGuiOverlay {
public:
    VulkanImGuiOverlay(
        const VulkanContext& context,
        const VulkanSwapchain& swapchain,
        GLFWwindow* window);
    ~VulkanImGuiOverlay();

    VulkanImGuiOverlay(const VulkanImGuiOverlay&) = delete;
    VulkanImGuiOverlay& operator=(const VulkanImGuiOverlay&) = delete;

    void addPanel(std::unique_ptr<IImGuiPanel> panel);
    uint32_t registerTexture(const VulkanTexture2D& texture);
    bool wantsKeyboardInput() const;
    bool wantsMouseInput() const;
    void record(VkCommandBuffer commandBuffer, uint32_t imageIndex, uint32_t frameIndex);

private:
    static constexpr uint32_t FontTextureId = 1;

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
    std::unique_ptr<VulkanDescriptorSet> descriptors_;
    std::unique_ptr<VulkanRenderPipeline> pipeline_;
    std::array<FrameBuffers, VulkanFrameSync::MaxFramesInFlight> frameBuffers_{};
    std::vector<std::unique_ptr<IImGuiPanel>> panels_;
    uint32_t nextTextureId_ = FontTextureId + 1;
};
