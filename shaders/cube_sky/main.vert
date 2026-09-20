#version 450

layout(location = 0) out vec2 outNdc;

void main() {
    const vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0));

    outNdc = positions[gl_VertexIndex];
    gl_Position = vec4(outNdc, 1.0, 1.0);
}
