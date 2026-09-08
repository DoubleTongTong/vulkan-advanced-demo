#include "ImageProcessor.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

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
