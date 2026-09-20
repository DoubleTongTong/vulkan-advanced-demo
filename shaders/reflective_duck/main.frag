#version 450

#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform sampler kSamplers[4];
layout(set = 0, binding = 1) uniform textureCube kTexturesCube[4];
layout(set = 0, binding = 2) uniform texture2D kTextures2D[];

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec3 inWorldPosition;
layout(location = 2) in vec3 inWorldNormal;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 centerRadius;
    vec4 cameraAndAngle;
    uint texture2DId;
    uint cubeTextureId;
} pc;

void main() {
    vec4 baseColor = texture(
        nonuniformEXT(sampler2D(kTextures2D[pc.texture2DId], kSamplers[0])), inUv);

    vec3 normal = normalize(inWorldNormal);
    vec3 incident = normalize(inWorldPosition - pc.cameraAndAngle.xyz);
    vec3 reflectionDirection = reflect(incident, normal);
    vec3 environmentHdr = texture(
        nonuniformEXT(samplerCube(kTexturesCube[pc.cubeTextureId], kSamplers[1])),
        reflectionDirection).rgb;
    vec3 environment = vec3(1.0) - exp(-environmentHdr * 0.8);

    float diffuse = max(dot(normal, normalize(vec3(-0.4, 0.8, 0.6))), 0.0);
    vec3 litBase = baseColor.rgb * (0.25 + 0.75 * diffuse);
    outColor = vec4(mix(litBase, environment, 0.35), baseColor.a);
}
