#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 outUv;
layout(location = 1) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec2 scale;
    vec2 translate;
    uint textureId;
} pc;

void main() {
    vec2 position = inPosition * pc.scale + pc.translate;
    // 正高度 Vulkan viewport 已将 NDC 的 -1 映射到窗口顶部，无须再翻转 Y。
    gl_Position = vec4(position, 0.0, 1.0);
    outUv = inUv;
    outColor = inColor;
}
