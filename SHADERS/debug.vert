#version 450

layout(push_constant) uniform PC {
    mat4 viewProj;
} pc;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 fragColor;

void main() {
    gl_Position = pc.viewProj * vec4(inPos, 1.0);
    fragColor   = inColor;
}
