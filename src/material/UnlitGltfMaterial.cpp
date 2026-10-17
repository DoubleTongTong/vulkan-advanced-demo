#include "material/UnlitGltfMaterial.h"

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
    void operator()(const aiScene* scene) const {
        aiReleaseImport(scene);
    }
};

using AssimpScene = std::unique_ptr<const aiScene, AssimpSceneDeleter>;

} // namespace

UnlitGltfMaterial::UnlitGltfMaterial(const std::filesystem::path& scenePath) {
    AssimpScene scene(aiImportFile(scenePath.string().c_str(), aiProcess_Triangulate));
    if (!scene || scene->mNumMeshes == 0) {
        throw std::runtime_error(
            "Failed to load glTF material: " + scenePath.string() + " (" +
            aiGetErrorString() + ")");
    }

    // 这个配方只演示一个 Unlit 材质。几何可包含多个 Mesh，但必须共享该材质。
    const uint32_t materialIndex = scene->mMeshes[0]->mMaterialIndex;
    for (uint32_t meshIndex = 1; meshIndex < scene->mNumMeshes; ++meshIndex) {
        if (scene->mMeshes[meshIndex]->mMaterialIndex != materialIndex) {
            throw std::runtime_error("The unlit sample expects one shared glTF material.");
        }
    }
    if (materialIndex >= scene->mNumMaterials) {
        throw std::runtime_error("glTF mesh references an invalid material.");
    }

    const aiMaterial& material = *scene->mMaterials[materialIndex];
    aiColor4D factor(1.0f, 1.0f, 1.0f, 1.0f);
    material.Get(AI_MATKEY_BASE_COLOR, factor);
    baseColorFactor_ = {factor.r, factor.g, factor.b, factor.a};

    aiString texturePath;
    if (material.GetTexture(aiTextureType_BASE_COLOR, 0, &texturePath) != AI_SUCCESS &&
        material.GetTexture(aiTextureType_DIFFUSE, 0, &texturePath) != AI_SUCCESS) {
        throw std::runtime_error("glTF unlit material has no base color texture.");
    }
    if (texturePath.length == 0 || texturePath.C_Str()[0] == '*') {
        throw std::runtime_error("Embedded glTF textures are not supported by this sample.");
    }
    baseColorTexturePath_ = scenePath.parent_path() / texturePath.C_Str();
}

const glm::vec4& UnlitGltfMaterial::baseColorFactor() const {
    return baseColorFactor_;
}

const std::filesystem::path& UnlitGltfMaterial::baseColorTexturePath() const {
    return baseColorTexturePath_;
}
