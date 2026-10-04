#include "MeshLodGenerator.h"

#include <meshoptimizer.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>

namespace {

void validateIndices(const MeshData& mesh) {
    const std::span indices = mesh.indices();
    if (indices.empty() || indices.size() % 3u != 0u) {
        throw std::invalid_argument("Model mesh indices must describe non-empty triangles.");
    }

    const size_t vertexCount = mesh.vertices().size();
    if (std::any_of(indices.begin(), indices.end(), [vertexCount](uint32_t index) {
            return index >= vertexCount;
        })) {
        throw std::invalid_argument("Model mesh index is outside the vertex range.");
    }
}

} // namespace

MeshData MeshLodGenerator::generate(
    const MeshData& source,
    std::span<const float> simplificationRatios) {
    validateIndices(source);
    const std::span sourceVertices = source.vertices();
    const std::span sourceIndices = source.indices();

    // 重映射依据完整顶点属性的二进制等价性，避免合并 UV 或法线不同的顶点。
    std::vector<uint32_t> remap(sourceIndices.size());
    const size_t uniqueVertexCount = meshopt_generateVertexRemap(
        remap.data(),
        sourceIndices.data(), sourceIndices.size(),
        sourceVertices.data(), sourceVertices.size(), sizeof(MeshVertex));

    std::vector<uint32_t> optimizedIndices(sourceIndices.size());
    std::vector<MeshVertex> optimizedVertices(uniqueVertexCount);
    meshopt_remapIndexBuffer(
        optimizedIndices.data(), sourceIndices.data(), sourceIndices.size(), remap.data());
    meshopt_remapVertexBuffer(
        optimizedVertices.data(), sourceVertices.data(), sourceVertices.size(), sizeof(MeshVertex), remap.data());

    // 依次优化变换结果缓存、深度测试前的三角形顺序和顶点抓取局部性。
    meshopt_optimizeVertexCache(
        optimizedIndices.data(), optimizedIndices.data(), optimizedIndices.size(), uniqueVertexCount);
    meshopt_optimizeOverdraw(
        optimizedIndices.data(), optimizedIndices.data(), optimizedIndices.size(),
        optimizedVertices[0].position, uniqueVertexCount, sizeof(MeshVertex), 1.05f);
    const size_t fetchedVertexCount = meshopt_optimizeVertexFetch(
        optimizedVertices.data(), optimizedIndices.data(), optimizedIndices.size(),
        optimizedVertices.data(), uniqueVertexCount, sizeof(MeshVertex));
    optimizedVertices.resize(fetchedVertexCount);

    std::vector<uint32_t> packedIndices = optimizedIndices;
    std::vector<uint32_t> lodOffsets{0u, static_cast<uint32_t>(packedIndices.size())};

    for (const float ratio : simplificationRatios) {
        if (ratio <= 0.0f || ratio >= 1.0f) {
            throw std::invalid_argument("Mesh LOD simplification ratios must be between zero and one.");
        }

        size_t targetIndexCount = static_cast<size_t>(optimizedIndices.size() * ratio);
        targetIndexCount = std::max<size_t>(3u, targetIndexCount - targetIndexCount % 3u);
        std::vector<uint32_t> lodIndices(optimizedIndices.size());
        const size_t lodIndexCount = meshopt_simplify(
            lodIndices.data(), optimizedIndices.data(), optimizedIndices.size(),
            optimizedVertices[0].position, optimizedVertices.size(), sizeof(MeshVertex),
            targetIndexCount, 0.01f);
        lodIndices.resize(lodIndexCount);
        packedIndices.insert(packedIndices.end(), lodIndices.begin(), lodIndices.end());
        lodOffsets.push_back(static_cast<uint32_t>(packedIndices.size()));
    }

    return MeshData::createSingle(
        std::move(optimizedVertices), std::move(packedIndices), source.bounds(), lodOffsets);
}
