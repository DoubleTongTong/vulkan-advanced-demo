#include "MeshLodGenerator.h"

#include <meshoptimizer.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>

namespace {

struct Vertex {
    float position[3] = {};
    float normal[3] = {};
    float uv[2] = {};
};

std::vector<Vertex> makeVertices(const MeshData& mesh) {
    const size_t vertexCount = mesh.positions.size() / 3u;
    if (vertexCount == 0 || mesh.positions.size() != vertexCount * 3u ||
        mesh.normals.size() != vertexCount * 3u ||
        mesh.texcoords.size() != vertexCount * 2u) {
        throw std::invalid_argument("Model mesh vertex attributes do not have matching sizes.");
    }

    std::vector<Vertex> vertices(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i) {
        std::copy_n(mesh.positions.data() + i * 3u, 3, vertices[i].position);
        std::copy_n(mesh.normals.data() + i * 3u, 3, vertices[i].normal);
        std::copy_n(mesh.texcoords.data() + i * 2u, 2, vertices[i].uv);
    }
    return vertices;
}

MeshData makeMeshData(const std::vector<Vertex>& vertices, const MeshData& source) {
    MeshData result;
    result.positions.resize(vertices.size() * 3u);
    result.normals.resize(vertices.size() * 3u);
    result.texcoords.resize(vertices.size() * 2u);
    for (size_t i = 0; i < vertices.size(); ++i) {
        std::copy_n(vertices[i].position, 3, result.positions.data() + i * 3u);
        std::copy_n(vertices[i].normal, 3, result.normals.data() + i * 3u);
        std::copy_n(vertices[i].uv, 2, result.texcoords.data() + i * 2u);
    }
    std::copy(std::begin(source.center), std::end(source.center), std::begin(result.center));
    result.radius = source.radius;
    return result;
}

void validateIndices(const MeshData& mesh) {
    if (mesh.indices.empty() || mesh.indices.size() % 3u != 0u) {
        throw std::invalid_argument("Model mesh indices must describe non-empty triangles.");
    }

    const size_t vertexCount = mesh.positions.size() / 3u;
    if (std::any_of(mesh.indices.begin(), mesh.indices.end(), [vertexCount](uint32_t index) {
            return index >= vertexCount;
        })) {
        throw std::invalid_argument("Model mesh index is outside the vertex range.");
    }
}

} // namespace

MeshLodSet MeshLodGenerator::generate(
    const MeshData& source,
    std::span<const float> simplificationRatios) {
    validateIndices(source);
    const std::vector<Vertex> inputVertices = makeVertices(source);

    // 重映射依据完整顶点属性的二进制等价性，避免合并 UV 或法线不同的顶点。
    std::vector<uint32_t> remap(source.indices.size());
    const size_t uniqueVertexCount = meshopt_generateVertexRemap(
        remap.data(),
        source.indices.data(), source.indices.size(),
        inputVertices.data(), inputVertices.size(), sizeof(Vertex));

    std::vector<uint32_t> optimizedIndices(source.indices.size());
    std::vector<Vertex> optimizedVertices(uniqueVertexCount);
    meshopt_remapIndexBuffer(
        optimizedIndices.data(), source.indices.data(), source.indices.size(), remap.data());
    meshopt_remapVertexBuffer(
        optimizedVertices.data(), inputVertices.data(), inputVertices.size(), sizeof(Vertex), remap.data());

    // 依次优化变换结果缓存、深度测试前的三角形顺序和顶点抓取局部性。
    meshopt_optimizeVertexCache(
        optimizedIndices.data(), optimizedIndices.data(), optimizedIndices.size(), uniqueVertexCount);
    meshopt_optimizeOverdraw(
        optimizedIndices.data(), optimizedIndices.data(), optimizedIndices.size(),
        optimizedVertices[0].position, uniqueVertexCount, sizeof(Vertex), 1.05f);
    const size_t fetchedVertexCount = meshopt_optimizeVertexFetch(
        optimizedVertices.data(), optimizedIndices.data(), optimizedIndices.size(),
        optimizedVertices.data(), uniqueVertexCount, sizeof(Vertex));
    optimizedVertices.resize(fetchedVertexCount);

    MeshLodSet result;
    result.mesh = makeMeshData(optimizedVertices, source);
    result.levels.push_back({.triangleRatio = 1.0f, .indices = optimizedIndices});

    for (const float ratio : simplificationRatios) {
        if (ratio <= 0.0f || ratio >= 1.0f) {
            throw std::invalid_argument("Mesh LOD simplification ratios must be between zero and one.");
        }

        size_t targetIndexCount = static_cast<size_t>(optimizedIndices.size() * ratio);
        targetIndexCount = std::max<size_t>(3u, targetIndexCount - targetIndexCount % 3u);
        std::vector<uint32_t> lodIndices(optimizedIndices.size());
        const size_t lodIndexCount = meshopt_simplify(
            lodIndices.data(), optimizedIndices.data(), optimizedIndices.size(),
            optimizedVertices[0].position, optimizedVertices.size(), sizeof(Vertex),
            targetIndexCount, 0.01f);
        lodIndices.resize(lodIndexCount);
        result.levels.push_back({.triangleRatio = ratio, .indices = std::move(lodIndices)});
    }

    return result;
}
