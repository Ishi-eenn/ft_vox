#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform mat4 uView;
uniform float uFogStart;
uniform float uFogEnd;

out vec3 vNormal;
out float vFogFactor;

void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vNormal = mat3(uModel) * aNormal;

    vec3 view_pos = (uView * uModel * vec4(aPos, 1.0)).xyz;
    float view_dist = length(view_pos);
    if (uFogEnd > uFogStart + 1.0)
        vFogFactor = smoothstep(uFogStart, uFogEnd, view_dist);
    else
        vFogFactor = 0.0;
}
