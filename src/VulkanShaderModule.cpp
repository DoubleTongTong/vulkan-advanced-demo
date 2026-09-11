#include "VulkanShaderModule.h"

#include "VulkanContext.h"
#include "VulkanUtils.h"

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

VulkanShaderModule VulkanShaderModule::fromFile(
    const VulkanContext& context,
    const std::filesystem::path& filePath) {
    const ShaderStage stage = ShaderCompiler::stageFromFileName(filePath.string());
    const std::string debugName = "Shader module: " + filePath.filename().string();

    if (filePath.extension() == ".spv") {
        const std::vector<uint8_t> spirv = readBinaryFile(filePath);
        return fromSpirv(context, stage, spirv.data(), spirv.size(), debugName.c_str());
    }

    return fromGlsl(context, stage, readTextFile(filePath), debugName.c_str());
}

VulkanShaderModule VulkanShaderModule::fromGlsl(
    const VulkanContext& context,
    ShaderStage stage,
    const std::string& source,
    const char* debugName) {
    ShaderCompiler compiler;
    const ShaderCompileResult compiled = compiler.compile(stage, source);
    if (!compiled.success) {
        throw std::runtime_error(compiled.message);
    }

    VulkanShaderModule module(context, stage, debugName);
    module.createFromSpirv(compiled.spirv.data(), compiled.spirv.size(), debugName);
    module.pushConstantSize_ = compiled.pushConstantSize;
    return module;
}

VulkanShaderModule VulkanShaderModule::fromSpirv(
    const VulkanContext& context,
    ShaderStage stage,
    const void* data,
    size_t byteSize,
    const char* debugName) {
    VulkanShaderModule module(context, stage, debugName);
    module.createFromSpirv(data, byteSize, debugName);
    return module;
}

VulkanShaderModule::~VulkanShaderModule() {
    destroy();
}

VulkanShaderModule::VulkanShaderModule(VulkanShaderModule&& other) noexcept {
    *this = std::move(other);
}

VulkanShaderModule& VulkanShaderModule::operator=(VulkanShaderModule&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    destroy();

    context_ = other.context_;
    handle_ = other.handle_;
    stage_ = other.stage_;
    pushConstantSize_ = other.pushConstantSize_;

    other.context_ = nullptr;
    other.handle_ = VK_NULL_HANDLE;
    other.pushConstantSize_ = 0;

    return *this;
}

VkShaderModule VulkanShaderModule::handle() const {
    return handle_;
}

ShaderStage VulkanShaderModule::stage() const {
    return stage_;
}

VkShaderStageFlagBits VulkanShaderModule::vkStage() const {
    return toVkStage(stage_);
}

uint32_t VulkanShaderModule::pushConstantSize() const {
    return pushConstantSize_;
}

VulkanShaderModule::VulkanShaderModule(
    const VulkanContext& context,
    ShaderStage stage,
    const char* debugName)
    : context_(&context), stage_(stage) {
    (void)debugName;
}

std::string VulkanShaderModule::readTextFile(const std::filesystem::path& filePath) {
    std::ifstream file(filePath);
    if (!file) {
        throw std::runtime_error("Failed to open shader file: " + filePath.string());
    }

    return {
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>(),
    };
}

std::vector<uint8_t> VulkanShaderModule::readBinaryFile(const std::filesystem::path& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open SPIR-V file: " + filePath.string());
    }

    return {
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>(),
    };
}

VkShaderStageFlagBits VulkanShaderModule::toVkStage(ShaderStage stage) {
    switch (stage) {
    case ShaderStage::Vertex:
        return VK_SHADER_STAGE_VERTEX_BIT;
    case ShaderStage::Fragment:
        return VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    throw std::runtime_error("Unsupported shader stage.");
}

void VulkanShaderModule::createFromSpirv(const void* data, size_t byteSize, const char* debugName) {
    if (!data || byteSize == 0 || byteSize % sizeof(uint32_t) != 0) {
        throw std::runtime_error("SPIR-V data must be non-empty and aligned to 32-bit words.");
    }

    const VkShaderModuleCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = byteSize,
        .pCode = static_cast<const uint32_t*>(data),
    };

    vulkan_utils::checkVk(
        vkCreateShaderModule(context_->device(), &createInfo, nullptr, &handle_),
        "vkCreateShaderModule");
    context_->setDebugObjectName(VK_OBJECT_TYPE_SHADER_MODULE, reinterpret_cast<uint64_t>(handle_), debugName);

    const std::vector<uint8_t> spirvBytes(
        static_cast<const uint8_t*>(data),
        static_cast<const uint8_t*>(data) + byteSize);
    const ShaderReflection reflection = ShaderCompiler::reflect(spirvBytes);
    if (!reflection.success) {
        destroy();
        throw std::runtime_error(reflection.message);
    }

    pushConstantSize_ = reflection.pushConstantSize;
}

void VulkanShaderModule::destroy() {
    if (context_ && handle_) {
        vkDestroyShaderModule(context_->device(), handle_, nullptr);
        handle_ = VK_NULL_HANDLE;
    }
}
