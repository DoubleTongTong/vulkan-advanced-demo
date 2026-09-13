#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

struct ModelMesh {
    std::vector<float> positions;
    std::vector<uint32_t> indices;
    float center[3] = {};
    float radius = 1.0f;
};

class ModelLoader {
public:
    static ModelMesh loadFirstMesh(const std::filesystem::path& scenePath);
};
