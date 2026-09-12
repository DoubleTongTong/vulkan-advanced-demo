#include "VulkanRenderPipeline.h"

#include "VulkanContext.h"
#include "VulkanShaderModule.h"
#include "VulkanUtils.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace {

class VulkanGraphicsPipelineBuilder {
public:
    VulkanGraphicsPipelineBuilder& shaderStage(const VkPipelineShaderStageCreateInfo& stage) {
        if (stage.module) {
            shaderStages_[shaderStageCount_++] = stage;
        }
        return *this;
    }

    VulkanGraphicsPipelineBuilder& dynamicState(VkDynamicState state) {
        dynamicStates_[dynamicStateCount_++] = state;
        return *this;
    }

    VulkanGraphicsPipelineBuilder& primitiveTopology(VkPrimitiveTopology topology) {
        inputAssembly_.topology = topology;
        return *this;
    }

    VulkanGraphicsPipelineBuilder& cullMode(VkCullModeFlags cullMode) {
        rasterization_.cullMode = cullMode;
        return *this;
    }

    VulkanGraphicsPipelineBuilder& frontFace(VkFrontFace frontFace) {
        rasterization_.frontFace = frontFace;
        return *this;
    }

    VulkanGraphicsPipelineBuilder& colorAttachment(VkFormat format) {
        colorFormat_ = format;
        colorBlendAttachment_ = {
            .blendEnable = VK_FALSE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                              VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT |
                              VK_COLOR_COMPONENT_A_BIT,
        };
        return *this;
    }

    VkResult build(
        VkDevice device,
        VkPipelineLayout layout,
        VkPipeline* outPipeline) const {
        const VkPipelineDynamicStateCreateInfo dynamicState{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = dynamicStateCount_,
            .pDynamicStates = dynamicStates_.data(),
        };

        const VkPipelineViewportStateCreateInfo viewportState{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .pViewports = nullptr,
            .scissorCount = 1,
            .pScissors = nullptr,
        };

        const VkPipelineColorBlendStateCreateInfo colorBlendState{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .logicOpEnable = VK_FALSE,
            .logicOp = VK_LOGIC_OP_COPY,
            .attachmentCount = 1,
            .pAttachments = &colorBlendAttachment_,
        };

        const VkPipelineRenderingCreateInfo renderingInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &colorFormat_,
        };

        const VkGraphicsPipelineCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &renderingInfo,
            .stageCount = shaderStageCount_,
            .pStages = shaderStages_.data(),
            .pVertexInputState = &vertexInput_,
            .pInputAssemblyState = &inputAssembly_,
            .pViewportState = &viewportState,
            .pRasterizationState = &rasterization_,
            .pMultisampleState = &multisample_,
            .pDepthStencilState = &depthStencil_,
            .pColorBlendState = &colorBlendState,
            .pDynamicState = &dynamicState,
            .layout = layout,
            .renderPass = VK_NULL_HANDLE,
            .subpass = 0,
        };

        return vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &createInfo, nullptr, outPipeline);
    }

private:
    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages_{};
    uint32_t shaderStageCount_ = 0;

    std::array<VkDynamicState, 2> dynamicStates_{};
    uint32_t dynamicStateCount_ = 0;

    VkPipelineVertexInputStateCreateInfo vertexInput_{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    VkPipelineInputAssemblyStateCreateInfo inputAssembly_{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkPipelineRasterizationStateCreateInfo rasterization_{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo multisample_{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
    };
    VkPipelineDepthStencilStateCreateInfo depthStencil_{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_FALSE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_ALWAYS,
        .stencilTestEnable = VK_FALSE,
    };
    VkPipelineColorBlendAttachmentState colorBlendAttachment_{};
    VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;
};

VkPipelineShaderStageCreateInfo shaderStageInfo(
    const VulkanShaderModule& shader,
    const char* entryPoint) {
    return {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = shader.vkStage(),
        .module = shader.handle(),
        .pName = entryPoint,
    };
}

} // namespace

VulkanRenderPipeline::VulkanRenderPipeline(
    const VulkanContext& context,
    const RenderPipelineDesc& desc)
    : context_(&context) {
    if (!desc.vertexShader || !desc.vertexShader->handle()) {
        throw std::runtime_error("A render pipeline requires a valid vertex shader.");
    }

    if (!desc.fragmentShader || !desc.fragmentShader->handle()) {
        throw std::runtime_error("A render pipeline requires a valid fragment shader.");
    }

    if (desc.colorFormat == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("A render pipeline requires a valid color attachment format.");
    }

    createPipelineLayout(desc);
    createPipeline(desc);
}

VulkanRenderPipeline::~VulkanRenderPipeline() {
    destroy();
}

VulkanRenderPipeline::VulkanRenderPipeline(VulkanRenderPipeline&& other) noexcept {
    *this = std::move(other);
}

VulkanRenderPipeline& VulkanRenderPipeline::operator=(VulkanRenderPipeline&& other) noexcept {
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

VkPipeline VulkanRenderPipeline::handle() const {
    return pipeline_;
}

VkPipelineLayout VulkanRenderPipeline::layout() const {
    return layout_;
}

uint32_t VulkanRenderPipeline::pushConstantSize() const {
    return pushConstantSize_;
}

void VulkanRenderPipeline::bind(VkCommandBuffer commandBuffer) const {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
}

void VulkanRenderPipeline::createPipelineLayout(const RenderPipelineDesc& desc) {
    pushConstantSize_ = std::max(
        desc.vertexShader->pushConstantSize(),
        desc.fragmentShader->pushConstantSize());

    VkShaderStageFlags pushConstantStages = 0;
    if (desc.vertexShader->pushConstantSize() > 0) {
        pushConstantStages |= desc.vertexShader->vkStage();
    }
    if (desc.fragmentShader->pushConstantSize() > 0) {
        pushConstantStages |= desc.fragmentShader->vkStage();
    }

    const VkPushConstantRange pushConstantRange{
        .stageFlags = pushConstantStages,
        .offset = 0,
        .size = pushConstantSize_,
    };

    const VkPipelineLayoutCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = pushConstantSize_ > 0 ? 1u : 0u,
        .pPushConstantRanges = pushConstantSize_ > 0 ? &pushConstantRange : nullptr,
    };

    vulkan_utils::checkVk(
        vkCreatePipelineLayout(context_->device(), &createInfo, nullptr, &layout_),
        "vkCreatePipelineLayout");
    context_->setDebugObjectName(
        VK_OBJECT_TYPE_PIPELINE_LAYOUT,
        reinterpret_cast<uint64_t>(layout_),
        "Render pipeline layout");
}

void VulkanRenderPipeline::createPipeline(const RenderPipelineDesc& desc) {
    VulkanGraphicsPipelineBuilder builder;
    builder
        .dynamicState(VK_DYNAMIC_STATE_VIEWPORT)
        .dynamicState(VK_DYNAMIC_STATE_SCISSOR)
        .primitiveTopology(desc.topology)
        .cullMode(desc.cullMode)
        .frontFace(desc.frontFace)
        .colorAttachment(desc.colorFormat)
        .shaderStage(shaderStageInfo(*desc.vertexShader, "main"))
        .shaderStage(shaderStageInfo(*desc.fragmentShader, "main"));

    vulkan_utils::checkVk(
        builder.build(context_->device(), layout_, &pipeline_),
        "vkCreateGraphicsPipelines");
    context_->setDebugObjectName(
        VK_OBJECT_TYPE_PIPELINE,
        reinterpret_cast<uint64_t>(pipeline_),
        desc.debugName);
}

void VulkanRenderPipeline::destroy() {
    if (!context_) {
        return;
    }

    if (pipeline_) {
        vkDestroyPipeline(context_->device(), pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }

    if (layout_) {
        vkDestroyPipelineLayout(context_->device(), layout_, nullptr);
        layout_ = VK_NULL_HANDLE;
    }
}
