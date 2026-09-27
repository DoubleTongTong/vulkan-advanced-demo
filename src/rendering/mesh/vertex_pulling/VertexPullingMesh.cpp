#include "rendering/mesh/vertex_pulling/VertexPullingMesh.h"

#include "VulkanContext.h"

#include <glm/ext/matrix_transform.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace {

// std430 中两个 vec4 的数组元素步长固定为 32 字节，与 PVP shader 的 Vertex 一一对应。
struct Vertex {
    glm::vec4 position{0.0f};
    glm::vec4 uv{0.0f};
};

size_t vertexCount(const MeshData& mesh) {
    const size_t count = mesh.positions.size() / 3u;
    if (count == 0 || mesh.positions.size() != count * 3u ||
        mesh.texcoords.size() != count * 2u) {
        throw std::invalid_argument(
            "Vertex pulling requires matching non-empty positions and texture coordinates.");
    }
    return count;
}

std::vector<Vertex> makeVertices(const MeshData& mesh) {
    std::vector<Vertex> vertices(vertexCount(mesh));
    for (size_t index = 0; index < vertices.size(); ++index) {
        vertices[index].position = {
            mesh.positions[index * 3u + 0u],
            mesh.positions[index * 3u + 1u],
            mesh.positions[index * 3u + 2u],
            1.0f,
        };
        // 当前图片解码方向以左上角为原点，需要翻转 glTF 的 V。
        vertices[index].uv = {
            mesh.texcoords[index * 2u + 0u],
            1.0f - mesh.texcoords[index * 2u + 1u],
            0.0f,
            0.0f,
        };
    }
    return vertices;
}

} // namespace

VertexPullingMesh::VertexPullingMesh(const VulkanContext& context, const MeshData& mesh)
    : vertexStorageBuffer_(
          context,
          {
              .usage = BufferUsage_Storage,
              .storage = BufferStorage::Device,
              .size = sizeof(Vertex) * vertexCount(mesh),
              .debugName = "Vertex pulling storage buffer",
          }),
      indexBuffer_(
          context,
          {
              .usage = BufferUsage_Index,
              .storage = BufferStorage::Device,
              .size = sizeof(uint32_t) * mesh.indices.size(),
              .data = mesh.indices.data(),
              .debugName = "Vertex pulling index buffer",
          }),
      center_(mesh.center[0], mesh.center[1], mesh.center[2]),
      radius_(mesh.radius),
      indexCount_(static_cast<uint32_t>(mesh.indices.size())) {
    if (mesh.indices.empty() || mesh.indices.size() % 3u != 0u || radius_ <= 0.0f) {
        throw std::invalid_argument("Vertex pulling requires indexed triangles and a positive mesh radius.");
    }

    const std::vector<Vertex> vertices = makeVertices(mesh);
    // 统一由 VulkanContext 的 staging 上传路径送往 device-local memory。
    context.uploadBuffer(vertexStorageBuffer_, 0, sizeof(Vertex) * vertices.size(), vertices.data());
}

const VulkanBuffer& VertexPullingMesh::vertexStorageBuffer() const {
    return vertexStorageBuffer_;
}

glm::mat4 VertexPullingMesh::normalizedModelMatrix(float rotationRadians) const {
    return glm::rotate(glm::mat4(1.0f), rotationRadians, glm::vec3(0.0f, 1.0f, 0.0f)) *
           glm::scale(glm::mat4(1.0f), glm::vec3(1.0f / radius_)) *
           glm::translate(glm::mat4(1.0f), -center_);
}

void VertexPullingMesh::bindIndexBuffer(VkCommandBuffer commandBuffer) const {
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer_.handle(), 0, VK_INDEX_TYPE_UINT32);
}

uint32_t VertexPullingMesh::indexCount() const {
    return indexCount_;
}
