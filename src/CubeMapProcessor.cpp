#include "CubeMapProcessor.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace {

glm::vec3 faceDirection(uint32_t face, float u, float v) {
    switch (face) {
    case 0: return glm::normalize(glm::vec3(1.0f, -v, -u));  // +X
    case 1: return glm::normalize(glm::vec3(-1.0f, -v, u));  // -X
    case 2: return glm::normalize(glm::vec3(u, 1.0f, v));    // +Y
    case 3: return glm::normalize(glm::vec3(u, -1.0f, -v));  // -Y
    case 4: return glm::normalize(glm::vec3(u, -v, 1.0f));   // +Z
    default: return glm::normalize(glm::vec3(-u, -v, -1.0f)); // -Z
    }
}

glm::vec4 texel(const RgbaFloatImage& image, uint32_t x, uint32_t y) {
    const size_t offset = (static_cast<size_t>(y) * image.width + x) * 4u;
    return glm::vec4(
        image.pixels[offset], image.pixels[offset + 1u],
        image.pixels[offset + 2u], image.pixels[offset + 3u]);
}

glm::vec4 sampleBilinear(const RgbaFloatImage& image, float x, float y) {
    // 经度首尾相接，纬度只需夹到南北极。
    const int width = static_cast<int>(image.width);
    const int height = static_cast<int>(image.height);
    const float clampedY = std::clamp(y, 0.0f, static_cast<float>(height - 1));
    const int x0Raw = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(clampedY));
    const int x0 = (x0Raw % width + width) % width;
    const int x1 = (x0 + 1) % width;
    const int y1 = std::min(y0 + 1, height - 1);
    const float tx = x - std::floor(x);
    const float ty = clampedY - std::floor(clampedY);
    return glm::mix(
        glm::mix(texel(image, x0, y0), texel(image, x1, y0), tx),
        glm::mix(texel(image, x0, y1), texel(image, x1, y1), tx),
        ty);
}

} // namespace

CubeMapImage CubeMapProcessor::fromEquirectangular(const RgbaFloatImage& source) const {
    if (source.width == 0 || source.height == 0 ||
        source.pixels.size() != static_cast<size_t>(source.width) * source.height * 4u) {
        throw std::runtime_error("Cannot build a cube map from an empty HDR image.");
    }
    if (source.width != source.height * 2u) {
        throw std::runtime_error("Equirectangular cube map source must use a 2:1 aspect ratio.");
    }

    CubeMapImage result;
    result.faceSize = source.width / 4u;
    result.pixels.resize(static_cast<size_t>(result.faceSize) * result.faceSize * 6u * 4u);

    constexpr float Pi = std::numbers::pi_v<float>;
    for (uint32_t face = 0; face < 6; ++face) {
        for (uint32_t y = 0; y < result.faceSize; ++y) {
            for (uint32_t x = 0; x < result.faceSize; ++x) {
                // 在像素中心取样，避免把面边界偏向某一侧。
                const float u = 2.0f * (static_cast<float>(x) + 0.5f) / result.faceSize - 1.0f;
                const float v = 2.0f * (static_cast<float>(y) + 0.5f) / result.faceSize - 1.0f;
                const glm::vec3 direction = faceDirection(face, u, v);
                const float longitude = std::atan2(direction.z, direction.x);
                const float latitude = std::asin(std::clamp(direction.y, -1.0f, 1.0f));
                const float sourceX = (longitude / (2.0f * Pi) + 0.5f) * source.width - 0.5f;
                const float sourceY = (0.5f - latitude / Pi) * source.height - 0.5f;
                const glm::vec4 color = sampleBilinear(source, sourceX, sourceY);
                const size_t offset =
                    ((static_cast<size_t>(face) * result.faceSize + y) * result.faceSize + x) * 4u;
                result.pixels[offset + 0u] = color.r;
                result.pixels[offset + 1u] = color.g;
                result.pixels[offset + 2u] = color.b;
                result.pixels[offset + 3u] = color.a;
            }
        }
    }
    return result;
}
