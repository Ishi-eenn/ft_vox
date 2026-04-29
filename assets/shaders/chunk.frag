#version 410 core
in vec2  vUV;
in float vLight;

uniform sampler2D uAtlas;

out vec4 FragColor;

void main() {
    vec4 col = texture(uAtlas, vUV);
    if (col.a < 0.1) discard;
    FragColor = vec4(col.rgb * vLight, col.a);
}
