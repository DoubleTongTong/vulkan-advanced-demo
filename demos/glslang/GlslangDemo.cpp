#include "ShaderCompiler.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

std::string readTextFile(const std::filesystem::path& filePath) {
    std::ifstream file(filePath);
    if (!file) {
        throw std::runtime_error("Failed to open shader file: " + filePath.string());
    }

    return {
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>(),
    };
}

void writeBinaryFile(const std::filesystem::path& filePath, const std::vector<uint8_t>& data) {
    std::filesystem::create_directories(filePath.parent_path());

    std::ofstream file(filePath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to create output file: " + filePath.string());
    }

    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

void compileShaderFile(
    const ShaderCompiler& compiler,
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& outputPath) {
    // 根据 .vert / .frag 后缀判断 shader stage，后面创建 VkShaderModule 也会用到这个信息。
    const ShaderStage stage = ShaderCompiler::stageFromFileName(sourcePath.string());
    const std::string source = readTextFile(sourcePath);

    const ShaderCompileResult result = compiler.compile(stage, source);
    if (!result.success) {
        throw std::runtime_error(result.message);
    }

    writeBinaryFile(outputPath, result.spirv);
    std::cout << "Compiled " << sourcePath.filename().string()
              << " -> " << outputPath.string()
              << " (" << result.spirv.size() << " bytes, "
              << result.pushConstantSize << " push constant bytes)\n";
}

} // namespace

int main() {
    try {
        ShaderCompiler compiler;

        const std::filesystem::path shaderDir = DEMO_SHADER_DIR;
        const std::filesystem::path outputDir = "debug-output/glslang";

        compileShaderFile(compiler, shaderDir / "main.vert", outputDir / "main.vert.spv");
        compileShaderFile(compiler, shaderDir / "main.frag", outputDir / "main.frag.spv");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
