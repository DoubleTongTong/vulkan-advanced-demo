#pragma once

#include "ShaderCompiler.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class VulkanBuffer;
class VulkanContext;
class VulkanShaderModule;
class VulkanTexture2D;
class VulkanTextureCube;

struct RuntimeDescriptorArrayDesc {
    std::string name;
    uint32_t capacity = 0;
};

struct DescriptorSetDesc {
    std::vector<const VulkanShaderModule*> shaders;
    std::vector<RuntimeDescriptorArrayDesc> runtimeArrays;
    uint32_t set = 0;
    const char* debugName = "Descriptor set";
};

// 通用 descriptor set：从 SPIR-V 反射布局，拥有 layout/pool/set，
// 并按资源名完成带类型检查的写入。set 序号在构造后不可变，绑定时不会绑错位置。
class VulkanDescriptorSet final {
public:
    VulkanDescriptorSet(const VulkanContext& context, const DescriptorSetDesc& desc);
    ~VulkanDescriptorSet();

    VulkanDescriptorSet(const VulkanDescriptorSet&) = delete;
    VulkanDescriptorSet& operator=(const VulkanDescriptorSet&) = delete;

    VulkanDescriptorSet(VulkanDescriptorSet&& other) noexcept;
    VulkanDescriptorSet& operator=(VulkanDescriptorSet&& other) noexcept;

    VkDescriptorSetLayout layout() const;
    VkDescriptorSet handle() const;
    uint32_t setIndex() const;
    uint32_t capacity(std::string_view name) const;

    void writeStorageBuffer(
        std::string_view name,
        const VulkanBuffer& buffer,
        uint32_t index = 0);
    void fillSamplers(std::string_view name, VkSampler sampler);
    void writeSampler(std::string_view name, uint32_t index, VkSampler sampler);
    void writeTexture2D(std::string_view name, uint32_t index, const VulkanTexture2D& texture);
    void writeTextureCube(std::string_view name, uint32_t index, const VulkanTextureCube& texture);

    void bind(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout) const;

private:
    struct Binding {
        std::string name;
        uint32_t binding = 0;
        VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_MAX_ENUM;
        ShaderImageDimension imageDimension = ShaderImageDimension::None;
        uint32_t descriptorCount = 0;
        VkShaderStageFlags stageFlags = 0;
        VkDescriptorBindingFlags flags = 0;
    };

    const Binding& binding(
        std::string_view name,
        VkDescriptorType expectedType,
        ShaderImageDimension expectedDimension = ShaderImageDimension::None) const;
    void checkArrayIndex(const Binding& binding, uint32_t index) const;
    void writeSampledImage(
        std::string_view name,
        uint32_t index,
        VkImageView view,
        VkImageLayout layout,
        ShaderImageDimension dimension);
    void reflectBindings(const DescriptorSetDesc& desc);
    void validateLimits() const;
    void createLayout(const char* debugName);
    void createPoolAndSet(const char* debugName);
    void destroy();

    const VulkanContext* context_ = nullptr;
    std::vector<Binding> bindings_;
    uint32_t setIndex_ = 0;
    bool usesUpdateAfterBind_ = false;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
};
