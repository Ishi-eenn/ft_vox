#version 410 core
in vec2 vUV;

uniform sampler2D gNormal;      // ビュー空間法線 (RGB16F)
uniform sampler2D gDepth;       // シーン深度 (Depth24)
uniform sampler2D uNoiseTex;    // 4x4 ランダム回転ベクトル (RGB16F)
uniform vec3  uSamples[64];     // ビュー空間ヘミスフィアサンプル
uniform mat4  uProjection;      // カメラ射影行列
uniform mat4  uInvProjection;   // 逆射影行列
uniform vec2  uNoiseScale;      // screenSize / 4.0 (ノイズタイリング)

const int   NUM_SAMPLES = 64;
const float RADIUS      = 1.2;   // ビュー空間単位 (ブロック数)
const float BIAS        = 0.03;  // self-occlusion 防止

out vec4 fragColor;

// 深度値からビュー空間座標を復元
vec3 reconstructPos(vec2 uv) {
    float d    = texture(gDepth, uv).r;
    vec4  clip = vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec4  view = uInvProjection * clip;
    return view.xyz / view.w;
}

void main() {
    // 空/遠景ピクセルはスキップ
    if (texture(gDepth, vUV).r >= 0.9999) {
        fragColor = vec4(1.0);
        return;
    }

    vec3 fragPos   = reconstructPos(vUV);
    vec3 normal    = normalize(texture(gNormal, vUV).rgb);
    vec3 randomVec = normalize(texture(uNoiseTex, vUV * uNoiseScale).rgb);

    // Gram-Schmidt で normal 基底の TBN 行列を作る
    vec3 tangent   = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN       = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;
    for (int i = 0; i < NUM_SAMPLES; ++i) {
        // サンプルをビュー空間へ変換
        vec3 samplePos  = fragPos + TBN * uSamples[i] * RADIUS;

        // スクリーン UV に投影
        vec4 proj        = uProjection * vec4(samplePos, 1.0);
        proj.xyz        /= proj.w;
        vec2 sampleUV    = proj.xy * 0.5 + 0.5;

        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 ||
            sampleUV.y < 0.0 || sampleUV.y > 1.0) continue;

        float sd = texture(gDepth, sampleUV).r;
        if (sd >= 0.9999) continue;  // 背景

        vec3  sampleActual = reconstructPos(sampleUV);

        // 距離減衰: RADIUS より遠い面の寄与を落とす
        float rangeCheck   = smoothstep(0.0, 1.0, RADIUS / abs(fragPos.z - sampleActual.z));

        // 実際の面がサンプルより手前にある → サンプルは面の内側 → オクルージョン
        // (ビュー空間は z < 0, 大きいほどカメラに近い)
        occlusion += (sampleActual.z >= samplePos.z + BIAS ? 1.0 : 0.0) * rangeCheck;
    }

    float ao = 1.0 - (occlusion / float(NUM_SAMPLES));
    fragColor = vec4(ao, ao, ao, 1.0);
}
