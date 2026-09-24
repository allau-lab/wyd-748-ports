#version 450
layout(location = 0) in vec3 vPos;
layout(location = 0) out vec4 outColor;
layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 color;
} pc;
void main() {
    float light = clamp(0.35 + 0.65 * normalize(vPos + vec3(0.2, 1.0, 0.3)).y, 0.2, 1.0);
    outColor = vec4(pc.color.rgb * light, pc.color.a);
}
