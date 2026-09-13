#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 0) out vec3 outColor;

layout(push_constant) uniform PushConstants {
    vec2 offset;
    float scale;
} pc;

void main() {
    gl_Position = vec4(inPosition * pc.scale + pc.offset, 0.0, 1.0);
    outColor = inColor;
}
