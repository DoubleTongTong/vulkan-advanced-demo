#version 450

layout(set = 0, binding = 0) uniform sampler kSamplers[4];
layout(set = 0, binding = 1) uniform texture2D kTextures2D[];

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 0) out vec4 outColor;

void main() {
    const vec3 normal = normalize(inNormal);
    const vec3 lightDirection = normalize(vec3(1.0, 0.0, 1.0));
    const float diffuse = clamp(dot(normal, lightDirection), 0.3, 1.0);
    const vec4 baseColor = texture(
        sampler2D(kTextures2D[0], kSamplers[0]), inUv);
    outColor = baseColor * diffuse * vec4(inColor, 1.0);
}
