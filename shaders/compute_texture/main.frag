#version 450

layout(set = 0, binding = 1) uniform texture2D kGeneratedTexture;
layout(set = 0, binding = 2) uniform sampler kLinearSampler;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(sampler2D(kGeneratedTexture, kLinearSampler), inUV);
}
