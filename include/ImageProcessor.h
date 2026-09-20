#pragma once

#include "Image.h"

#include <cstdint>
#include <filesystem>

class ImageProcessor {
public:
    // 统一把常见图片格式转成 RGBA8，后续流程就不需要关心原图通道数。
    RgbaImage loadRgba8(const std::filesystem::path& path) const;

    // HDR 等浮点图片保持线性 RGBA32F，避免加载时丢失高亮范围。
    RgbaFloatImage loadRgba32Float(const std::filesystem::path& path) const;

    // 将线性浮点图片写成可查看的 Radiance HDR 调试文件；HDR 不保存 Alpha 通道。
    void saveHdr(
        const std::filesystem::path& path,
        const RgbaFloatImage& image) const;

    // 使用 stb 的 sRGB 重采样缩放 RGBA8 图片，适合生成 mip 链。
    RgbaImage resizeRgba8(const RgbaImage& image, uint32_t width, uint32_t height) const;
};
