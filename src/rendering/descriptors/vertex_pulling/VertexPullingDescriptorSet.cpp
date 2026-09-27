#include "rendering/descriptors/vertex_pulling/VertexPullingDescriptorSet.h"

#include "ShaderCompiler.h"
#include "VulkanBuffer.h"
#include "VulkanContext.h"
#include "VulkanShaderModule.h"
#include "VulkanUtils.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace {

constexpr uint32_t DescriptorSetIndex = 1;
constexpr const char* VerticesResource = "kVertices";

const ShaderDescriptorBinding& findVerticesBinding(const VulkanShaderModule& shader) {
    const auto it = std::find_if(
        shader.descriptorBindings().begin(), shader.descriptorBindings().end(),
        [](const ShaderDescriptorBinding& binding) {
            return binding.set == DescriptorSetIndex &&
                   binding.name == VerticesResource &&
                   binding.type == ShaderDescriptorType::StorageBuffer;
        });
    if (it == shader.descriptorBindings().end() || it->count != 1) {
        throw std::runtime_error(
            "Vertex pulling shader must declare one storage buffer named kVertices at set 1.");
    }
    return *it;
}

} // namespace

VertexPullingDescriptorSet::VertexPullingDescriptorSet(
    const VulkanContext& context,
    const VulkanShaderModule& vertexShader,
    const VulkanBuffer& vertexStorageBuffer)
    : context_(&context) {
    if ((vertexStorageBuffer.usageFlags() & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) == 0) {
        throw std::invalid_argument("Vertex pulling descriptors require storage buffer usage.");
    }

    const ShaderDescriptorBinding& reflected = findVerticesBinding(vertexShader);
    const VkDescriptorSetLayoutBinding binding{
        .binding = reflected.binding,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
    };
    const VkDescriptorSetLayoutCreateInfo layoutInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding,
    };
    vulkan_utils::checkVk(
        vkCreateDescriptorSetLayout(context_->device(), &layoutInfo, nullptr, &layout_),
        "vkCreateDescriptorSetLayout");
    context_->setDebugObjectName(
        VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
        reinterpret_cast<uint64_t>(layout_),
        "Vertex pulling descriptor layout");

    const VkDescriptorPoolSize poolSize{
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
    };
    const VkDescriptorPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize,
    };
    vulkan_utils::checkVk(
        vkCreateDescriptorPool(context_->device(), &poolInfo, nullptr, &pool_),
        "vkCreateDescriptorPool");

    const VkDescriptorSetAllocateInfo allocateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = pool_,
        .descriptorSetCount = 1,
        .pSetLayouts = &layout_,
    };
    vulkan_utils::checkVk(
        vkAllocateDescriptorSets(context_->device(), &allocateInfo, &set_),
        "vkAllocateDescriptorSets");

    const VkDescriptorBufferInfo bufferInfo{
        .buffer = vertexStorageBuffer.handle(),
        .offset = 0,
        .range = vertexStorageBuffer.size(),
    };
    const VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set_,
        .dstBinding = reflected.binding,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &bufferInfo,
    };
    vkUpdateDescriptorSets(context_->device(), 1, &write, 0, nullptr);
}

VertexPullingDescriptorSet::~VertexPullingDescriptorSet() {
    destroy();
}

VertexPullingDescriptorSet::VertexPullingDescriptorSet(
    VertexPullingDescriptorSet&& other) noexcept {
    *this = std::move(other);
}

VertexPullingDescriptorSet& VertexPullingDescriptorSet::operator=(
    VertexPullingDescriptorSet&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    destroy();
    context_ = other.context_;
    layout_ = other.layout_;
    pool_ = other.pool_;
    set_ = other.set_;
    other.context_ = nullptr;
    other.layout_ = VK_NULL_HANDLE;
    other.pool_ = VK_NULL_HANDLE;
    other.set_ = VK_NULL_HANDLE;
    return *this;
}

VkDescriptorSetLayout VertexPullingDescriptorSet::layout() const {
    return layout_;
}

void VertexPullingDescriptorSet::bind(
    VkCommandBuffer commandBuffer,
    VkPipelineLayout pipelineLayout,
    uint32_t setIndex) const {
    if (setIndex != DescriptorSetIndex) {
        throw std::invalid_argument("Vertex pulling descriptors must bind at set 1.");
    }
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout,
        setIndex,
        1,
        &set_,
        0,
        nullptr);
}

void VertexPullingDescriptorSet::destroy() {
    if (!context_) {
        return;
    }
    if (pool_) {
        vkDestroyDescriptorPool(context_->device(), pool_, nullptr);
    }
    if (layout_) {
        vkDestroyDescriptorSetLayout(context_->device(), layout_, nullptr);
    }
    context_ = nullptr;
    layout_ = VK_NULL_HANDLE;
    pool_ = VK_NULL_HANDLE;
    set_ = VK_NULL_HANDLE;
}
