#version 410 core
layout(location = 0) in vec3  aPos;     // タイル内ローカル座標
layout(location = 1) in float aBright;  // 面ごとの明るさ (top=1.0, bottom=0.75, side=0.82-0.88)

uniform mat4 uView;
uniform mat4 uProj;
uniform vec3 uOffset;   // タイルのワールド座標オフセット

out float vBright;

void main() {
    vec3 world  = aPos + uOffset;
    vBright     = aBright;
    gl_Position = uProj * uView * vec4(world, 1.0);
}
