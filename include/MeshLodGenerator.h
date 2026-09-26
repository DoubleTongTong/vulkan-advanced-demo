#pragma once

#include "ModelLoader.h"

#include <cstdint>
#include <span>
#include <vector>

// 一组 LOD 共用 MeshLodSet::mesh 中的顶点，只各自持有不同的三角形索引。
struct MeshLod {
    float triangleRatio = 1.0f;
    std::vector<uint32_t> indices;
};

struct MeshLodSet {
    ModelMesh mesh;
    std::vector<MeshLod> levels;
};

// 将通用 ModelMesh 重排为适合 GPU 访问的形式，并生成离散 LOD 索引。
class MeshLodGenerator {
public:
    // simplificationRatios 取值在 (0, 1)，例如 0.2 表示目标保留约 20% 三角形。
    static MeshLodSet generate(
        const ModelMesh& source,
        std::span<const float> simplificationRatios);
};
