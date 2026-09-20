#pragma once

#include <cstdint>
#include <vector>

struct RgbaImage {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels;
};

struct RgbaFloatImage {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<float> pixels;
};

// 六个面按 Vulkan cube face 顺序紧密排列：+X、-X、+Y、-Y、+Z、-Z。
struct CubeMapImage {
    uint32_t faceSize = 0;
    std::vector<float> pixels;
};
