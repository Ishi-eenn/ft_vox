// ─────────────────────────────────────────────────────────────────────────────
// noise.cpp — ノイズ生成器の初期化とアクセス
//
// 【パーリンノイズとは？】
//   なめらかでランダムな値を生成する関数。
//   単純な乱数（毎回まったく違う値）と違い、隣り合う座標では似た値が返るため
//   「自然に見える」地形・雲・波紋などに使われる。
//
// 【FastNoiseLite とは？】
//   ヘッダーオンリーの軽量ノイズライブラリ。
//   Perlin・Simplex・Cellular など複数のノイズアルゴリズムを提供する。
//   このファイルでは5種類のノイズを目的別に初期化している。
//
// 【フラクタル FBm (Fractional Brownian Motion) とは？】
//   ノイズを複数の「周波数（オクターブ）」で重ね合わせる技法。
//   細かい変化（高周波）と大きなうねり（低周波）を合成することで
//   より自然な地形起伏が得られる。
//   Lacunarity: 各オクターブで周波数を何倍にするか（通常は2.0）
//   Gain: 各オクターブで振幅を何倍にするか（通常は0.5で徐々に小さく）
// ─────────────────────────────────────────────────────────────────────────────
#include "world/noise.hpp"
#include "FastNoiseLite.h"
#include <cstdlib>

// ─────────────────────────────────────────────────────────────────────────────
// コンストラクタ / デストラクタ
//
// FastNoiseLite をポインタで保持するのは、ヘッダーオンリーライブラリの型を
// このヘッダーに露出させないため（PImpl パターン）。
// ─────────────────────────────────────────────────────────────────────────────
NoiseGen::NoiseGen() {
    height_noise_ = new FastNoiseLite();  // 地形の高さ用
    valley_noise_ = new FastNoiseLite();  // 谷・山脈の切り立ち用
    cave_noise_   = new FastNoiseLite();  // 洞窟の空洞用
    temp_noise_   = new FastNoiseLite();  // バイオーム：気温マップ用
    humid_noise_  = new FastNoiseLite();  // バイオーム：湿度マップ用
}

NoiseGen::~NoiseGen() {
    delete (FastNoiseLite*)height_noise_;
    delete (FastNoiseLite*)valley_noise_;
    delete (FastNoiseLite*)cave_noise_;
    delete (FastNoiseLite*)temp_noise_;
    delete (FastNoiseLite*)humid_noise_;
}

// ─────────────────────────────────────────────────────────────────────────────
// setSeed() — シードに基づいて各ノイズの設定を初期化する
//
// シードが同じなら毎回同じ世界が生成される（再現性）。
// 各ノイズのシードは XOR で異なる値にして、似た形にならないようにする。
// ─────────────────────────────────────────────────────────────────────────────
void NoiseGen::setSeed(uint32_t seed) {
    // ── 高さノイズ ── 地形の基本的な起伏を作る ──────────────────────────────
    // Perlin + FBm で「丘と平地が混在する」自然な地形を生成する。
    // Frequency 0.004: 1000ブロックで地形が大きく変化する（バイオームスケール）
    // Octaves 5: 大きなうねり + 小さな凹凸の5層を重ねる
    auto* hn = (FastNoiseLite*)height_noise_;
    hn->SetSeed((int)seed);
    hn->SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    hn->SetFrequency(0.004f);
    hn->SetFractalType(FastNoiseLite::FractalType_FBm);
    hn->SetFractalOctaves(5);
    hn->SetFractalLacunarity(2.0f);
    hn->SetFractalGain(0.5f);

    // ── 谷ノイズ ── 山脈と谷を切り立てる ─────────────────────────────────
    // Ridged FBm: 谷底が鋭く、山頂が丸い「稜線」状の形状を作る。
    // （FBm とは逆に、値が0に近いところが峰となる）
    auto* vn = (FastNoiseLite*)valley_noise_;
    vn->SetSeed((int)(seed ^ 0xCAFEBABEu));
    vn->SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    vn->SetFractalType(FastNoiseLite::FractalType_Ridged);
    vn->SetFrequency(0.007f);
    vn->SetFractalOctaves(3);
    vn->SetFractalLacunarity(2.0f);
    vn->SetFractalGain(0.5f);

    // ── 洞窟ノイズ ── 地下に空洞を作る ────────────────────────────────────
    // 3D ノイズ（X/Y/Z）でノイズ値が閾値を超えた場所を空洞にする。
    // Frequency 0.05: 細かいスケールで洞窟を刻む
    auto* cn = (FastNoiseLite*)cave_noise_;
    cn->SetSeed((int)(seed ^ 0xDEADBEEFu));
    cn->SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    cn->SetFrequency(0.05f);

    // ── 気温ノイズ ── バイオームの大きな区分けを作る ────────────────────────
    // 非常に低い周波数（0.0008）= 約1250ブロックで1サイクル → 広大なバイオーム域
    // オクターブ2で僅かな凹凸を加える
    auto* tn = (FastNoiseLite*)temp_noise_;
    tn->SetSeed((int)(seed ^ 0xABCD1234u));
    tn->SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    tn->SetFrequency(0.0008f);
    tn->SetFractalType(FastNoiseLite::FractalType_FBm);
    tn->SetFractalOctaves(2);

    // ── 湿度ノイズ ── 気温と組み合わせてバイオームを決める ──────────────────
    // 気温と少し違う周波数（0.0011）にすることでグリッド状の境界線が出にくくなる
    auto* hun = (FastNoiseLite*)humid_noise_;
    hun->SetSeed((int)(seed ^ 0x5678EFABu));
    hun->SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    hun->SetFrequency(0.0011f);
    hun->SetFractalType(FastNoiseLite::FractalType_FBm);
    hun->SetFractalOctaves(2);
}

// ─────────────────────────────────────────────────────────────────────────────
// ノイズ値の取得関数
// それぞれ -1.0〜+1.0 の範囲の値を返す。
// ─────────────────────────────────────────────────────────────────────────────

// 地形の高さノイズ（2D: X と Z で変化）
float NoiseGen::getHeight(float x, float z) const {
    return ((FastNoiseLite*)height_noise_)->GetNoise(x, z);
}

// 谷・山脈ノイズ（2D）
float NoiseGen::getValley(float x, float z) const {
    return ((FastNoiseLite*)valley_noise_)->GetNoise(x, z);
}

// 洞窟ノイズ（3D: X/Y/Z 全方向で変化）
float NoiseGen::getCave(float x, float y, float z) const {
    return ((FastNoiseLite*)cave_noise_)->GetNoise(x, y, z);
}

// バイオーム気温ノイズ（2D）
float NoiseGen::getTemperature(float x, float z) const {
    return ((FastNoiseLite*)temp_noise_)->GetNoise(x, z);
}

// バイオーム湿度ノイズ（2D）
float NoiseGen::getHumidity(float x, float z) const {
    return ((FastNoiseLite*)humid_noise_)->GetNoise(x, z);
}
