#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class ShaderStage {
    Vertex,
    Fragment,
};

struct ShaderCompileResult {
    bool success = false;
    std::vector<uint8_t> spirv;
    uint32_t pushConstantSize = 0;
    std::string message;
};

struct ShaderReflection {
    bool success = false;
    uint32_t pushConstantSize = 0;
    std::string message;
};

class ShaderCompiler {
public:
    ShaderCompiler();
    ~ShaderCompiler();

    ShaderCompiler(const ShaderCompiler&) = delete;
    ShaderCompiler& operator=(const ShaderCompiler&) = delete;

    ShaderCompileResult compile(ShaderStage stage, const std::string& source) const;

    static ShaderStage stageFromFileName(const std::string& fileName);
    static ShaderReflection reflect(const std::vector<uint8_t>& spirv);

private:
    static std::string buildLog(const char* title, const char* infoLog, const char* debugLog);
};
