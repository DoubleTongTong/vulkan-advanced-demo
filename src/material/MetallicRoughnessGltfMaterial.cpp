#include "material/MetallicRoughnessGltfMaterial.h"

#include <assimp/GltfMaterial.h>
#include <assimp/cimport.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

struct AssimpSceneDeleter {
    void operator()(const aiScene* scene) const { aiReleaseImport(scene); }
};

using AssimpScene = std::unique_ptr<const aiScene, AssimpSceneDeleter>;

std::filesystem::path requireTexture(
    const aiMaterial& material,
    const std::filesystem::path& scenePath,
    aiTextureType type,
    const char* semantic) {
    aiString relativePath;
    if (material.GetTexture(type, 0, &relativePath) != AI_SUCCESS ||
        relativePath.length == 0 || relativePath.C_Str()[0] == '*') {
        throw std::runtime_error(
            std::string("glTF metallic-roughness material has no external ") + semantic +
            " texture.");
    }
    const std::filesystem::path path = scenePath.parent_path() / relativePath.C_Str();
    if (!std::filesystem::is_regular_file(path)) {
        throw std::runtime_error("glTF texture does not exist: " + path.string());
    }
    return path;
}

} // namespace

MetallicRoughnessGltfMaterial::MetallicRoughnessGltfMaterial(
    const std::filesystem::path& scenePath) {
    AssimpScene scene(aiImportFile(scenePath.string().c_str(), aiProcess_Triangulate));
    if (!scene || scene->mNumMeshes == 0) {
        throw std::runtime_error(
            "Failed to load glTF PBR material: " + scenePath.string() + " (" +
            aiGetErrorString() + ")");
    }

    const uint32_t materialIndex = scene->mMeshes[0]->mMaterialIndex;
    for (uint32_t meshIndex = 1; meshIndex < scene->mNumMeshes; ++meshIndex) {
        if (scene->mMeshes[meshIndex]->mMaterialIndex != materialIndex) {
            throw std::runtime_error("The metallic-roughness sample expects one shared material.");
        }
    }
    if (materialIndex >= scene->mNumMaterials) {
        throw std::runtime_error("glTF mesh references an invalid material.");
    }

    const aiMaterial& material = *scene->mMaterials[materialIndex];
    aiColor4D baseColor(1.0f, 1.0f, 1.0f, 1.0f);
    material.Get(AI_MATKEY_BASE_COLOR, baseColor);
    baseColorFactor_ = {baseColor.r, baseColor.g, baseColor.b, baseColor.a};

    aiColor3D emissive(0.0f, 0.0f, 0.0f);
    material.Get(AI_MATKEY_COLOR_EMISSIVE, emissive);
    emissiveFactor_ = {emissive.r, emissive.g, emissive.b};
    material.Get(AI_MATKEY_METALLIC_FACTOR, metallicFactor_);
    material.Get(AI_MATKEY_ROUGHNESS_FACTOR, roughnessFactor_);
    material.Get(AI_MATKEY_GLTF_TEXTURE_SCALE(aiTextureType_NORMALS, 0), normalScale_);
    material.Get(AI_MATKEY_GLTF_TEXTURE_STRENGTH(aiTextureType_LIGHTMAP, 0), occlusionStrength_);
    int twoSided = 0;
    material.Get(AI_MATKEY_TWOSIDED, twoSided);
    doubleSided_ = twoSided != 0;

    baseColorTexturePath_ = requireTexture(
        material, scenePath, aiTextureType_BASE_COLOR, "base color");
    metallicRoughnessTexturePath_ = requireTexture(
        material, scenePath, aiTextureType_METALNESS, "metallic-roughness");
    normalTexturePath_ = requireTexture(
        material, scenePath, aiTextureType_NORMALS, "normal");
    occlusionTexturePath_ = requireTexture(
        material, scenePath, aiTextureType_LIGHTMAP, "occlusion");
    emissiveTexturePath_ = requireTexture(
        material, scenePath, aiTextureType_EMISSIVE, "emissive");
}

const glm::vec4& MetallicRoughnessGltfMaterial::baseColorFactor() const { return baseColorFactor_; }
const glm::vec3& MetallicRoughnessGltfMaterial::emissiveFactor() const { return emissiveFactor_; }
float MetallicRoughnessGltfMaterial::metallicFactor() const { return metallicFactor_; }
float MetallicRoughnessGltfMaterial::roughnessFactor() const { return roughnessFactor_; }
float MetallicRoughnessGltfMaterial::normalScale() const { return normalScale_; }
float MetallicRoughnessGltfMaterial::occlusionStrength() const { return occlusionStrength_; }
bool MetallicRoughnessGltfMaterial::doubleSided() const { return doubleSided_; }
const std::filesystem::path& MetallicRoughnessGltfMaterial::baseColorTexturePath() const { return baseColorTexturePath_; }
const std::filesystem::path& MetallicRoughnessGltfMaterial::metallicRoughnessTexturePath() const { return metallicRoughnessTexturePath_; }
const std::filesystem::path& MetallicRoughnessGltfMaterial::normalTexturePath() const { return normalTexturePath_; }
const std::filesystem::path& MetallicRoughnessGltfMaterial::occlusionTexturePath() const { return occlusionTexturePath_; }
const std::filesystem::path& MetallicRoughnessGltfMaterial::emissiveTexturePath() const { return emissiveTexturePath_; }
