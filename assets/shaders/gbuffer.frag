#version 410 core
in vec3 vViewPos;
in vec3 vViewNormal;

layout(location = 0) out vec4 gNormal;

void main() {
    // ビュー空間の法線を正規化して格納。深度はFBOが自動書き込み。
    gNormal = vec4(normalize(vViewNormal), 0.0);
}
