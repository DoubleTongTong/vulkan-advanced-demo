#version 450

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

layout(location = 0) in vec2 inUv[];
layout(location = 0) out vec2 outUv;
layout(location = 1) out vec3 outBarycentric;

void main() {
    const vec3 barycentric[3] = vec3[3](
        vec3(1.0, 0.0, 0.0),
        vec3(0.0, 1.0, 0.0),
        vec3(0.0, 0.0, 1.0));

    // 为细分后每个小三角形重新生成重心坐标，供 Fragment Shader 画线框。
    for (int index = 0; index < 3; ++index) {
        gl_Position = gl_in[index].gl_Position;
        outUv = inUv[index];
        outBarycentric = barycentric[index];
        EmitVertex();
    }
    EndPrimitive();
}
