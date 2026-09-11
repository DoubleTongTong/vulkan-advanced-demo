#pragma once

#include "ShaderCompiler.h"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

class VulkanContext;

class VulkanShaderModule {
public:
    static VulkanShaderModule fromFile(const VulkanContext& context, const std::filesystem::path& filePath);
    static VulkanShaderModule fromGlsl(
        const VulkanContext& context,
        ShaderStage stage,
        const std::string& source,
        const char* debugName = nullptr);
    static VulkanShaderModule fromSpirv(
        const VulkanContext& context,
        ShaderStage stage,
        const void* data,
        size_t byteSize,
        const char* debugName = nullptr);

    ~VulkanShaderModule();

    VulkanShaderModule(const VulkanShaderModule&) = delete;
    VulkanShaderModule& operator=(const VulkanShaderModule&) = delete;

    VulkanShaderModule(VulkanShaderModule&& other) noexcept;
    VulkanShaderModule& operator=(VulkanShaderModule&& other) noexcept;

    VkShaderModule handle() const;
    ShaderStage stage() const;
    VkShaderStageFlagBits vkStage() const;
    uint32_t pushConstantSize() const;

private:
    VulkanShaderModule(const VulkanContext& context, ShaderStage stage, const char* debugName);

    static std::string readTextFile(const std::filesystem::path& filePath);
    static std::vector<uint8_t> readBinaryFile(const std::filesystem::path& filePath);
    static VkShaderStageFlagBits toVkStage(ShaderStage stage);

    void createFromSpirv(const void* data, size_t byteSize, const char* debugName);
    void destroy();

    const VulkanContext* context_ = nullptr;
    VkShaderModule handle_ = VK_NULL_HANDLE;
    ShaderStage stage_ = ShaderStage::Fragment;
    uint32_t pushConstantSize_ = 0;
};
