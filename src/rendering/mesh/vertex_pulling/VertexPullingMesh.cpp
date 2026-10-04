#include "rendering/mesh/vertex_pulling/VertexPullingMesh.h"

#include "VulkanContext.h"

#include <glm/ext/matrix_transform.hpp>
#include <stdexcept>

VertexPullingMesh::VertexPullingMesh(const VulkanContext& context, const MeshData& mesh)
    : vertexStorageBuffer_(
          context,
          {
              .usage = BufferUsage_Storage,
              .storage = BufferStorage::Device,
              .size = mesh.vertexBytes().size(),
              .data = mesh.vertexBytes().data(),
              .debugName = "Vertex pulling storage buffer",
          }),
      indexBuffer_(
          context,
          {
              .usage = BufferUsage_Index,
              .storage = BufferStorage::Device,
              .size = mesh.indexBytes().size(),
              .data = mesh.indexBytes().data(),
              .debugName = "Vertex pulling index buffer",
          }),
      center_(mesh.bounds().center[0], mesh.bounds().center[1], mesh.bounds().center[2]),
      radius_(mesh.bounds().radius),
      indexCount_(static_cast<uint32_t>(mesh.indices().size())) {
    if (mesh.indices().empty() || mesh.indices().size() % 3u != 0u || radius_ <= 0.0f) {
        throw std::invalid_argument("Vertex pulling requires indexed triangles and a positive mesh radius.");
    }
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
