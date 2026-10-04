#version 450

// 输入的每个三角形是一个包含 3 个控制点的 patch。
layout(vertices = 3) out;

layout(std430, set = 1, binding = 0) readonly buffer FrameBuffer {
    mat4 model;
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 tessellationParameters;
} kFrame;

layout(location = 0) in vec2 inUv[];
layout(location = 1) in vec3 inWorldPosition[];
layout(location = 0) out vec2 outUv[];

float tessellationLevel(float distance0, float distance1) {
    const float scale = max(kFrame.tessellationParameters.x, 0.01);
    const float averageDistance = (distance0 + distance1) / (2.0 * scale);

    if (averageDistance <= 2.0) return 8.0;
    if (averageDistance <= 4.0) return 4.0;
    return 1.0;
}

void main() {
    // 三次 invocation 各自传递一个控制点。
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
    outUv[gl_InvocationID] = inUv[gl_InvocationID];

    // Patch 级细分因子只需由一个 invocation 写入。
    if (gl_InvocationID == 0) {
        const vec3 camera = kFrame.cameraPosition.xyz;
        const float d0 = distance(camera, inWorldPosition[0]);
        const float d1 = distance(camera, inWorldPosition[1]);
        const float d2 = distance(camera, inWorldPosition[2]);

        // Outer[i] 控制与第 i 个控制点相对的那条边。
        gl_TessLevelOuter[0] = tessellationLevel(d1, d2);
        gl_TessLevelOuter[1] = tessellationLevel(d2, d0);
        gl_TessLevelOuter[2] = tessellationLevel(d0, d1);
        gl_TessLevelInner[0] = max(
            gl_TessLevelOuter[0],
            max(gl_TessLevelOuter[1], gl_TessLevelOuter[2]));
    }
}
