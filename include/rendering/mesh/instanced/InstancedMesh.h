#pragma once

#include "VulkanBuffer.h"
#include "mesh/MeshData.h"

#include <vulkan/vulkan.h>
#include <glm/vec4.hpp>

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
    glm::vec4 centerRadius() const;

private:
    VulkanBuffer vertexBuffer_;
    VulkanBuffer indexBuffer_;
    glm::vec4 centerRadius_{0.0f, 0.0f, 0.0f, 1.0f};
    uint32_t indexCount_ = 0;
};
