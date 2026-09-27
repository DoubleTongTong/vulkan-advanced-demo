#pragma once

#include "rendering/descriptors/IVulkanDescriptorSet.h"
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

struct TextureRuntimeArrayDesc {
    std::string name;
    uint32_t capacity = 0;
};

struct TextureDescriptorSetDesc {
    std::vector<const VulkanShaderModule*> shaders;
    std::vector<TextureRuntimeArrayDesc> runtimeArrays;
    uint32_t set = 0;
    const char* debugName = "Texture descriptor set";
};

// 一张可复用的 sampler 与纹理描述符表。binding、descriptor 类型和 shader stage
// 均来自 SPIR-V 反射；内部用 descriptor indexing 支持纹理运行时数组。
class TextureDescriptorSet : public IVulkanDescriptorSet {
public:
    TextureDescriptorSet(const VulkanContext& context, const TextureDescriptorSetDesc& desc);
    ~TextureDescriptorSet();

    TextureDescriptorSet(const TextureDescriptorSet&) = delete;
    TextureDescriptorSet& operator=(const TextureDescriptorSet&) = delete;

    TextureDescriptorSet(TextureDescriptorSet&& other) noexcept;
    TextureDescriptorSet& operator=(TextureDescriptorSet&& other) noexcept;

    VkDescriptorSetLayout layout() const override;
    VkDescriptorSet set() const;
    uint32_t capacity(std::string_view name) const;

    void fillSamplers(std::string_view name, VkSampler sampler);
    void writeSampler(std::string_view name, uint32_t index, VkSampler sampler);
    void writeTexture2D(std::string_view name, uint32_t index, const VulkanTexture2D& texture);
    void writeTextureCube(std::string_view name, uint32_t index, const VulkanTextureCube& texture);
    void bind(
        VkCommandBuffer commandBuffer,
        VkPipelineLayout pipelineLayout,
        uint32_t setIndex = 0) const override;

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
    void reflectBindings(const TextureDescriptorSetDesc& desc);
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
