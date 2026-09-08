#include "TextureCompressor.h"

#include "ImageProcessor.h"

#include <ktx.h>
#include <vkformat_enum.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <memory>

namespace {

constexpr ktx_uint32_t GlCompressedRgbaBptcUnorm = 0x8E8C;

std::string ktxErrorMessage(const char* action, KTX_error_code code) {
    std::string message = action;
    message += ": ";
    message += ktxErrorString(code);
    return message;
}

void createOutputDirectory(const std::filesystem::path& outputPath) {
    const std::filesystem::path parentPath = outputPath.parent_path();
    if (!parentPath.empty()) {
        std::filesystem::create_directories(parentPath);
    }
}

struct KtxTexture2Deleter {
    void operator()(ktxTexture2* texture) const {
        ktxTexture2_Destroy(texture);
    }
};

struct KtxTexture1Deleter {
    void operator()(ktxTexture1* texture) const {
        ktxTexture1_Destroy(texture);
    }
};

} // namespace

TextureCompressionResult TextureCompressor::saveBc7Ktx2(
    const RgbaImage& image,
    const std::filesystem::path& outputPath) const {
    return saveBc7KtxFiles(image, outputPath, {});
}

TextureCompressionResult TextureCompressor::saveBc7Ktx1(
    const RgbaImage& image,
    const std::filesystem::path& outputPath) const {
    return saveBc7KtxFiles(image, {}, outputPath);
}

TextureCompressionResult TextureCompressor::saveBc7KtxFiles(
    const RgbaImage& image,
    const std::filesystem::path& ktx2OutputPath,
    const std::filesystem::path& ktx1OutputPath) const {
    if (!isValidRgbaImage(image)) {
        return {.message = "Invalid RGBA image data."};
    }

    if (ktx2OutputPath.empty() && ktx1OutputPath.empty()) {
        return {.message = "No texture output path was provided."};
    }

    const uint32_t mipLevels = calcMipLevelCount(image.width, image.height);

    ktxTextureCreateInfo createInfo{};
    createInfo.vkFormat = VK_FORMAT_R8G8B8A8_UNORM;
    createInfo.baseWidth = image.width;
    createInfo.baseHeight = image.height;
    createInfo.baseDepth = 1;
    createInfo.numDimensions = 2;
    createInfo.numLevels = mipLevels;
    createInfo.numLayers = 1;
    createInfo.numFaces = 1;
    createInfo.isArray = KTX_FALSE;
    createInfo.generateMipmaps = KTX_FALSE;

    ktxTexture2* rawTexture = nullptr;
    KTX_error_code result =
        ktxTexture2_Create(&createInfo, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &rawTexture);

    if (result != KTX_SUCCESS) {
        return {.message = ktxErrorMessage("Failed to create KTX2 texture", result)};
    }

    using TexturePtr = std::unique_ptr<ktxTexture2, KtxTexture2Deleter>;
    TexturePtr texture(rawTexture);

    RgbaImage mip = image;

    try {
        for (uint32_t level = 0; level < mipLevels; ++level) {
            // 每一层 mip 都写入 RGBA8 数据，KTX 后面会统一压缩整条 mip 链。
            result = ktxTexture_SetImageFromMemory(
                ktxTexture(texture.get()),
                level,
                0,
                0,
                mip.pixels.data(),
                mip.pixels.size());

            if (result != KTX_SUCCESS) {
                return {.message = ktxErrorMessage("Failed to upload mip level into KTX2 texture", result)};
            }

            if (level + 1 < mipLevels) {
                mip = resizeHalf(mip);
            }
        }
    } catch (const std::exception& error) {
        return {.message = error.what()};
    }

    result = ktxTexture2_CompressBasis(texture.get(), 255);
    if (result != KTX_SUCCESS) {
        return {.message = ktxErrorMessage("Failed to compress texture with Basis", result)};
    }

    result = ktxTexture2_TranscodeBasis(texture.get(), KTX_TTF_BC7_RGBA, 0);
    if (result != KTX_SUCCESS) {
        return {.message = ktxErrorMessage("Failed to transcode Basis texture to BC7", result)};
    }

    if (!ktx2OutputPath.empty()) {
        createOutputDirectory(ktx2OutputPath);

        result = ktxTexture_WriteToNamedFile(ktxTexture(texture.get()), ktx2OutputPath.string().c_str());
        if (result != KTX_SUCCESS) {
            return {.message = ktxErrorMessage("Failed to write BC7 KTX2 texture", result)};
        }
    }

    if (!ktx1OutputPath.empty()) {
        ktxTextureCreateInfo ktx1CreateInfo{};
        ktx1CreateInfo.glInternalformat = GlCompressedRgbaBptcUnorm;
        ktx1CreateInfo.baseWidth = image.width;
        ktx1CreateInfo.baseHeight = image.height;
        ktx1CreateInfo.baseDepth = 1;
        ktx1CreateInfo.numDimensions = 2;
        ktx1CreateInfo.numLevels = mipLevels;
        ktx1CreateInfo.numLayers = 1;
        ktx1CreateInfo.numFaces = 1;
        ktx1CreateInfo.isArray = KTX_FALSE;
        ktx1CreateInfo.generateMipmaps = KTX_FALSE;

        ktxTexture1* rawKtx1Texture = nullptr;
        result = ktxTexture1_Create(&ktx1CreateInfo, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &rawKtx1Texture);
        if (result != KTX_SUCCESS) {
            return {.message = ktxErrorMessage("Failed to create KTX1 texture", result)};
        }

        using Ktx1TexturePtr = std::unique_ptr<ktxTexture1, KtxTexture1Deleter>;
        Ktx1TexturePtr ktx1Texture(rawKtx1Texture);

        for (uint32_t level = 0; level < mipLevels; ++level) {
            ktx_size_t imageOffset = 0;
            result = ktxTexture_GetImageOffset(ktxTexture(texture.get()), level, 0, 0, &imageOffset);
            if (result != KTX_SUCCESS) {
                return {.message = ktxErrorMessage("Failed to find BC7 mip data offset", result)};
            }

            const ktx_size_t imageSize = ktxTexture_GetImageSize(ktxTexture(texture.get()), level);
            result = ktxTexture_SetImageFromMemory(
                ktxTexture(ktx1Texture.get()),
                level,
                0,
                0,
                texture->pData + imageOffset,
                imageSize);

            if (result != KTX_SUCCESS) {
                return {.message = ktxErrorMessage("Failed to upload BC7 mip level into KTX1 texture", result)};
            }
        }

        createOutputDirectory(ktx1OutputPath);

        result = ktxTexture1_WriteToNamedFile(ktx1Texture.get(), ktx1OutputPath.string().c_str());
        if (result != KTX_SUCCESS) {
            return {.message = ktxErrorMessage("Failed to write BC7 KTX1 texture", result)};
        }
    }

    return {.success = true};
}

uint32_t TextureCompressor::calcMipLevelCount(uint32_t width, uint32_t height) {
    uint32_t levels = 1;

    while (width > 1 || height > 1) {
        width = std::max(width / 2, 1u);
        height = std::max(height / 2, 1u);
        ++levels;
    }

    return levels;
}

bool TextureCompressor::isValidRgbaImage(const RgbaImage& image) {
    if (image.width == 0 || image.height == 0) {
        return false;
    }

    return image.pixels.size() == static_cast<size_t>(image.width) * image.height * 4;
}

RgbaImage TextureCompressor::resizeHalf(const RgbaImage& image) const {
    const uint32_t width = std::max(image.width / 2, 1u);
    const uint32_t height = std::max(image.height / 2, 1u);
    const ImageProcessor imageProcessor;
    return imageProcessor.resizeRgba8(image, width, height);
}
