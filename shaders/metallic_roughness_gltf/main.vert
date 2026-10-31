#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec3 outWorldPosition;
layout(location = 1) out vec3 outWorldNormal;
layout(location = 2) out vec2 outUv;

struct PbrFrameData {
    mat4 model;
    mat4 mvp;
    vec4 cameraPosition;
    vec4 baseColorFactor;
    vec4 emissiveFactor;
    vec4 materialFactors;
};

layout(std430, set = 0, binding = 10) readonly buffer FrameData {
    PbrFrameData frames[];
} kFrameData;

layout(push_constant) uniform PushConstants {
    uint frameIndex;
} pc;

void main() {
    const PbrFrameData frame = kFrameData.frames[pc.frameIndex];
    const vec4 worldPosition = frame.model * vec4(inPosition, 1.0);
    outWorldPosition = worldPosition.xyz;
    outWorldNormal = normalize(transpose(inverse(mat3(frame.model))) * inNormal);
    outUv = inUv;
    gl_Position = frame.mvp * vec4(inPosition, 1.0);
}
