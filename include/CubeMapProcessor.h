#pragma once

#include "Image.h"

class CubeMapProcessor {
public:
    // 将 2:1 经纬图反向采样成 Vulkan 顺序的六个正方形面。
    CubeMapImage fromEquirectangular(const RgbaFloatImage& source) const;

    // 将 Vulkan 顺序的六个面重新展开成 2:1 经纬度 HDR，便于检查方向与滤波结果。
    RgbaFloatImage toEquirectangular(const CubeMapImage& source) const;
};
