#version 450

layout(location = 0) in vec2 inWorldPosition;
layout(location = 1) in vec2 inCameraPosition;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 origin;
    vec4 gridParameters;
} pc;

const vec4 ThinLineColor = vec4(0.5, 0.5, 0.5, 1.0);
const vec4 ThickLineColor = vec4(0.0, 0.0, 0.0, 1.0);

float saturate(float value) {
    return clamp(value, 0.0, 1.0);
}

vec2 saturate(vec2 value) {
    return clamp(value, vec2(0.0), vec2(1.0));
}

float maxComponent(vec2 value) {
    return max(value.x, value.y);
}

// 计算某一级网格线在当前像素中的覆盖率，返回值即抗锯齿 Alpha。
float lineCoverage(vec2 position, vec2 derivative, float cellSize) {
    const vec2 cellPosition = mod(position, cellSize) / derivative;
    const vec2 distanceToLine =
        vec2(1.0) - abs(saturate(cellPosition) * 2.0 - vec2(1.0));
    return maxComponent(distanceToLine);
}

vec4 gridColor(vec2 position, vec2 cameraPosition) {
    const float gridExtent = pc.gridParameters.x;
    const float gridCellSize = pc.gridParameters.y;
    const float minPixelsBetweenCells = pc.gridParameters.z;

    // dFdx/dFdy 给出世界坐标跨越一个屏幕像素时的变化量。
    // 它让远处网格自动选择更粗的层级，从而抑制摩尔纹与闪烁。
    vec2 derivative = vec2(
        length(vec2(dFdx(position.x), dFdy(position.x))),
        length(vec2(dFdx(position.y), dFdy(position.y))));

    const float lod = max(
        0.0,
        log(length(derivative) * minPixelsBetweenCells / gridCellSize) /
            log(10.0) + 1.0);
    const float lodBlend = fract(lod);
    const float cellSize0 = gridCellSize * pow(10.0, floor(lod));
    const float cellSize1 = cellSize0 * 10.0;
    const float cellSize2 = cellSize1 * 10.0;

    // 最多让线覆盖约 4 个像素，再通过 Alpha 得到平滑边缘。
    derivative = max(derivative * 4.0, vec2(1e-6));
    position += derivative * 0.5;

    const float coverage0 = lineCoverage(position, derivative, cellSize0);
    const float coverage1 = lineCoverage(position, derivative, cellSize1);
    const float coverage2 = lineCoverage(position, derivative, cellSize2);

    vec4 color = coverage2 > 0.0
        ? ThickLineColor
        : coverage1 > 0.0
            ? mix(ThickLineColor, ThinLineColor, lodBlend)
            : ThinLineColor;

    const float lineAlpha = coverage2 > 0.0
        ? coverage2
        : coverage1 > 0.0
            ? coverage1
            : coverage0 * (1.0 - lodBlend);
    const float distanceFade =
        1.0 - saturate(length(position - cameraPosition) / gridExtent);
    color.a *= lineAlpha * distanceFade;
    return color;
}

void main() {
    outColor = gridColor(inWorldPosition, inCameraPosition);
}
