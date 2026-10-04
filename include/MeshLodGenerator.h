#pragma once

#include "mesh/MeshData.h"

#include <span>

// 优化统一顶点数据，并把所有 LOD 索引紧邻地写入同一个 MeshData 索引块。
class MeshLodGenerator {
public:
    // simplificationRatios 取值在 (0, 1)，例如 0.2 表示目标保留约 20% 三角形。
    static MeshData generate(
        const MeshData& source,
        std::span<const float> simplificationRatios);
};
