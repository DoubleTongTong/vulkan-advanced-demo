#include "rendering/descriptors/textures/TextureDescriptorSet.h"

#include "VulkanContext.h"
#include "VulkanShaderModule.h"
#include "VulkanTexture2D.h"
#include "VulkanTextureCube.h"
#include "VulkanUtils.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

constexpr VkDescriptorBindingFlags TextureArrayBindingFlags =
    VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
    VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT |
    VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;

VkDescriptorType toVkDescriptorType(ShaderDescriptorType type) {
    switch (type) {
    case ShaderDescriptorType::Sampler:
        return VK_DESCRIPTOR_TYPE_SAMPLER;
    case ShaderDescriptorType::SampledImage:
        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case ShaderDescriptorType::StorageBuffer:
        break;
    }
    throw std::runtime_error("Unsupported reflected descriptor type.");
}

} // namespace

TextureDescriptorSet::TextureDescriptorSet(
    const VulkanContext& context,
    const TextureDescriptorSetDesc& desc)
    : context_(&context) {
    reflectBindings(desc);
    validateLimits();
    createLayout(desc.debugName);
    createPoolAndSet(desc.debugName);
}

TextureDescriptorSet::~TextureDescriptorSet() {
    destroy();
}

TextureDescriptorSet::TextureDescriptorSet(TextureDescriptorSet&& other) noexcept {
    *this = std::move(other);
}

TextureDescriptorSet& TextureDescriptorSet::operator=(TextureDescriptorSet&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    destroy();
    context_ = other.context_;
    bindings_ = std::move(other.bindings_);
    layout_ = other.layout_;
    pool_ = other.pool_;
    set_ = other.set_;

    other.context_ = nullptr;
    other.layout_ = VK_NULL_HANDLE;
    other.pool_ = VK_NULL_HANDLE;
    other.set_ = VK_NULL_HANDLE;
    return *this;
}

VkDescriptorSetLayout TextureDescriptorSet::layout() const {
    return layout_;
}

VkDescriptorSet TextureDescriptorSet::set() const {
    return set_;
}

uint32_t TextureDescriptorSet::capacity(std::string_view name) const {
    const auto it = std::find_if(bindings_.begin(), bindings_.end(), [name](const Binding& item) {
        return item.name == name;
    });
    if (it == bindings_.end()) {
        throw std::out_of_range("Descriptor resource '" + std::string(name) + "' does not exist.");
    }
    return it->descriptorCount;
}

void TextureDescriptorSet::fillSamplers(std::string_view name, VkSampler sampler) {
    const Binding& item = binding(name, VK_DESCRIPTOR_TYPE_SAMPLER);
    const uint32_t count = item.descriptorCount;
    std::vector<VkDescriptorImageInfo> samplerInfos(count);
    for (VkDescriptorImageInfo& info : samplerInfos) {
        info.sampler = sampler;
    }

    const VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set_,
        .dstBinding = item.binding,
        .descriptorCount = count,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
        .pImageInfo = samplerInfos.data(),
    };
    vkUpdateDescriptorSets(context_->device(), 1, &write, 0, nullptr);
}

void TextureDescriptorSet::writeSampler(
    std::string_view name,
    uint32_t index,
    VkSampler sampler) {
    const Binding& item = binding(name, VK_DESCRIPTOR_TYPE_SAMPLER);
    if (index >= item.descriptorCount) {
        throw std::out_of_range("Texture descriptor sampler index is out of range.");
    }

    const VkDescriptorImageInfo samplerInfo{.sampler = sampler};
    const VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set_,
        .dstBinding = item.binding,
        .dstArrayElement = index,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
        .pImageInfo = &samplerInfo,
    };
    vkUpdateDescriptorSets(context_->device(), 1, &write, 0, nullptr);
}

void TextureDescriptorSet::writeTexture2D(
    std::string_view name,
    uint32_t index,
    const VulkanTexture2D& texture) {
    writeSampledImage(
        name, index, texture.imageView(), texture.layout(), ShaderImageDimension::Image2D);
}

void TextureDescriptorSet::writeTextureCube(
    std::string_view name,
    uint32_t index,
    const VulkanTextureCube& texture) {
    writeSampledImage(
        name, index, texture.imageView(), texture.layout(), ShaderImageDimension::Cube);
}

void TextureDescriptorSet::bind(
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

const TextureDescriptorSet::Binding& TextureDescriptorSet::binding(
    std::string_view name,
    VkDescriptorType expectedType,
    ShaderImageDimension expectedDimension) const {
    const auto it = std::find_if(bindings_.begin(), bindings_.end(), [name](const Binding& item) {
        return item.name == name;
    });
    if (it == bindings_.end() ||
        it->descriptorType != expectedType ||
        it->imageDimension != expectedDimension) {
        throw std::out_of_range(
            "Descriptor resource '" + std::string(name) + "' has an incompatible type or image dimension.");
    }
    return *it;
}

void TextureDescriptorSet::writeSampledImage(
    std::string_view name,
    uint32_t index,
    VkImageView view,
    VkImageLayout layout,
    ShaderImageDimension dimension) {
    const Binding& item = binding(name, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, dimension);
    if (index >= item.descriptorCount) {
        throw std::out_of_range("Texture descriptor sampled image index is out of range.");
    }

    const VkDescriptorImageInfo imageInfo{
        .imageView = view,
        .imageLayout = layout,
    };
    const VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set_,
        .dstBinding = item.binding,
        .dstArrayElement = index,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .pImageInfo = &imageInfo,
    };
    vkUpdateDescriptorSets(context_->device(), 1, &write, 0, nullptr);
}

void TextureDescriptorSet::reflectBindings(const TextureDescriptorSetDesc& desc) {
    if (desc.shaders.empty()) {
        throw std::runtime_error("Texture descriptor reflection requires at least one shader.");
    }

    for (const VulkanShaderModule* shader : desc.shaders) {
        if (!shader) {
            throw std::runtime_error("Cannot reflect a null shader module.");
        }

        for (const ShaderDescriptorBinding& reflected : shader->descriptorBindings()) {
            if (reflected.set != desc.set) {
                continue;
            }

            const VkDescriptorType descriptorType = toVkDescriptorType(reflected.type);
            const bool runtimeArray = reflected.count == 0;
            if (reflected.name.empty()) {
                throw std::runtime_error(
                    "Descriptor binding " + std::to_string(reflected.binding) +
                    " has no reflected name. Keep SPIR-V descriptor names for resource lookup.");
            }
            uint32_t descriptorCount = reflected.count;
            if (runtimeArray) {
                const auto capacity = std::find_if(
                    desc.runtimeArrays.begin(),
                    desc.runtimeArrays.end(),
                    [&reflected](const TextureRuntimeArrayDesc& item) {
                        return item.name == reflected.name;
                    });
                if (capacity == desc.runtimeArrays.end() || capacity->capacity == 0) {
                    throw std::runtime_error(
                        "Runtime descriptor array binding " + std::to_string(reflected.binding) +
                        " requires a non-zero capacity.");
                }
                descriptorCount = capacity->capacity;
            }

            const auto existing = std::find_if(
                bindings_.begin(), bindings_.end(), [&reflected](const Binding& item) {
                    return item.binding == reflected.binding;
                });
            if (existing == bindings_.end()) {
                bindings_.push_back({
                    .name = reflected.name,
                    .binding = reflected.binding,
                    .descriptorType = descriptorType,
                    .imageDimension = reflected.imageDimension,
                    .descriptorCount = descriptorCount,
                    .stageFlags = static_cast<VkShaderStageFlags>(shader->vkStage()),
                    .runtimeArray = runtimeArray,
                });
            } else {
                if (existing->descriptorType != descriptorType ||
                    existing->name != reflected.name ||
                    existing->imageDimension != reflected.imageDimension ||
                    existing->descriptorCount != descriptorCount ||
                    existing->runtimeArray != runtimeArray) {
                    throw std::runtime_error(
                        "Shaders declare incompatible descriptor binding " +
                        std::to_string(reflected.binding) + ".");
                }
                existing->stageFlags |= shader->vkStage();
            }
        }
    }

    if (bindings_.empty()) {
        throw std::runtime_error("Shaders do not declare descriptors in the requested set.");
    }
    std::ranges::sort(bindings_, {}, &Binding::binding);
}

void TextureDescriptorSet::validateLimits() const {
    uint64_t sampledImageCount = 0;
    uint64_t samplerCount = 0;
    for (const Binding& item : bindings_) {
        if (item.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER) {
            samplerCount += item.descriptorCount;
        } else if (item.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) {
            sampledImageCount += item.descriptorCount;
        }
    }

    const VulkanDescriptorIndexingLimits limits = context_->descriptorIndexingLimits();
    if (sampledImageCount > limits.maxUpdateAfterBindSampledImages) {
        throw std::runtime_error("Texture sampled image capacity exceeds the device limit.");
    }
    if (samplerCount > limits.maxUpdateAfterBindSamplers) {
        throw std::runtime_error("Texture sampler capacity exceeds the device limit.");
    }
}

void TextureDescriptorSet::createLayout(const char* debugName) {
    std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
    std::vector<VkDescriptorBindingFlags> bindingFlags;
    layoutBindings.reserve(bindings_.size());
    bindingFlags.reserve(bindings_.size());

    for (const Binding& item : bindings_) {
        layoutBindings.push_back({
            .binding = item.binding,
            .descriptorType = item.descriptorType,
            .descriptorCount = item.descriptorCount,
            .stageFlags = item.stageFlags,
        });
        bindingFlags.push_back(
            item.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                ? TextureArrayBindingFlags
                : 0);
    }

    const VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = static_cast<uint32_t>(bindingFlags.size()),
        .pBindingFlags = bindingFlags.data(),
    };
    const VkDescriptorSetLayoutCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &bindingFlagsInfo,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = static_cast<uint32_t>(layoutBindings.size()),
        .pBindings = layoutBindings.data(),
    };
    vulkan_utils::checkVk(
        vkCreateDescriptorSetLayout(context_->device(), &createInfo, nullptr, &layout_),
        "vkCreateDescriptorSetLayout");
    context_->setDebugObjectName(
        VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
        reinterpret_cast<uint64_t>(layout_),
        debugName);
}

void TextureDescriptorSet::createPoolAndSet(const char* debugName) {
    std::vector<VkDescriptorPoolSize> poolSizes;
    for (const Binding& binding : bindings_) {
        const auto existing = std::find_if(
            poolSizes.begin(), poolSizes.end(), [&binding](const VkDescriptorPoolSize& size) {
                return size.type == binding.descriptorType;
            });
        if (existing == poolSizes.end()) {
            poolSizes.push_back({binding.descriptorType, binding.descriptorCount});
        } else {
            existing->descriptorCount += binding.descriptorCount;
        }
    }

    const VkDescriptorPoolCreateInfo poolCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 1,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data(),
    };
    vulkan_utils::checkVk(
        vkCreateDescriptorPool(context_->device(), &poolCreateInfo, nullptr, &pool_),
        "vkCreateDescriptorPool");
    context_->setDebugObjectName(
        VK_OBJECT_TYPE_DESCRIPTOR_POOL,
        reinterpret_cast<uint64_t>(pool_),
        (std::string(debugName) + " pool").c_str());

    const VkDescriptorSetAllocateInfo allocateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = pool_,
        .descriptorSetCount = 1,
        .pSetLayouts = &layout_,
    };
    vulkan_utils::checkVk(
        vkAllocateDescriptorSets(context_->device(), &allocateInfo, &set_),
        "vkAllocateDescriptorSets");
}

void TextureDescriptorSet::destroy() {
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
