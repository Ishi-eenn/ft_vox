#version 410 core
in vec2 vUV;

uniform sampler2D uSSAOInput;

out vec4 fragColor;

void main() {
    vec2  texelSize = 1.0 / vec2(textureSize(uSSAOInput, 0));
    float result    = 0.0;
    // 4x4 ボックスブラーでノイズを滑らかにする
    for (int x = -2; x < 2; ++x) {
        for (int y = -2; y < 2; ++y) {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            result += texture(uSSAOInput, vUV + offset).r;
        }
    }
    float ao = result / 16.0;
    fragColor = vec4(ao, ao, ao, 1.0);
}
