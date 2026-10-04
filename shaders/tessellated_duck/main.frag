#version 450

layout(set = 0, binding = 0) uniform sampler kSamplers[4];
layout(set = 0, binding = 1) uniform texture2D kTextures2D[];

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec3 inBarycentric;
layout(location = 0) out vec4 outColor;

float edgeFactor(float thickness) {
    // 任一重心坐标接近 0，就表示当前 Fragment 靠近三角形的一条边。
    const vec3 antialiasWidth = fwidth(inBarycentric) * thickness;
    const vec3 smoothed = smoothstep(vec3(0.0), antialiasWidth, inBarycentric);
    return min(smoothed.x, min(smoothed.y, smoothed.z));
}

void main() {
    const vec4 baseColor = texture(
        sampler2D(kTextures2D[0], kSamplers[0]), inUv);
    const vec4 wireColor = vec4(0.06, 0.06, 0.06, 1.0);
    outColor = mix(wireColor, baseColor, edgeFactor(1.25));
}
