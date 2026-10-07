#include "VulkanDescriptorSet.h"

#include "VulkanBuffer.h"
#include "VulkanContext.h"
#include "VulkanShaderModule.h"
#include "VulkanStorageTexture2D.h"
#include "VulkanTexture2D.h"
#include "VulkanTextureCube.h"
#include "VulkanUtils.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace {

constexpr VkDescriptorBindingFlags RuntimeTextureFlags =
    VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
    VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT |
    VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;

VkDescriptorType toVkDescriptorType(ShaderDescriptorType type) {
    switch (type) {
    case ShaderDescriptorType::Sampler:
        return VK_DESCRIPTOR_TYPE_SAMPLER;
    case ShaderDescriptorType::SampledImage:
        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case ShaderDescriptorType::StorageImage:
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case ShaderDescriptorType::StorageBuffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }
    throw std::runtime_error("Unsupported reflected descriptor type.");
}

} // namespace

VulkanDescriptorSet::VulkanDescriptorSet(
    const VulkanContext& context,
    const DescriptorSetDesc& desc)
    : context_(&context), setIndex_(desc.set) {
    reflectBindings(desc);
    validateLimits();
    const char* debugName = desc.debugName ? desc.debugName : "Descriptor set";

    // 构造函数抛异常时析构函数不会运行，因此这里保证部分创建的 Vulkan 资源也能回收。
    try {
        createLayout(debugName);
        createPoolAndSet(debugName);
    } catch (...) {
        destroy();
        throw;
    }
}

VulkanDescriptorSet::~VulkanDescriptorSet() {
    destroy();
}

VulkanDescriptorSet::VulkanDescriptorSet(VulkanDescriptorSet&& other) noexcept {
    *this = std::move(other);
}

VulkanDescriptorSet& VulkanDescriptorSet::operator=(VulkanDescriptorSet&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    destroy();
    context_ = other.context_;
    bindings_ = std::move(other.bindings_);
    setIndex_ = other.setIndex_;
    usesUpdateAfterBind_ = other.usesUpdateAfterBind_;
    layout_ = other.layout_;
    pool_ = other.pool_;
    set_ = other.set_;

    other.context_ = nullptr;
    other.layout_ = VK_NULL_HANDLE;
    other.pool_ = VK_NULL_HANDLE;
    other.set_ = VK_NULL_HANDLE;
    return *this;
}

VkDescriptorSetLayout VulkanDescriptorSet::layout() const {
    return layout_;
}

VkDescriptorSet VulkanDescriptorSet::handle() const {
    return set_;
}

uint32_t VulkanDescriptorSet::setIndex() const {
    return setIndex_;
}

uint32_t VulkanDescriptorSet::capacity(std::string_view name) const {
    const auto it = std::find_if(bindings_.begin(), bindings_.end(), [name](const Binding& item) {
        return item.name == name;
    });
    if (it == bindings_.end()) {
        throw std::out_of_range("Descriptor resource '" + std::string(name) + "' does not exist.");
    }
    return it->descriptorCount;
}

void VulkanDescriptorSet::writeStorageBuffer(
    std::string_view name,
    const VulkanBuffer& buffer,
    uint32_t index) {
    const Binding& item = binding(name, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    checkArrayIndex(item, index);
    if ((buffer.usageFlags() & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) == 0) {
        throw std::invalid_argument(
            "Descriptor resource '" + std::string(name) + "' requires storage buffer usage.");
    }

    const VkDescriptorBufferInfo bufferInfo{
        .buffer = buffer.handle(),
        .offset = 0,
        .range = buffer.size(),
    };
    const VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set_,
        .dstBinding = item.binding,
        .dstArrayElement = index,
        .descriptorCount = 1,
        .descriptorType = item.descriptorType,
        .pBufferInfo = &bufferInfo,
    };
    vkUpdateDescriptorSets(context_->device(), 1, &write, 0, nullptr);
}

void VulkanDescriptorSet::fillSamplers(std::string_view name, VkSampler sampler) {
    const Binding& item = binding(name, VK_DESCRIPTOR_TYPE_SAMPLER);
    std::vector<VkDescriptorImageInfo> samplerInfos(item.descriptorCount);
    for (VkDescriptorImageInfo& info : samplerInfos) {
        info.sampler = sampler;
    }

    const VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set_,
        .dstBinding = item.binding,
        .descriptorCount = item.descriptorCount,
        .descriptorType = item.descriptorType,
        .pImageInfo = samplerInfos.data(),
    };
    vkUpdateDescriptorSets(context_->device(), 1, &write, 0, nullptr);
}

void VulkanDescriptorSet::writeSampler(
    std::string_view name,
    uint32_t index,
    VkSampler sampler) {
    const Binding& item = binding(name, VK_DESCRIPTOR_TYPE_SAMPLER);
    checkArrayIndex(item, index);

    const VkDescriptorImageInfo samplerInfo{.sampler = sampler};
    const VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set_,
        .dstBinding = item.binding,
        .dstArrayElement = index,
        .descriptorCount = 1,
        .descriptorType = item.descriptorType,
        .pImageInfo = &samplerInfo,
    };
    vkUpdateDescriptorSets(context_->device(), 1, &write, 0, nullptr);
}

void VulkanDescriptorSet::writeTexture2D(
    std::string_view name,
    uint32_t index,
    const VulkanTexture2D& texture) {
    writeSampledImage(
        name, index, texture.imageView(), texture.layout(), ShaderImageDimension::Image2D);
}

void VulkanDescriptorSet::writeTextureCube(
    std::string_view name,
    uint32_t index,
    const VulkanTextureCube& texture) {
    writeSampledImage(
        name, index, texture.imageView(), texture.layout(), ShaderImageDimension::Cube);
}

void VulkanDescriptorSet::writeTexture2D(
    std::string_view name,
    uint32_t index,
    const VulkanStorageTexture2D& texture) {
    writeSampledImage(
        name,
        index,
        texture.imageView(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        ShaderImageDimension::Image2D);
}

void VulkanDescriptorSet::writeStorageImage2D(
    std::string_view name,
    uint32_t index,
    const VulkanStorageTexture2D& texture) {
    writeStorageImage(name, index, texture.imageView(), ShaderImageDimension::Image2D);
}

void VulkanDescriptorSet::bind(
    VkCommandBuffer commandBuffer,
    VkPipelineLayout pipelineLayout) const {
    bind(commandBuffer, pipelineLayout, VK_PIPELINE_BIND_POINT_GRAPHICS);
}

void VulkanDescriptorSet::bindCompute(
    VkCommandBuffer commandBuffer,
    VkPipelineLayout pipelineLayout) const {
    bind(commandBuffer, pipelineLayout, VK_PIPELINE_BIND_POINT_COMPUTE);
}

void VulkanDescriptorSet::bind(
    VkCommandBuffer commandBuffer,
    VkPipelineLayout pipelineLayout,
    VkPipelineBindPoint bindPoint) const {
    vkCmdBindDescriptorSets(
        commandBuffer,
        bindPoint,
        pipelineLayout,
        setIndex_,
        1,
        &set_,
        0,
        nullptr);
}

const VulkanDescriptorSet::Binding& VulkanDescriptorSet::binding(
    std::string_view name,
    VkDescriptorType expectedType,
    ShaderImageDimension expectedDimension) const {
    const auto it = std::find_if(bindings_.begin(), bindings_.end(), [name](const Binding& item) {
        return item.name == name;
    });
    if (it == bindings_.end()) {
        throw std::out_of_range("Descriptor resource '" + std::string(name) + "' does not exist.");
    }
    if (it->descriptorType != expectedType || it->imageDimension != expectedDimension) {
        throw std::invalid_argument(
            "Descriptor resource '" + std::string(name) + "' has an incompatible type.");
    }
    return *it;
}

void VulkanDescriptorSet::checkArrayIndex(const Binding& item, uint32_t index) const {
    if (index >= item.descriptorCount) {
        throw std::out_of_range(
            "Descriptor resource '" + item.name + "' array index is out of range.");
    }
}

void VulkanDescriptorSet::writeSampledImage(
    std::string_view name,
    uint32_t index,
    VkImageView view,
    VkImageLayout layout,
    ShaderImageDimension dimension) {
    const Binding& item = binding(name, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, dimension);
    checkArrayIndex(item, index);

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
        .descriptorType = item.descriptorType,
        .pImageInfo = &imageInfo,
    };
    vkUpdateDescriptorSets(context_->device(), 1, &write, 0, nullptr);
}

void VulkanDescriptorSet::writeStorageImage(
    std::string_view name,
    uint32_t index,
    VkImageView view,
    ShaderImageDimension dimension) {
    const Binding& item = binding(name, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, dimension);
    checkArrayIndex(item, index);

    const VkDescriptorImageInfo imageInfo{
        .imageView = view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };
    const VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set_,
        .dstBinding = item.binding,
        .dstArrayElement = index,
        .descriptorCount = 1,
        .descriptorType = item.descriptorType,
        .pImageInfo = &imageInfo,
    };
    vkUpdateDescriptorSets(context_->device(), 1, &write, 0, nullptr);
}

void VulkanDescriptorSet::reflectBindings(const DescriptorSetDesc& desc) {
    if (desc.shaders.empty()) {
        throw std::invalid_argument("Descriptor reflection requires at least one shader.");
    }

    for (const VulkanShaderModule* shader : desc.shaders) {
        if (!shader) {
            throw std::invalid_argument("Cannot reflect a null shader module.");
        }

        for (const ShaderDescriptorBinding& reflected : shader->descriptorBindings()) {
            if (reflected.set != setIndex_) {
                continue;
            }
            if (reflected.name.empty()) {
                throw std::runtime_error(
                    "Descriptor binding " + std::to_string(reflected.binding) +
                    " has no reflected name. Keep SPIR-V descriptor names for resource lookup.");
            }

            const VkDescriptorType descriptorType = toVkDescriptorType(reflected.type);
            const bool runtimeArray = reflected.count == 0;
            uint32_t descriptorCount = reflected.count;
            if (runtimeArray) {
                const auto capacity = std::find_if(
                    desc.runtimeArrays.begin(), desc.runtimeArrays.end(),
                    [&reflected](const RuntimeDescriptorArrayDesc& item) {
                        return item.name == reflected.name;
                    });
                if (capacity == desc.runtimeArrays.end() || capacity->capacity == 0) {
                    throw std::runtime_error(
                        "Runtime descriptor array '" + reflected.name +
                        "' requires a non-zero capacity.");
                }
                descriptorCount = capacity->capacity;
            }

            const VkDescriptorBindingFlags flags =
                runtimeArray && descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                    ? RuntimeTextureFlags
                    : 0;
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
                    .flags = flags,
                });
            } else {
                if (existing->name != reflected.name ||
                    existing->descriptorType != descriptorType ||
                    existing->imageDimension != reflected.imageDimension ||
                    existing->descriptorCount != descriptorCount ||
                    existing->flags != flags) {
                    throw std::runtime_error(
                        "Shaders declare incompatible descriptor binding " +
                        std::to_string(reflected.binding) + " in set " +
                        std::to_string(setIndex_) + ".");
                }
                existing->stageFlags |= shader->vkStage();
            }
        }
    }

    if (bindings_.empty()) {
        throw std::runtime_error(
            "Shaders do not declare descriptors in set " + std::to_string(setIndex_) + ".");
    }
    std::ranges::sort(bindings_, {}, &Binding::binding);
    for (size_t index = 1; index < bindings_.size(); ++index) {
        const auto duplicate = std::find_if(
            bindings_.begin(), bindings_.begin() + index,
            [this, index](const Binding& item) {
                return item.name == bindings_[index].name;
            });
        if (duplicate != bindings_.begin() + index) {
            throw std::runtime_error(
                "Descriptor resource name '" + bindings_[index].name +
                "' is used by more than one binding in set " +
                std::to_string(setIndex_) + ".");
        }
    }
    usesUpdateAfterBind_ = std::ranges::any_of(bindings_, [](const Binding& item) {
        return (item.flags & VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT) != 0;
    });
}

void VulkanDescriptorSet::validateLimits() const {
    if (!usesUpdateAfterBind_) {
        return;
    }

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
        throw std::runtime_error("Sampled image descriptor capacity exceeds the device limit.");
    }
    if (samplerCount > limits.maxUpdateAfterBindSamplers) {
        throw std::runtime_error("Sampler descriptor capacity exceeds the device limit.");
    }
}

void VulkanDescriptorSet::createLayout(const char* debugName) {
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
        bindingFlags.push_back(item.flags);
    }

    const VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = static_cast<uint32_t>(bindingFlags.size()),
        .pBindingFlags = bindingFlags.data(),
    };
    const VkDescriptorSetLayoutCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &bindingFlagsInfo,
        .flags = usesUpdateAfterBind_
            ? VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT
            : VkDescriptorSetLayoutCreateFlags{},
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

void VulkanDescriptorSet::createPoolAndSet(const char* debugName) {
    std::vector<VkDescriptorPoolSize> poolSizes;
    for (const Binding& item : bindings_) {
        const auto existing = std::find_if(
            poolSizes.begin(), poolSizes.end(), [&item](const VkDescriptorPoolSize& size) {
                return size.type == item.descriptorType;
            });
        if (existing == poolSizes.end()) {
            poolSizes.push_back({item.descriptorType, item.descriptorCount});
        } else {
            existing->descriptorCount += item.descriptorCount;
        }
    }

    const VkDescriptorPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = usesUpdateAfterBind_
            ? VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT
            : VkDescriptorPoolCreateFlags{},
        .maxSets = 1,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data(),
    };
    vulkan_utils::checkVk(
        vkCreateDescriptorPool(context_->device(), &poolInfo, nullptr, &pool_),
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

void VulkanDescriptorSet::destroy() {
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
    pool_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
    set_ = VK_NULL_HANDLE;
}
