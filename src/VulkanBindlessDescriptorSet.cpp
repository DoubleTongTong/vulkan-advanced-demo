#include "VulkanBindlessDescriptorSet.h"

#include "VulkanContext.h"
#include "VulkanTexture2D.h"
#include "VulkanUtils.h"

#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

VulkanBindlessDescriptorSet::VulkanBindlessDescriptorSet(
    const VulkanContext& context,
    uint32_t maxTextures,
    uint32_t maxSamplers,
    const char* debugName)
    : context_(&context),
      maxTextures_(maxTextures),
      maxSamplers_(maxSamplers) {
    if (maxTextures_ == 0 || maxSamplers_ == 0) {
        throw std::runtime_error("Bindless descriptor table sizes must be greater than zero.");
    }

    validateLimits();
    createLayout(debugName);
    createPoolAndSet(debugName);
}

VulkanBindlessDescriptorSet::~VulkanBindlessDescriptorSet() {
    destroy();
}

VulkanBindlessDescriptorSet::VulkanBindlessDescriptorSet(VulkanBindlessDescriptorSet&& other) noexcept {
    *this = std::move(other);
}

VulkanBindlessDescriptorSet& VulkanBindlessDescriptorSet::operator=(
    VulkanBindlessDescriptorSet&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    destroy();

    context_ = other.context_;
    maxTextures_ = other.maxTextures_;
    maxSamplers_ = other.maxSamplers_;
    layout_ = other.layout_;
    pool_ = other.pool_;
    set_ = other.set_;

    other.context_ = nullptr;
    other.maxTextures_ = 0;
    other.maxSamplers_ = 0;
    other.layout_ = VK_NULL_HANDLE;
    other.pool_ = VK_NULL_HANDLE;
    other.set_ = VK_NULL_HANDLE;

    return *this;
}

VkDescriptorSetLayout VulkanBindlessDescriptorSet::layout() const {
    return layout_;
}

VkDescriptorSet VulkanBindlessDescriptorSet::set() const {
    return set_;
}

uint32_t VulkanBindlessDescriptorSet::maxTextures() const {
    return maxTextures_;
}

uint32_t VulkanBindlessDescriptorSet::maxSamplers() const {
    return maxSamplers_;
}

void VulkanBindlessDescriptorSet::fillSamplers(VkSampler sampler) {
    std::vector<VkDescriptorImageInfo> samplerInfos(maxSamplers_);
    for (VkDescriptorImageInfo& info : samplerInfos) {
        info.sampler = sampler;
    }

    const VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set_,
        .dstBinding = SamplersBinding,
        .dstArrayElement = 0,
        .descriptorCount = maxSamplers_,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
        .pImageInfo = samplerInfos.data(),
    };
    vkUpdateDescriptorSets(context_->device(), 1, &write, 0, nullptr);
}

void VulkanBindlessDescriptorSet::writeSampler(uint32_t samplerIndex, VkSampler sampler) {
    if (samplerIndex >= maxSamplers_) {
        throw std::out_of_range("Bindless sampler index is out of range.");
    }

    const VkDescriptorImageInfo samplerInfo{
        .sampler = sampler,
    };
    const VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set_,
        .dstBinding = SamplersBinding,
        .dstArrayElement = samplerIndex,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
        .pImageInfo = &samplerInfo,
    };
    vkUpdateDescriptorSets(context_->device(), 1, &write, 0, nullptr);
}

void VulkanBindlessDescriptorSet::writeTexture2D(uint32_t textureIndex, const VulkanTexture2D& texture) {
    if (textureIndex >= maxTextures_) {
        throw std::out_of_range("Bindless texture index is out of range.");
    }

    const VkDescriptorImageInfo textureInfo{
        .imageView = texture.imageView(),
        .imageLayout = texture.layout(),
    };
    const VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set_,
        .dstBinding = Textures2DBinding,
        .dstArrayElement = textureIndex,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .pImageInfo = &textureInfo,
    };
    vkUpdateDescriptorSets(context_->device(), 1, &write, 0, nullptr);
}

void VulkanBindlessDescriptorSet::bind(
    VkCommandBuffer commandBuffer,
    VkPipelineLayout pipelineLayout,
    uint32_t setIndex) const {
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

void VulkanBindlessDescriptorSet::validateLimits() const {
    const VulkanDescriptorIndexingLimits limits = context_->descriptorIndexingLimits();

    if (maxTextures_ > limits.maxUpdateAfterBindSampledImages) {
        throw std::runtime_error(
            "Bindless texture capacity " + std::to_string(maxTextures_) +
            " exceeds maxDescriptorSetUpdateAfterBindSampledImages " +
            std::to_string(limits.maxUpdateAfterBindSampledImages) + ".");
    }

    if (maxSamplers_ > limits.maxUpdateAfterBindSamplers) {
        throw std::runtime_error(
            "Bindless sampler capacity " + std::to_string(maxSamplers_) +
            " exceeds maxDescriptorSetUpdateAfterBindSamplers " +
            std::to_string(limits.maxUpdateAfterBindSamplers) + ".");
    }
}

void VulkanBindlessDescriptorSet::createLayout(const char* debugName) {
    constexpr VkShaderStageFlags ShaderStages =
        VK_SHADER_STAGE_VERTEX_BIT |
        VK_SHADER_STAGE_FRAGMENT_BIT;

    const VkDescriptorSetLayoutBinding bindings[] = {
        {
            .binding = SamplersBinding,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
            .descriptorCount = maxSamplers_,
            .stageFlags = ShaderStages,
        },
        {
            .binding = Textures2DBinding,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .descriptorCount = maxTextures_,
            .stageFlags = ShaderStages,
        },
    };
    const VkDescriptorBindingFlags bindingFlags[] = {
        0,
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
            VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT |
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
            VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT,
    };
    const VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = static_cast<uint32_t>(std::size(bindingFlags)),
        .pBindingFlags = bindingFlags,
    };

    const VkDescriptorSetLayoutCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &bindingFlagsInfo,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = static_cast<uint32_t>(std::size(bindings)),
        .pBindings = bindings,
    };

    vulkan_utils::checkVk(
        vkCreateDescriptorSetLayout(context_->device(), &createInfo, nullptr, &layout_),
        "vkCreateDescriptorSetLayout");
    context_->setDebugObjectName(
        VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
        reinterpret_cast<uint64_t>(layout_),
        debugName);
}

void VulkanBindlessDescriptorSet::createPoolAndSet(const char* debugName) {
    const VkDescriptorPoolSize poolSizes[] = {
        {
            .type = VK_DESCRIPTOR_TYPE_SAMPLER,
            .descriptorCount = maxSamplers_,
        },
        {
            .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .descriptorCount = maxTextures_,
        },
    };
    const VkDescriptorPoolCreateInfo poolCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 1,
        .poolSizeCount = static_cast<uint32_t>(std::size(poolSizes)),
        .pPoolSizes = poolSizes,
    };

    vulkan_utils::checkVk(
        vkCreateDescriptorPool(context_->device(), &poolCreateInfo, nullptr, &pool_),
        "vkCreateDescriptorPool");
    context_->setDebugObjectName(
        VK_OBJECT_TYPE_DESCRIPTOR_POOL,
        reinterpret_cast<uint64_t>(pool_),
        (std::string(debugName) + " pool").c_str());

    const uint32_t textureDescriptorCount = maxTextures_;
    const VkDescriptorSetVariableDescriptorCountAllocateInfo variableDescriptorCountInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
        .descriptorSetCount = 1,
        .pDescriptorCounts = &textureDescriptorCount,
    };
    const VkDescriptorSetAllocateInfo allocateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = &variableDescriptorCountInfo,
        .descriptorPool = pool_,
        .descriptorSetCount = 1,
        .pSetLayouts = &layout_,
    };

    vulkan_utils::checkVk(
        vkAllocateDescriptorSets(context_->device(), &allocateInfo, &set_),
        "vkAllocateDescriptorSets");
}

void VulkanBindlessDescriptorSet::destroy() {
    if (!context_) {
        return;
    }

    if (pool_) {
        vkDestroyDescriptorPool(context_->device(), pool_, nullptr);
        pool_ = VK_NULL_HANDLE;
        set_ = VK_NULL_HANDLE;
    }

    if (layout_) {
        vkDestroyDescriptorSetLayout(context_->device(), layout_, nullptr);
        layout_ = VK_NULL_HANDLE;
    }
}
