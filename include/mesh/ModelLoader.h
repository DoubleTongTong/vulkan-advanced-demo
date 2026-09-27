#pragma once

#include "mesh/MeshData.h"

#include <filesystem>

class ModelLoader {
public:
    static MeshData loadFirstMesh(const std::filesystem::path& scenePath);
};
