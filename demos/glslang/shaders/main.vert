#version 450

layout(location = 0) out vec3 outColor;

layout(push_constant) uniform PushConstants {
    vec2 offset;
    float scale;
} pc;

vec2 positions[3] = vec2[](
    vec2(0.0, -0.5),
    vec2(0.5, 0.5),
    vec2(-0.5, 0.5)
);

vec3 colors[3] = vec3[](
    vec3(1.0, 0.2, 0.2),
    vec3(0.2, 1.0, 0.2),
    vec3(0.2, 0.4, 1.0)
);

void main() {
    gl_Position = vec4(positions[gl_VertexIndex] * pc.scale + pc.offset, 0.0, 1.0);
    outColor = colors[gl_VertexIndex];
}
