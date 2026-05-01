#version 410 core

// ─── 頂点属性（struct Vertex と一致させること）─────────────────────────────
layout(location = 0) in vec3  aPos;         // ブロックの頂点座標
layout(location = 1) in vec2  aUV;          // テクスチャUV座標
layout(location = 2) in vec3  aNormal;      // 法線ベクトル
layout(location = 3) in float aSkyLight;    // 空の明るさ（0.0〜1.0、0/15〜15/15）
layout(location = 4) in float aBlockLight;  // ブロックの明るさ（0.0〜1.0）

// ─── ユニフォーム ────────────────────────────────────────────────────────────
uniform mat4 uMVP;
// Minecraft準拠の昼夜サイクルによる空の明るさ減衰（0=昼, 11=夜, 範囲0〜15）
uniform int  uSkyDarken;

out vec2  vUV;
out float vLight;

// ─────────────────────────────────────────────────────────────────────────────
// Minecraftの明るさカーブ（0〜15段階）
//
// light_level = 0  → 最低輝度 0.05（真っ暗にはしない）
// light_level = 15 → 最大輝度 1.0
//
// 近似式: max(0.05, pow(0.8, 15.0 * (1.0 - normalized_level)))
//   level  0 → 0.9^15 ≈ 0.035 → clamp → 0.05
//   level  8 → 0.9^7  ≈ 0.478
//   level 15 → 0.9^0  = 1.0
// ─────────────────────────────────────────────────────────────────────────────
float lightLevelToBrightness(float normalized_level) {
    return max(0.05, pow(0.9, 15.0 * (1.0 - normalized_level)));
}

void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vUV = aUV;

    // ── 昼夜サイクルによる空の明るさ減衰を適用 ─────────────────────────────
    // uSkyDarken: 0（昼）〜 11（夜）
    float sky_darken_normalized = float(uSkyDarken) / 15.0;
    float sky_reduced = max(0.0, aSkyLight - sky_darken_normalized);

    // ── 有効な明るさ = 空の明るさとブロックの明るさの大きい方 ──────────────
    // （Minecraft準拠: max(skyLight - skyDarken, blockLight)）
    float effective = max(sky_reduced, aBlockLight);

    // ── Minecraftの明るさカーブで輝度に変換 ─────────────────────────────────
    float brightness = lightLevelToBrightness(effective);

    // ── Minecraft準拠の面ごとの陰影（方向による固定的な暗さ）────────────────
    // 上面=1.0, 下面=0.5, 東西面=0.6, 南北面=0.8
    float face_shade;
    if      (aNormal.y >  0.5) face_shade = 1.00;  // 上面: 最も明るい
    else if (aNormal.y < -0.5) face_shade = 0.50;  // 下面: 最も暗い
    else if (abs(aNormal.x) > 0.5) face_shade = 0.60;  // 東西面
    else                            face_shade = 0.80;  // 南北面

    vLight = brightness * face_shade;
}
