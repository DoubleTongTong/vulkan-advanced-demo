#version 450

// 此 buffer 的布局与 C++ Vertex 完全一致：两个 vec4，每个元素固定 32 字节。
struct Vertex {
    vec4 position;
    vec4 uv;
};

layout(std430, set = 1, binding = 0) readonly buffer VertexBuffer {
    Vertex vertices[];
} kVertices;

layout(location = 0) out vec2 outUv;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    uint textureId;
} pc;

void main() {
    // vkCmdDrawIndexed 提供的 gl_VertexIndex 已经是 index buffer 解引用后的顶点编号。
    const Vertex vertex = kVertices.vertices[gl_VertexIndex];
    gl_Position = pc.mvp * vertex.position;
    outUv = vertex.uv.xy;
}
