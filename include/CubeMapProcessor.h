#pragma once

#include "Image.h"

class CubeMapProcessor {
public:
    // 将 2:1 经纬图反向采样成 Vulkan 顺序的六个正方形面。
    CubeMapImage fromEquirectangular(const RgbaFloatImage& source) const;
};
