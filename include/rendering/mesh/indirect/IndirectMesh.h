#pragma once

#include "VulkanBuffer.h"
#include "mesh/MeshData.h"

#include <vulkan/vulkan.h>

#include <cstdint>

class VulkanContext;

// 把一个多 MeshData 场景上传为共享顶点、索引和间接命令缓冲。
// 每个 MeshDescriptor 对应一条 VkDrawIndexedIndirectCommand。
class IndirectMesh final {
public:
    IndirectMesh(const VulkanContext& context, const MeshData& meshData);

    IndirectMesh(const IndirectMesh&) = delete;
    IndirectMesh& operator=(const IndirectMesh&) = delete;

    void bindAndDraw(VkCommandBuffer commandBuffer) const;
    uint32_t drawCount() const;

private:
    VulkanBuffer vertexBuffer_;
    VulkanBuffer indexBuffer_;
    VulkanBuffer indirectBuffer_;
    uint32_t drawCount_ = 0;
};
