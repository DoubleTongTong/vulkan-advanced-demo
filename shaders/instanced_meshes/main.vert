#version 450

struct Vertex {
    vec4 position;
    vec4 normal;
    vec4 uv;
};

layout(std430, set = 1, binding = 1) readonly buffer MatrixBuffer {
    mat4 models[];
} kMatrices;

layout(std430, set = 1, binding = 2) readonly buffer VertexBuffer {
    Vertex vertices[];
} kVertices;

layout(push_constant) uniform PushConstants {
    mat4 viewProjection;
} pc;

layout(location = 0) out vec2 outUv;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec3 outColor;

const vec3 colors[3] = vec3[3](
    vec3(1.0, 0.25, 0.25),
    vec3(0.25, 1.0, 0.25),
    vec3(1.0));

void main() {
    // indexed draw 解引用后的 gl_VertexIndex 直接作为 SSBO 顶点下标。
    const Vertex vertex = kVertices.vertices[gl_VertexIndex];
    const mat4 model = kMatrices.models[gl_InstanceIndex];
    const float meshScale = 5.0;

    gl_Position = pc.viewProjection * model *
                  vec4(meshScale * vertex.position.xyz, 1.0);
    outUv = vertex.uv.xy;

    // model 只有旋转和平移，没有非均匀缩放，因此无需 inverse-transpose。
    outNormal = mat3(model) * vertex.normal.xyz;
    outColor = colors[gl_InstanceIndex % 3];
}
