#include "mesh/ModelLoader.h"

#include <assimp/cimport.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <glm/vec3.hpp>

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>

namespace {

struct AssimpSceneDeleter {
    void operator()(const aiScene* scene) const {
        aiReleaseImport(scene);
    }
};

using AssimpScene = std::unique_ptr<const aiScene, AssimpSceneDeleter>;

AssimpScene importScene(const std::filesystem::path& scenePath) {
    AssimpScene scene(
        aiImportFile(
            scenePath.string().c_str(),
            aiProcess_Triangulate |
                aiProcess_JoinIdenticalVertices |
                aiProcess_GenSmoothNormals |
                aiProcess_PreTransformVertices));
    if (!scene || scene->mNumMeshes == 0) {
        const std::string reason = aiGetErrorString();
        throw std::runtime_error(
            "Failed to load model scene: " + scenePath.string() +
            (reason.empty() ? std::string{} : " (" + reason + ")"));
    }
    return scene;
}

void convertMesh(
    const aiMesh& source,
    MeshDataBuilder& builder,
    const std::filesystem::path& scenePath) {
    if (!source.HasNormals() || source.mNumVertices == 0 || source.mNumFaces == 0) {
        throw std::runtime_error("Model scene contains an incomplete mesh: " + scenePath.string());
    }

    const uint32_t indexCount = source.mNumFaces * 3u;
    MeshWriteView destination = builder.appendMesh(
        source.mNumVertices, indexCount, source.mMaterialIndex);
    glm::vec3 minBounds(std::numeric_limits<float>::max());
    glm::vec3 maxBounds(std::numeric_limits<float>::lowest());

    // 直接写入 MeshData 最终连续存储，不创建逐 Mesh 临时顶点数组。
    for (uint32_t index = 0; index < source.mNumVertices; ++index) {
        const aiVector3D& position = source.mVertices[index];
        const aiVector3D& normal = source.mNormals[index];
        MeshVertex& vertex = destination.vertices[index];
        vertex.position[0] = position.x;
        vertex.position[1] = position.y;
        vertex.position[2] = position.z;
        vertex.normal[0] = normal.x;
        vertex.normal[1] = normal.y;
        vertex.normal[2] = normal.z;

        if (source.HasTextureCoords(0)) {
            const aiVector3D& uv = source.mTextureCoords[0][index];
            vertex.uv[0] = uv.x;
            // 图片解码以左上角为原点，只在导入边界翻转一次。
            vertex.uv[1] = 1.0f - uv.y;
        }

        minBounds.x = std::min(minBounds.x, position.x);
        minBounds.y = std::min(minBounds.y, position.y);
        minBounds.z = std::min(minBounds.z, position.z);
        maxBounds.x = std::max(maxBounds.x, position.x);
        maxBounds.y = std::max(maxBounds.y, position.y);
        maxBounds.z = std::max(maxBounds.z, position.z);
    }

    for (uint32_t faceIndex = 0; faceIndex < source.mNumFaces; ++faceIndex) {
        const aiFace& face = source.mFaces[faceIndex];
        if (face.mNumIndices != 3) {
            throw std::runtime_error(
                "Model loader expects triangulated mesh faces: " + scenePath.string());
        }
        const size_t offset = static_cast<size_t>(faceIndex) * 3u;
        destination.indices[offset + 0u] = face.mIndices[0];
        destination.indices[offset + 1u] = face.mIndices[1];
        destination.indices[offset + 2u] = face.mIndices[2];
    }

    const glm::vec3 center = (minBounds + maxBounds) * 0.5f;
    const glm::vec3 halfSize = (maxBounds - minBounds) * 0.5f;
    builder.setBounds(
        destination.meshIndex,
        MeshBounds{
            .min = {minBounds.x, minBounds.y, minBounds.z},
            .max = {maxBounds.x, maxBounds.y, maxBounds.z},
            .center = {center.x, center.y, center.z},
            .radius = std::max({halfSize.x, halfSize.y, halfSize.z, 0.001f}),
        });
}

MeshData convertScene(const aiScene& scene, const std::filesystem::path& scenePath) {
    size_t totalVertices = 0;
    size_t totalIndices = 0;
    for (uint32_t index = 0; index < scene.mNumMeshes; ++index) {
        if (!scene.mMeshes[index]) {
            throw std::runtime_error("Model scene contains a null mesh: " + scenePath.string());
        }
        totalVertices += scene.mMeshes[index]->mNumVertices;
        totalIndices += static_cast<size_t>(scene.mMeshes[index]->mNumFaces) * 3u;
    }

    MeshDataBuilder builder(scene.mNumMeshes, totalVertices, totalIndices);
    for (uint32_t index = 0; index < scene.mNumMeshes; ++index) {
        convertMesh(*scene.mMeshes[index], builder, scenePath);
    }
    return builder.build();
}

} // namespace

MeshData ModelLoader::load(const std::filesystem::path& scenePath) {
    const AssimpScene scene = importScene(scenePath);
    return convertScene(*scene, scenePath);
}
