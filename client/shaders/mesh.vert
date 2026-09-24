#version 450
layout(location = 0) in vec3 inPos;
layout(location = 0) out vec3 vPos;
layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 color;
} pc;
void main() {
    vPos = inPos;
    gl_Position = pc.mvp * vec4(inPos, 1.0);
}
