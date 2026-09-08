#pragma once

#include "Image.h"

#include <cstdint>
#include <filesystem>
#include <string>

struct TextureCompressionResult {
    bool success = false;
    std::string message;
};

class TextureCompressor {
public:
    TextureCompressionResult saveBc7Ktx2(
        const RgbaImage& image,
        const std::filesystem::path& outputPath) const;

    TextureCompressionResult saveBc7Ktx1(
        const RgbaImage& image,
        const std::filesystem::path& outputPath) const;

    TextureCompressionResult saveBc7KtxFiles(
        const RgbaImage& image,
        const std::filesystem::path& ktx2OutputPath,
        const std::filesystem::path& ktx1OutputPath) const;

    static uint32_t calcMipLevelCount(uint32_t width, uint32_t height);

private:
    static bool isValidRgbaImage(const RgbaImage& image);
    RgbaImage resizeHalf(const RgbaImage& image) const;
};
