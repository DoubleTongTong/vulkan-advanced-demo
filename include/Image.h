#pragma once

#include <cstdint>
#include <vector>

struct RgbaImage {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels;
};
