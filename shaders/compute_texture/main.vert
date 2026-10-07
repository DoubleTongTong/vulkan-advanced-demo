#version 450

layout(location = 0) out vec2 outUV;

void main() {
    // 一个超大三角形覆盖整个屏幕，避免四边形中间的重复边和顶点缓冲。
    const vec2 positions[3] = vec2[3](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0));

    const vec2 position = positions[gl_VertexIndex];
    gl_Position = vec4(position, 0.0, 1.0);
    outUV = position * 0.5 + 0.5;
}
