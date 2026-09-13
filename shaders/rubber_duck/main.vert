#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 0) out vec3 outColor;

layout(constant_id = 0) const uint isWireframe = 0;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
} pc;

void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    outColor = isWireframe != 0 ? vec3(0.02, 0.02, 0.02) : vec3(1.0, 0.72, 0.18);
}
