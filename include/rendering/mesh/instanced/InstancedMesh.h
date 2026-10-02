#pragma once

#include "VulkanBuffer.h"
#include "mesh/MeshData.h"

#include <vulkan/vulkan.h>

#include <cstdint>

class VulkanContext;

// Compute 实例化路径的 GPU 网格：顶点属性供 vertex shader 从 SSBO 主动拉取。
class InstancedMesh final {
public:
    InstancedMesh(const VulkanContext& context, const MeshData& mesh);

    InstancedMesh(const InstancedMesh&) = delete;
    InstancedMesh& operator=(const InstancedMesh&) = delete;

    const VulkanBuffer& vertexBuffer() const;
    void bindIndexBuffer(VkCommandBuffer commandBuffer) const;
    uint32_t indexCount() const;

private:
    VulkanBuffer vertexBuffer_;
    VulkanBuffer indexBuffer_;
    uint32_t indexCount_ = 0;
};
