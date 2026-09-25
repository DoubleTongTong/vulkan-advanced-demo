#version 450

layout(location = 0) in vec4 inPosition;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
} pc;

void main() {
    outColor = inColor;
    gl_Position = pc.mvp * inPosition;
}
