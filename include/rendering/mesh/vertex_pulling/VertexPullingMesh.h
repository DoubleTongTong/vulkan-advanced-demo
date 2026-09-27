#pragma once

#include "mesh/MeshData.h"
#include "VulkanBuffer.h"

#include <glm/mat4x4.hpp>
#include <vulkan/vulkan.h>

#include <cstdint>

class VulkanContext;

// 顶点拉取路径的 GPU 网格：顶点属性保存在 std430 storage buffer 中。
class VertexPullingMesh final {
public:
    VertexPullingMesh(const VulkanContext& context, const MeshData& mesh);

    VertexPullingMesh(const VertexPullingMesh&) = delete;
    VertexPullingMesh& operator=(const VertexPullingMesh&) = delete;

    const VulkanBuffer& vertexStorageBuffer() const;
    glm::mat4 normalizedModelMatrix(float rotationRadians) const;
    void bindIndexBuffer(VkCommandBuffer commandBuffer) const;
    uint32_t indexCount() const;

private:
    VulkanBuffer vertexStorageBuffer_;
    VulkanBuffer indexBuffer_;
    glm::vec3 center_{0.0f};
    float radius_ = 1.0f;
    uint32_t indexCount_ = 0;
};
