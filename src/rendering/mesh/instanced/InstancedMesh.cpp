#include "rendering/mesh/instanced/InstancedMesh.h"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace {

// 全部使用 vec4，确保 C++ 与 GLSL std430 的数组 stride 明确且稳定。
struct Vertex {
    glm::vec4 position{0.0f};
    glm::vec4 normal{0.0f};
    glm::vec4 uv{0.0f};
};

size_t vertexCount(const MeshData& mesh) {
    const size_t count = mesh.positions.size() / 3u;
    if (count == 0 ||
        mesh.positions.size() != count * 3u ||
        mesh.normals.size() != count * 3u ||
        mesh.texcoords.size() != count * 2u ||
        mesh.radius <= 0.0f) {
        throw std::invalid_argument(
            "Instanced mesh requires matching positions, normals, UVs, and valid bounds.");
    }
    return count;
}

std::vector<Vertex> makeVertices(const MeshData& mesh) {
    std::vector<Vertex> vertices(vertexCount(mesh));
    const glm::vec3 center(mesh.center[0], mesh.center[1], mesh.center[2]);

    for (size_t index = 0; index < vertices.size(); ++index) {
        const size_t positionOffset = index * 3u;
        const size_t uvOffset = index * 2u;
        const glm::vec3 position(
            mesh.positions[positionOffset + 0u],
            mesh.positions[positionOffset + 1u],
            mesh.positions[positionOffset + 2u]);

        // 在上传阶段把模型居中并归一化，Shader 就只需处理实例旋转和平移。
        vertices[index].position = glm::vec4((position - center) / mesh.radius, 1.0f);
        vertices[index].normal = {
            mesh.normals[positionOffset + 0u],
            mesh.normals[positionOffset + 1u],
            mesh.normals[positionOffset + 2u],
            0.0f,
        };
        vertices[index].uv = {
            mesh.texcoords[uvOffset + 0u],
            1.0f - mesh.texcoords[uvOffset + 1u],
            0.0f,
            0.0f,
        };
    }
    return vertices;
}

VulkanBuffer makeVertexBuffer(const VulkanContext& context, const MeshData& mesh) {
    // VulkanBuffer 会在构造期间完成 staging 上传，返回后即可释放 CPU 顶点数组。
    const std::vector<Vertex> vertices = makeVertices(mesh);
    return VulkanBuffer(
        context,
        {
            .usage = BufferUsage_Storage,
            .storage = BufferStorage::Device,
            .size = sizeof(Vertex) * vertices.size(),
            .data = vertices.data(),
            .debugName = "Instanced mesh vertices",
        });
}

} // namespace

InstancedMesh::InstancedMesh(const VulkanContext& context, const MeshData& mesh)
    : vertexBuffer_(makeVertexBuffer(context, mesh)),
      indexBuffer_(
          context,
          {
              .usage = BufferUsage_Index,
              .storage = BufferStorage::Device,
              .size = sizeof(uint32_t) * mesh.indices.size(),
              .data = mesh.indices.data(),
              .debugName = "Instanced mesh indices",
          }),
      indexCount_(static_cast<uint32_t>(mesh.indices.size())) {
    if (mesh.indices.empty() || mesh.indices.size() % 3u != 0u) {
        throw std::invalid_argument("Instanced mesh requires indexed triangles.");
    }
}

const VulkanBuffer& InstancedMesh::vertexBuffer() const {
    return vertexBuffer_;
}

void InstancedMesh::bindIndexBuffer(VkCommandBuffer commandBuffer) const {
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer_.handle(), 0, VK_INDEX_TYPE_UINT32);
}

uint32_t InstancedMesh::indexCount() const {
    return indexCount_;
}
