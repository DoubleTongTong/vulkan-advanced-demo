#pragma once

#include "ShaderCompiler.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class VulkanContext;
class VulkanShaderModule;
class VulkanTexture2D;
class VulkanTextureCube;

struct BindlessRuntimeArrayDesc {
    std::string name;
    uint32_t capacity = 0;
};

struct BindlessDescriptorSetDesc {
    std::vector<const VulkanShaderModule*> shaders;
    std::vector<BindlessRuntimeArrayDesc> runtimeArrays;
    uint32_t set = 0;
    const char* debugName = "Bindless descriptor set";
};

// binding、descriptor 类型和 shader stage 均来自 SPIR-V 反射。
// 调用方只需为 shader 中的 [] runtime array 指定实际容量。
class VulkanBindlessDescriptorSet {
public:
    VulkanBindlessDescriptorSet(const VulkanContext& context, const BindlessDescriptorSetDesc& desc);
    ~VulkanBindlessDescriptorSet();

    VulkanBindlessDescriptorSet(const VulkanBindlessDescriptorSet&) = delete;
    VulkanBindlessDescriptorSet& operator=(const VulkanBindlessDescriptorSet&) = delete;

    VulkanBindlessDescriptorSet(VulkanBindlessDescriptorSet&& other) noexcept;
    VulkanBindlessDescriptorSet& operator=(VulkanBindlessDescriptorSet&& other) noexcept;

    VkDescriptorSetLayout layout() const;
    VkDescriptorSet set() const;
    uint32_t capacity(std::string_view name) const;

    void fillSamplers(std::string_view name, VkSampler sampler);
    void writeSampler(std::string_view name, uint32_t index, VkSampler sampler);
    void writeTexture2D(std::string_view name, uint32_t index, const VulkanTexture2D& texture);
    void writeTextureCube(std::string_view name, uint32_t index, const VulkanTextureCube& texture);
    void bind(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, uint32_t setIndex = 0) const;

private:
    struct Binding {
        std::string name;
        uint32_t binding = 0;
        VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_MAX_ENUM;
        ShaderImageDimension imageDimension = ShaderImageDimension::None;
        uint32_t descriptorCount = 0;
        VkShaderStageFlags stageFlags = 0;
        bool runtimeArray = false;
    };

    const Binding& binding(
        std::string_view name,
        VkDescriptorType expectedType,
        ShaderImageDimension expectedDimension = ShaderImageDimension::None) const;
    void writeSampledImage(
        std::string_view name,
        uint32_t index,
        VkImageView view,
        VkImageLayout layout,
        ShaderImageDimension dimension);
    void reflectBindings(const BindlessDescriptorSetDesc& desc);
    void validateLimits() const;
    void createLayout(const char* debugName);
    void createPoolAndSet(const char* debugName);
    void destroy();

    const VulkanContext* context_ = nullptr;
    std::vector<Binding> bindings_;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
};
