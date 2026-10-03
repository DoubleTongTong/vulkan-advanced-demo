#version 450

layout(push_constant) uniform PushConstants {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 origin;
    vec4 gridParameters;
} pc;

layout(location = 0) out vec2 outWorldPosition;
layout(location = 1) out vec2 outCameraPosition;

// 只画一个大四边形。它会在 XZ 平面上跟随相机，所以看起来没有边界。
const vec3 Positions[4] = vec3[4](
    vec3(-1.0, 0.0, -1.0),
    vec3( 1.0, 0.0, -1.0),
    vec3( 1.0, 0.0,  1.0),
    vec3(-1.0, 0.0,  1.0));
const int Indices[6] = int[6](0, 1, 2, 2, 3, 0);

void main() {
    vec3 position =
        Positions[Indices[gl_VertexIndex]] * pc.gridParameters.x;
    position.xz += pc.cameraPosition.xz;
    position += pc.origin.xyz;

    outWorldPosition = position.xz;
    outCameraPosition = pc.cameraPosition.xz;
    gl_Position = pc.viewProjection * vec4(position, 1.0);
}
