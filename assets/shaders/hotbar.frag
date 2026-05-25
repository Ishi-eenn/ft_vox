#version 410 core
in  vec2 vUV;
out vec4 fragColor;
uniform sampler2D uAtlas;
uniform float     uBright;
void main() {
    vec4 c  = texture(uAtlas, vUV);
    fragColor = vec4(c.rgb * uBright, c.a);
}
