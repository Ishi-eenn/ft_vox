#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec3 aNormal;

uniform mat4  uMVP;
uniform mat4  uModel;          // チャンクのワールド変換行列
uniform mat4  uView;           // カメラ視点への変換行列
uniform mat4  uLightSpaceMat;  // 光源視点の (Proj * View)
uniform vec3  uSunDir;         // normalized sun direction (world space)
uniform float uAmbient;        // ambient light level  [0.03 .. 0.35]
uniform float uSunStrength;    // sun diffuse strength  [0.0  .. 0.65]
uniform float uFogStart;
uniform float uFogEnd;

out vec2  vUV;
out float vLight;
out float vLightShadow;  // 影の中にいるときの明るさ (ambient のみ)
out vec4  vShadowCoord;  // 光源クリップ空間の座標
out float vFogFactor;

void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vUV = aUV;

    // Static per-face shading (Minecraft style: gives 3-D feel regardless of sun angle)
    float face_shade;
    if      (aNormal.y >  0.5) face_shade = 1.00;   // top:   full bright
    else if (aNormal.y < -0.5) face_shade = 0.50;   // bottom: darkest
    else if (abs(aNormal.x) > 0.5) face_shade = 0.60; // E/W:  medium-dark
    else                           face_shade = 0.80;  // N/S:  medium

    // Dynamic sun contribution
    float sun_diff  = max(dot(aNormal, uSunDir), 0.0);
    vLight       = (uAmbient + uSunStrength * sun_diff) * face_shade;
    vLightShadow = uAmbient * face_shade;  // 太陽光なし (影の中)

    // 光源空間での頂点位置 (PCF shadow sampling に使用)
    vShadowCoord = uLightSpaceMat * uModel * vec4(aPos, 1.0);

    vec3 view_pos = (uView * uModel * vec4(aPos, 1.0)).xyz;
    float view_dist = length(view_pos);
    if (uFogEnd > uFogStart + 1.0)
        vFogFactor = smoothstep(uFogStart, uFogEnd, view_dist);
    else
        vFogFactor = 0.0;
}
