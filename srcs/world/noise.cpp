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
    height_noise_      = new FastNoiseLite();
    valley_noise_      = new FastNoiseLite();
    cave_noise_        = new FastNoiseLite();
    cave_horiz_noise_  = new FastNoiseLite();
    cave_entrance_noise_ = new FastNoiseLite();
    temp_noise_        = new FastNoiseLite();
    humid_noise_       = new FastNoiseLite();
}

NoiseGen::~NoiseGen() {
    delete (FastNoiseLite*)height_noise_;
    delete (FastNoiseLite*)valley_noise_;
    delete (FastNoiseLite*)cave_noise_;
    delete (FastNoiseLite*)cave_horiz_noise_;
    delete (FastNoiseLite*)cave_entrance_noise_;
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

    // ── 通常洞窟ノイズ ── 斜め方向に伸びる空洞を作る ─────────────────────
    // Frequency 0.020: 0.025 より低くして洞窟スケールを広げる
    // getCave() 内で Y 依存の XZ オフセットを加え、斜め方向のトンネルにする
    // 閾値は terrain_gen.cpp 側で 0.58 に設定
    auto* cn = (FastNoiseLite*)cave_noise_;
    cn->SetSeed((int)(seed ^ 0xDEADBEEFu));
    cn->SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    cn->SetFrequency(0.020f);

    // ── 横長洞窟ノイズ ── Y方向を圧縮しつつ斜め方向に伸ばす ──────────────
    // getCaveHoriz() 内で Y に 3.5 を掛けて Y 幅を絞り、
    // さらに Y 依存の XZ オフセットで斜め方向の平たいトンネルにする
    auto* chn = (FastNoiseLite*)cave_horiz_noise_;
    chn->SetSeed((int)(seed ^ 0xBEEFDEADu));
    chn->SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    chn->SetFrequency(0.020f);

    // ── 地表入口ノイズ ── 等方性の丸い開口部を地表付近に作る ─────────────────
    // 周波数 0.05: 特徴スケール ~20 ブロック → 半径 3〜6 ブロックの丸い洞窟口
    // Yオフセットなし（等方性）なのでスパゲッティ方式の「地割れ」にならない
    auto* cen = (FastNoiseLite*)cave_entrance_noise_;
    cen->SetSeed((int)(seed ^ 0xCAFED00Du));
    cen->SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    cen->SetFrequency(0.05f);

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

// 斜め洞窟ノイズ（3D: Y に比例した XZ オフセットで斜め方向のトンネルを作る）
float NoiseGen::getCave(float x, float y, float z) const {
    // y が深くなるほど XZ がずれる → 斜め方向に伸びる洞窟
    // y * 0.5 で Y 方向を若干圧縮し、横への広がりを強調
    return ((FastNoiseLite*)cave_noise_)->GetNoise(x + y * 0.40f, y * 0.50f, z + y * 0.25f);
}

// 地表入口ノイズ（3D: 等方性 — Yオフセットなし → 丸い開口部）
float NoiseGen::getCaveEntrance(float x, float y, float z) const {
    // Y を若干圧縮（0.8倍）して縦長の丸い穴にする
    return ((FastNoiseLite*)cave_entrance_noise_)->GetNoise(x, y * 0.8f, z);
}

// 斜め横長洞窟ノイズ（3D: Y 圧縮 + 斜めオフセット）
// y * 2.0 で縦幅を絞る（y * 3.5 では天井2ブロック未満になりすぎる）
// スパゲッティ式 n1²+n2²<T と組み合わせることで縦幅 ~6-8 ブロックのトンネルを生成する
float NoiseGen::getCaveHoriz(float x, float y, float z) const {
    return ((FastNoiseLite*)cave_horiz_noise_)->GetNoise(x + y * 0.30f, y * 2.0f, z - y * 0.20f);
}

// バイオーム気温ノイズ（2D）
float NoiseGen::getTemperature(float x, float z) const {
    return ((FastNoiseLite*)temp_noise_)->GetNoise(x, z);
}

// バイオーム湿度ノイズ（2D）
float NoiseGen::getHumidity(float x, float z) const {
    return ((FastNoiseLite*)humid_noise_)->GetNoise(x, z);
}
