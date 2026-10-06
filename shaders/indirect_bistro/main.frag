#version 450

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec3 inBarycentric;

layout(location = 0) out vec4 outColor;

float edgeFactor() {
    const vec3 width = fwidth(inBarycentric) * 1.25;
    const vec3 coverage = smoothstep(vec3(0.0), width, inBarycentric);
    return min(coverage.x, min(coverage.y, coverage.z));
}

void main() {
    const vec3 normal = normalize(inNormal);
    const vec3 lightDirection = normalize(vec3(-1.0, 1.5, -0.75));
    const float lighting = clamp(dot(normal, lightDirection), 0.25, 1.0);
    const vec3 surface = vec3(0.78, 0.76, 0.68) * lighting;
    const vec3 wire = vec3(0.07, 0.08, 0.09);
    outColor = vec4(mix(wire, surface, edgeFactor()), 1.0);
}
