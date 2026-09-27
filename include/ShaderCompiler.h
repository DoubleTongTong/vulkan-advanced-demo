#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class ShaderStage {
    Vertex,
    Fragment,
};

enum class ShaderDescriptorType {
    Sampler,
    SampledImage,
    StorageBuffer,
};

enum class ShaderImageDimension {
    None,
    Image2D,
    Cube,
};

struct ShaderDescriptorBinding {
    std::string name;
    uint32_t set = 0;
    uint32_t binding = 0;
    ShaderDescriptorType type = ShaderDescriptorType::SampledImage;
    ShaderImageDimension imageDimension = ShaderImageDimension::None;
    // 0 表示 shader 声明的是 [] runtime array，实际容量由使用方决定。
    uint32_t count = 0;
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
    std::vector<ShaderDescriptorBinding> descriptorBindings;
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
