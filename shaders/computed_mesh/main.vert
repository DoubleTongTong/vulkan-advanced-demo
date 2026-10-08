#version 450

layout(push_constant) uniform RenderParameters {
    mat4 mvp;
    vec4 options;
} pc;

layout(location = 0) in vec4 inPosition;
layout(location = 1) in vec4 inTexCoord;
layout(location = 2) in vec4 inNormal;

layout(location = 0) out vec2 outUv;
layout(location = 1) out vec3 outNormal;

void main() {
    gl_Position = pc.mvp * inPosition;
    outUv = inTexCoord.xy;
    outNormal = inNormal.xyz;
}
