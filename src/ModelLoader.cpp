#include "ModelLoader.h"

#include <assimp/cimport.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>

namespace {

struct AssimpSceneDeleter {
    void operator()(const aiScene* scene) const {
        aiReleaseImport(scene);
    }
};

} // namespace

ModelMesh ModelLoader::loadFirstMesh(const std::filesystem::path& scenePath) {
    const std::unique_ptr<const aiScene, AssimpSceneDeleter> scene(
        aiImportFile(
            scenePath.string().c_str(),
            aiProcess_Triangulate |
                aiProcess_JoinIdenticalVertices |
                aiProcess_PreTransformVertices));
    if (!scene || scene->mNumMeshes == 0 || !scene->mMeshes[0]) {
        throw std::runtime_error("Failed to load model scene: " + scenePath.string());
    }

    const aiMesh* mesh = scene->mMeshes[0];

    ModelMesh data;
    data.positions.resize(static_cast<size_t>(mesh->mNumVertices) * 3u);
    data.indices.resize(static_cast<size_t>(mesh->mNumFaces) * 3u);
    glm::vec3 minBounds(std::numeric_limits<float>::max());
    glm::vec3 maxBounds(std::numeric_limits<float>::lowest());

    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        const aiVector3D& vertex = mesh->mVertices[i];
        const size_t offset = static_cast<size_t>(i) * 3u;
        data.positions[offset + 0u] = vertex.x;
        data.positions[offset + 1u] = vertex.y;
        data.positions[offset + 2u] = vertex.z;

        minBounds.x = std::min(minBounds.x, vertex.x);
        minBounds.y = std::min(minBounds.y, vertex.y);
        minBounds.z = std::min(minBounds.z, vertex.z);
        maxBounds.x = std::max(maxBounds.x, vertex.x);
        maxBounds.y = std::max(maxBounds.y, vertex.y);
        maxBounds.z = std::max(maxBounds.z, vertex.z);
    }

    for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
        const aiFace& face = mesh->mFaces[i];
        if (face.mNumIndices != 3) {
            throw std::runtime_error("Model loader expects triangulated mesh faces: " + scenePath.string());
        }

        const size_t offset = static_cast<size_t>(i) * 3u;
        data.indices[offset + 0u] = face.mIndices[0];
        data.indices[offset + 1u] = face.mIndices[1];
        data.indices[offset + 2u] = face.mIndices[2];
    }

    if (data.positions.empty() || data.indices.empty()) {
        throw std::runtime_error("Model scene does not contain drawable mesh data: " + scenePath.string());
    }

    const glm::vec3 center = (minBounds + maxBounds) * 0.5f;
    const glm::vec3 halfSize = (maxBounds - minBounds) * 0.5f;
    data.center[0] = center.x;
    data.center[1] = center.y;
    data.center[2] = center.z;
    data.radius = std::max({halfSize.x, halfSize.y, halfSize.z, 0.001f});

    return data;
}
