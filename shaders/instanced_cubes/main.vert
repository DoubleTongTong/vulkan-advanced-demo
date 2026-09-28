#version 450

layout(std430, set = 1, binding = 0) readonly buffer InstanceBuffer {
    // xyz：立方体中心；w：初始旋转角度。
    vec4 centers[];
} kInstances;

layout(location = 0) out vec3 outColor;
layout(location = 1) out vec2 outUv;

layout(push_constant) uniform PushConstants {
    mat4 viewProjection;
    float time;
} pc;

// 六个面，每个面两个三角形。这样无需 vertex/index buffer。
const int indices[36] = int[36](
    0, 2, 1, 2, 3, 1, 5, 4, 1, 1, 4, 0,
    0, 4, 6, 0, 6, 2, 6, 5, 7, 6, 4, 5,
    2, 6, 3, 6, 7, 3, 7, 1, 3, 7, 5, 1);

const vec3 colors[7] = vec3[7](
    vec3(1.0, 0.2, 0.2), vec3(0.2, 1.0, 0.2),
    vec3(0.2, 0.4, 1.0), vec3(1.0, 1.0, 0.2),
    vec3(0.2, 1.0, 1.0), vec3(1.0, 0.2, 1.0),
    vec3(1.0));

mat4 translate(vec3 value) {
    mat4 result = mat4(1.0);
    result[3] = vec4(value, 1.0);
    return result;
}

// 绕立方体对角线 (1, 1, 1) 旋转。
mat4 rotate(float angle) {
    const vec3 axis = normalize(vec3(1.0));
    const float c = cos(angle);
    const float s = sin(angle);
    const vec3 t = (1.0 - c) * axis;

    mat4 result = mat4(1.0);
    result[0].xyz = vec3(c + t.x * axis.x,
                         t.x * axis.y + s * axis.z,
                         t.x * axis.z - s * axis.y);
    result[1].xyz = vec3(t.y * axis.x - s * axis.z,
                         c + t.y * axis.y,
                         t.y * axis.z + s * axis.x);
    result[2].xyz = vec3(t.z * axis.x + s * axis.y,
                         t.z * axis.y - s * axis.x,
                         c + t.z * axis.z);
    return result;
}

void main() {
    const vec4 center = kInstances.centers[gl_InstanceIndex];
    const mat4 model = translate(center.xyz) * rotate(pc.time + center.w);

    const int index = indices[gl_VertexIndex];
    const vec3 corner = vec3(index & 1, (index & 4) >> 2, (index & 2) >> 1);
    gl_Position = pc.viewProjection * model * vec4(corner - vec3(0.5), 1.0);

    const int face = gl_VertexIndex / 6;
    if (face == 0 || face == 3) outUv = corner.xz;
    if (face == 1 || face == 4) outUv = corner.xy;
    if (face == 2 || face == 5) outUv = corner.yz;
    outColor = colors[gl_InstanceIndex % 7];
}
