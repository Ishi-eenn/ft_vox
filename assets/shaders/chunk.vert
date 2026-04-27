#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec3 aNormal;

uniform mat4 uMVP;

out vec2  vUV;
out float vLight;

void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vUV = aUV;
    // Simple directional light
    vec3 lightDir = normalize(vec3(0.8, 1.0, 0.6));
    float diff = max(dot(aNormal, lightDir), 0.0);
    vLight = 0.3 + 0.7 * diff;
}
