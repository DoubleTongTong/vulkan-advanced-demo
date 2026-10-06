#include "rendering/mesh/indirect/IndirectMesh.h"

#include "VulkanContext.h"

#include <limits>
#include <stdexcept>
#include <vector>

namespace {

std::vector<VkDrawIndexedIndirectCommand> makeCommands(const MeshData& meshData) {
    if (meshData.meshCount() > std::numeric_limits<uint32_t>::max()) {
        throw std::overflow_error("Indirect mesh draw count exceeds the Vulkan limit.");
    }

    std::vector<VkDrawIndexedIndirectCommand> commands(meshData.meshCount());
    for (size_t index = 0; index < meshData.meshCount(); ++index) {
        const MeshDescriptor& mesh = meshData.descriptor(index);
        if (mesh.vertexOffset > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
            throw std::overflow_error("Indirect mesh base vertex exceeds the signed Vulkan limit.");
        }
        commands[index] = {
            .indexCount = mesh.lodIndexCount(0),
            .instanceCount = 1,
            .firstIndex = mesh.indexOffset + mesh.lodOffsets[0],
            .vertexOffset = static_cast<int32_t>(mesh.vertexOffset),
            .firstInstance = 0,
        };
    }
    return commands;
}

VulkanBuffer makeIndirectBuffer(const VulkanContext& context, const MeshData& meshData) {
    const std::vector<VkDrawIndexedIndirectCommand> commands = makeCommands(meshData);
    return VulkanBuffer(
        context,
        BufferDesc{
            .usage = BufferUsage_Indirect,
            .storage = BufferStorage::Device,
            .size = sizeof(VkDrawIndexedIndirectCommand) * commands.size(),
            .data = commands.data(),
            .debugName = "Bistro indexed indirect commands",
        });
}

} // namespace

IndirectMesh::IndirectMesh(const VulkanContext& context, const MeshData& meshData)
    : vertexBuffer_(
          context,
          BufferDesc{
              .usage = BufferUsage_Vertex,
              .storage = BufferStorage::Device,
              .size = meshData.vertexBytes().size(),
              .data = meshData.vertexBytes().data(),
              .debugName = "Bistro shared vertex buffer",
          }),
      indexBuffer_(
          context,
          BufferDesc{
              .usage = BufferUsage_Index,
              .storage = BufferStorage::Device,
              .size = meshData.indexBytes().size(),
              .data = meshData.indexBytes().data(),
              .debugName = "Bistro shared index buffer",
          }),
      indirectBuffer_(makeIndirectBuffer(context, meshData)),
      drawCount_(static_cast<uint32_t>(meshData.meshCount())) {
    if (drawCount_ == 0) {
        throw std::invalid_argument("Indirect mesh requires at least one draw command.");
    }
}

void IndirectMesh::bindAndDraw(VkCommandBuffer commandBuffer) const {
    const VkBuffer vertexBuffers[] = {vertexBuffer_.handle()};
    constexpr VkDeviceSize VertexOffsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, VertexOffsets);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer_.handle(), 0, VK_INDEX_TYPE_UINT32);

    // 命令数量当前由 CPU 已知，因此直接使用 Vulkan 1.0 的 indirect 入口；
    // GPU culling 接入后可把数量写入 count buffer，再切换到 IndirectCount。
    vkCmdDrawIndexedIndirect(
        commandBuffer,
        indirectBuffer_.handle(),
        0,
        drawCount_,
        sizeof(VkDrawIndexedIndirectCommand));
}

uint32_t IndirectMesh::drawCount() const {
    return drawCount_;
}
