#include "rendering/mesh/computed/VulkanComputedMesh.h"

#include "VulkanContext.h"

#include <cstddef>
#include <stdexcept>

namespace {

// 使用三个 vec4，保证 C++ Vertex Input 与 GLSL std430 写入布局完全一致。
constexpr VkDeviceSize VertexStride = 12 * sizeof(float);
constexpr uint32_t PositionOffset = 0;
constexpr uint32_t TexCoordOffset = 4 * sizeof(float);
constexpr uint32_t NormalOffset = 8 * sizeof(float);

} // namespace

VulkanComputedMesh::VulkanComputedMesh(
    const VulkanContext& context,
    uint32_t uSegments,
    uint32_t vSegments)
    : uSegments_(uSegments),
      vSegments_(vSegments),
      indices_(generateIndices(uSegments, vSegments)),
      indexCount_(static_cast<uint32_t>(indices_.size())),
      vertexBuffer_(
          context,
          BufferDesc{
              .usage = BufferUsage_Vertex | BufferUsage_Storage,
              .storage = BufferStorage::Device,
              .size = VertexStride * uSegments * vSegments,
              .debugName = "Computed torus knot vertices",
          }),
      indexBuffer_(
          context,
          BufferDesc{
              .usage = BufferUsage_Index,
              .storage = BufferStorage::Device,
              .size = indices_.size() * sizeof(uint32_t),
              .data = indices_.data(),
              .debugName = "Computed torus knot indices",
          }) {
    if (uSegments_ < 2 || vSegments_ < 2) {
        throw std::invalid_argument("Computed mesh dimensions must be at least 2x2.");
    }
}

uint32_t VulkanComputedMesh::uSegments() const {
    return uSegments_;
}

uint32_t VulkanComputedMesh::vSegments() const {
    return vSegments_;
}

uint32_t VulkanComputedMesh::vertexCount() const {
    return uSegments_ * vSegments_;
}

uint32_t VulkanComputedMesh::indexCount() const {
    return indexCount_;
}

const VulkanBuffer& VulkanComputedMesh::vertexBuffer() const {
    return vertexBuffer_;
}

std::vector<VkVertexInputBindingDescription> VulkanComputedMesh::vertexBindings() {
    return {{0, static_cast<uint32_t>(VertexStride), VK_VERTEX_INPUT_RATE_VERTEX}};
}

std::vector<VkVertexInputAttributeDescription> VulkanComputedMesh::vertexAttributes() {
    return {
        {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, PositionOffset},
        {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, TexCoordOffset},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, NormalOffset},
    };
}

void VulkanComputedMesh::transitionForComputeWrite(VkCommandBuffer commandBuffer) {
    if (accessState_ == AccessState::ComputeWrite) {
        throw std::logic_error("Computed mesh is already prepared for compute writes.");
    }

    const bool firstUse = accessState_ == AccessState::Undefined;
    const VkBufferMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = firstUse ? 0u : VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = vertexBuffer_.handle(),
        .offset = 0,
        .size = VK_WHOLE_SIZE,
    };
    vkCmdPipelineBarrier(
        commandBuffer,
        firstUse ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        1, &barrier,
        0, nullptr);
    accessState_ = AccessState::ComputeWrite;
}

void VulkanComputedMesh::transitionForVertexRead(VkCommandBuffer commandBuffer) {
    if (accessState_ != AccessState::ComputeWrite) {
        throw std::logic_error("Computed mesh must be generated before vertex input reads it.");
    }

    const VkBufferMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = vertexBuffer_.handle(),
        .offset = 0,
        .size = VK_WHOLE_SIZE,
    };
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        0,
        0, nullptr,
        1, &barrier,
        0, nullptr);
    accessState_ = AccessState::VertexRead;
}

void VulkanComputedMesh::bindAndDraw(VkCommandBuffer commandBuffer) const {
    if (accessState_ != AccessState::VertexRead) {
        throw std::logic_error("Computed mesh is not ready for drawing.");
    }

    const VkBuffer vertexBuffers[] = {vertexBuffer_.handle()};
    const VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer_.handle(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, indexCount_, 1, 0, 0, 0);
}

std::vector<uint32_t> VulkanComputedMesh::generateIndices(
    uint32_t uSegments,
    uint32_t vSegments) {
    if (uSegments < 2 || vSegments < 2) {
        throw std::invalid_argument("Computed mesh dimensions must be at least 2x2.");
    }

    std::vector<uint32_t> indices;
    indices.reserve(
        static_cast<size_t>(uSegments - 1) *
        static_cast<size_t>(vSegments - 1) * 6);
    for (uint32_t u = 0; u + 1 < uSegments; ++u) {
        for (uint32_t v = 0; v + 1 < vSegments; ++v) {
            const uint32_t i0 = u * vSegments + v;
            const uint32_t i1 = (u + 1) * vSegments + v;
            const uint32_t i2 = (u + 1) * vSegments + v + 1;
            const uint32_t i3 = u * vSegments + v + 1;
            indices.insert(indices.end(), {i0, i1, i3, i1, i2, i3});
        }
    }
    return indices;
}
