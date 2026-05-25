#version 410 core
in vec2  vUV;
in float vLight;
in float vLightShadow;
in vec4  vShadowCoord;
in float vFogFactor;

uniform sampler2D uAtlas;
uniform sampler2D uShadowMap;
uniform sampler2D uSSAOMap;
uniform float     uSunStrength;  // 0のとき(夜)は影判定をスキップ
uniform vec2      uScreenSize;
uniform vec3      uFogColor;

out vec4 FragColor;

// PCF 3x3 シャドウサンプリング
// 戻り値: 1.0=完全に照らされている, 0.0=完全に影の中
float calcShadow(vec4 shadowCoord) {
    // 透視除算 → NDC [-1,1]
    vec3 proj = shadowCoord.xyz / shadowCoord.w;
    // NDC → UV空間 [0,1]
    proj = proj * 0.5 + 0.5;

    // シャドウマップの範囲外(または near より手前)は影なし
    if (proj.z > 1.0 || proj.z < 0.0 ||
        proj.x < 0.0 || proj.x > 1.0 ||
        proj.y < 0.0 || proj.y > 1.0)
        return 1.0;

    float currentDepth = proj.z;

    // bias はシャドウマップの1テクセル深度単位より小さくする必要がある。
    // near=1, far=750 のとき 1ブロック分の深度差 = 1/749 ≈ 0.00134。
    // self-shadowing は glPolygonOffset(2,4) で防いでいるため、
    // ここでは必要最低限の定数バイアスのみ使う。
    float bias = 0.0002;

    // 3x3 PCF でエッジをぼかす
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(uShadowMap, 0));
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(uShadowMap, proj.xy + vec2(x, y) * texelSize).r;
            shadow += (currentDepth - bias > pcfDepth) ? 0.0 : 1.0;
        }
    }
    return shadow / 9.0;
}

void main() {
    vec4 col = texture(uAtlas, vUV);
    if (col.a < 0.1) discard;

    float shadowFactor = (uSunStrength > 0.01) ? calcShadow(vShadowCoord) : 1.0;

    // SSAO: アンビエント成分のみに適用し、直接光は影響させない
    float ssao = texture(uSSAOMap, gl_FragCoord.xy / uScreenSize).r;

    // vLightShadow = アンビエント成分（影の中でも残る光）
    // vLight - vLightShadow = 太陽直接光成分
    float sun_direct = vLight - vLightShadow;
    float light = vLightShadow * ssao + sun_direct * shadowFactor;

    vec3 lit = col.rgb * light;
    vec3 fogged = mix(lit, uFogColor, clamp(vFogFactor, 0.0, 1.0));
    FragColor = vec4(fogged, col.a);
}
