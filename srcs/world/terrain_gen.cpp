// ─────────────────────────────────────────────────────────────────────────────
// terrain_gen.cpp — 地形生成
//
// 【地形生成の仕組み】
//   「ノイズ関数」を使って地形の高さを決める。
//   ノイズ関数は座標(x, z)を入れると -1〜1 のなめらかなランダム値を返す関数で、
//   隣り合う座標では値が急変しない（なだらかに変化する）という特性がある。
//   これを使うと、自然に見える起伏のある地形が作れる。
//
// 【バイオームブレンド】
//   温度・湿度の2軸で4種類のバイオームを定義し、
//   境界では複数バイオームの値を「ブレンド」して滑らかにつなぐ。
//
//              乾燥(Dry)       湿潤(Wet)
//   高温(Hot): 砂漠(Desert)  平原(Plains)
//   低温(Cold): 岩山(Rocky)  ツンドラ(Tundra)
// ─────────────────────────────────────────────────────────────────────────────
#include "world/terrain_gen.hpp"
#include <algorithm>
#include <cmath>

// シードを設定して乱数の初期化
void TerrainGenerator::setSeed(uint32_t seed) {
    seed_ = seed;
    noise_.setSeed(seed);
}

// ─── バイオームパラメーター ───────────────────────────────────────────────────
// base:   地形の基準高さ（ブロック数）
// amp:    ノイズを何倍に増幅するか（大きいほど山が高くなる）
// valley: 谷の深さを決める係数
struct BiomeParams { float base, amp, valley; };
static constexpr BiomeParams kPlains = {56.0f, 38.0f, 22.0f};  // 平原: 中程度の丘
static constexpr BiomeParams kDesert = {48.0f, 10.0f,  3.0f};  // 砂漠: 低くて平坦
static constexpr BiomeParams kTundra = {54.0f, 30.0f, 18.0f};  // ツンドラ: 少し起伏あり
static constexpr BiomeParams kRocky  = {72.0f, 55.0f, 32.0f};  // 岩山: 高くて険しい

// ─────────────────────────────────────────────────────────────────────────────
// hash3() — 3つの整数座標からランダムな値を生成（FNVハッシュ）
// 木の配置など「同じ座標では常に同じ結果が欲しい」乱数として使う。
// ─────────────────────────────────────────────────────────────────────────────
static uint32_t hash3(int x, int y, int z) {
    uint32_t h = 2166136261u;
    h = (h ^ (uint32_t)x) * 16777619u;
    h = (h ^ (uint32_t)y) * 16777619u;
    h = (h ^ (uint32_t)z) * 16777619u;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

// 4つのバイオームの重みを使って単一の float 値をブレンドする
static float blendBiome(float wP, float wD, float wT, float wR,
                        float vP, float vD, float vT, float vR) {
    return wP*vP + wD*vD + wT*vT + wR*vR;
}

// ─────────────────────────────────────────────────────────────────────────────
// biomeWeights() — 温度・湿度のノイズ値から4バイオームの重みを計算
//
// temp, humid はノイズから得た -1〜1 の値。
// それを 0〜1 に変換（t01, h01）して、4隅のバイオームとの距離に基づく重みを求める。
// 合計が1になる（重心座標ブレンド）。
// ─────────────────────────────────────────────────────────────────────────────
static void biomeWeights(float temp, float humid,
                         float& wP, float& wD, float& wT, float& wR) {
    float t01 = temp  * 0.5f + 0.5f;   // 0=低温, 1=高温
    float h01 = humid * 0.5f + 0.5f;   // 0=乾燥, 1=湿潤
    wP = t01 * h01;                     // 平原 (高温+湿潤)
    wD = t01 * (1.0f - h01);           // 砂漠 (高温+乾燥)
    wT = (1.0f - t01) * h01;           // ツンドラ (低温+湿潤)
    wR = (1.0f - t01) * (1.0f - h01); // 岩山 (低温+乾燥)
}

// 指定座標に木を配置できるか判定する。
// チャンク端・海面付近・草ブロックでない場所・周囲が均一でない場所には置かない。
static bool canPlaceTreeAt(const Chunk& chunk, int x, int z, int surface) {
    if (x < 2 || x > CHUNK_SIZE_X - 3 || z < 2 || z > CHUNK_SIZE_Z - 3) return false;
    if (surface < SEA_LEVEL + 2 || surface > CHUNK_SIZE_Y - 10) return false;
    if (chunk.getBlock(x, surface, z) != BlockType::Grass) return false;

    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            BlockType base = chunk.getBlock(x + dx, surface, z + dz);
            if (base != BlockType::Grass && base != BlockType::Dirt) {
                return false;
            }
        }
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// placeTree() — 木を1本生成する
//
// 幹（Wood）を surface+1 〜 trunk_top まで積み上げ、
// 葉（Leaves）を trunk_top の周囲に半球状に配置する。
// 木の高さはハッシュで決まる（シードごとに変わる）。
// ─────────────────────────────────────────────────────────────────────────────
static void placeTree(Chunk& chunk, int x, int z, int surface, uint32_t seed) {
    uint32_t h = hash3(chunk.pos.x * CHUNK_SIZE_X + x, surface, chunk.pos.z * CHUNK_SIZE_Z + z);
    h ^= seed * 0x9e3779b9u;

    int trunk_height = 4 + (int)(h % 3u);  // 幹の高さ: 4〜6ブロック
    int trunk_top = surface + trunk_height;
    if (trunk_top + 2 >= CHUNK_SIZE_Y) return;  // チャンク上端を超えるなら生成しない

    // 幹を積み上げる
    for (int y = surface + 1; y <= trunk_top; ++y) {
        chunk.setBlock(x, y, z, BlockType::Wood);
    }

    // 葉を配置（trunk_top の-2〜0の高さ、半径2ブロックの円形）
    for (int dy = -2; dy <= 0; ++dy) {
        int radius = (dy == 0) ? 1 : 2;
        int cy = trunk_top + dy;
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (std::abs(dx) == radius && std::abs(dz) == radius && dy == 0) continue;  // 四隅を丸める
                if (dx == 0 && dz == 0 && dy <= -1) continue;  // 幹があるので空ける
                if (chunk.getBlock(x + dx, cy, z + dz) == BlockType::Air) {
                    chunk.setBlock(x + dx, cy, z + dz, BlockType::Leaves);
                }
            }
        }
    }

    // 頂上の葉（trunk_top+1〜+2、より小さな半径）
    for (int dy = 1; dy <= 2; ++dy) {
        int cy = trunk_top + dy;
        int radius = (dy == 1) ? 1 : 0;  // +1段: 半径1, +2段: 中心1つだけ
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (chunk.getBlock(x + dx, cy, z + dz) == BlockType::Air) {
                    chunk.setBlock(x + dx, cy, z + dz, BlockType::Leaves);
                }
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// generate() — チャンク1つ分の地形を生成する
//
// 処理の流れ:
//   1. チャンク全域＋1ブロック外枠の高さマップを計算（隣接との高低差を調べるため）
//   2. 各(x,z)列について:
//      a. バイオームの重みを計算
//      b. 表面ブロック種（草/砂/雪/石）を決定
//      c. 地表から下の地層（土/砂）と岩盤を埋める
//      d. 海面以下なら水を配置
//      e. 地下洞窟のノイズが閾値超なら空洞に
//   3. 条件を満たす座標に確率で木を生成
// ─────────────────────────────────────────────────────────────────────────────
void TerrainGenerator::generate(Chunk& chunk) const {
    const int world_x = chunk.pos.x * CHUNK_SIZE_X;
    const int world_z = chunk.pos.z * CHUNK_SIZE_Z;

    // 高さマップ: チャンク外周1ブロック分まで含めて事前計算（隣接との傾斜を見るため）
    int heights[CHUNK_SIZE_X + 2][CHUNK_SIZE_Z + 2];

    // ある世界座標 (lx, lz) の地表高さを計算するラムダ
    auto computeHeight = [&](int lx, int lz) -> int {
        float wx = (float)(world_x + lx);
        float wz = (float)(world_z + lz);

        // 温度・湿度ノイズからバイオーム重みを計算
        float temp  = noise_.getTemperature(wx, wz);
        float humid = noise_.getHumidity(wx, wz);
        float wP, wD, wT, wR;
        biomeWeights(temp, humid, wP, wD, wT, wR);

        // 4バイオームのパラメーターをブレンドして最終的な地形パラメーターを求める
        float base   = blendBiome(wP, wD, wT, wR, kPlains.base,   kDesert.base,   kTundra.base,   kRocky.base);
        float amp    = blendBiome(wP, wD, wT, wR, kPlains.amp,    kDesert.amp,    kTundra.amp,    kRocky.amp);
        float valley = blendBiome(wP, wD, wT, wR, kPlains.valley, kDesert.valley, kTundra.valley, kRocky.valley);

        // 高さノイズ・谷ノイズから最終高さを計算
        // n: 高さ方向のノイズ（山の形）
        // v: 谷ノイズ（正なら谷を深く彫り込む）
        float n   = noise_.getHeight(wx, wz);
        float v   = noise_.getValley(wx, wz);
        float cut = std::max(0.0f, v) * valley;  // 谷の削り量（負なら削らない）
        int   s   = (int)(base + n * amp - cut);
        return std::clamp(s, 2, CHUNK_SIZE_Y - 2);  // 最低2、最高 CHUNK_SIZE_Y-2 に制限
    };

    // 1ブロック広めに高さマップを計算
    for (int x = -1; x <= CHUNK_SIZE_X; ++x)
        for (int z = -1; z <= CHUNK_SIZE_Z; ++z)
            heights[x + 1][z + 1] = computeHeight(x, z);

    // 各(x, z)列の上から下までブロックを埋める
    for (int x = 0; x < CHUNK_SIZE_X; ++x) {
        for (int z = 0; z < CHUNK_SIZE_Z; ++z) {
            int surface = heights[x + 1][z + 1];

            // 隣接4方向との高さの差（傾斜の急さ）を計算
            // 傾斜が急な場所は草/雪ではなく石や土にする
            int max_diff = std::max({
                std::abs(surface - heights[x + 1][z + 2]),
                std::abs(surface - heights[x + 1][z + 0]),
                std::abs(surface - heights[x + 2][z + 1]),
                std::abs(surface - heights[x + 0][z + 1])
            });

            // バイオーム重みを取得
            float wx = (float)(world_x + x);
            float wz = (float)(world_z + z);
            float temp  = noise_.getTemperature(wx, wz);
            float humid = noise_.getHumidity(wx, wz);
            float wP, wD, wT, wR;
            biomeWeights(temp, humid, wP, wD, wT, wR);

            bool is_desert = wD > 0.35f;            // 砂漠バイオームが強い
            bool is_snowy  = (wT + wR) > 0.55f;    // ツンドラ or 岩山が強い（寒冷）

            // 地表ブロックの種類を決定
            BlockType top;
            if      (surface > 88)             top = BlockType::Snow;   // 高山 → 雪
            else if (surface <= SEA_LEVEL + 3) top = BlockType::Sand;   // 海岸・海底 → 砂
            else if (is_desert)                top = BlockType::Sand;   // 砂漠 → 砂
            else if (is_snowy && max_diff < 3) top = BlockType::Snow;   // 寒冷・平坦 → 雪
            else if (max_diff >= 5)            top = BlockType::Stone;  // 急傾斜 → 石
            else if (max_diff >= 2)            top = BlockType::Dirt;   // 中傾斜 → 土
            else                               top = BlockType::Grass;  // 平坦 → 草

            // 地表直下の地層の深さと種類（砂漠は砂4層、それ以外は土3層）
            int  sub_depth  = is_desert ? 4 : 3;
            BlockType sub_t = is_desert ? BlockType::Sand : BlockType::Dirt;

            // Y方向（下から上）にブロックを配置
            for (int y = 0; y < CHUNK_SIZE_Y; ++y) {
                BlockType t = BlockType::Air;

                if (y == 0) {
                    t = BlockType::Stone;               // 最下層は常に石（岩盤）
                } else if (y < surface) {
                    int depth = surface - y;            // 地表からの深さ
                    t = (depth <= sub_depth) ? sub_t : BlockType::Stone;
                } else if (y == surface) {
                    t = top;                            // 地表ブロック
                } else if (y > surface && y <= SEA_LEVEL && surface < SEA_LEVEL) {
                    t = BlockType::Water;               // 海面以下の空洞 → 水
                }

                // 洞窟の生成: 地下の岩盤部分にノイズが高い箇所を空洞にする
                // 地表近くや海水域は空洞にしない（水が漏れるなどの問題を防ぐ）
                if (t != BlockType::Air && t != BlockType::Water
                        && y > 5 && y < surface - 3) {
                    if (noise_.getCave(wx, (float)y, wz) > 0.55f)
                        t = BlockType::Air;
                }

                chunk.setBlock(x, y, z, t);
                // 水ブロックには水位（満水=8）を設定
                if (t == BlockType::Water) {
                    chunk.setWaterLevel(x, y, z, 8);
                }
            }
        }
    }

    // ─── 木の生成 ──────────────────────────────────────────────────────────────
    // 平原・ツンドラのバイオームで、草ブロック上に7%の確率で木を置く
    for (int x = 0; x < CHUNK_SIZE_X; ++x) {
        for (int z = 0; z < CHUNK_SIZE_Z; ++z) {
            int surface = heights[x + 1][z + 1];
            float wx = (float)(world_x + x);
            float wz = (float)(world_z + z);
            float temp  = noise_.getTemperature(wx, wz);
            float humid = noise_.getHumidity(wx, wz);
            float wP, wD, wT, wR;
            biomeWeights(temp, humid, wP, wD, wT, wR);

            // 砂漠・岩山には木を生やさない
            if ((wP + wT) < 0.35f || wD > 0.45f || wR > 0.40f) continue;
            if (!canPlaceTreeAt(chunk, x, z, surface)) continue;

            // ハッシュで確率判定（100分の7 = 7%）
            uint32_t chance = hash3(world_x + x, (int)seed_, world_z + z);
            if ((chance % 100u) < 7u) {
                placeTree(chunk, x, z, surface, seed_);
            }
        }
    }

    chunk.is_generated = true;
    chunk.is_dirty     = true;  // メッシュ再構築が必要
}
