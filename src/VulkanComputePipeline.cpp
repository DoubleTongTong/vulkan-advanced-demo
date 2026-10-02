#include "VulkanComputePipeline.h"

#include "VulkanContext.h"
#include "VulkanShaderModule.h"
#include "VulkanUtils.h"

#include <stdexcept>
#include <utility>

VulkanComputePipeline::VulkanComputePipeline(
    const VulkanContext& context,
    const ComputePipelineDesc& desc)
    : context_(&context) {
    if (!desc.shader || !desc.shader->handle() || desc.shader->stage() != ShaderStage::Compute) {
        throw std::invalid_argument("A compute pipeline requires a valid compute shader.");
    }

    pushConstantSize_ = desc.shader->pushConstantSize();
    const VkPushConstantRange pushConstantRange{
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = pushConstantSize_,
    };
    const VkPipelineLayoutCreateInfo layoutInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = static_cast<uint32_t>(desc.descriptorSetLayouts.size()),
        .pSetLayouts = desc.descriptorSetLayouts.empty()
            ? nullptr
            : desc.descriptorSetLayouts.data(),
        .pushConstantRangeCount = pushConstantSize_ > 0 ? 1u : 0u,
        .pPushConstantRanges = pushConstantSize_ > 0 ? &pushConstantRange : nullptr,
    };

    try {
        vulkan_utils::checkVk(
            vkCreatePipelineLayout(context_->device(), &layoutInfo, nullptr, &layout_),
            "vkCreatePipelineLayout");

        const VkPipelineShaderStageCreateInfo stageInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = desc.shader->handle(),
            .pName = "main",
        };
        const VkComputePipelineCreateInfo pipelineInfo{
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = stageInfo,
            .layout = layout_,
        };
        vulkan_utils::checkVk(
            vkCreateComputePipelines(
                context_->device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_),
            "vkCreateComputePipelines");
    } catch (...) {
        destroy();
        throw;
    }

    const char* debugName = desc.debugName ? desc.debugName : "Compute pipeline";
    context_->setDebugObjectName(
        VK_OBJECT_TYPE_PIPELINE_LAYOUT,
        reinterpret_cast<uint64_t>(layout_),
        "Compute pipeline layout");
    context_->setDebugObjectName(
        VK_OBJECT_TYPE_PIPELINE,
        reinterpret_cast<uint64_t>(pipeline_),
        debugName);
}

VulkanComputePipeline::~VulkanComputePipeline() {
    destroy();
}

VulkanComputePipeline::VulkanComputePipeline(VulkanComputePipeline&& other) noexcept {
    *this = std::move(other);
}

VulkanComputePipeline& VulkanComputePipeline::operator=(VulkanComputePipeline&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    destroy();
    context_ = other.context_;
    layout_ = other.layout_;
    pipeline_ = other.pipeline_;
    pushConstantSize_ = other.pushConstantSize_;
    other.context_ = nullptr;
    other.layout_ = VK_NULL_HANDLE;
    other.pipeline_ = VK_NULL_HANDLE;
    other.pushConstantSize_ = 0;
    return *this;
}

VkPipelineLayout VulkanComputePipeline::layout() const {
    return layout_;
}

uint32_t VulkanComputePipeline::pushConstantSize() const {
    return pushConstantSize_;
}

void VulkanComputePipeline::bind(VkCommandBuffer commandBuffer) const {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
}

void VulkanComputePipeline::destroy() {
    if (!context_) {
        return;
    }
    if (pipeline_) {
        vkDestroyPipeline(context_->device(), pipeline_, nullptr);
    }
    if (layout_) {
        vkDestroyPipelineLayout(context_->device(), layout_, nullptr);
    }
    context_ = nullptr;
    pipeline_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
}
