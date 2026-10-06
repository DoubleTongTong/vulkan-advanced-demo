#pragma once

#include "mesh/MeshData.h"

#include <filesystem>

class ModelLoader {
public:
    // 一个模型文件可能包含多个 aiMesh；加载器始终保留整个场景，避免调用者
    // 在“首个网格”和“完整场景”两套语义之间做选择。
    static MeshData load(const std::filesystem::path& scenePath);
};
