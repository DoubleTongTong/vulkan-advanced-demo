#pragma once

#include "VulkanDepthAttachment.h"
#include "VulkanDescriptorSet.h"
#include "VulkanRenderPipeline.h"
#include "VulkanTexture2D.h"
#include "material/UnlitGltfMaterial.h"
#include "mesh/MeshData.h"
#include "rendering/IRenderCommandRecorder.h"
#include "rendering/mesh/indirect/IndirectMesh.h"

#include <filesystem>
#include <vector>

class VulkanContext;
class VulkanShaderModule;
class VulkanSwapchain;

// KHR_materials_unlit 的最小渲染路径：基础色纹理与 baseColorFactor 相乘。
// 当前 DamagedHelmet 没有 COLOR_0，顶点色按 glTF 规定的白色默认值处理。
class UnlitGltfCommandRecorder final : public IRenderCommandRecorder {
public:
    UnlitGltfCommandRecorder(
        const VulkanContext& context,
        const VulkanSwapchain& swapchain,
        const VulkanShaderModule& vertexShader,
        const VulkanShaderModule& fragmentShader,
        const std::filesystem::path& scenePath);

    void record(const RenderFrameContext& frame) override;

private:
    const VulkanContext& context_;
    const VulkanSwapchain& swapchain_;
    // 几何只使用项目统一的 MeshData；Unlit 特有信息单独属于材质层。
    MeshData meshData_;
    IndirectMesh mesh_;
    UnlitGltfMaterial material_;
    VulkanTexture2D baseColorTexture_;
    VulkanDescriptorSet materialDescriptors_;
    VulkanRenderPipeline pipeline_;
    VulkanDepthAttachment depthAttachment_;
    std::vector<VkImageLayout> imageLayouts_;
};
