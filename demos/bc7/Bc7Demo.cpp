#include "ImageProcessor.h"
#include "TextureCompressor.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

std::filesystem::path makeOutputPath(const std::filesystem::path& inputPath) {
    return std::filesystem::path("debug-output/bc7") /
           (inputPath.stem().string() + "_bc7.ktx2");
}

std::filesystem::path makeKtx1OutputPath(const std::filesystem::path& inputPath) {
    return std::filesystem::path("debug-output/bc7") /
           (inputPath.stem().string() + "_bc7.ktx");
}

} // namespace

int main() {
    try {
        const std::filesystem::path inputPath = DEMO_BC7_INPUT_TEXTURE;
        const ImageProcessor imageProcessor;
        const RgbaImage image = imageProcessor.loadRgba8(inputPath);
        const std::filesystem::path ktx2OutputPath = makeOutputPath(inputPath);
        const std::filesystem::path ktx1OutputPath = makeKtx1OutputPath(inputPath);
        const TextureCompressor compressor;

        const TextureCompressionResult result =
            compressor.saveBc7KtxFiles(image, ktx2OutputPath, ktx1OutputPath);
        if (!result.success) {
            std::cerr << result.message << '\n';
            return 1;
        }

        std::cout << "Source texture: " << inputPath.string() << '\n';
        std::cout << "Saved BC7 KTX2 texture: " << ktx2OutputPath.string() << '\n';
        std::cout << "Saved BC7 KTX1 texture: " << ktx1OutputPath.string() << '\n';
        std::cout << "Source size: " << image.width << "x" << image.height << '\n';
        std::cout << "Mip levels: " << TextureCompressor::calcMipLevelCount(image.width, image.height) << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
