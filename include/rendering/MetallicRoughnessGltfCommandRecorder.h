#pragma once

#include "VulkanBuffer.h"
#include "VulkanDepthAttachment.h"
#include "VulkanDescriptorSet.h"
#include "VulkanKtxTexture.h"
#include "VulkanRenderPipeline.h"
#include "VulkanTexture2D.h"
#include "material/MetallicRoughnessGltfMaterial.h"
#include "mesh/MeshData.h"
#include "rendering/IRenderCommandRecorder.h"
#include "rendering/mesh/indirect/IndirectMesh.h"

#include <filesystem>
#include <vector>

class VulkanContext;
class VulkanShaderModule;
class VulkanSwapchain;

// glTF 2.0 metallic-roughness 的最小完整路径：五张材质贴图、直接光照和 IBL。
// 几何继续使用统一 MeshData，预计算资源直接从上一章生成的 KTX2 加载。
class MetallicRoughnessGltfCommandRecorder final : public IRenderCommandRecorder {
public:
    MetallicRoughnessGltfCommandRecorder(
        const VulkanContext& context,
        const VulkanSwapchain& swapchain,
        const VulkanShaderModule& vertexShader,
        const VulkanShaderModule& fragmentShader,
        const std::filesystem::path& scenePath,
        const std::filesystem::path& brdfLutPath,
        const std::filesystem::path& irradiancePath,
        const std::filesystem::path& ggxEnvironmentPath);

    void record(const RenderFrameContext& frame) override;

private:
    const VulkanContext& context_;
    const VulkanSwapchain& swapchain_;
    MeshData meshData_;
    IndirectMesh mesh_;
    MetallicRoughnessGltfMaterial material_;
    VulkanTexture2D baseColorTexture_;
    VulkanTexture2D metallicRoughnessTexture_;
    VulkanTexture2D normalTexture_;
    VulkanTexture2D occlusionTexture_;
    VulkanTexture2D emissiveTexture_;
    VulkanKtxTexture brdfLut_;
    VulkanKtxTexture irradianceMap_;
    VulkanKtxTexture ggxEnvironment_;
    VulkanBuffer frameDataBuffer_;
    VulkanDescriptorSet descriptors_;
    VulkanRenderPipeline pipeline_;
    VulkanDepthAttachment depthAttachment_;
    std::vector<VkImageLayout> imageLayouts_;
};
