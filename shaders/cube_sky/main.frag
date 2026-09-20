#version 450

layout(set = 0, binding = 0) uniform sampler kSamplers[4];
layout(set = 0, binding = 1) uniform textureCube kTexturesCube[4];

layout(location = 0) in vec2 inNdc;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 inverseViewProjection;
    vec4 cameraPosition;
} pc;

void main() {
    vec4 worldPosition = pc.inverseViewProjection * vec4(inNdc, 1.0, 1.0);
    vec3 direction = normalize(worldPosition.xyz / worldPosition.w - pc.cameraPosition.xyz);
    vec3 hdrColor = texture(samplerCube(kTexturesCube[0], kSamplers[1]), direction).rgb;

    // HDR 环境先做简单曝光映射，再交给 sRGB swapchain 完成 gamma 编码。
    vec3 mappedColor = vec3(1.0) - exp(-hdrColor * 0.8);
    outColor = vec4(mappedColor, 1.0);
}
