#pragma once

#include "mesh/MeshData.h"

#include <filesystem>
#include <span>

// 运行时几何缓存：优先读取已经转换好的 MeshData；缓存缺失、过期或损坏时，
// 自动通过 Assimp 重新导入，并按需生成 LOD 后写回缓存。
class GeometryCache final {
public:
    static MeshData loadOrConvert(
        const std::filesystem::path& sourcePath,
        std::span<const float> lodRatios = {});

private:
    static std::filesystem::path cachePathFor(
        const std::filesystem::path& sourcePath,
        std::span<const float> lodRatios);
    static bool isCurrent(
        const std::filesystem::path& cachePath,
        const std::filesystem::path& sourcePath);
};
