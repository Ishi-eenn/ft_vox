#version 410 core
in vec2 vUV;
uniform sampler2D uMap;
out vec4 fragColor;
void main() {
    fragColor = texture(uMap, vUV);
}
