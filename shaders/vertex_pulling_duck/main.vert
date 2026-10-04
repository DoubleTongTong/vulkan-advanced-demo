#version 450

// 8 个标量严格对应 C++ MeshVertex 的 32 字节交错布局。
struct Vertex {
    float px; float py; float pz;
    float nx; float ny; float nz;
    float u; float v;
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
    gl_Position = pc.mvp * vec4(vertex.px, vertex.py, vertex.pz, 1.0);
    outUv = vec2(vertex.u, vertex.v);
}
