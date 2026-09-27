#pragma once

#include <cstdint>
#include <vector>

// 与图形 API 无关的 CPU 网格数据，由模型加载器和各类 GPU mesh 共享。
struct MeshData {
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> texcoords;
    std::vector<uint32_t> indices;
    float center[3] = {};
    float radius = 1.0f;
};
