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

// 4つのバイオームの重みを使って単一の float 値をブレンドする
static float blendBiome(float wP, float wD, float wT, float wR,
                        float vP, float vD, float vT, float vR) {
    return wP*vP + wD*vD + wT*vT + wR*vR;
}

// ─────────────────────────────────────────────────────────────────────────────
// biomeWeights() — 温度・湿度のノイズ値から4バイオームの重みを計算
// ─────────────────────────────────────────────────────────────────────────────
static void biomeWeights(float temp, float humid,
                         float& wP, float& wD, float& wT, float& wR) {
    float t01 = temp  * 0.5f + 0.5f;   // 0=低温, 1=高温
    float h01 = humid * 0.5f + 0.5f;   // 0=乾燥, 1=湿潤
    wP = t01 * h01;
    wD = t01 * (1.0f - h01);
    wT = (1.0f - t01) * h01;
    wR = (1.0f - t01) * (1.0f - h01);
}

// ─────────────────────────────────────────────────────────────────────────────
// computeTerrainHeight() — 任意のワールド座標の地表高さを計算
// ─────────────────────────────────────────────────────────────────────────────
static int computeTerrainHeight(const NoiseGen& noise, int wx, int wz) {
    float fwx = (float)wx, fwz = (float)wz;
    float temp  = noise.getTemperature(fwx, fwz);
    float humid = noise.getHumidity(fwx, fwz);
    float wP, wD, wT, wR;
    biomeWeights(temp, humid, wP, wD, wT, wR);
    float base   = blendBiome(wP, wD, wT, wR, kPlains.base,   kDesert.base,   kTundra.base,   kRocky.base);
    float amp    = blendBiome(wP, wD, wT, wR, kPlains.amp,    kDesert.amp,    kTundra.amp,    kRocky.amp);
    float valley = blendBiome(wP, wD, wT, wR, kPlains.valley, kDesert.valley, kTundra.valley, kRocky.valley);
    float n   = noise.getHeight(fwx, fwz);
    float v   = noise.getValley(fwx, fwz);
    float cut = std::max(0.0f, v) * valley;
    int s = (int)(base + n * amp - cut);
    return std::clamp(s, 2, CHUNK_SIZE_Y - 2);
}

// 指定座標に木を配置できるか判定する。
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

// 村に適した場所か（砂漠・岩山除外、平坦・高さチェック）
static bool isVillageSuitable(const NoiseGen& noise, int vx, int vz) {
    float temp  = noise.getTemperature((float)vx, (float)vz);
    float humid = noise.getHumidity((float)vx, (float)vz);
    float wP, wD, wT, wR;
    biomeWeights(temp, humid, wP, wD, wT, wR);
    if (wD > 0.35f || wR > 0.35f) return false;

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
            float wP, wD, wT, wR;
            biomeWeights(temp, humid, wP, wD, wT, wR);
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
            float wP, wD, wT, wR;
            biomeWeights(temp, humid, wP, wD, wT, wR);

            bool is_desert = wD > 0.35f;
            bool is_snowy  = (wT + wR) > 0.55f;

            BlockType top;
            if      (surface > 88)             top = BlockType::Snow;
            else if (surface <= SEA_LEVEL + 3) top = BlockType::Sand;
            else if (is_desert)                top = BlockType::Sand;
            else if (is_snowy && max_diff < 3) top = BlockType::Snow;
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

                if (t != BlockType::Air && t != BlockType::Water
                        && y > 5 && y < surface - 3) {
                    if (noise_.getCave(wx, (float)y, wz) > 0.55f)
                        t = BlockType::Air;
                }

                chunk.setBlock(x, y, z, t);
                if (t == BlockType::Water)
                    chunk.setWaterLevel(x, y, z, 8);
            }
        }
    }

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
            float wP, wD, wT, wR;
            biomeWeights(temp, humid, wP, wD, wT, wR);

            if ((wP + wT) < 0.35f || wD > 0.45f || wR > 0.40f) continue;
            if (!canPlaceTreeAt(chunk, x, z, surface)) continue;

            // 村の半径内は木を生やさない
            bool near_village = false;
            int pwx = world_x + x, pwz = world_z + z;
            for (int vi = 0; vi < v_count; ++vi) {
                int dx = pwx - v_wx[vi], dz = pwz - v_wz[vi];
                if (dx*dx + dz*dz <= TREE_EXCL_R * TREE_EXCL_R) { near_village = true; break; }
            }
            if (near_village) continue;

            uint32_t chance = hash3(world_x + x, (int)seed_, world_z + z);
            if ((chance % 100u) < 7u)
                placeTree(chunk, x, z, surface, seed_);
        }
    }

    chunk.is_generated = true;
    chunk.is_dirty     = true;
}
