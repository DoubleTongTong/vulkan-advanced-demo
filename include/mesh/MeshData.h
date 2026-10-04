#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

// 项目统一使用一个紧凑的交错顶点格式。它既能直接作为 Vulkan vertex input，
// 也能以 8 个 float 的形式被 programmable vertex pulling 读取。
struct MeshVertex final {
    float position[3] = {};
    float normal[3] = {};
    float uv[2] = {};
};

static_assert(sizeof(MeshVertex) == sizeof(float) * 8u);

struct MeshBounds final {
    float min[3] = {};
    float max[3] = {};
    float center[3] = {};
    float radius = 1.0f;
};

constexpr uint32_t MaxMeshLods = 8;

// 描述符只保存连续数据块中的偏移，不保存指针，因此可以原样写入磁盘。
struct MeshDescriptor final {
    uint32_t lodCount = 1;
    uint32_t indexOffset = 0;
    uint32_t vertexOffset = 0;
    uint32_t vertexCount = 0;
    uint32_t materialId = 0;
    std::array<uint32_t, MaxMeshLods + 1> lodOffsets{};

    uint32_t lodIndexCount(uint32_t lod) const;
};

// 与图形 API 无关的几何资产。所有网格共享一块顶点数据和一块索引数据；
// 原始网格是 LOD 0，lodOffsets 末尾的哨兵用来推导每一级的索引数。
class MeshData final {
public:
    static MeshData createSingle(
        std::vector<MeshVertex> vertices,
        std::vector<uint32_t> indices,
        const MeshBounds& bounds,
        std::span<const uint32_t> lodOffsets = {});

    // 运行时格式只含固定宽度 POD 数据，读取后即可直接上传到 GPU。
    static MeshData load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path) const;

    size_t meshCount() const;
    const MeshDescriptor& descriptor(size_t meshIndex = 0) const;
    const MeshBounds& bounds(size_t meshIndex = 0) const;
    std::span<const MeshVertex> vertices(size_t meshIndex = 0) const;
    std::span<const uint32_t> indices(size_t meshIndex = 0, uint32_t lod = 0) const;

    std::span<const MeshVertex> vertexData() const;
    std::span<const uint32_t> indexData() const;
    std::span<const std::byte> vertexBytes() const;
    std::span<const std::byte> indexBytes() const;

private:
    void validate() const;

    std::vector<MeshDescriptor> meshes_;
    std::vector<MeshBounds> bounds_;
    std::vector<MeshVertex> vertexData_;
    std::vector<uint32_t> indexData_;
};
