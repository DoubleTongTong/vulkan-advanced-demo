#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUv;

layout(location = 0) out vec2 outUv;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    uint textureId;
} pc;

void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    outUv = inUv;
}
