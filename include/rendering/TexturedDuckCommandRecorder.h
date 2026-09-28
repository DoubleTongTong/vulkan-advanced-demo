#pragma once

#include "VulkanBuffer.h"
#include "VulkanDescriptorSet.h"
#include "VulkanRenderPipeline.h"
#include "VulkanTexture2D.h"
#include "rendering/IRenderCommandRecorder.h"

#include <vulkan/vulkan.h>

#include <filesystem>
#include <memory>
#include <vector>

class VulkanContext;
class VulkanShaderModule;
class VulkanSwapchain;
struct VmaAllocation_T;
using VmaAllocation = VmaAllocation_T*;

class TexturedDuckCommandRecorder final : public IRenderCommandRecorder {
public:
    TexturedDuckCommandRecorder(
        const VulkanContext& context,
        const VulkanSwapchain& swapchain,
        const VulkanShaderModule& vertexShader,
        const VulkanShaderModule& fragmentShader,
        const std::filesystem::path& scenePath,
        const std::filesystem::path& texturePath);
    ~TexturedDuckCommandRecorder();

    void record(const RenderFrameContext& frame) override;
    const VulkanTexture2D& texture() const { return texture_; }

private:
    struct Vertex {
        float position[3] = {};
        float uv[2] = {};
    };

    struct SceneData {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        float center[3] = {};
        float radius = 1.0f;
    };

    static SceneData loadSceneData(const std::filesystem::path& scenePath);

    void createPipeline(const VulkanShaderModule& vertexShader, const VulkanShaderModule& fragmentShader);
    void createDepthAttachment();
    void destroyDepthAttachment();

    const VulkanContext& context_;
    const VulkanSwapchain& swapchain_;
    SceneData sceneData_;
    VulkanTexture2D texture_;
    VulkanDescriptorSet textureDescriptors_;
    VulkanBuffer vertexBuffer_;
    VulkanBuffer indexBuffer_;
    std::unique_ptr<VulkanRenderPipeline> pipeline_;
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkImageView depthImageView_ = VK_NULL_HANDLE;
    VmaAllocation depthAllocation_ = nullptr;
    VkImageLayout depthLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    float meshCenter_[3] = {};
    float meshRadius_ = 1.0f;
    uint32_t indexCount_ = 0;
    std::vector<VkImageLayout> imageLayouts_;
};
