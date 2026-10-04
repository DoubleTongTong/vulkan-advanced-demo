#include "rendering/mesh/instanced/InstancedMesh.h"

#include <stdexcept>

InstancedMesh::InstancedMesh(const VulkanContext& context, const MeshData& mesh)
    : vertexBuffer_(
          context,
          {
              .usage = BufferUsage_Storage,
              .storage = BufferStorage::Device,
              .size = mesh.vertexBytes().size(),
              .data = mesh.vertexBytes().data(),
              .debugName = "Instanced mesh vertices",
          }),
      indexBuffer_(
          context,
          {
              .usage = BufferUsage_Index,
              .storage = BufferStorage::Device,
              .size = mesh.indexBytes().size(),
              .data = mesh.indexBytes().data(),
              .debugName = "Instanced mesh indices",
          }),
      centerRadius_(
          mesh.bounds().center[0], mesh.bounds().center[1], mesh.bounds().center[2],
          mesh.bounds().radius),
      indexCount_(static_cast<uint32_t>(mesh.indices().size())) {
    if (mesh.indices().empty() || mesh.indices().size() % 3u != 0u) {
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

glm::vec4 InstancedMesh::centerRadius() const {
    return centerRadius_;
}
