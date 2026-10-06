#version 450

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

layout(location = 0) in vec3 inNormal[];
layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec3 outBarycentric;

const vec3 Barycentrics[3] = vec3[3](
    vec3(1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0));

void main() {
    for (int vertex = 0; vertex < 3; ++vertex) {
        gl_Position = gl_in[vertex].gl_Position;
        outNormal = inNormal[vertex];
        outBarycentric = Barycentrics[vertex];
        EmitVertex();
    }
    EndPrimitive();
}
