#include "mesh/MeshData.h"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace {

constexpr uint32_t MeshFileMagic = 0x4853454du; // ASCII: MESH
constexpr uint32_t MeshFileVersion = 1;

struct MeshFileHeader final {
    uint32_t magic = MeshFileMagic;
    uint32_t version = MeshFileVersion;
    uint32_t meshCount = 0;
    uint32_t indexDataSize = 0;
    uint32_t vertexDataSize = 0;
};

static_assert(std::is_trivially_copyable_v<MeshFileHeader>);
static_assert(std::is_trivially_copyable_v<MeshDescriptor>);
static_assert(std::is_trivially_copyable_v<MeshBounds>);
static_assert(std::is_trivially_copyable_v<MeshVertex>);

struct FileCloser {
    void operator()(std::FILE* file) const {
        if (file) {
            std::fclose(file);
        }
    }
};

using File = std::unique_ptr<std::FILE, FileCloser>;

File openFile(const std::filesystem::path& path, const wchar_t* mode) {
    std::FILE* rawFile = nullptr;
    if (_wfopen_s(&rawFile, path.c_str(), mode) != 0 || !rawFile) {
        throw std::runtime_error("Cannot open mesh file: " + path.string());
    }
    return File(rawFile);
}

void readExact(std::FILE* file, void* destination, size_t byteCount, const char* section) {
    if (byteCount != 0 && std::fread(destination, 1, byteCount, file) != byteCount) {
        throw std::runtime_error(std::string("Mesh file is truncated while reading ") + section + ".");
    }
}

void writeExact(std::FILE* file, const void* source, size_t byteCount, const char* section) {
    if (byteCount != 0 && std::fwrite(source, 1, byteCount, file) != byteCount) {
        throw std::runtime_error(std::string("Failed to write mesh ") + section + ".");
    }
}

uint32_t checkedU32(size_t value, const char* name) {
    if (value > std::numeric_limits<uint32_t>::max()) {
        throw std::overflow_error(std::string(name) + " exceeds the 32-bit mesh format limit.");
    }
    return static_cast<uint32_t>(value);
}

} // namespace

uint32_t MeshDescriptor::lodIndexCount(uint32_t lod) const {
    return lod < lodCount ? lodOffsets[lod + 1] - lodOffsets[lod] : 0;
}

MeshData MeshData::createSingle(
    std::vector<MeshVertex> vertices,
    std::vector<uint32_t> indices,
    const MeshBounds& bounds,
    std::span<const uint32_t> lodOffsets) {
    MeshData result;
    result.vertexData_ = std::move(vertices);
    result.indexData_ = std::move(indices);
    result.bounds_.push_back(bounds);

    MeshDescriptor descriptor;
    descriptor.vertexCount = checkedU32(result.vertexData_.size(), "Vertex count");
    if (lodOffsets.empty()) {
        descriptor.lodOffsets[1] = checkedU32(result.indexData_.size(), "Index count");
    } else {
        if (lodOffsets.size() < 2 || lodOffsets.size() > MaxMeshLods + 1) {
            throw std::invalid_argument("LOD offsets must contain 2 to MaxMeshLods + 1 entries.");
        }
        descriptor.lodCount = static_cast<uint32_t>(lodOffsets.size() - 1);
        std::copy(lodOffsets.begin(), lodOffsets.end(), descriptor.lodOffsets.begin());
    }
    result.meshes_.push_back(descriptor);
    result.validate();
    return result;
}

MeshDataBuilder::MeshDataBuilder(
    size_t meshCapacity,
    size_t vertexCapacity,
    size_t indexCapacity) {
    data_.meshes_.reserve(meshCapacity);
    data_.bounds_.reserve(meshCapacity);
    data_.vertexData_.reserve(vertexCapacity);
    data_.indexData_.reserve(indexCapacity);
}

MeshWriteView MeshDataBuilder::appendMesh(
    uint32_t vertexCount,
    uint32_t indexCount,
    uint32_t materialId) {
    if (built_) {
        throw std::logic_error("Cannot append to a completed mesh data builder.");
    }
    if (vertexCount == 0 || indexCount == 0 || indexCount % 3u != 0u) {
        throw std::invalid_argument("A mesh must contain vertices and triangle indices.");
    }

    const uint32_t vertexOffset = checkedU32(data_.vertexData_.size(), "Vertex offset");
    const uint32_t indexOffset = checkedU32(data_.indexData_.size(), "Index offset");
    checkedU32(data_.vertexData_.size() + vertexCount, "Total vertex count");
    checkedU32(data_.indexData_.size() + indexCount, "Total index count");

    MeshDescriptor descriptor{
        .indexOffset = indexOffset,
        .vertexOffset = vertexOffset,
        .vertexCount = vertexCount,
        .materialId = materialId,
    };
    descriptor.lodOffsets[1] = indexCount;
    data_.meshes_.push_back(descriptor);
    data_.bounds_.emplace_back();
    data_.vertexData_.resize(data_.vertexData_.size() + vertexCount);
    data_.indexData_.resize(data_.indexData_.size() + indexCount);

    return {
        .meshIndex = data_.meshes_.size() - 1,
        .vertices = std::span(data_.vertexData_).subspan(vertexOffset, vertexCount),
        .indices = std::span(data_.indexData_).subspan(indexOffset, indexCount),
    };
}

void MeshDataBuilder::setBounds(size_t meshIndex, const MeshBounds& bounds) {
    if (built_) {
        throw std::logic_error("Cannot modify a completed mesh data builder.");
    }
    data_.bounds_.at(meshIndex) = bounds;
}

MeshData MeshDataBuilder::build() {
    if (built_) {
        throw std::logic_error("Mesh data builder can only be completed once.");
    }
    data_.validate();
    built_ = true;
    return std::move(data_);
}

MeshData MeshData::load(const std::filesystem::path& path) {
    const File file = openFile(path, L"rb");
    MeshFileHeader header;
    readExact(file.get(), &header, sizeof(header), "header");
    if (header.magic != MeshFileMagic || header.version != MeshFileVersion) {
        throw std::runtime_error("Unsupported or invalid mesh file: " + path.string());
    }
    if (header.vertexDataSize % sizeof(MeshVertex) != 0 ||
        header.indexDataSize % sizeof(uint32_t) != 0) {
        throw std::runtime_error("Mesh file data blocks have invalid alignment: " + path.string());
    }
    const uintmax_t expectedSize =
        sizeof(MeshFileHeader) +
        static_cast<uintmax_t>(header.meshCount) * (sizeof(MeshDescriptor) + sizeof(MeshBounds)) +
        header.indexDataSize + header.vertexDataSize;
    if (std::filesystem::file_size(path) != expectedSize) {
        throw std::runtime_error("Mesh file size does not match its header: " + path.string());
    }

    MeshData result;
    result.meshes_.resize(header.meshCount);
    result.bounds_.resize(header.meshCount);
    result.vertexData_.resize(header.vertexDataSize / sizeof(MeshVertex));
    result.indexData_.resize(header.indexDataSize / sizeof(uint32_t));
    readExact(file.get(), result.meshes_.data(), result.meshes_.size() * sizeof(MeshDescriptor), "descriptors");
    readExact(file.get(), result.bounds_.data(), result.bounds_.size() * sizeof(MeshBounds), "bounds");
    readExact(file.get(), result.indexData_.data(), header.indexDataSize, "indices");
    readExact(file.get(), result.vertexData_.data(), header.vertexDataSize, "vertices");
    result.validate();
    return result;
}

void MeshData::save(const std::filesystem::path& path) const {
    validate();
    const MeshFileHeader header{
        .meshCount = checkedU32(meshes_.size(), "Mesh count"),
        .indexDataSize = checkedU32(indexData_.size() * sizeof(uint32_t), "Index data size"),
        .vertexDataSize = checkedU32(vertexData_.size() * sizeof(MeshVertex), "Vertex data size"),
    };
    const File file = openFile(path, L"wb");
    writeExact(file.get(), &header, sizeof(header), "header");
    writeExact(file.get(), meshes_.data(), meshes_.size() * sizeof(MeshDescriptor), "descriptors");
    writeExact(file.get(), bounds_.data(), bounds_.size() * sizeof(MeshBounds), "bounds");
    writeExact(file.get(), indexData_.data(), header.indexDataSize, "indices");
    writeExact(file.get(), vertexData_.data(), header.vertexDataSize, "vertices");
}

size_t MeshData::meshCount() const { return meshes_.size(); }

const MeshDescriptor& MeshData::descriptor(size_t meshIndex) const { return meshes_.at(meshIndex); }

const MeshBounds& MeshData::bounds(size_t meshIndex) const { return bounds_.at(meshIndex); }

std::span<const MeshVertex> MeshData::vertices(size_t meshIndex) const {
    const MeshDescriptor& mesh = descriptor(meshIndex);
    return std::span(vertexData_).subspan(mesh.vertexOffset, mesh.vertexCount);
}

std::span<const uint32_t> MeshData::indices(size_t meshIndex, uint32_t lod) const {
    const MeshDescriptor& mesh = descriptor(meshIndex);
    if (lod >= mesh.lodCount) {
        throw std::out_of_range("Mesh LOD index is out of range.");
    }
    const size_t first = static_cast<size_t>(mesh.indexOffset) + mesh.lodOffsets[lod];
    return std::span(indexData_).subspan(first, mesh.lodIndexCount(lod));
}

std::span<const MeshVertex> MeshData::vertexData() const { return vertexData_; }
std::span<const uint32_t> MeshData::indexData() const { return indexData_; }
std::span<const std::byte> MeshData::vertexBytes() const { return std::as_bytes(vertexData()); }
std::span<const std::byte> MeshData::indexBytes() const { return std::as_bytes(indexData()); }

void MeshData::validate() const {
    if (meshes_.empty() || meshes_.size() != bounds_.size()) {
        throw std::invalid_argument("Mesh data requires one bounds record per descriptor.");
    }
    for (size_t meshIndex = 0; meshIndex < meshes_.size(); ++meshIndex) {
        const MeshDescriptor& mesh = meshes_[meshIndex];
        if (mesh.lodCount == 0 || mesh.lodCount > MaxMeshLods || mesh.vertexCount == 0 ||
            bounds_[meshIndex].radius <= 0.0f ||
            static_cast<size_t>(mesh.vertexOffset) + mesh.vertexCount > vertexData_.size()) {
            throw std::invalid_argument("Mesh descriptor contains invalid vertex data or bounds.");
        }
        if (mesh.lodOffsets[0] != 0) {
            throw std::invalid_argument("The first mesh LOD offset must be zero.");
        }
        for (uint32_t lod = 0; lod < mesh.lodCount; ++lod) {
            if (mesh.lodOffsets[lod] > mesh.lodOffsets[lod + 1] ||
                mesh.lodIndexCount(lod) == 0 || mesh.lodIndexCount(lod) % 3u != 0u) {
                throw std::invalid_argument("Mesh LOD offsets do not describe triangle index ranges.");
            }
        }
        const size_t last = static_cast<size_t>(mesh.indexOffset) + mesh.lodOffsets[mesh.lodCount];
        if (last > indexData_.size()) {
            throw std::invalid_argument("Mesh descriptor index range exceeds the index data block.");
        }
        for (uint32_t lod = 0; lod < mesh.lodCount; ++lod) {
            for (uint32_t index : indices(meshIndex, lod)) {
                if (index >= mesh.vertexCount) {
                    throw std::invalid_argument("Mesh index exceeds its vertex range.");
                }
            }
        }
    }
}
