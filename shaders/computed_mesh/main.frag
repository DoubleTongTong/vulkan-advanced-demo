#version 450

layout(push_constant) uniform RenderParameters {
    mat4 mvp;
    vec4 options;
} pc;

layout(set = 0, binding = 0) uniform texture2D kGeneratedTexture;
layout(set = 0, binding = 1) uniform sampler kLinearSampler;

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inBarycentric;
layout(location = 0) out vec4 outColor;

vec3 hueToRgb(float hue) {
    const float h = fract(hue);
    return clamp(vec3(
        abs(h * 6.0 - 3.0) - 1.0,
        2.0 - abs(h * 6.0 - 2.0),
        2.0 - abs(h * 6.0 - 4.0)), 0.0, 1.0);
}

float edgeFactor(float thickness) {
    const vec3 width = fwidth(inBarycentric) * thickness;
    const vec3 smoothed = smoothstep(vec3(0.0), width, inBarycentric);
    return min(smoothed.x, min(smoothed.y, smoothed.z));
}

void main() {
    const vec3 normal = normalize(inNormal);
    const float lighting = clamp(dot(normal, normalize(vec3(0.2, 0.3, 1.0))), 0.35, 1.0);
    const bool colored = pc.options.x > 0.5;

    vec3 color;
    if (colored) {
        color = lighting * hueToRgb(inUv.x);
        // 彩色模式叠加抗锯齿线框，直观看到 Compute 生成的三角网格。
        color = mix(vec3(0.015), color, edgeFactor(1.15));
    } else {
        color = texture(
            sampler2D(kGeneratedTexture, kLinearSampler),
            vec2(8.0, 1.0) * inUv).rgb;
        color *= lighting;
    }

    outColor = vec4(color, 1.0);
}
