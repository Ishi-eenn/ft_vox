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
//
// 【村の生成】
//   96ブロックグリッドに1つの潜在的な村中心を配置し、
//   平坦な平原・ツンドラバイオームにのみ村を生成する。
//
//   村レイアウトはシードから3種類をランダムに選ぶ：
//     Classic  : 広場を中心に南北の大型棟・東西の中型棟・望楼・農地
//     Compact  : 小広場を囲む4-5の小屋が密集した小村
//     Street   : メイン街道に沿って家が並ぶ宿場町風
//
//   バイオームに応じて素材も変わる:
//     平原  : 石壁 + 木の屋根
//     ツンドラ: 石壁 + 雪の屋根
//
//   各建物はフットプリントの最低地形高さで置かれ、
//   石の土台で埋めたり山を掘り下げたりして地形に馴染む。
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
static constexpr BiomeParams kPlains = {56.0f, 38.0f, 22.0f};
static constexpr BiomeParams kDesert = {48.0f, 10.0f,  3.0f};
static constexpr BiomeParams kTundra = {54.0f, 30.0f, 18.0f};
static constexpr BiomeParams kRocky  = {72.0f, 55.0f, 32.0f};
static constexpr BiomeParams kSwamp  = {41.0f,  6.0f,  1.0f}; // 低平地、水没しやすい

// ─────────────────────────────────────────────────────────────────────────────
// hash3() — 3つの整数座標からランダムな値を生成（FNVハッシュ）
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

// 5つのバイオームの重みを使って単一の float 値をブレンドする
static float blendBiome(float wP, float wD, float wT, float wR, float wSw,
                        float vP, float vD, float vT, float vR, float vSw) {
    return wP*vP + wD*vD + wT*vT + wR*vR + wSw*vSw;
}

// ─────────────────────────────────────────────────────────────────────────────
// biomeWeights() — 温度・湿度のノイズ値から5バイオームの重みを計算
//
//   Plains (hot+wet), Desert (hot+dry), Tundra (cold+wet), Rocky (cold+dry),
//   Swamp (very wet) の5バイオーム。
//   湿度が非常に高い(h01>0.72)とSwampが台頭し、他のバイオームを押しのける。
// ─────────────────────────────────────────────────────────────────────────────
static void biomeWeights(float temp, float humid,
                         float& wP, float& wD, float& wT, float& wR, float& wSw) {
    const float t01 = temp  * 0.5f + 0.5f;   // 0=低温, 1=高温
    const float h01 = humid * 0.5f + 0.5f;   // 0=乾燥, 1=湿潤

    // Swamp weight: h01>0.72 から立ち上がり h01=1 で最大1
    wSw = std::max(0.0f, (h01 - 0.72f) / 0.28f);

    // 残り(1-wSw)を4バイオームで分け合う
    const float base = 1.0f - wSw;
    wP = t01 * h01        * base;
    wD = t01 * (1.0f-h01) * base;
    wT = (1.0f-t01) * h01 * base;
    wR = (1.0f-t01) * (1.0f-h01) * base;
}

// ─────────────────────────────────────────────────────────────────────────────
// computeTerrainHeight() — 任意のワールド座標の地表高さを計算
// ─────────────────────────────────────────────────────────────────────────────
static int computeTerrainHeight(const NoiseGen& noise, int wx, int wz) {
    float fwx = (float)wx, fwz = (float)wz;
    float temp  = noise.getTemperature(fwx, fwz);
    float humid = noise.getHumidity(fwx, fwz);
    float wP, wD, wT, wR, wSw;
    biomeWeights(temp, humid, wP, wD, wT, wR, wSw);
    float base   = blendBiome(wP, wD, wT, wR, wSw, kPlains.base,   kDesert.base,   kTundra.base,   kRocky.base,   kSwamp.base);
    float amp    = blendBiome(wP, wD, wT, wR, wSw, kPlains.amp,    kDesert.amp,    kTundra.amp,    kRocky.amp,    kSwamp.amp);
    float valley = blendBiome(wP, wD, wT, wR, wSw, kPlains.valley, kDesert.valley, kTundra.valley, kRocky.valley, kSwamp.valley);
    float n   = noise.getHeight(fwx, fwz);
    float v   = noise.getValley(fwx, fwz);
    float cut = std::max(0.0f, v) * valley;
    int s = (int)(base + n * amp - cut);
    return std::clamp(s, 2, CHUNK_SIZE_Y - 2);
}

// 指定座標に木を配置できるか判定する。
// allow_dirt=true のとき Dirt 地表にも配置可能（沼地用）。
static bool canPlaceTreeAt(const Chunk& chunk, int x, int z, int surface,
                            bool allow_dirt = false) {
    if (x < 3 || x > CHUNK_SIZE_X - 4 || z < 3 || z > CHUNK_SIZE_Z - 4) return false;
    if (surface < SEA_LEVEL + 1 || surface > CHUNK_SIZE_Y - 12) return false;

    BlockType top = chunk.getBlock(x, surface, z);
    if (top == BlockType::Grass) { /* OK */ }
    else if (allow_dirt && top == BlockType::Dirt) { /* OK */ }
    else return false;

    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            BlockType bt = chunk.getBlock(x + dx, surface, z + dz);
            if (bt != BlockType::Grass && bt != BlockType::Dirt) return false;
        }
    }
    return true;
}

// サボテン配置チェック（周囲にサボテンや木がないか確認）
static bool canPlaceCactusAt(const Chunk& chunk, int x, int z, int surface) {
    if (x < 1 || x > CHUNK_SIZE_X - 2 || z < 1 || z > CHUNK_SIZE_Z - 2) return false;
    if (surface < SEA_LEVEL + 2 || surface > CHUNK_SIZE_Y - 6) return false;
    if (chunk.getBlock(x, surface, z) != BlockType::Sand) return false;
    // 隣接にサボテンがないこと（重なり防止）
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx)
            if (chunk.getBlock(x + dx, surface + 1, z + dz) != BlockType::Air) return false;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// placeTree() — 木を1本生成する
// ─────────────────────────────────────────────────────────────────────────────
static void placeTree(Chunk& chunk, int x, int z, int surface, uint32_t seed) {
    uint32_t h = hash3(chunk.pos.x * CHUNK_SIZE_X + x, surface, chunk.pos.z * CHUNK_SIZE_Z + z);
    h ^= seed * 0x9e3779b9u;

    int trunk_height = 4 + (int)(h % 3u);
    int trunk_top = surface + trunk_height;
    if (trunk_top + 2 >= CHUNK_SIZE_Y) return;

    for (int y = surface + 1; y <= trunk_top; ++y)
        chunk.setBlock(x, y, z, BlockType::Wood);

    for (int dy = -2; dy <= 0; ++dy) {
        int radius = (dy == 0) ? 1 : 2;
        int cy = trunk_top + dy;
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (std::abs(dx) == radius && std::abs(dz) == radius && dy == 0) continue;
                if (dx == 0 && dz == 0 && dy <= -1) continue;
                if (chunk.getBlock(x + dx, cy, z + dz) == BlockType::Air)
                    chunk.setBlock(x + dx, cy, z + dz, BlockType::Leaves);
            }
        }
    }

    for (int dy = 1; dy <= 2; ++dy) {
        int cy = trunk_top + dy;
        int radius = (dy == 1) ? 1 : 0;
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (chunk.getBlock(x + dx, cy, z + dz) == BlockType::Air)
                    chunk.setBlock(x + dx, cy, z + dz, BlockType::Leaves);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// placePineTree() — 針葉樹（寒冷地・岩山バイオーム用）
//
// 高くて細い円錐形。幹は太め、葉は階段状に細くなる。
// ─────────────────────────────────────────────────────────────────────────────
static void placePineTree(Chunk& chunk, int x, int z, int surface, uint32_t seed) {
    uint32_t h = hash3(chunk.pos.x * CHUNK_SIZE_X + x, surface, chunk.pos.z * CHUNK_SIZE_Z + z);
    h ^= seed * 0xB5297A4Du;

    int trunk_height = 6 + (int)(h % 5u);  // 6〜10 ブロック
    int trunk_top = surface + trunk_height;
    if (trunk_top + 2 >= CHUNK_SIZE_Y) return;

    // 幹
    for (int y = surface + 1; y <= trunk_top; ++y)
        chunk.setBlock(x, y, z, BlockType::Wood);

    // 葉（階段状に: 下から半径 3,2,2,1,1,0 と細くなる）
    static const int leaf_radius[] = {3, 2, 2, 1, 1, 1, 0};
    int leaf_start = trunk_top - 5;
    for (int dy = 0; dy <= 6; ++dy) {
        int cy = leaf_start + dy;
        if (cy < 0 || cy >= CHUNK_SIZE_Y) continue;
        int radius = (dy < 7) ? leaf_radius[dy] : 0;
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (std::abs(dx) == radius && std::abs(dz) == radius) continue; // 角を丸める
                if (chunk.getBlock(x + dx, cy, z + dz) == BlockType::Air)
                    chunk.setBlock(x + dx, cy, z + dz, BlockType::Leaves);
            }
        }
    }
    // 頂点の葉
    if (trunk_top + 1 < CHUNK_SIZE_Y)
        chunk.setBlock(x, trunk_top + 1, z, BlockType::Leaves);
}

// ─────────────────────────────────────────────────────────────────────────────
// placeSwampTree() — 沼地の木（沼バイオーム用）
//
// 低く太い幹、広く垂れ下がった葉が特徴。暗い雰囲気を演出する。
// ─────────────────────────────────────────────────────────────────────────────
static void placeSwampTree(Chunk& chunk, int x, int z, int surface, uint32_t seed) {
    uint32_t h = hash3(chunk.pos.x * CHUNK_SIZE_X + x, surface + 1, chunk.pos.z * CHUNK_SIZE_Z + z);
    h ^= seed * 0xF1234567u;

    int trunk_height = 3 + (int)(h % 3u);  // 3〜5 ブロック（低め）
    int trunk_top = surface + trunk_height;
    if (trunk_top + 3 >= CHUNK_SIZE_Y) return;

    // 幹
    for (int y = surface + 1; y <= trunk_top; ++y)
        chunk.setBlock(x, y, z, BlockType::Wood);

    // 葉（広く平たく広がる、縁は垂れ下がり）
    for (int dy = -1; dy <= 2; ++dy) {
        int cy = trunk_top + dy;
        if (cy < 0 || cy >= CHUNK_SIZE_Y) continue;
        int radius;
        if      (dy == -1) radius = 1;
        else if (dy ==  0) radius = 3;
        else if (dy ==  1) radius = 2;
        else               radius = 1;
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (dx == 0 && dz == 0 && dy < 0) continue;
                if (std::abs(dx) == radius && std::abs(dz) == radius) continue;
                if (chunk.getBlock(x + dx, cy, z + dz) == BlockType::Air)
                    chunk.setBlock(x + dx, cy, z + dz, BlockType::Leaves);
            }
        }
    }
    // 垂れ下がった葉（ランダムに周囲の下段に追加）
    for (int dz = -2; dz <= 2; ++dz) {
        for (int dx = -2; dx <= 2; ++dx) {
            if (dx == 0 && dz == 0) continue;
            uint32_t rnd = hash3(x + dx, trunk_top, z + dz) ^ seed;
            if ((rnd % 3u) == 0) {
                int cy = trunk_top - 1;
                if (cy >= 0 && chunk.getBlock(x + dx, cy, z + dz) == BlockType::Air)
                    chunk.setBlock(x + dx, cy, z + dz, BlockType::Leaves);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// placeCactus() — サボテン（砂漠バイオーム用）
//
// 単純な縦柱。高さ1〜4ブロック。稀に腕（横枝）が出る。
// ─────────────────────────────────────────────────────────────────────────────
static void placeCactus(Chunk& chunk, int x, int z, int surface, uint32_t seed) {
    uint32_t h = hash3(chunk.pos.x * CHUNK_SIZE_X + x, surface, chunk.pos.z * CHUNK_SIZE_Z + z);
    h ^= seed * 0x12345679u;

    int height = 1 + (int)(h % 4u);  // 1〜4 ブロック
    int top = surface + height;
    if (top >= CHUNK_SIZE_Y) return;

    for (int y = surface + 1; y <= top; ++y)
        chunk.setBlock(x, y, z, BlockType::Cactus);

    // 稀にY字型の腕（幹高さが3以上の場合のみ）
    if (height >= 3 && (h >> 8) % 3u == 0) {
        int arm_y = surface + 2;
        // 腕の方向：0=+X, 1=-X, 2=+Z, 3=-Z
        static const int arm_dx[] = {1, -1, 0, 0};
        static const int arm_dz[] = {0, 0, 1, -1};
        int dir = (int)((h >> 4) % 4u);
        int ax = x + arm_dx[dir], az = z + arm_dz[dir];
        if (ax >= 0 && ax < CHUNK_SIZE_X && az >= 0 && az < CHUNK_SIZE_Z) {
            if (chunk.getBlock(ax, arm_y, az) == BlockType::Air)
                chunk.setBlock(ax, arm_y, az, BlockType::Cactus);
            if (arm_y + 1 < CHUNK_SIZE_Y &&
                chunk.getBlock(ax, arm_y + 1, az) == BlockType::Air)
                chunk.setBlock(ax, arm_y + 1, az, BlockType::Cactus);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 村の生成
// ─────────────────────────────────────────────────────────────────────────────

// ヘルパー: ワールド座標でブロックをチャンクに配置（範囲外は無視）
static void setBlockWorld(Chunk& chunk, int chunk_wx, int chunk_wz,
                          int wx, int wy, int wz, BlockType t) {
    int lx = wx - chunk_wx;
    int lz = wz - chunk_wz;
    if (lx < 0 || lx >= CHUNK_SIZE_X || lz < 0 || lz >= CHUNK_SIZE_Z) return;
    if (wy < 0 || wy >= CHUNK_SIZE_Y) return;
    chunk.setBlock(lx, wy, lz, t);
}

// ─────────────────────────────────────────────────────────────────────────────
// バイオームに応じた村の素材セット
// ─────────────────────────────────────────────────────────────────────────────
struct VillageMat {
    BlockType wall;  // 壁ブロック
    BlockType roof;  // 屋根ブロック
};

// ツンドラ寄りなら雪の屋根、平原なら木の屋根
static VillageMat getVillageMat(float wT) {
    if (wT > 0.45f) return {BlockType::Stone, BlockType::Snow};
    return {BlockType::Stone, BlockType::Wood};
}

// ─────────────────────────────────────────────────────────────────────────────
// computeFootprintBase() — 建物フットプリント内の最低地形高さを計算
//
// フットプリントの9点（四隅・辺中央・中心）をサンプリングして最低値を返す。
// 建物はこの高さに配置することで「最も低い地面」に合わせて立つ。
// ─────────────────────────────────────────────────────────────────────────────
static int computeFootprintBase(const NoiseGen& noise,
                                 int origin_wx, int origin_wz, int fw, int fd) {
    int min_h = CHUNK_SIZE_Y;
    int xs[3] = {0, fw / 2, fw - 1};
    int zs[3] = {0, fd / 2, fd - 1};
    for (int xi = 0; xi < 3; ++xi) {
        for (int zi = 0; zi < 3; ++zi) {
            int h = computeTerrainHeight(noise, origin_wx + xs[xi], origin_wz + zs[zi]);
            if (h < min_h) min_h = h;
        }
    }
    return min_h;
}

// ─────────────────────────────────────────────────────────────────────────────
// levelBuilding() — 建物フットプリント下の地形を平らにする
//
// base_y より上にある地形ブロックを空気で掘り下げ（切り通し）、
// base_y より下の空洞を石で埋める（土台・杭）。
// これにより建物が地形にぴったり馴染む。
// ─────────────────────────────────────────────────────────────────────────────
static void levelBuilding(Chunk& chunk, const NoiseGen& noise,
                           int chunk_wx, int chunk_wz,
                           int origin_wx, int origin_wz,
                           int fw, int fd, int base_y) {
    for (int dz = 0; dz < fd; ++dz) {
        for (int dx = 0; dx < fw; ++dx) {
            int wx = origin_wx + dx, wz = origin_wz + dz;
            int terrain_y = computeTerrainHeight(noise, wx, wz);
            // 地形が base_y より高い場合: 上のブロックを掘り下げる
            for (int y = base_y + 1; y <= terrain_y + 1; ++y)
                setBlockWorld(chunk, chunk_wx, chunk_wz, wx, y, wz, BlockType::Air);
            // 地形が base_y より低い場合: 石の土台で埋める
            for (int y = terrain_y + 1; y <= base_y; ++y)
                setBlockWorld(chunk, chunk_wx, chunk_wz, wx, y, wz, BlockType::Stone);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 村中心の決定
// ─────────────────────────────────────────────────────────────────────────────
static constexpr int VILLAGE_GRID = 96;

static bool getVillageCenter(int cell_cx, int cell_cz, uint32_t seed,
                              int& out_wx, int& out_wz) {
    uint32_t h = hash3(cell_cx, (int)(seed ^ 0xDEADBEEFu), cell_cz);
    if ((h % 100u) >= 65u) return false;
    int jx = (int)((h >>  8) % 33u) - 16;
    int jz = (int)((h >> 16) % 33u) - 16;
    out_wx = cell_cx * VILLAGE_GRID + VILLAGE_GRID / 2 + jx;
    out_wz = cell_cz * VILLAGE_GRID + VILLAGE_GRID / 2 + jz;
    return true;
}

// 村に適した場所か（砂漠・岩山・沼除外、平坦・高さチェック）
static bool isVillageSuitable(const NoiseGen& noise, int vx, int vz) {
    float temp  = noise.getTemperature((float)vx, (float)vz);
    float humid = noise.getHumidity((float)vx, (float)vz);
    float wP, wD, wT, wR, wSw;
    biomeWeights(temp, humid, wP, wD, wT, wR, wSw);
    if (wD > 0.35f || wR > 0.35f || wSw > 0.25f) return false;

    int center_h = computeTerrainHeight(noise, vx, vz);
    if (center_h < SEA_LEVEL + 4 || center_h > 80) return false;

    int min_h = center_h, max_h = center_h;
    for (int dx = -8; dx <= 8; dx += 4) {
        for (int dz = -8; dz <= 8; dz += 4) {
            int h = computeTerrainHeight(noise, vx + dx, vz + dz);
            min_h = std::min(min_h, h);
            max_h = std::max(max_h, h);
        }
    }
    return (max_h - min_h) <= 4;
}

// ─────────────────────────────────────────────────────────────────────────────
// placePlaza() — 村の中心に石畳の広場を敷く
// ─────────────────────────────────────────────────────────────────────────────
static void placePlaza(Chunk& chunk, int chunk_wx, int chunk_wz,
                       int vx, int vz, int base_y, int radius) {
    for (int dz = -radius; dz <= radius; ++dz)
        for (int dx = -radius; dx <= radius; ++dx)
            setBlockWorld(chunk, chunk_wx, chunk_wz, vx + dx, base_y, vz + dz, BlockType::Stone);
}

// ─────────────────────────────────────────────────────────────────────────────
// placeWell() — 広場の中心に井戸（3×3）を置く
// ─────────────────────────────────────────────────────────────────────────────
static void placeWell(Chunk& chunk, int chunk_wx, int chunk_wz,
                      int vx, int vz, int base_y) {
    int ox = vx - 1, oz = vz - 1;
    for (int dz = 0; dz < 3; ++dz) {
        for (int dx = 0; dx < 3; ++dx) {
            bool center = (dx == 1 && dz == 1);
            int wx = ox + dx, wz = oz + dz;
            if (center) {
                int lx = wx - chunk_wx, lz = wz - chunk_wz;
                if (lx >= 0 && lx < CHUNK_SIZE_X && lz >= 0 && lz < CHUNK_SIZE_Z) {
                    chunk.setBlock(lx, base_y + 1, lz, BlockType::Water);
                    chunk.setWaterLevel(lx, base_y + 1, lz, 8);
                }
            } else {
                setBlockWorld(chunk, chunk_wx, chunk_wz, wx, base_y + 1, wz, BlockType::Stone);
                setBlockWorld(chunk, chunk_wx, chunk_wz, wx, base_y + 2, wz, BlockType::Stone);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// placePath() — 2点間を石の通路で結ぶ（L字形、地形の高さに追従）
// ─────────────────────────────────────────────────────────────────────────────
static void placePath(Chunk& chunk, const NoiseGen& noise,
                      int chunk_wx, int chunk_wz,
                      int wx1, int wz1, int wx2, int wz2) {
    int x = wx1, z = wz1;
    int sx = (wx2 > wx1) ? 1 : (wx2 < wx1) ? -1 : 0;
    int sz = (wz2 > wz1) ? 1 : (wz2 < wz1) ? -1 : 0;
    // X方向に進む
    while (x != wx2) {
        int y = computeTerrainHeight(noise, x, z);
        setBlockWorld(chunk, chunk_wx, chunk_wz, x, y, z, BlockType::Stone);
        x += sx;
    }
    // Z方向に進む
    while (z != wz2) {
        int y = computeTerrainHeight(noise, x, z);
        setBlockWorld(chunk, chunk_wx, chunk_wz, x, y, z, BlockType::Stone);
        z += sz;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// placeSmallHut() — 小屋（5×4フットプリント、壁2層）
//
// Compact レイアウトで使う小型の建物。
// orient: 0=-Z面にドア, 1=+X面, 2=+Z面, 3=-X面
// ─────────────────────────────────────────────────────────────────────────────
static void placeSmallHut(Chunk& chunk, int chunk_wx, int chunk_wz,
                          int origin_wx, int origin_wz, int base_y, int orient,
                          const VillageMat& mat) {
    constexpr int W = 5, D = 4, WALL_H = 2, ROOF_DY = 3;
    static const int door_dx[4] = {2, W-1, 2, 0};
    static const int door_dz[4] = {0, 1,   D-1, 1};

    for (int dz = 0; dz < D; ++dz) {
        for (int dx = 0; dx < W; ++dx) {
            int wx = origin_wx + dx, wz = origin_wz + dz;
            bool wall = (dx == 0 || dx == W-1 || dz == 0 || dz == D-1);
            setBlockWorld(chunk, chunk_wx, chunk_wz, wx, base_y,           wz, BlockType::Dirt);
            for (int dy = 1; dy <= WALL_H; ++dy)
                setBlockWorld(chunk, chunk_wx, chunk_wz, wx, base_y + dy,  wz,
                              wall ? mat.wall : BlockType::Air);
            setBlockWorld(chunk, chunk_wx, chunk_wz, wx, base_y + ROOF_DY, wz, mat.roof);
        }
    }
    int ddx = door_dx[orient & 3], ddz = door_dz[orient & 3];
    setBlockWorld(chunk, chunk_wx, chunk_wz, origin_wx+ddx, base_y+1, origin_wz+ddz, BlockType::Air);
}

// ─────────────────────────────────────────────────────────────────────────────
// placeHouse() — 中型の家（7×5フットプリント、壁3層）
//
// orient: 0=-Z面にドア, 1=+X面, 2=+Z面, 3=-X面
// ─────────────────────────────────────────────────────────────────────────────
static void placeHouse(Chunk& chunk, int chunk_wx, int chunk_wz,
                       int origin_wx, int origin_wz, int base_y, int orient,
                       const VillageMat& mat) {
    constexpr int W = 7, D = 5, WALL_H = 3, ROOF_DY = 4;
    static const int door_dx[4] = {3, W-1, 3, 0};
    static const int door_dz[4] = {0, 2, D-1, 2};

    for (int dz = 0; dz < D; ++dz) {
        for (int dx = 0; dx < W; ++dx) {
            int wx = origin_wx + dx, wz = origin_wz + dz;
            bool wall = (dx == 0 || dx == W-1 || dz == 0 || dz == D-1);
            setBlockWorld(chunk, chunk_wx, chunk_wz, wx, base_y,           wz, BlockType::Dirt);
            for (int dy = 1; dy <= WALL_H; ++dy)
                setBlockWorld(chunk, chunk_wx, chunk_wz, wx, base_y + dy,  wz,
                              wall ? mat.wall : BlockType::Air);
            setBlockWorld(chunk, chunk_wx, chunk_wz, wx, base_y + ROOF_DY, wz, mat.roof);
        }
    }
    int ddx = door_dx[orient & 3], ddz = door_dz[orient & 3];
    setBlockWorld(chunk, chunk_wx, chunk_wz, origin_wx+ddx, base_y+1, origin_wz+ddz, BlockType::Air);
    setBlockWorld(chunk, chunk_wx, chunk_wz, origin_wx+ddx, base_y+2, origin_wz+ddz, BlockType::Air);
}

// ─────────────────────────────────────────────────────────────────────────────
// placeLargeHouse() — 大型の家（9×7フットプリント、壁4層）
// ─────────────────────────────────────────────────────────────────────────────
static void placeLargeHouse(Chunk& chunk, int chunk_wx, int chunk_wz,
                             int origin_wx, int origin_wz, int base_y, int orient,
                             const VillageMat& mat) {
    constexpr int W = 9, D = 7, WALL_H = 4, ROOF_DY = 5;
    static const int door_dx[4] = {4, W-1, 4, 0};
    static const int door_dz[4] = {0, 3, D-1, 3};

    for (int dz = 0; dz < D; ++dz) {
        for (int dx = 0; dx < W; ++dx) {
            int wx = origin_wx + dx, wz = origin_wz + dz;
            bool wall = (dx == 0 || dx == W-1 || dz == 0 || dz == D-1);
            setBlockWorld(chunk, chunk_wx, chunk_wz, wx, base_y,           wz, BlockType::Dirt);
            for (int dy = 1; dy <= WALL_H; ++dy)
                setBlockWorld(chunk, chunk_wx, chunk_wz, wx, base_y + dy,  wz,
                              wall ? mat.wall : BlockType::Air);
            setBlockWorld(chunk, chunk_wx, chunk_wz, wx, base_y + ROOF_DY, wz, mat.roof);
        }
    }
    int ddx = door_dx[orient & 3], ddz = door_dz[orient & 3];
    for (int dy = 1; dy <= 3; ++dy)
        setBlockWorld(chunk, chunk_wx, chunk_wz, origin_wx+ddx, base_y+dy, origin_wz+ddz, BlockType::Air);
}

// ─────────────────────────────────────────────────────────────────────────────
// placeTower() — 望楼（4×4フットプリント、高さ9）
// ─────────────────────────────────────────────────────────────────────────────
static void placeTower(Chunk& chunk, int chunk_wx, int chunk_wz,
                       int origin_wx, int origin_wz, int base_y,
                       const VillageMat& /*mat*/) {
    constexpr int W = 4, D = 4;
    for (int dz = 0; dz < D; ++dz) {
        for (int dx = 0; dx < W; ++dx) {
            int wx = origin_wx + dx, wz = origin_wz + dz;
            bool wall = (dx == 0 || dx == W-1 || dz == 0 || dz == D-1);

            setBlockWorld(chunk, chunk_wx, chunk_wz, wx, base_y, wz, BlockType::Stone);

            // 壁（Y+1〜+7）
            for (int dy = 1; dy <= 7; ++dy)
                setBlockWorld(chunk, chunk_wx, chunk_wz, wx, base_y + dy, wz,
                              wall ? BlockType::Stone : BlockType::Air);

            // 展望台床（Y+8）
            setBlockWorld(chunk, chunk_wx, chunk_wz, wx, base_y + 8, wz, BlockType::Stone);

            // 胸壁（Y+9、外周のみ）
            if (wall)
                setBlockWorld(chunk, chunk_wx, chunk_wz, wx, base_y + 9, wz, BlockType::Stone);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// placeFarm() — 農地（6×8の土の畑＋四隅に木の支柱）
// ─────────────────────────────────────────────────────────────────────────────
static void placeFarm(Chunk& chunk, int chunk_wx, int chunk_wz,
                      int origin_wx, int origin_wz, int base_y) {
    constexpr int FW = 6, FD = 8;

    for (int dz = 0; dz < FD; ++dz)
        for (int dx = 0; dx < FW; ++dx)
            setBlockWorld(chunk, chunk_wx, chunk_wz,
                         origin_wx + dx, base_y, origin_wz + dz, BlockType::Dirt);

    // 四隅に木の支柱（高さ2）
    static const int cx[4] = {0, FW-1, 0,    FW-1};
    static const int cz[4] = {0, 0,    FD-1, FD-1};
    for (int i = 0; i < 4; ++i) {
        setBlockWorld(chunk, chunk_wx, chunk_wz,
                     origin_wx+cx[i], base_y+1, origin_wz+cz[i], BlockType::Wood);
        setBlockWorld(chunk, chunk_wx, chunk_wz,
                     origin_wx+cx[i], base_y+2, origin_wz+cz[i], BlockType::Wood);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Layout A — Classic（広場中心型）
//
// 広場を中心に南北大型・東西中型・望楼・農地を配置する伝統的な村。
// 建物位置はハッシュで ±3 ブロックのジッターを加えてバリエーションを出す。
// ─────────────────────────────────────────────────────────────────────────────
static void layoutClassic(Chunk& chunk, const NoiseGen& noise, uint32_t vh,
                           int chunk_wx, int chunk_wz,
                           int vx, int vz, int base_y,
                           const VillageMat& mat) {
    // 建物ごとの位置ジッター（-2〜+2）
    auto jit = [&](int shift) -> int {
        return (int)((vh >> shift) % 5u) - 2;
    };

    // 広場 (7×7)
    placePlaza(chunk, chunk_wx, chunk_wz, vx, vz, base_y, 3);
    placeWell(chunk, chunk_wx, chunk_wz, vx, vz, base_y);

    // 望楼（北東）
    int tx = vx + 14 + jit(4), tz = vz - 17 + jit(8);
    int tb = computeFootprintBase(noise, tx, tz, 4, 4);
    levelBuilding(chunk, noise, chunk_wx, chunk_wz, tx, tz, 4, 4, tb);
    placeTower(chunk, chunk_wx, chunk_wz, tx, tz, tb, mat);
    placePath(chunk, noise, chunk_wx, chunk_wz, tx + 2, tz + 4, vx + 3, vz - 3);

    // 農地（西）
    int fx = vx - 19 + jit(12), fz = vz - 4 + jit(16);
    int fb = computeFootprintBase(noise, fx, fz, 6, 8);
    levelBuilding(chunk, noise, chunk_wx, chunk_wz, fx, fz, 6, 8, fb);
    placeFarm(chunk, chunk_wx, chunk_wz, fx, fz, fb);

    // 大型 N棟
    int nhx = vx - 4 + jit(20), nhz = vz - 10 + jit(24);
    int nhb = computeFootprintBase(noise, nhx, nhz, 9, 7);
    levelBuilding(chunk, noise, chunk_wx, chunk_wz, nhx, nhz, 9, 7, nhb);
    placeLargeHouse(chunk, chunk_wx, chunk_wz, nhx, nhz, nhb, 2, mat);
    placePath(chunk, noise, chunk_wx, chunk_wz, nhx + 4, nhz + 7, vx, vz);

    // 大型 S棟
    int shx = vx - 4 + jit(28), shz = vz + 4 + jit(0);
    int shb = computeFootprintBase(noise, shx, shz, 9, 7);
    levelBuilding(chunk, noise, chunk_wx, chunk_wz, shx, shz, 9, 7, shb);
    placeLargeHouse(chunk, chunk_wx, chunk_wz, shx, shz, shb, 0, mat);
    placePath(chunk, noise, chunk_wx, chunk_wz, shx + 4, shz, vx, vz);

    // 中型 E棟
    int ehx = vx + 4 + jit(4), ehz = vz - 2 + jit(8);
    int ehb = computeFootprintBase(noise, ehx, ehz, 7, 5);
    levelBuilding(chunk, noise, chunk_wx, chunk_wz, ehx, ehz, 7, 5, ehb);
    placeHouse(chunk, chunk_wx, chunk_wz, ehx, ehz, ehb, 3, mat);
    placePath(chunk, noise, chunk_wx, chunk_wz, ehx, ehz + 2, vx, vz);

    // 中型 W棟
    int whx = vx - 11 + jit(12), whz = vz - 2 + jit(16);
    int whb = computeFootprintBase(noise, whx, whz, 7, 5);
    levelBuilding(chunk, noise, chunk_wx, chunk_wz, whx, whz, 7, 5, whb);
    placeHouse(chunk, chunk_wx, chunk_wz, whx, whz, whb, 1, mat);
    placePath(chunk, noise, chunk_wx, chunk_wz, whx + 7, whz + 2, vx, vz);
}

// ─────────────────────────────────────────────────────────────────────────────
// Layout B — Compact（密集小村）
//
// 小さな広場を囲む4〜5棟の小屋が密集した集落。
// 農地はなく、全体がコンパクトにまとまる。
// ─────────────────────────────────────────────────────────────────────────────
static void layoutCompact(Chunk& chunk, const NoiseGen& noise, uint32_t vh,
                           int chunk_wx, int chunk_wz,
                           int vx, int vz, int base_y,
                           const VillageMat& mat) {
    auto jit = [&](int shift) -> int {
        return (int)((vh >> shift) % 3u) - 1;
    };

    // 小広場（5×5）
    placePlaza(chunk, chunk_wx, chunk_wz, vx, vz, base_y, 2);
    placeWell(chunk, chunk_wx, chunk_wz, vx, vz, base_y);

    // 北の小屋
    {
        int ox = vx - 2 + jit(4), oz = vz - 9 + jit(8);
        int b = computeFootprintBase(noise, ox, oz, 5, 4);
        levelBuilding(chunk, noise, chunk_wx, chunk_wz, ox, oz, 5, 4, b);
        placeSmallHut(chunk, chunk_wx, chunk_wz, ox, oz, b, 2, mat);
        placePath(chunk, noise, chunk_wx, chunk_wz, ox + 2, oz + 4, vx, vz - 2);
    }
    // 南の小屋
    {
        int ox = vx - 2 + jit(12), oz = vz + 6 + jit(16);
        int b = computeFootprintBase(noise, ox, oz, 5, 4);
        levelBuilding(chunk, noise, chunk_wx, chunk_wz, ox, oz, 5, 4, b);
        placeSmallHut(chunk, chunk_wx, chunk_wz, ox, oz, b, 0, mat);
        placePath(chunk, noise, chunk_wx, chunk_wz, ox + 2, oz, vx, vz + 2);
    }
    // 東の中型家
    {
        int ox = vx + 4 + jit(20), oz = vz - 2 + jit(24);
        int b = computeFootprintBase(noise, ox, oz, 7, 5);
        levelBuilding(chunk, noise, chunk_wx, chunk_wz, ox, oz, 7, 5, b);
        placeHouse(chunk, chunk_wx, chunk_wz, ox, oz, b, 3, mat);
        placePath(chunk, noise, chunk_wx, chunk_wz, ox, oz + 2, vx + 2, vz);
    }
    // 西の小屋
    {
        int ox = vx - 10 + jit(28), oz = vz - 2 + jit(0);
        int b = computeFootprintBase(noise, ox, oz, 5, 4);
        levelBuilding(chunk, noise, chunk_wx, chunk_wz, ox, oz, 5, 4, b);
        placeSmallHut(chunk, chunk_wx, chunk_wz, ox, oz, b, 1, mat);
        placePath(chunk, noise, chunk_wx, chunk_wz, ox + 5, oz + 2, vx - 2, vz);
    }
    // 北東の望楼（一部の村のみ）
    if ((vh & 0x3u) >= 1u) {
        int ox = vx + 9 + jit(4), oz = vz - 12 + jit(8);
        int b = computeFootprintBase(noise, ox, oz, 4, 4);
        levelBuilding(chunk, noise, chunk_wx, chunk_wz, ox, oz, 4, 4, b);
        placeTower(chunk, chunk_wx, chunk_wz, ox, oz, b, mat);
        placePath(chunk, noise, chunk_wx, chunk_wz, ox + 2, oz + 4, vx + 2, vz - 2);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Layout C — Street（街道沿い）
//
// 南北に伸びるメイン街道の両側に家が並ぶ宿場町風の村。
// 街道中央に井戸、北端にオプションで望楼を配置。
// ─────────────────────────────────────────────────────────────────────────────
static void layoutStreet(Chunk& chunk, const NoiseGen& noise, uint32_t vh,
                          int chunk_wx, int chunk_wz,
                          int vx, int vz, int base_y,
                          const VillageMat& mat) {
    auto jit = [&](int shift) -> int {
        return (int)((vh >> shift) % 3u) - 1;
    };

    // 中央の井戸と広場（小さめ、3×3）
    placePlaza(chunk, chunk_wx, chunk_wz, vx, vz, base_y, 2);
    placeWell(chunk, chunk_wx, chunk_wz, vx, vz, base_y);

    // 南北メイン街道（3ブロック幅の石畳）
    for (int dz = -18; dz <= 18; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            int y = computeTerrainHeight(noise, vx + dx, vz + dz);
            setBlockWorld(chunk, chunk_wx, chunk_wz, vx + dx, y, vz + dz, BlockType::Stone);
        }
    }

    // 西側の建物（街道の西に3棟）
    static const int west_dz[3]   = {-12, -2, 8};
    static const int west_orient[3] = {1, 1, 1};  // 東向き（街道側にドア）
    for (int i = 0; i < 3; ++i) {
        int ox = vx - 10 + jit(i * 8);
        int oz = vz + west_dz[i] + jit(i * 8 + 4);
        bool large = (i == 1);
        if (large) {
            int b = computeFootprintBase(noise, ox, oz, 9, 7);
            levelBuilding(chunk, noise, chunk_wx, chunk_wz, ox, oz, 9, 7, b);
            placeLargeHouse(chunk, chunk_wx, chunk_wz, ox, oz, b, west_orient[i], mat);
            placePath(chunk, noise, chunk_wx, chunk_wz, ox + 9, oz + 3, vx - 1, vz + west_dz[i] + 3);
        } else {
            int b = computeFootprintBase(noise, ox, oz, 7, 5);
            levelBuilding(chunk, noise, chunk_wx, chunk_wz, ox, oz, 7, 5, b);
            placeHouse(chunk, chunk_wx, chunk_wz, ox, oz, b, west_orient[i], mat);
            placePath(chunk, noise, chunk_wx, chunk_wz, ox + 7, oz + 2, vx - 1, vz + west_dz[i] + 2);
        }
    }

    // 東側の建物（街道の東に3棟）
    static const int east_dz[3]   = {-10, 0, 9};
    static const int east_orient[3] = {3, 3, 3};  // 西向き（街道側にドア）
    for (int i = 0; i < 3; ++i) {
        int ox = vx + 4 + jit(i * 8 + 2);
        int oz = vz + east_dz[i] + jit(i * 8 + 6);
        bool small = (i == 2);
        if (small) {
            int b = computeFootprintBase(noise, ox, oz, 5, 4);
            levelBuilding(chunk, noise, chunk_wx, chunk_wz, ox, oz, 5, 4, b);
            placeSmallHut(chunk, chunk_wx, chunk_wz, ox, oz, b, east_orient[i], mat);
            placePath(chunk, noise, chunk_wx, chunk_wz, ox, oz + 2, vx + 1, vz + east_dz[i] + 2);
        } else {
            int b = computeFootprintBase(noise, ox, oz, 7, 5);
            levelBuilding(chunk, noise, chunk_wx, chunk_wz, ox, oz, 7, 5, b);
            placeHouse(chunk, chunk_wx, chunk_wz, ox, oz, b, east_orient[i], mat);
            placePath(chunk, noise, chunk_wx, chunk_wz, ox, oz + 2, vx + 1, vz + east_dz[i] + 2);
        }
    }

    // 農地（南端）
    {
        int ox = vx - 3 + jit(20), oz = vz + 13 + jit(24);
        int b = computeFootprintBase(noise, ox, oz, 6, 8);
        levelBuilding(chunk, noise, chunk_wx, chunk_wz, ox, oz, 6, 8, b);
        placeFarm(chunk, chunk_wx, chunk_wz, ox, oz, b);
    }

    // 北端の望楼（オプション）
    if ((vh & 0x3u) >= 1u) {
        int ox = vx - 2 + jit(28), oz = vz - 18 + jit(0);
        int b = computeFootprintBase(noise, ox, oz, 4, 4);
        levelBuilding(chunk, noise, chunk_wx, chunk_wz, ox, oz, 4, 4, b);
        placeTower(chunk, chunk_wx, chunk_wz, ox, oz, b, mat);
        placePath(chunk, noise, chunk_wx, chunk_wz, ox + 2, oz + 4, vx, vz - 2);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// placeVillage() — 村全体を配置するマスター関数
//
// ハッシュでレイアウトを選択：
//   hash % 3 == 0: Classic（広場中心型）
//   hash % 3 == 1: Compact（密集小村）
//   hash % 3 == 2: Street（街道沿い）
// ─────────────────────────────────────────────────────────────────────────────
static void placeVillage(Chunk& chunk, const NoiseGen& noise, uint32_t seed,
                          int chunk_wx, int chunk_wz) {
    constexpr int MAX_REACH = 45;

    int min_cx = (int)std::floor((float)(chunk_wx - MAX_REACH) / VILLAGE_GRID);
    int max_cx = (int)std::floor((float)(chunk_wx + CHUNK_SIZE_X + MAX_REACH) / VILLAGE_GRID);
    int min_cz = (int)std::floor((float)(chunk_wz - MAX_REACH) / VILLAGE_GRID);
    int max_cz = (int)std::floor((float)(chunk_wz + CHUNK_SIZE_Z + MAX_REACH) / VILLAGE_GRID);

    for (int gcx = min_cx; gcx <= max_cx; ++gcx) {
        for (int gcz = min_cz; gcz <= max_cz; ++gcz) {
            int vx, vz;
            if (!getVillageCenter(gcx, gcz, seed, vx, vz)) continue;
            if (!isVillageSuitable(noise, vx, vz))         continue;

            int base_y = computeTerrainHeight(noise, vx, vz);

            // バイオーム素材を決定
            float temp  = noise.getTemperature((float)vx, (float)vz);
            float humid = noise.getHumidity((float)vx, (float)vz);
            float wP, wD, wT, wR, wSw;
            biomeWeights(temp, humid, wP, wD, wT, wR, wSw);
            (void)wSw;
            VillageMat mat = getVillageMat(wT);

            // レイアウトをハッシュで選択
            uint32_t vh = hash3(vx, (int)(seed ^ 0xBEEFCAFEu), vz);
            int layout = (int)(vh % 3u);

            if (layout == 0)
                layoutClassic(chunk, noise, vh, chunk_wx, chunk_wz, vx, vz, base_y, mat);
            else if (layout == 1)
                layoutCompact(chunk, noise, vh, chunk_wx, chunk_wz, vx, vz, base_y, mat);
            else
                layoutStreet(chunk, noise, vh, chunk_wx, chunk_wz, vx, vz, base_y, mat);
        }
    }
}

struct OreClusterSpec {
    BlockType type;
    int grid;
    int min_y;
    int max_y;
    int chance_percent;
    int min_radius_xz;
    int radius_xz_span;
    int min_radius_y;
    int radius_y_span;
    uint32_t salt;
};

static int oreCell(int v, int grid) {
    return (int)std::floor((float)v / (float)grid);
}

static bool inOreCluster(int wx, int wy, int wz, uint32_t seed,
                         const OreClusterSpec& spec) {
    if (wy < spec.min_y || wy > spec.max_y) return false;

    int base_cx = oreCell(wx, spec.grid);
    int base_cz = oreCell(wz, spec.grid);
    for (int cz = base_cz - 1; cz <= base_cz + 1; ++cz) {
        for (int cx = base_cx - 1; cx <= base_cx + 1; ++cx) {
            uint32_t h = hash3(cx, (int)(seed ^ spec.salt), cz);
            if ((int)(h % 100u) >= spec.chance_percent) continue;

            int cy_range = spec.max_y - spec.min_y + 1;
            int center_x = cx * spec.grid + (int)((h >> 8) % (uint32_t)spec.grid);
            int center_z = cz * spec.grid + (int)((h >> 16) % (uint32_t)spec.grid);
            int center_y = spec.min_y + (int)((h >> 24) % (uint32_t)cy_range);

            int rx = spec.min_radius_xz + (int)((h >>  3) % (uint32_t)spec.radius_xz_span);
            int rz = spec.min_radius_xz + (int)((h >> 11) % (uint32_t)spec.radius_xz_span);
            int ry = spec.min_radius_y  + (int)((h >> 19) % (uint32_t)spec.radius_y_span);

            float dx = (float)(wx - center_x) / (float)rx;
            float dy = (float)(wy - center_y) / (float)ry;
            float dz = (float)(wz - center_z) / (float)rz;
            float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 > 1.18f) continue;

            uint32_t edge = hash3(wx, wy ^ (int)spec.salt, wz);
            if (d2 < 0.78f || (edge % 100u) < 58u)
                return true;
        }
    }
    return false;
}

static BlockType oreAt(int wx, int wy, int wz, uint32_t seed) {
    static constexpr OreClusterSpec kDiamond = {
        BlockType::DiamondOre, 48, 5, 28, 24, 2, 3, 1, 3, 0xD1A00D5u
    };
    static constexpr OreClusterSpec kGold = {
        BlockType::GoldOre, 34, 8, 54, 36, 3, 4, 2, 3, 0xA11C0DEu
    };

    if (inOreCluster(wx, wy, wz, seed, kDiamond)) return BlockType::DiamondOre;
    if (inOreCluster(wx, wy, wz, seed, kGold))    return BlockType::GoldOre;
    return BlockType::Stone;
}

// ─────────────────────────────────────────────────────────────────────────────
// carveSurfaceCaveEntrances() — 地表から歩いて入れる洞窟入口を掘る
//
// ノイズだけの地表穴は小さすぎたり地下トンネルと噛み合わないことがあるため、
// 低頻度のグリッド候補から「入口 → 斜め通路 → 小部屋」を決定論的に生成する。
// ─────────────────────────────────────────────────────────────────────────────
static void carveSphere(Chunk& chunk, int chunk_wx, int chunk_wz,
                        float cx, float cy, float cz,
                        float rx, float ry, float rz) {
    int min_x = (int)std::floor(cx - rx - 1.0f);
    int max_x = (int)std::ceil (cx + rx + 1.0f);
    int min_y = (int)std::floor(cy - ry - 1.0f);
    int max_y = (int)std::ceil (cy + ry + 1.0f);
    int min_z = (int)std::floor(cz - rz - 1.0f);
    int max_z = (int)std::ceil (cz + rz + 1.0f);

    for (int wz = min_z; wz <= max_z; ++wz) {
        for (int wx = min_x; wx <= max_x; ++wx) {
            int lx = wx - chunk_wx;
            int lz = wz - chunk_wz;
            if (lx < 0 || lx >= CHUNK_SIZE_X || lz < 0 || lz >= CHUNK_SIZE_Z)
                continue;
            for (int wy = min_y; wy <= max_y; ++wy) {
                if (wy <= 1 || wy >= CHUNK_SIZE_Y - 1) continue;

                float dx = ((float)wx + 0.5f - cx) / rx;
                float dy = ((float)wy + 0.5f - cy) / ry;
                float dz = ((float)wz + 0.5f - cz) / rz;
                if (dx * dx + dy * dy + dz * dz > 1.0f) continue;

                chunk.setBlock(lx, wy, lz, BlockType::Air);
            }
        }
    }
}

static void carveSurfaceCaveEntrances(Chunk& chunk, const NoiseGen& noise,
                                      uint32_t seed, int chunk_wx, int chunk_wz) {
    constexpr int ENTRANCE_GRID = 80;
    constexpr int MAX_REACH = 56;

    int min_cx = (int)std::floor((float)(chunk_wx - MAX_REACH) / ENTRANCE_GRID);
    int max_cx = (int)std::floor((float)(chunk_wx + CHUNK_SIZE_X + MAX_REACH) / ENTRANCE_GRID);
    int min_cz = (int)std::floor((float)(chunk_wz - MAX_REACH) / ENTRANCE_GRID);
    int max_cz = (int)std::floor((float)(chunk_wz + CHUNK_SIZE_Z + MAX_REACH) / ENTRANCE_GRID);

    for (int gcx = min_cx; gcx <= max_cx; ++gcx) {
        for (int gcz = min_cz; gcz <= max_cz; ++gcz) {
            uint32_t h = hash3(gcx, (int)(seed ^ 0xC0A7E11u), gcz);
            if ((h % 100u) >= 68u) continue;

            int mouth_wx = gcx * ENTRANCE_GRID + ENTRANCE_GRID / 2
                         + (int)((h >>  8) % 33u) - 16;
            int mouth_wz = gcz * ENTRANCE_GRID + ENTRANCE_GRID / 2
                         + (int)((h >> 16) % 33u) - 16;
            int surface = computeTerrainHeight(noise, mouth_wx, mouth_wz);
            if (surface <= SEA_LEVEL + 5 || surface >= CHUNK_SIZE_Y - 24)
                continue;

            float angle = ((float)((h >> 24) & 255u) / 255.0f) * 6.2831853f;
            float dir_x = std::cos(angle);
            float dir_z = std::sin(angle);
            float side_x = -dir_z;
            float side_z =  dir_x;
            float depth = 24.0f + (float)((h >> 3) % 13u);
            float run   = 18.0f + (float)((h >> 11) % 15u);
            int steps = (int)std::ceil(std::max(depth, run)) + 8;

            // 入口の口を広めに開け、地表から視認しやすくする。
            carveSphere(chunk, chunk_wx, chunk_wz,
                        (float)mouth_wx + 0.5f,
                        (float)surface - 0.8f,
                        (float)mouth_wz + 0.5f,
                        3.2f, 2.8f, 3.2f);

            float end_x = (float)mouth_wx;
            float end_y = (float)surface - depth;
            float end_z = (float)mouth_wz;

            for (int i = 0; i <= steps; ++i) {
                float t = (float)i / (float)steps;
                float cx = (float)mouth_wx + dir_x * run * t;
                float cz = (float)mouth_wz + dir_z * run * t;
                float cy = (float)surface - 1.0f - depth * t
                         + std::sin(t * 3.1415926f) * 1.8f;
                float mouth_bonus = (t < 0.20f) ? (1.0f - t / 0.20f) * 0.9f : 0.0f;

                carveSphere(chunk, chunk_wx, chunk_wz,
                            cx + 0.5f, cy, cz + 0.5f,
                            2.3f + mouth_bonus, 2.1f, 2.3f + mouth_bonus);
                end_x = cx;
                end_y = cy;
                end_z = cz;
            }

            // 行き止まりに見えないよう、下部に小部屋と横穴を作る。
            carveSphere(chunk, chunk_wx, chunk_wz,
                        end_x + 0.5f, end_y, end_z + 0.5f,
                        4.2f, 3.0f, 4.2f);
            for (int i = 0; i <= 16; ++i) {
                float t = (float)i / 16.0f;
                carveSphere(chunk, chunk_wx, chunk_wz,
                            end_x + side_x * 13.0f * t + 0.5f,
                            end_y - 1.5f + std::sin(t * 3.1415926f) * 1.2f,
                            end_z + side_z * 13.0f * t + 0.5f,
                            2.0f, 1.8f, 2.0f);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// generate() — チャンク1つ分の地形を生成する
// ─────────────────────────────────────────────────────────────────────────────
void TerrainGenerator::generate(Chunk& chunk) const {
    const int world_x = chunk.pos.x * CHUNK_SIZE_X;
    const int world_z = chunk.pos.z * CHUNK_SIZE_Z;

    int heights[CHUNK_SIZE_X + 2][CHUNK_SIZE_Z + 2];

    auto computeHeight = [&](int lx, int lz) -> int {
        return computeTerrainHeight(noise_, world_x + lx, world_z + lz);
    };

    for (int x = -1; x <= CHUNK_SIZE_X; ++x)
        for (int z = -1; z <= CHUNK_SIZE_Z; ++z)
            heights[x + 1][z + 1] = computeHeight(x, z);

    for (int x = 0; x < CHUNK_SIZE_X; ++x) {
        for (int z = 0; z < CHUNK_SIZE_Z; ++z) {
            int surface = heights[x + 1][z + 1];

            int max_diff = std::max({
                std::abs(surface - heights[x + 1][z + 2]),
                std::abs(surface - heights[x + 1][z + 0]),
                std::abs(surface - heights[x + 2][z + 1]),
                std::abs(surface - heights[x + 0][z + 1])
            });

            float wx = (float)(world_x + x);
            float wz = (float)(world_z + z);
            float temp  = noise_.getTemperature(wx, wz);
            float humid = noise_.getHumidity(wx, wz);
            float wP, wD, wT, wR, wSw;
            biomeWeights(temp, humid, wP, wD, wT, wR, wSw);
            (void)wP;

            bool is_desert = wD > 0.35f;
            bool is_snowy  = (wT + wR) > 0.55f;
            bool is_swamp  = wSw > 0.40f;

            BlockType top;
            if      (surface > 88)             top = BlockType::Snow;
            else if (surface <= SEA_LEVEL + 3) top = BlockType::Sand;
            else if (is_desert)                top = BlockType::Sand;
            else if (is_snowy && max_diff < 3) top = BlockType::Snow;
            else if (is_swamp  && max_diff < 4) top = BlockType::Dirt;  // 沼は泥の地表
            else if (max_diff >= 5)            top = BlockType::Stone;
            else if (max_diff >= 2)            top = BlockType::Dirt;
            else                               top = BlockType::Grass;

            int       sub_depth = is_desert ? 4 : 3;
            BlockType sub_t     = is_desert ? BlockType::Sand : BlockType::Dirt;

            for (int y = 0; y < CHUNK_SIZE_Y; ++y) {
                BlockType t = BlockType::Air;

                if (y == 0) {
                    t = BlockType::Stone;
                } else if (y < surface) {
                    int depth = surface - y;
                    t = (depth <= sub_depth) ? sub_t : BlockType::Stone;
                } else if (y == surface) {
                    t = top;
                } else if (y > surface && y <= SEA_LEVEL && surface < SEA_LEVEL) {
                    t = BlockType::Water;
                }

                if (t != BlockType::Air && t != BlockType::Water && y > 5) {
                    float fy    = (float)y;
                    float depth = (float)(surface - y);

                    // ── 地下トンネル層（depth 10以深）─────────────────────────
                    // スパゲッティ方式（n1²+n2²<threshold）で斜めトンネルを生成。
                    // 地表入口は carveSurfaceCaveEntrances() が別パスで掘る。
                    if (depth >= 10.0f && t != BlockType::Air) {
                        float n1 = noise_.getCave     (wx, fy, wz);
                        float n2 = noise_.getCaveHoriz(wx, fy, wz);
                        float fade      = std::min((depth - 10.0f) / 14.0f, 1.0f);
                        float threshold = 0.006f + fade * 0.012f;  // 0.006 → 0.018
                        if (n1 * n1 + n2 * n2 < threshold)
                            t = BlockType::Air;
                    }
                }

                if (t == BlockType::Stone)
                    t = oreAt(world_x + x, y, world_z + z, seed_);

                chunk.setBlock(x, y, z, t);
                if (t == BlockType::Water)
                    chunk.setWaterLevel(x, y, z, 8);
            }
        }
    }

    carveSurfaceCaveEntrances(chunk, noise_, seed_, world_x, world_z);

    // 村を先に生成することで、建物の石/木ブロックが後の木の生成チェック（Grass必須）を阻害する
    placeVillage(chunk, noise_, seed_, world_x, world_z);

    // 木の生成ループで使う村中心座標を事前収集する（木が村に重ならないよう除外するため）
    constexpr int TREE_EXCL_R  = 25;
    constexpr int MAX_VILLAGES = 8;
    int v_wx[MAX_VILLAGES], v_wz[MAX_VILLAGES];
    int v_count = 0;
    {
        int min_cx = (int)std::floor((float)(world_x - TREE_EXCL_R) / VILLAGE_GRID);
        int max_cx = (int)std::floor((float)(world_x + CHUNK_SIZE_X + TREE_EXCL_R) / VILLAGE_GRID);
        int min_cz = (int)std::floor((float)(world_z - TREE_EXCL_R) / VILLAGE_GRID);
        int max_cz = (int)std::floor((float)(world_z + CHUNK_SIZE_Z + TREE_EXCL_R) / VILLAGE_GRID);
        for (int gcx = min_cx; gcx <= max_cx && v_count < MAX_VILLAGES; ++gcx) {
            for (int gcz = min_cz; gcz <= max_cz && v_count < MAX_VILLAGES; ++gcz) {
                int vx, vz;
                if (!getVillageCenter(gcx, gcz, seed_, vx, vz)) continue;
                if (!isVillageSuitable(noise_, vx, vz))         continue;
                v_wx[v_count] = vx;
                v_wz[v_count] = vz;
                ++v_count;
            }
        }
    }

    for (int x = 0; x < CHUNK_SIZE_X; ++x) {
        for (int z = 0; z < CHUNK_SIZE_Z; ++z) {
            int surface = heights[x + 1][z + 1];
            float wx = (float)(world_x + x);
            float wz = (float)(world_z + z);
            float temp  = noise_.getTemperature(wx, wz);
            float humid = noise_.getHumidity(wx, wz);
            float wP, wD, wT, wR, wSw;
            biomeWeights(temp, humid, wP, wD, wT, wR, wSw);

            uint32_t chance = hash3(world_x + x, (int)seed_, world_z + z);

            // ── 砂漠: サボテン ────────────────────────────────────────────
            if (wD > 0.45f) {
                if ((chance % 100u) < 4u && canPlaceCactusAt(chunk, x, z, surface))
                    placeCactus(chunk, x, z, surface, seed_);
                continue;
            }

            // 村の半径内は植生をスキップ
            bool near_village = false;
            int pwx = world_x + x, pwz = world_z + z;
            for (int vi = 0; vi < v_count; ++vi) {
                int dx = pwx - v_wx[vi], dz = pwz - v_wz[vi];
                if (dx*dx + dz*dz <= TREE_EXCL_R * TREE_EXCL_R) { near_village = true; break; }
            }
            if (near_village) continue;

            // ── 沼地: 沼の木（低密度）────────────────────────────────────
            if (wSw > 0.40f) {
                if ((chance % 100u) < 8u && canPlaceTreeAt(chunk, x, z, surface, /*allow_dirt=*/true))
                    placeSwampTree(chunk, x, z, surface, seed_);
                continue;
            }

            // ── 寒冷バイオーム: 松（針葉樹）──────────────────────────────
            if ((wT + wR) > 0.45f) {
                if (!canPlaceTreeAt(chunk, x, z, surface)) continue;
                if ((chance % 100u) < 9u)
                    placePineTree(chunk, x, z, surface, seed_);
                continue;
            }

            // ── 平原: オーク（広葉樹）────────────────────────────────────
            if ((wP + wT) < 0.30f) continue;  // 岩山は木が少ない
            if (!canPlaceTreeAt(chunk, x, z, surface)) continue;
            if ((chance % 100u) < 7u)
                placeTree(chunk, x, z, surface, seed_);
        }
    }

    chunk.is_generated = true;
    chunk.is_dirty     = true;
}
