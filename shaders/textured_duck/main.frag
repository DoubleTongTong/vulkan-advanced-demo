#version 450

#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform sampler kSamplers[4];
layout(set = 0, binding = 1) uniform texture2D kTextures2D[];

layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    uint textureId;
} pc;

vec4 textureBindless2D(uint textureId, uint samplerId, vec2 uv) {
    return texture(nonuniformEXT(sampler2D(kTextures2D[textureId], kSamplers[samplerId])), uv);
}

void main() {
    outColor = textureBindless2D(pc.textureId, 0, inUv);
}
