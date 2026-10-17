#version 450

layout(set = 0, binding = 0) uniform sampler kBaseColorSampler;
layout(set = 0, binding = 1) uniform texture2D kBaseColorTexture;

layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 baseColor;
} pc;

void main() {
    // DamagedHelmet 没有 COLOR_0，因此顶点色按 glTF 默认值 vec4(1) 处理。
    // Unlit 不计算法线、光源或 BRDF，只组合纹理和 baseColorFactor。
    vec4 texel = texture(sampler2D(kBaseColorTexture, kBaseColorSampler), inUv);
    outColor = texel * pc.baseColor;
}
