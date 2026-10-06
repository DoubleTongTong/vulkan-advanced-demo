#include "mesh/GeometryCache.h"

#include "MeshLodGenerator.h"
#include "mesh/ModelLoader.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {

#ifdef APP_MESH_CACHE_DIR
const std::filesystem::path CacheDirectory = APP_MESH_CACHE_DIR;
#else
const std::filesystem::path CacheDirectory = ".cache/meshes";
#endif

// 修改导入规则、LOD 算法参数或顶点预处理方式时递增此值。
// 它与 MeshFileVersion 分工：前者描述“怎么转换”，后者描述“怎么存储”。
// v2 起，模型导入固定转换整个场景，而不再只取第一个 Mesh。
constexpr uint32_t GeometryConversionVersion = 2;

// 使用稳定的 FNV-1a，而不是实现可以改变结果的 std::hash。
void hashBytes(uint64_t& hash, const void* data, size_t size) {
    constexpr uint64_t Prime = 1099511628211ull;
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= Prime;
    }
}

uint64_t cacheKey(
    const std::filesystem::path& sourcePath,
    std::span<const float> lodRatios) {
    uint64_t hash = 14695981039346656037ull;
    hashBytes(hash, &GeometryConversionVersion, sizeof(GeometryConversionVersion));
    const std::string normalizedPath =
        std::filesystem::absolute(sourcePath).lexically_normal().generic_string();
    hashBytes(hash, normalizedPath.data(), normalizedPath.size());
    for (float ratio : lodRatios) {
        const uint32_t bits = std::bit_cast<uint32_t>(ratio);
        hashBytes(hash, &bits, sizeof(bits));
    }
    return hash;
}

std::filesystem::file_time_type latestSourceWriteTime(
    const std::filesystem::path& sourcePath) {
    std::filesystem::file_time_type latest = std::filesystem::last_write_time(sourcePath);

    // glTF 经常引用同目录中的 .bin，OBJ 也可能引用 .mtl。检查同目录文件可以
    // 避免只更新辅助文件时继续使用旧缓存，代价只是偶尔进行一次保守重建。
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(sourcePath.parent_path())) {
        if (entry.is_regular_file()) {
            latest = std::max(latest, entry.last_write_time());
        }
    }
    return latest;
}

void replaceCacheFile(
    const MeshData& data,
    const std::filesystem::path& cachePath) {
    std::filesystem::create_directories(cachePath.parent_path());
    std::filesystem::path temporaryPath = cachePath;
    temporaryPath += ".tmp";

    try {
        data.save(temporaryPath);
        // 新文件完整写完以后才替换旧文件；写入失败不会破坏已有缓存。
        std::filesystem::remove(cachePath);
        std::filesystem::rename(temporaryPath, cachePath);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporaryPath, ignored);
        throw;
    }
}

} // namespace

MeshData GeometryCache::loadOrConvert(
    const std::filesystem::path& sourcePath,
    std::span<const float> lodRatios) {
    if (!std::filesystem::is_regular_file(sourcePath)) {
        throw std::runtime_error("Geometry source does not exist: " + sourcePath.string());
    }

    const std::filesystem::path cachePath = cachePathFor(sourcePath, lodRatios);
    if (isCurrent(cachePath, sourcePath)) {
        try {
            MeshData cached = MeshData::load(cachePath);
            std::cout << "Loaded geometry cache: " << cachePath.string() << '\n';
            return cached;
        } catch (const std::exception& error) {
            // 校验失败意味着缓存不可信，保留源文件并自动走完整转换即可。
            std::cerr << "Ignoring invalid geometry cache: " << error.what() << '\n';
        }
    }

    std::cout << "Converting geometry: " << sourcePath.string() << '\n';
    MeshData converted = ModelLoader::load(sourcePath);
    if (!lodRatios.empty()) {
        converted = MeshLodGenerator::generate(converted, lodRatios);
    }
    try {
        replaceCacheFile(converted, cachePath);
        std::cout << "Saved geometry cache: " << cachePath.string() << '\n';
    } catch (const std::exception& error) {
        // 缓存是启动优化而不是渲染前提；目录只读时仍返回已经转换好的网格。
        std::cerr << "Unable to save geometry cache: " << error.what() << '\n';
    }
    return converted;
}

std::filesystem::path GeometryCache::cachePathFor(
    const std::filesystem::path& sourcePath,
    std::span<const float> lodRatios) {
    std::ostringstream name;
    name << sourcePath.stem().string() << '-'
         << std::hex << std::setw(16) << std::setfill('0')
         << cacheKey(sourcePath, lodRatios) << ".mesh";
    return CacheDirectory / name.str();
}

bool GeometryCache::isCurrent(
    const std::filesystem::path& cachePath,
    const std::filesystem::path& sourcePath) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(cachePath, error) || error) {
        return false;
    }
    return std::filesystem::last_write_time(cachePath) >= latestSourceWriteTime(sourcePath);
}
