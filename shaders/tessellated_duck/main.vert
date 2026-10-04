#version 450

struct Vertex {
    vec4 position;
    vec4 uv;
};

layout(std430, set = 1, binding = 0) readonly buffer FrameBuffer {
    mat4 model;
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 tessellationParameters;
} kFrame;

layout(std430, set = 1, binding = 1) readonly buffer VertexBuffer {
    Vertex vertices[];
} kVertices;

layout(location = 0) out vec2 outUv;
layout(location = 1) out vec3 outWorldPosition;

void main() {
    // 索引缓冲解引用后的 gl_VertexIndex 直接用于 PVP 顶点拉取。
    const Vertex vertex = kVertices.vertices[gl_VertexIndex];
    const vec4 worldPosition = kFrame.model * vertex.position;

    gl_Position = kFrame.viewProjection * worldPosition;
    outUv = vertex.uv.xy;
    outWorldPosition = worldPosition.xyz;
}
