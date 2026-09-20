#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec2 outUv;
layout(location = 1) out vec3 outWorldPosition;
layout(location = 2) out vec3 outWorldNormal;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 centerRadius;
    vec4 cameraAndAngle;
    uint texture2DId;
    uint cubeTextureId;
} pc;

vec3 rotateY(vec3 value, float angle) {
    float sine = sin(angle);
    float cosine = cos(angle);
    return vec3(
        cosine * value.x + sine * value.z,
        value.y,
        -sine * value.x + cosine * value.z);
}

void main() {
    vec3 localPosition = (inPosition - pc.centerRadius.xyz) / pc.centerRadius.w;
    outWorldPosition = rotateY(localPosition, pc.cameraAndAngle.w);
    outWorldNormal = normalize(rotateY(inNormal, pc.cameraAndAngle.w));
    outUv = inUv;
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
}
