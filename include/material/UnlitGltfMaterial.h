#pragma once

#include <glm/vec4.hpp>

#include <filesystem>

// Unlit 示例的材质资产。这里只保存材质语义，不拥有或复制任何 Mesh 数据。
class UnlitGltfMaterial final {
public:
    explicit UnlitGltfMaterial(const std::filesystem::path& scenePath);

    const glm::vec4& baseColorFactor() const;
    const std::filesystem::path& baseColorTexturePath() const;

private:
    glm::vec4 baseColorFactor_{1.0f};
    std::filesystem::path baseColorTexturePath_;
};
