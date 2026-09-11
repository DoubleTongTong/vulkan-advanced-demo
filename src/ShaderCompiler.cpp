#include "ShaderCompiler.h"

#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>
#include <spirv_reflect.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <stdexcept>

namespace {

glslang_stage_t toGlslangStage(ShaderStage stage) {
    switch (stage) {
    case ShaderStage::Vertex:
        return GLSLANG_STAGE_VERTEX;
    case ShaderStage::Fragment:
        return GLSLANG_STAGE_FRAGMENT;
    }

    throw std::runtime_error("Unsupported shader stage.");
}

} // namespace

ShaderCompiler::ShaderCompiler() {
    if (!glslang_initialize_process()) {
        throw std::runtime_error("Failed to initialize glslang.");
    }
}

ShaderCompiler::~ShaderCompiler() {
    glslang_finalize_process();
}

ShaderCompileResult ShaderCompiler::compile(ShaderStage stage, const std::string& source) const {
    glslang_input_t input{};
    input.language = GLSLANG_SOURCE_GLSL;
    input.stage = toGlslangStage(stage);
    input.client = GLSLANG_CLIENT_VULKAN;
    input.client_version = GLSLANG_TARGET_VULKAN_1_3;
    input.target_language = GLSLANG_TARGET_SPV;
    input.target_language_version = GLSLANG_TARGET_SPV_1_6;
    input.code = source.c_str();
    input.default_version = 450;
    input.default_profile = GLSLANG_CORE_PROFILE;
    input.force_default_version_and_profile = false;
    input.forward_compatible = false;
    input.messages = GLSLANG_MSG_DEFAULT_BIT;
    input.resource = glslang_default_resource();

    using ShaderPtr = std::unique_ptr<glslang_shader_t, decltype(&glslang_shader_delete)>;
    ShaderPtr shader(glslang_shader_create(&input), glslang_shader_delete);

    if (!shader) {
        return {.message = "Failed to create glslang shader."};
    }

    // 预处理会检查 #version、#extension、宏等 GLSL 前置内容。
    if (!glslang_shader_preprocess(shader.get(), &input)) {
        return {
            .message = buildLog(
                "Shader preprocessing failed.",
                glslang_shader_get_info_log(shader.get()),
                glslang_shader_get_info_debug_log(shader.get())),
        };
    }

    // parse 会把 GLSL 源码转换成 glslang 内部表示。
    if (!glslang_shader_parse(shader.get(), &input)) {
        return {
            .message = buildLog(
                "Shader parsing failed.",
                glslang_shader_get_info_log(shader.get()),
                glslang_shader_get_info_debug_log(shader.get())),
        };
    }

    using ProgramPtr = std::unique_ptr<glslang_program_t, decltype(&glslang_program_delete)>;
    ProgramPtr program(glslang_program_create(), glslang_program_delete);

    if (!program) {
        return {.message = "Failed to create glslang program."};
    }

    glslang_program_add_shader(program.get(), shader.get());

    if (!glslang_program_link(program.get(), GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT)) {
        return {
            .message = buildLog(
                "Shader linking failed.",
                glslang_program_get_info_log(program.get()),
                glslang_program_get_info_debug_log(program.get())),
        };
    }

    glslang_spv_options_t options{};
    options.generate_debug_info = true;
    options.strip_debug_info = false;
    options.disable_optimizer = false;
    options.optimize_size = true;
    options.disassemble = false;
    options.validate = true;
    options.emit_nonsemantic_shader_debug_info = false;
    options.emit_nonsemantic_shader_debug_source = false;

    glslang_program_SPIRV_generate_with_options(program.get(), input.stage, &options);

    const char* spirvMessages = glslang_program_SPIRV_get_messages(program.get());
    if (spirvMessages && *spirvMessages) {
        return {.message = spirvMessages};
    }

    const auto* words = glslang_program_SPIRV_get_ptr(program.get());
    const size_t wordCount = glslang_program_SPIRV_get_size(program.get());
    const auto* bytes = reinterpret_cast<const uint8_t*>(words);

    ShaderCompileResult result;
    result.success = true;
    result.spirv.assign(bytes, bytes + wordCount * sizeof(uint32_t));

    const ShaderReflection reflection = reflect(result.spirv);
    if (!reflection.success) {
        return {.message = reflection.message};
    }

    result.pushConstantSize = reflection.pushConstantSize;
    return result;
}

ShaderStage ShaderCompiler::stageFromFileName(const std::string& fileName) {
    const std::filesystem::path path(fileName);
    std::filesystem::path stagePath = path;
    if (stagePath.extension() == ".spv") {
        stagePath = stagePath.stem();
    }

    std::string extension = stagePath.extension().string();

    std::ranges::transform(extension, extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (extension == ".vert") {
        return ShaderStage::Vertex;
    }

    if (extension == ".frag") {
        return ShaderStage::Fragment;
    }

    throw std::runtime_error("Unsupported shader file extension: " + extension);
}

ShaderReflection ShaderCompiler::reflect(const std::vector<uint8_t>& spirv) {
    if (spirv.empty()) {
        return {.message = "Cannot reflect empty SPIR-V data."};
    }

    SpvReflectShaderModule module{};
    const SpvReflectResult createResult =
        spvReflectCreateShaderModule(spirv.size(), spirv.data(), &module);

    if (createResult != SPV_REFLECT_RESULT_SUCCESS) {
        return {.message = "Failed to create SPIR-V reflection module."};
    }

    struct ModuleGuard {
        SpvReflectShaderModule* module = nullptr;

        ~ModuleGuard() {
            spvReflectDestroyShaderModule(module);
        }
    } guard{&module};

    uint32_t pushConstantSize = 0;

    // push constant 可能有多个 block；Vulkan pipeline layout 需要能覆盖最大使用范围。
    for (uint32_t i = 0; i < module.push_constant_block_count; ++i) {
        const SpvReflectBlockVariable& block = module.push_constant_blocks[i];
        pushConstantSize = std::max(pushConstantSize, block.offset + block.size);
    }

    return {
        .success = true,
        .pushConstantSize = pushConstantSize,
    };
}

std::string ShaderCompiler::buildLog(const char* title, const char* infoLog, const char* debugLog) {
    std::string message = title;

    if (infoLog && *infoLog) {
        message += "\n";
        message += infoLog;
    }

    if (debugLog && *debugLog) {
        message += "\n";
        message += debugLog;
    }

    return message;
}
