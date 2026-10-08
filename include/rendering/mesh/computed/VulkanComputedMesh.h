#pragma once

#include "VulkanBuffer.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

class VulkanContext;

// 顶点由 Compute Shader 每帧生成，索引拓扑只在构造时生成一次。
// 类内同时维护 Compute Write 与 Vertex Input Read 之间的 Buffer 屏障。
class VulkanComputedMesh final {
public:
    VulkanComputedMesh(
        const VulkanContext& context,
        uint32_t uSegments,
        uint32_t vSegments);

    uint32_t uSegments() const;
    uint32_t vSegments() const;
    uint32_t vertexCount() const;
    uint32_t indexCount() const;

    const VulkanBuffer& vertexBuffer() const;
    static std::vector<VkVertexInputBindingDescription> vertexBindings();
    static std::vector<VkVertexInputAttributeDescription> vertexAttributes();

    void transitionForComputeWrite(VkCommandBuffer commandBuffer);
    void transitionForVertexRead(VkCommandBuffer commandBuffer);
    void bindAndDraw(VkCommandBuffer commandBuffer) const;

private:
    enum class AccessState {
        Undefined,
        ComputeWrite,
        VertexRead,
    };

    static std::vector<uint32_t> generateIndices(
        uint32_t uSegments,
        uint32_t vSegments);

    uint32_t uSegments_ = 0;
    uint32_t vSegments_ = 0;
    // 构造 Device Local Index Buffer 时保证上传源数据一直有效。
    std::vector<uint32_t> indices_;
    uint32_t indexCount_ = 0;
    VulkanBuffer vertexBuffer_;
    VulkanBuffer indexBuffer_;
    AccessState accessState_ = AccessState::Undefined;
};
