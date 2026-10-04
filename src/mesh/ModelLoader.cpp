#include "mesh/ModelLoader.h"

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

MeshData ModelLoader::loadFirstMesh(const std::filesystem::path& scenePath) {
    const std::unique_ptr<const aiScene, AssimpSceneDeleter> scene(
        aiImportFile(
            scenePath.string().c_str(),
            aiProcess_Triangulate |
                aiProcess_JoinIdenticalVertices |
                aiProcess_GenSmoothNormals |
                aiProcess_PreTransformVertices));
    if (!scene || scene->mNumMeshes == 0 || !scene->mMeshes[0]) {
        throw std::runtime_error("Failed to load model scene: " + scenePath.string());
    }

    const aiMesh* mesh = scene->mMeshes[0];
    if (!mesh->HasNormals()) {
        throw std::runtime_error("Model scene does not contain vertex normals: " + scenePath.string());
    }

    std::vector<MeshVertex> vertices(mesh->mNumVertices);
    std::vector<uint32_t> indices(static_cast<size_t>(mesh->mNumFaces) * 3u);
    glm::vec3 minBounds(std::numeric_limits<float>::max());
    glm::vec3 maxBounds(std::numeric_limits<float>::lowest());

    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        const aiVector3D& vertex = mesh->mVertices[i];
        MeshVertex& destination = vertices[i];
        destination.position[0] = vertex.x;
        destination.position[1] = vertex.y;
        destination.position[2] = vertex.z;

        const aiVector3D& normal = mesh->mNormals[i];
        destination.normal[0] = normal.x;
        destination.normal[1] = normal.y;
        destination.normal[2] = normal.z;

        if (mesh->HasTextureCoords(0)) {
            const aiVector3D& texcoord = mesh->mTextureCoords[0][i];
            destination.uv[0] = texcoord.x;
            // 图片解码以左上角为原点，只在导入边界翻转一次，后续消费者不再重复转换。
            destination.uv[1] = 1.0f - texcoord.y;
        }

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
        indices[offset + 0u] = face.mIndices[0];
        indices[offset + 1u] = face.mIndices[1];
        indices[offset + 2u] = face.mIndices[2];
    }

    if (vertices.empty() || indices.empty()) {
        throw std::runtime_error("Model scene does not contain drawable mesh data: " + scenePath.string());
    }

    const glm::vec3 center = (minBounds + maxBounds) * 0.5f;
    const glm::vec3 halfSize = (maxBounds - minBounds) * 0.5f;
    const MeshBounds bounds{
        .min = {minBounds.x, minBounds.y, minBounds.z},
        .max = {maxBounds.x, maxBounds.y, maxBounds.z},
        .center = {center.x, center.y, center.z},
        .radius = std::max({halfSize.x, halfSize.y, halfSize.z, 0.001f}),
    };
    return MeshData::createSingle(std::move(vertices), std::move(indices), bounds);
}
