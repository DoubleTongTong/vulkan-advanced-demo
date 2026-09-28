#version 450

layout(set = 0, binding = 0) uniform sampler kSamplers[4];
layout(set = 0, binding = 1) uniform texture2D kTextures2D[];

layout(location = 0) in vec3 inColor;
layout(location = 1) in vec2 inUv;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(sampler2D(kTextures2D[0], kSamplers[0]), inUv) * vec4(inColor, 1.0);
}
