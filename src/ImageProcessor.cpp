#include "ImageProcessor.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

RgbaImage ImageProcessor::loadRgba8(const std::filesystem::path& path) const {
    int width = 0;
    int height = 0;
    int channels = 0;

    stbi_uc* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels) {
        throw std::runtime_error("Failed to load image: " + path.string());
    }

    RgbaImage image;
    image.width = static_cast<uint32_t>(width);
    image.height = static_cast<uint32_t>(height);
    image.pixels.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);

    stbi_image_free(pixels);
    return image;
}

RgbaFloatImage ImageProcessor::loadRgba32Float(const std::filesystem::path& path) const {
    int width = 0;
    int height = 0;
    int channels = 0;

    float* pixels = stbi_loadf(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels) {
        throw std::runtime_error("Failed to load HDR image: " + path.string());
    }

    RgbaFloatImage image;
    image.width = static_cast<uint32_t>(width);
    image.height = static_cast<uint32_t>(height);
    image.pixels.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);

    stbi_image_free(pixels);
    return image;
}

void ImageProcessor::saveHdr(
    const std::filesystem::path& path,
    const RgbaFloatImage& image) const {
    if (image.width == 0 || image.height == 0 ||
        image.pixels.size() != static_cast<size_t>(image.width) * image.height * 4u) {
        throw std::runtime_error("Cannot save an empty or invalid RGBA32F image.");
    }

    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }

    if (!stbi_write_hdr(
            path.string().c_str(),
            static_cast<int>(image.width),
            static_cast<int>(image.height),
            4,
            image.pixels.data())) {
        throw std::runtime_error("Failed to save HDR image: " + path.string());
    }
}

RgbaImage ImageProcessor::resizeRgba8(const RgbaImage& image, uint32_t width, uint32_t height) const {
    if (image.width == 0 || image.height == 0 || image.pixels.empty()) {
        throw std::runtime_error("Cannot resize an empty image.");
    }

    RgbaImage resized;
    resized.width = width;
    resized.height = height;
    resized.pixels.resize(static_cast<size_t>(width) * height * 4);

    const unsigned char* result = stbir_resize_uint8_srgb(
        image.pixels.data(),
        static_cast<int>(image.width),
        static_cast<int>(image.height),
        0,
        resized.pixels.data(),
        static_cast<int>(width),
        static_cast<int>(height),
        0,
        STBIR_RGBA);

    if (!result) {
        throw std::runtime_error("Failed to resize RGBA image.");
    }

    return resized;
}
