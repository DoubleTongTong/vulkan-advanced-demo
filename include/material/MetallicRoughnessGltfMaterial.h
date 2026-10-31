#pragma once

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <filesystem>

// 本章只承载 glTF metallic-roughness 的材质语义；几何继续复用统一 MeshData。
// DamagedHelmet 使用一个材质和 UV0，因此不为本示例引入材质数组或 bindless 纹理。
class MetallicRoughnessGltfMaterial final {
public:
    explicit MetallicRoughnessGltfMaterial(const std::filesystem::path& scenePath);

    const glm::vec4& baseColorFactor() const;
    const glm::vec3& emissiveFactor() const;
    float metallicFactor() const;
    float roughnessFactor() const;
    float normalScale() const;
    float occlusionStrength() const;
    bool doubleSided() const;

    const std::filesystem::path& baseColorTexturePath() const;
    const std::filesystem::path& metallicRoughnessTexturePath() const;
    const std::filesystem::path& normalTexturePath() const;
    const std::filesystem::path& occlusionTexturePath() const;
    const std::filesystem::path& emissiveTexturePath() const;

private:
    glm::vec4 baseColorFactor_{1.0f};
    glm::vec3 emissiveFactor_{0.0f};
    float metallicFactor_ = 1.0f;
    float roughnessFactor_ = 1.0f;
    float normalScale_ = 1.0f;
    float occlusionStrength_ = 1.0f;
    bool doubleSided_ = false;
    std::filesystem::path baseColorTexturePath_;
    std::filesystem::path metallicRoughnessTexturePath_;
    std::filesystem::path normalTexturePath_;
    std::filesystem::path occlusionTexturePath_;
    std::filesystem::path emissiveTexturePath_;
};
