#version 450

layout(triangles, equal_spacing, ccw) in;

layout(location = 0) in vec2 inUv[];
layout(location = 0) out vec2 outUv;

void main() {
    // gl_TessCoord 是新顶点在原始三角形中的重心坐标，三个分量之和为 1。
    gl_Position =
        gl_in[0].gl_Position * gl_TessCoord.x +
        gl_in[1].gl_Position * gl_TessCoord.y +
        gl_in[2].gl_Position * gl_TessCoord.z;
    outUv =
        inUv[0] * gl_TessCoord.x +
        inUv[1] * gl_TessCoord.y +
        inUv[2] * gl_TessCoord.z;
}
