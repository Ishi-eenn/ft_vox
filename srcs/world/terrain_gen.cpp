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

const char* TerrainGenerator::biomeName(BiomeType biome) {
    switch (biome) {
        case BiomeType::Plains:   return "PLAINS";
        case BiomeType::Desert:   return "DESERT";
        case BiomeType::Tundra:   return "TUNDRA";
        case BiomeType::Rocky:    return "ROCKY";
        case BiomeType::Swamp:    return "SWAMP";
        case BiomeType::Mountain: return "MOUNTAIN";
        case BiomeType::Canyon:   return "CANYON";
        case BiomeType::Spring:   return "SPRING";
        case BiomeType::Autumn:   return "AUTUMN";
    }
    return "UNKNOWN";
}

// ─── バイオームパラメーター ───────────────────────────────────────────────────
// base:   地形の基準高さ（ブロック数）
// amp:    ノイズを何倍に増幅するか（大きいほど山が高くなる）
// valley: 谷の深さを決める係数
struct BiomeParams { float base, amp, valley; };
static constexpr BiomeParams kPlains   = {56.0f,  38.0f, 22.0f};
static constexpr BiomeParams kDesert   = {48.0f,  10.0f,  3.0f};
static constexpr BiomeParams kTundra   = {54.0f,  30.0f, 18.0f};
static constexpr BiomeParams kRocky    = {72.0f,  55.0f, 32.0f};
static constexpr BiomeParams kSwamp    = {41.0f,   6.0f,  1.0f}; // 低平地、水没しやすい
static constexpr BiomeParams kMountain = {90.0f,  85.0f, 50.0f}; // 高峰、雪冠、steep
static constexpr BiomeParams kCanyon   = {85.0f,   5.0f,  0.0f}; // 平坦な台地（canyon cut別途）
static constexpr BiomeParams kSpring   = {55.0f,  36.0f, 21.0f}; // 春: Plains系、桜の木
static constexpr BiomeParams kAutumn   = {55.0f,  29.0f, 18.0f}; // 秋: Tundra系、紅葉、雪なし

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

// ─────────────────────────────────────────────────────────────────────────────
// biomeWeights() — 3×3 グリッドで9バイオームを均等に配置する
//
//  温度軸 (3段): Cold / Warm / Hot
//  湿度軸 (3段): Dry  / Med  / Wet
//
//         Dry        Med       Wet
//  Hot  [ Desert ] [ Canyon ] [ Plains ]
//  Warm [ Rocky  ] [ Spring ] [ Swamp  ]
//  Cold [ Mountain] [ Autumn ] [ Tundra ]
//
//  各バイオームは世界の約1/9を占める。
//  variation ノイズ(±0.10)を温度軸にわずかに加えて境界を自然にずらす。
//  バイオーム境界はバイリニア補間で滑らかにブレンドする。
// ─────────────────────────────────────────────────────────────────────────────
static void biomeWeights(float temp, float humid, float variation,
                         float& wP, float& wD, float& wT, float& wR, float& wSw,
                         float& wM, float& wC, float& wSp, float& wAu) {
    // Perlin FBm 2オクターブの分布は三角形型（0付近が最多）。
    // 振幅 1.2 にすると Cold/Warm/Hot がそれぞれ約 33% になる均等分布が得られる。
    float t01 = std::clamp(temp  * 1.2f + 0.5f + variation * 0.07f, 0.0f, 1.0f);
    float h01 = std::clamp(humid * 1.2f + 0.5f,                      0.0f, 1.0f);

    // 3×3 グリッド: [temp_cell][humid_cell] → バイオームインデックス
    // 0=Mountain, 1=Autumn, 2=Tundra,
    // 3=Rocky,    4=Spring, 5=Swamp,
    // 6=Desert,   7=Canyon, 8=Plains
    static const int kGrid[3][3] = {
        {0, 1, 2},  // Cold
        {3, 4, 5},  // Warm
        {6, 7, 8},  // Hot
    };
    float* ws[9] = {&wM, &wAu, &wT, &wR, &wSp, &wSw, &wD, &wC, &wP};
    wP = wD = wT = wR = wSw = wM = wC = wSp = wAu = 0.0f;

    // グリッドセル内の位置（バイリニア補間）
    // 端セル(ti==2 or hi==2)では tf/hf=0 にして隣セルへのはみ出しを防ぐ
    float tc = t01 * 3.0f;
    float hc = h01 * 3.0f;
    int   ti = std::min((int)tc, 2);
    int   hi = std::min((int)hc, 2);
    float tf = (ti < 2) ? (tc - (float)ti) : 0.0f;
    float hf = (hi < 2) ? (hc - (float)hi) : 0.0f;

    auto addW = [&](int t, int h, float w) {
        *ws[kGrid[std::min(t, 2)][std::min(h, 2)]] += w;
    };
    addW(ti,   hi,   (1.0f - tf) * (1.0f - hf));
    addW(ti+1, hi,   tf          * (1.0f - hf));
    addW(ti,   hi+1, (1.0f - tf) * hf         );
    addW(ti+1, hi+1, tf          * hf         );
}

static TerrainGenerator::BiomeType dominantBiome(float wP, float wD, float wT,
                                                  float wR, float wSw, float wM,
                                                  float wC, float wSp, float wAu) {
    TerrainGenerator::BiomeType best = TerrainGenerator::BiomeType::Plains;
    float best_w = wP;
    auto choose = [&](TerrainGenerator::BiomeType biome, float w) {
        if (w > best_w) {
            best = biome;
            best_w = w;
        }
    };
    choose(TerrainGenerator::BiomeType::Desert,   wD);
    choose(TerrainGenerator::BiomeType::Tundra,   wT);
    choose(TerrainGenerator::BiomeType::Rocky,    wR);
    choose(TerrainGenerator::BiomeType::Swamp,    wSw);
    choose(TerrainGenerator::BiomeType::Mountain, wM);
    choose(TerrainGenerator::BiomeType::Canyon,   wC);
    choose(TerrainGenerator::BiomeType::Spring,   wSp);
    choose(TerrainGenerator::BiomeType::Autumn,   wAu);
    return best;
}

TerrainGenerator::BiomeType TerrainGenerator::getBiomeAt(float wx, float wz) const {
    float temp      = noise_.getTemperature(wx, wz);
    float humid     = noise_.getHumidity(wx, wz);
    float variation = noise_.getVariation(wx, wz);
    float wP, wD, wT, wR, wSw, wM, wC, wSp, wAu;
    biomeWeights(temp, humid, variation, wP, wD, wT, wR, wSw, wM, wC, wSp, wAu);
    return dominantBiome(wP, wD, wT, wR, wSw, wM, wC, wSp, wAu);
}

const char* TerrainGenerator::getBiomeNameAt(float wx, float wz) const {
    return biomeName(getBiomeAt(wx, wz));
}

// ─────────────────────────────────────────────────────────────────────────────
// computeTerrainHeight() — 任意のワールド座標の地表高さを計算
// ─────────────────────────────────────────────────────────────────────────────
static int computeTerrainHeight(const NoiseGen& noise, int wx, int wz) {
    float fwx = (float)wx, fwz = (float)wz;
    float temp      = noise.getTemperature(fwx, fwz);
    float humid     = noise.getHumidity(fwx, fwz);
    float variation = noise.getVariation(fwx, fwz);
    float wP, wD, wT, wR, wSw, wM, wC, wSp, wAu;
    biomeWeights(temp, humid, variation, wP, wD, wT, wR, wSw, wM, wC, wSp, wAu);

    float base   = wP*kPlains.base   + wD*kDesert.base   + wT*kTundra.base   + wR*kRocky.base
                 + wSw*kSwamp.base  + wM*kMountain.base + wC*kCanyon.base   + wSp*kSpring.base + wAu*kAutumn.base;
    float amp    = wP*kPlains.amp    + wD*kDesert.amp    + wT*kTundra.amp    + wR*kRocky.amp
                 + wSw*kSwamp.amp   + wM*kMountain.amp  + wC*kCanyon.amp    + wSp*kSpring.amp  + wAu*kAutumn.amp;
    float valley = wP*kPlains.valley + wD*kDesert.valley + wT*kTundra.valley + wR*kRocky.valley
                 + wSw*kSwamp.valley+ wM*kMountain.valley                   + wSp*kSpring.valley + wAu*kAutumn.valley;

    float n   = noise.getHeight(fwx, fwz);
    float v   = noise.getValley(fwx, fwz);
    float vClamped = std::max(0.0f, v);
    float cut = vClamped * valley;

    // Canyon: cubic valley cut でメサ台地と急峻な崖を作る
    // v^3 により、台地の平坦部分はほぼゼロ cut、崖・谷は急激に深くなる
    if (wC > 0.01f) {
        float canyonCut = vClamped * vClamped * vClamped * 42.0f;
        cut = cut * (1.0f - wC) + canyonCut * wC;
    }

    int s = (int)(base + n * amp - cut);
    return std::clamp(s, 2, CHUNK_SIZE_Y - 2);
}

// ─────────────────────────────────────────────────────────────────────────────
// Rivers
//
// Rivers are generated from sparse world-space "stripes" that drift sideways
// with low-frequency value noise. Because the centreline depends only on world
// coordinates and seed, it stays continuous across chunk boundaries.
// ─────────────────────────────────────────────────────────────────────────────
struct RiverInfo {
    bool  active      = false;  // inside river bank influence
    bool  has_water   = false;  // inside wet channel
    int   land_y      = 0;      // lowered bank surface when has_water=false
    int   water_y     = 0;      // top water block
    int   bed_y       = 0;      // solid block directly under the water
    float bank_factor = 0.0f;   // 0 at outside edge, 1 at centre
};

static float smoothHashNoise(uint32_t seed, float x, float z) {
    int ix = (int)std::floor(x);
    int iz = (int)std::floor(z);
    float fx = x - (float)ix;
    float fz = z - (float)iz;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fz = fz * fz * (3.0f - 2.0f * fz);

    auto sample = [&](int sx, int sz) {
        uint32_t h = hash3(sx, (int)seed, sz);
        return ((float)(h & 0x00ffffffu) / 8388607.5f) - 1.0f;
    };

    float a = sample(ix,     iz);
    float b = sample(ix + 1, iz);
    float c = sample(ix,     iz + 1);
    float d = sample(ix + 1, iz + 1);
    float ab = a + (b - a) * fx;
    float cd = c + (d - c) * fx;
    return ab + (cd - ab) * fz;
}

static float riverFbm(uint32_t seed, float x, float z) {
    float sum = 0.0f;
    float amp = 0.55f;
    float norm = 0.0f;
    for (int i = 0; i < 4; ++i) {
        sum += smoothHashNoise(seed + (uint32_t)i * 131u, x, z) * amp;
        norm += amp;
        x *= 2.03f;
        z *= 2.03f;
        amp *= 0.52f;
    }
    return sum / norm;
}

static void evalRiverFamily(float wx, float wz, uint32_t seed, bool vertical,
                            float& best_dist, float& best_width) {
    constexpr float SPACING = 320.0f;

    float across = vertical ? wx : wz;
    float along  = vertical ? wz : wx;
    int base_cell = (int)std::floor(across / SPACING);

    for (int cell = base_cell - 1; cell <= base_cell + 1; ++cell) {
        uint32_t h = hash3(cell, (int)(seed ^ (vertical ? 0x72697658u : 0x7269765Au)),
                           vertical ? 17 : 31);
        if ((h % 100u) >= 72u) continue;

        float jitter = ((float)((h >> 8) & 1023u) / 1023.0f - 0.5f) * SPACING * 0.36f;
        float centre = ((float)cell + 0.5f) * SPACING + jitter;
        float meander =
            riverFbm(seed ^ (vertical ? 0xA771CEu : 0xB771CEu),
                     along * 0.0060f, (float)cell * 4.17f) * 46.0f +
            riverFbm(seed ^ (vertical ? 0xC0FFEEu : 0x51DE5u),
                     along * 0.0210f, (float)cell * 8.31f) * 13.0f;

        float dist = std::abs(across - (centre + meander));
        float width = 5.5f + (float)((h >> 20) & 7u) * 0.65f;
        if (dist < best_dist) {
            best_dist = dist;
            best_width = width;
        }
    }
}

static RiverInfo computeRiver(uint32_t seed, int wx, int wz, int surface) {
    RiverInfo river;
    river.land_y = surface;

    if (surface >= CHUNK_SIZE_Y - 5)
        return river;

    float best_dist = 1.0e9f;
    float water_width = 0.0f;
    evalRiverFamily((float)wx, (float)wz, seed, true,  best_dist, water_width);
    evalRiverFamily((float)wx, (float)wz, seed, false, best_dist, water_width);

    const int water_level = SEA_LEVEL;
    const float bank_span = std::max(10.0f, (float)std::max(0, surface - water_level) * 0.85f);
    const bool coastal = surface <= water_level + 3;
    const float channel_width = coastal ? water_width + 3.0f : water_width;

    if (coastal && best_dist > channel_width)
        return river;

    if (!coastal && best_dist > water_width + bank_span)
        return river;

    river.active = true;
    river.bank_factor = coastal
        ? 1.0f
        : 1.0f - std::clamp(best_dist / (water_width + bank_span), 0.0f, 1.0f);

    if (best_dist <= channel_width) {
        float centre_factor = 1.0f - std::clamp(best_dist / channel_width, 0.0f, 1.0f);
        int depth = 2 + (int)std::round(centre_factor * 3.0f);
        river.has_water = true;
        river.water_y = std::clamp(water_level, 2, CHUNK_SIZE_Y - 3);
        int target_bed = river.water_y - depth;
        river.bed_y = std::max(1, std::min(surface, target_bed));
        river.land_y = river.bed_y;
    } else {
        if (surface <= water_level + 1)
            return RiverInfo{};
        float bank_t = 1.0f - (best_dist - water_width) / bank_span;
        int max_cut = std::max(1, surface - (water_level + 1));
        int bank_cut = (int)std::round(std::pow(bank_t, 1.35f) * (float)max_cut);
        river.land_y = std::max(water_level + 1, surface - bank_cut);
    }
    return river;
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

static bool canPlaceDecorativeAt(const Chunk& chunk, int x, int z, int surface,
                                 bool allow_dirt = false) {
    if (x < 0 || x >= CHUNK_SIZE_X || z < 0 || z >= CHUNK_SIZE_Z) return false;
    if (surface < SEA_LEVEL + 1 || surface + 1 >= CHUNK_SIZE_Y) return false;
    BlockType ground = chunk.getBlock(x, surface, z);
    if (ground != BlockType::Grass && !(allow_dirt && ground == BlockType::Dirt))
        return false;
    return chunk.getBlock(x, surface + 1, z) == BlockType::Air;
}

static void placeDecorative(Chunk& chunk, int x, int z, int surface, BlockType type) {
    if (surface + 1 < CHUNK_SIZE_Y)
        chunk.setBlock(x, surface + 1, z, type);
}

// ─────────────────────────────────────────────────────────────────────────────
// placeLeaf() — 葉1ブロックを密度判定付きで置く共通ヘルパー
//   skip_mod: 5〜12 の整数。per-leaf hash が 0 になる確率でスキップする。
//             skip_mod が大きいほど密（スキップ率 1/skip_mod）。
// ─────────────────────────────────────────────────────────────────────────────
static inline void placeLeaf(Chunk& chunk, int wx, int x, int cy, int wz, int z,
                              int dx, int dz, uint32_t skip_mod, BlockType leaf) {
    if ((hash3(wx + dx, cy, wz + dz) % skip_mod) == 0) return;
    if (chunk.getBlock(x + dx, cy, z + dz) == BlockType::Air)
        chunk.setBlock(x + dx, cy, z + dz, leaf);
}

// ─────────────────────────────────────────────────────────────────────────────
// placeTree() — 広葉樹（オーク）
//
// 変化するパラメータ:
//   trunk_height : 4〜7
//   crown_radius : 2〜3  （王冠部分の横半径）
//   crown_layers : 2〜3  （幹頂点より下に葉を張る層数）
//   skip_mod     : 5〜8  （葉の密度。大きいほど密、小さいほど疎）
// ─────────────────────────────────────────────────────────────────────────────
static void placeTree(Chunk& chunk, int x, int z, int surface, uint32_t seed) {
    int wx = chunk.pos.x * CHUNK_SIZE_X + x;
    int wz = chunk.pos.z * CHUNK_SIZE_Z + z;
    uint32_t h = hash3(wx, surface, wz) ^ (seed * 0x9e3779b9u);

    int trunk_height  = 4 + (int)(h         % 4u);  // 4〜7
    int crown_radius  = 2 + (int)((h >>  4) % 2u);  // 2〜3
    int crown_layers  = 2 + (int)((h >>  6) % 2u);  // 2〜3
    uint32_t skip_mod = 5 + (h >>  8) % 4u;         // 5〜8

    int trunk_top = surface + trunk_height;
    if (trunk_top + 2 >= CHUNK_SIZE_Y) return;

    for (int y = surface + 1; y <= trunk_top; ++y)
        chunk.setBlock(x, y, z, BlockType::Wood);

    // 王冠部（幹頂点から crown_layers 層下まで crown_radius、頂点だけ半径1）
    for (int dy = -crown_layers; dy <= 0; ++dy) {
        int radius = (dy == 0) ? 1 : crown_radius;
        int cy = trunk_top + dy;
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (std::abs(dx) == radius && std::abs(dz) == radius) continue;
                if (dx == 0 && dz == 0 && dy < 0) continue;
                placeLeaf(chunk, wx, x, cy, wz, z, dx, dz, skip_mod, BlockType::Leaves);
            }
        }
    }
    // 上部キャップ（半径1 → 頂点1）
    for (int dy = 1; dy <= 2; ++dy) {
        int cy = trunk_top + dy;
        if (cy >= CHUNK_SIZE_Y) break;
        int radius = (dy == 1) ? 1 : 0;
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx)
                placeLeaf(chunk, wx, x, cy, wz, z, dx, dz, skip_mod, BlockType::Leaves);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// placePineTree() — 針葉樹（寒冷地・岩山バイオーム用）
//
// 変化するパラメータ:
//   trunk_height : 6〜11
//   max_radius   : 2〜4  （最下段の葉の横半径）
//   leaf_tiers   : 5〜7  （葉を張る層数）
//   skip_mod     : 7〜11 （葉の密度）
// ─────────────────────────────────────────────────────────────────────────────
static void placePineTree(Chunk& chunk, int x, int z, int surface, uint32_t seed) {
    int wx = chunk.pos.x * CHUNK_SIZE_X + x;
    int wz = chunk.pos.z * CHUNK_SIZE_Z + z;
    uint32_t h = hash3(wx, surface, wz) ^ (seed * 0xB5297A4Du);

    int trunk_height  = 6  + (int)(h         % 6u);  // 6〜11
    int max_radius    = 2  + (int)((h >>  4) % 3u);  // 2〜4
    int leaf_tiers    = 5  + (int)((h >>  7) % 3u);  // 5〜7
    uint32_t skip_mod = 7u + (h >> 10) % 5u;         // 7〜11（針葉樹は比較的密）

    int trunk_top = surface + trunk_height;
    if (trunk_top + 2 >= CHUNK_SIZE_Y) return;

    for (int y = surface + 1; y <= trunk_top; ++y)
        chunk.setBlock(x, y, z, BlockType::Wood);

    // 階段状に細くなる葉（最下段 max_radius、上へ1ずつ減少）
    int leaf_start = trunk_top - (leaf_tiers - 1);
    for (int dy = 0; dy < leaf_tiers; ++dy) {
        int cy = leaf_start + dy;
        if (cy < 0 || cy >= CHUNK_SIZE_Y) continue;
        // 下から上へ線形に半径を減らす（最下段 max_radius → 最上段 0 or 1）
        int radius = max_radius - (dy * max_radius) / std::max(leaf_tiers - 1, 1);
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (std::abs(dx) == radius && std::abs(dz) == radius) continue;
                placeLeaf(chunk, wx, x, cy, wz, z, dx, dz, skip_mod, BlockType::Leaves);
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
// 変化するパラメータ:
//   trunk_height : 3〜6
//   spread       : 2〜4  （葉の最大横半径）
//   skip_mod     : 3〜6  （沼は葉が疎らに垂れる）
// ─────────────────────────────────────────────────────────────────────────────
static void placeSwampTree(Chunk& chunk, int x, int z, int surface, uint32_t seed) {
    int wx = chunk.pos.x * CHUNK_SIZE_X + x;
    int wz = chunk.pos.z * CHUNK_SIZE_Z + z;
    uint32_t h = hash3(wx, surface + 1, wz) ^ (seed * 0xF1234567u);

    int trunk_height  = 3 + (int)(h         % 4u);  // 3〜6
    int spread        = 2 + (int)((h >>  4) % 3u);  // 2〜4
    uint32_t skip_mod = 3u + (h >>  7) % 4u;        // 3〜6（疎らな垂れ葉）

    int trunk_top = surface + trunk_height;
    if (trunk_top + 3 >= CHUNK_SIZE_Y) return;

    for (int y = surface + 1; y <= trunk_top; ++y)
        chunk.setBlock(x, y, z, BlockType::Wood);

    // 葉の層（-1〜+2）: 下から spread-1, spread, spread-1, spread-2
    for (int dy = -1; dy <= 2; ++dy) {
        int cy = trunk_top + dy;
        if (cy < 0 || cy >= CHUNK_SIZE_Y) continue;
        int radius;
        if      (dy == -1) radius = spread - 1;
        else if (dy ==  0) radius = spread;
        else if (dy ==  1) radius = spread - 1;
        else               radius = std::max(1, spread - 2);
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (dx == 0 && dz == 0 && dy < 0) continue;
                if (std::abs(dx) == radius && std::abs(dz) == radius) continue;
                placeLeaf(chunk, wx, x, cy, wz, z, dx, dz, skip_mod, BlockType::Leaves);
            }
        }
    }
    // 垂れ下がった葉（spread 範囲でランダムに追加）
    for (int dz = -spread; dz <= spread; ++dz) {
        for (int dx = -spread; dx <= spread; ++dx) {
            if (dx == 0 && dz == 0) continue;
            uint32_t rnd = hash3(wx + dx, trunk_top, wz + dz) ^ seed;
            if ((rnd % 3u) == 0) {
                int cy = trunk_top - 1;
                if (cy >= 0 && chunk.getBlock(x + dx, cy, z + dz) == BlockType::Air)
                    chunk.setBlock(x + dx, cy, z + dz, BlockType::Leaves);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// placeSpringTree() — 桜の木（春バイオーム用）
//
// 変化するパラメータ:
//   trunk_height  : 3〜6
//   crown_radius  : 2〜4  （傘の最大半径）
//   skip_mod      : 4〜8  （葉の密度）
// ─────────────────────────────────────────────────────────────────────────────
static void placeSpringTree(Chunk& chunk, int x, int z, int surface, uint32_t seed) {
    int wx = chunk.pos.x * CHUNK_SIZE_X + x;
    int wz = chunk.pos.z * CHUNK_SIZE_Z + z;
    uint32_t h = hash3(wx, surface, wz) ^ (seed * 0x7C4ACDD5u);

    int trunk_height  = 3 + (int)(h         % 4u);  // 3〜6
    int crown_radius  = 2 + (int)((h >>  4) % 3u);  // 2〜4
    uint32_t skip_mod = 4u + (h >>  7) % 5u;        // 4〜8

    int trunk_top = surface + trunk_height;
    if (trunk_top + 3 >= CHUNK_SIZE_Y) return;

    for (int y = surface + 1; y <= trunk_top; ++y)
        chunk.setBlock(x, y, z, BlockType::Wood);

    // 傘状に広がる桜の冠（dy=-1〜+2）: 下寄りで最大半径、上へ向けて収束
    for (int dy = -1; dy <= 2; ++dy) {
        int cy = trunk_top + dy;
        if (cy < 0 || cy >= CHUNK_SIZE_Y) continue;
        int radius;
        if      (dy == -1) radius = crown_radius - 1;
        else if (dy ==  0) radius = crown_radius;
        else if (dy ==  1) radius = crown_radius - 1;
        else               radius = std::max(1, crown_radius - 2);
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (std::abs(dx) == radius && std::abs(dz) == radius) continue;
                if (dx == 0 && dz == 0 && dy < 0) continue;
                placeLeaf(chunk, wx, x, cy, wz, z, dx, dz, skip_mod, BlockType::PinkLeaves);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// placeAutumnTree() — 紅葉の木（秋バイオーム用）
//
// 変化するパラメータ:
//   trunk_height : 4〜8
//   crown_radius : 2〜3
//   crown_layers : 2〜3
//   skip_mod     : 5〜8  （葉の密度）
//   葉色         : OrangeLeaves / Leaves をハッシュでランダムに混在
// ─────────────────────────────────────────────────────────────────────────────
static void placeAutumnTree(Chunk& chunk, int x, int z, int surface, uint32_t seed) {
    int wx = chunk.pos.x * CHUNK_SIZE_X + x;
    int wz = chunk.pos.z * CHUNK_SIZE_Z + z;
    uint32_t h = hash3(wx, surface, wz) ^ (seed * 0xD3AD1337u);

    int trunk_height  = 4 + (int)(h         % 5u);  // 4〜8
    int crown_radius  = 2 + (int)((h >>  4) % 2u);  // 2〜3
    int crown_layers  = 2 + (int)((h >>  6) % 2u);  // 2〜3
    uint32_t skip_mod = 5u + (h >>  8) % 4u;        // 5〜8

    int trunk_top = surface + trunk_height;
    if (trunk_top + 2 >= CHUNK_SIZE_Y) return;

    for (int y = surface + 1; y <= trunk_top; ++y)
        chunk.setBlock(x, y, z, BlockType::Wood);

    auto autumnLeaf = [](int, int, int) -> BlockType {
        return BlockType::OrangeLeaves;
    };

    auto placeAutumnLeaf = [&](int ldx, int lcy, int ldz) {
        if ((hash3(wx + ldx, lcy, wz + ldz) % skip_mod) == 0) return;
        if (chunk.getBlock(x + ldx, lcy, z + ldz) == BlockType::Air)
            chunk.setBlock(x + ldx, lcy, z + ldz, autumnLeaf(ldx, lcy, ldz));
    };

    for (int dy = -crown_layers; dy <= 0; ++dy) {
        int radius = (dy == 0) ? 1 : crown_radius;
        int cy = trunk_top + dy;
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (std::abs(dx) == radius && std::abs(dz) == radius) continue;
                if (dx == 0 && dz == 0 && dy < 0) continue;
                placeAutumnLeaf(dx, cy, dz);
            }
        }
    }
    for (int dy = 1; dy <= 2; ++dy) {
        int cy = trunk_top + dy;
        if (cy >= CHUNK_SIZE_Y) break;
        int radius = (dy == 1) ? 1 : 0;
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx)
                placeAutumnLeaf(dx, cy, dz);
        }
    }
}

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
    BlockType wall;   // 壁ブロック
    BlockType roof;   // 屋根ブロック
    BlockType floor;  // 床ブロック
};

// ツンドラ: 石壁+雪屋根、平原: 丸石壁+板材屋根（Java Minecraft 再現）
static VillageMat getVillageMat(float wT) {
    if (wT > 0.45f) return {BlockType::Stone,       BlockType::Snow,   BlockType::Dirt};
    return           {BlockType::Cobblestone, BlockType::Planks, BlockType::Planks};
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
static bool isVillageSuitable(const NoiseGen& noise, uint32_t seed, int vx, int vz) {
    float temp      = noise.getTemperature((float)vx, (float)vz);
    float humid     = noise.getHumidity((float)vx, (float)vz);
    float variation = noise.getVariation((float)vx, (float)vz);
    float wP, wD, wT, wR, wSw, wM, wC, wSp, wAu;
    biomeWeights(temp, humid, variation, wP, wD, wT, wR, wSw, wM, wC, wSp, wAu);
    (void)wP; (void)wSp; (void)wAu;
    if (wD > 0.30f || wC > 0.30f || wM > 0.30f || wSw > 0.30f) return false;

    int center_h = computeTerrainHeight(noise, vx, vz);
    if (center_h < SEA_LEVEL + 4 || center_h > 80) return false;

    // 川チェック: center_h を近似高さとして5点確認（computeTerrainHeight の追加呼び出しを省略）
    static const int kRiverOffsets[5][2] = {{0,0},{16,0},{-16,0},{0,16},{0,-16}};
    for (auto& o : kRiverOffsets) {
        if (computeRiver(seed, vx + o[0], vz + o[1], center_h).active) return false;
    }

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
            setBlockWorld(chunk, chunk_wx, chunk_wz, vx + dx, base_y, vz + dz, BlockType::GravelPath);
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
                setBlockWorld(chunk, chunk_wx, chunk_wz, wx, base_y + 1, wz, BlockType::Cobblestone);
                setBlockWorld(chunk, chunk_wx, chunk_wz, wx, base_y + 2, wz, BlockType::Cobblestone);
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
        setBlockWorld(chunk, chunk_wx, chunk_wz, x, y, z, BlockType::GravelPath);
        x += sx;
    }
    // Z方向に進む
    while (z != wz2) {
        int y = computeTerrainHeight(noise, x, z);
        setBlockWorld(chunk, chunk_wx, chunk_wz, x, y, z, BlockType::GravelPath);
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
            setBlockWorld(chunk, chunk_wx, chunk_wz, wx, base_y,           wz,
                          wall ? BlockType::Dirt : mat.floor);
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
            setBlockWorld(chunk, chunk_wx, chunk_wz, wx, base_y,           wz,
                          wall ? BlockType::Dirt : mat.floor);
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
            setBlockWorld(chunk, chunk_wx, chunk_wz, wx, base_y,           wz,
                          wall ? BlockType::Dirt : mat.floor);
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

            setBlockWorld(chunk, chunk_wx, chunk_wz, wx, base_y, wz, BlockType::Cobblestone);

            // 壁（Y+1〜+7）
            for (int dy = 1; dy <= 7; ++dy)
                setBlockWorld(chunk, chunk_wx, chunk_wz, wx, base_y + dy, wz,
                              wall ? BlockType::Cobblestone : BlockType::Air);

            // 展望台床（Y+8）
            setBlockWorld(chunk, chunk_wx, chunk_wz, wx, base_y + 8, wz, BlockType::Cobblestone);

            // 胸壁（Y+9、外周のみ）
            if (wall)
                setBlockWorld(chunk, chunk_wx, chunk_wz, wx, base_y + 9, wz, BlockType::Cobblestone);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// placeFarm() — 農地（6×8の土の畑＋四隅に木の支柱）
// ─────────────────────────────────────────────────────────────────────────────
static void placeFarm(Chunk& chunk, int chunk_wx, int chunk_wz,
                      int origin_wx, int origin_wz, int base_y) {
    constexpr int FW = 6, FD = 8;

    for (int dz = 0; dz < FD; ++dz) {
        for (int dx = 0; dx < FW; ++dx) {
            bool edge = (dx == 0 || dx == FW-1 || dz == 0 || dz == FD-1);
            setBlockWorld(chunk, chunk_wx, chunk_wz,
                         origin_wx + dx, base_y, origin_wz + dz, BlockType::Farmland);
            if (!edge)
                setBlockWorld(chunk, chunk_wx, chunk_wz,
                             origin_wx + dx, base_y + 1, origin_wz + dz, BlockType::Wheat);
        }
    }

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
// 有機的な道路ベース村生成（Java Minecraft 再現）
//
// 井戸を起点に4方向へ道路を伸ばし、道路沿いに建物を配置する。
// 道路はランダムに垂直分岐し、有機的な村の形を生む。
// ─────────────────────────────────────────────────────────────────────────────

enum class VillDir : uint8_t { NORTH=0, SOUTH=1, EAST=2, WEST=3 };
enum class VillBType : uint8_t { SmallHut=0, House=1, LargeHouse=2, Tower=3, Farm=4 };

// フットプリント [SmallHut, House, LargeHouse, Tower, Farm]
static constexpr int kBFW[5] = {5, 7, 9, 4, 6};  // X幅
static constexpr int kBFD[5] = {4, 5, 7, 4, 8};  // Z奥行き

// 重みテーブル (Java weights を集約)
// SmallHut=3, House=35(House1+House2), LargeHouse=27(House4Garden+Hall+House3), Tower=20(Church), Farm=6
static constexpr int kBWeight[5]   = {3, 35, 27, 20, 6};
static constexpr int kBWeightTotal = 91;

struct VillBuilding {
    int       ox, oz;
    VillBType type;
    int8_t    orient;     // 0=-Z扉, 1=+X扉, 2=+Z扉, 3=-X扉
    int8_t    road_idx;
};

struct VillRoad {
    int     sx, sz;
    VillDir dir;
    int     length;
    int     depth;
};

struct VillageLayout {
    VillBuilding buildings[48];
    int          n_buildings;
    VillRoad     roads[32];
    int          n_roads;
};

struct VillRNG {
    uint64_t state;
    explicit VillRNG(uint32_t seed) {
        state = (uint64_t)seed * 6364136223846793005ULL + 1442695040888963407ULL;
        next(); next();
    }
    uint32_t next() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return (uint32_t)(state >> 33);
    }
    int nextInt(int n) { return n <= 1 ? 0 : (int)(next() % (uint32_t)n); }
};

static VillDir perpLeft(VillDir d) {
    static const VillDir t[4] = {VillDir::WEST, VillDir::EAST, VillDir::NORTH, VillDir::SOUTH};
    return t[(int)d];
}
static VillDir perpRight(VillDir d) {
    static const VillDir t[4] = {VillDir::EAST, VillDir::WEST, VillDir::SOUTH, VillDir::NORTH};
    return t[(int)d];
}

static VillBType selectBType(VillRNG& rng) {
    int r = rng.nextInt(kBWeightTotal);
    for (int i = 0; i < 5; ++i) {
        r -= kBWeight[i];
        if (r < 0) return (VillBType)i;
    }
    return VillBType::House;
}

static bool overlapsAny(const VillageLayout& lay, int ox, int oz, int fw, int fd) {
    for (int k = 0; k < lay.n_buildings; ++k) {
        const VillBuilding& b = lay.buildings[k];
        int bfw = kBFW[(int)b.type], bfd = kBFD[(int)b.type];
        if (ox + fw <= b.ox || b.ox + bfw <= ox) continue;
        if (oz + fd <= b.oz || b.oz + bfd <= oz) continue;
        return true;
    }
    return false;
}

// 左側建物原点 (Java getNextComponentNN 相当)
static void leftBuildingOrigin(VillDir dir, int sx, int sz, int i,
                                VillBType t, int& ox, int& oz, int& orient) {
    int W = kBFW[(int)t], D = kBFD[(int)t];
    switch (dir) {
    case VillDir::NORTH:  ox = sx - W;         oz = sz - i;          orient = 1; break;
    case VillDir::SOUTH:  ox = sx + 1;          oz = sz + i - D + 1;  orient = 3; break;
    case VillDir::EAST:   ox = sx + i;          oz = sz - D;          orient = 2; break;
    case VillDir::WEST:   ox = sx - i - W + 1; oz = sz + 1;          orient = 0; break;
    }
}

// 右側建物原点 (Java getNextComponentPP 相当)
static void rightBuildingOrigin(VillDir dir, int sx, int sz, int i,
                                 VillBType t, int& ox, int& oz, int& orient) {
    int W = kBFW[(int)t], D = kBFD[(int)t];
    switch (dir) {
    case VillDir::NORTH:  ox = sx + 1;          oz = sz - i;          orient = 3; break;
    case VillDir::SOUTH:  ox = sx - W;          oz = sz + i - D + 1;  orient = 1; break;
    case VillDir::EAST:   ox = sx + i;          oz = sz + 1;          orient = 0; break;
    case VillDir::WEST:   ox = sx - i - W + 1; oz = sz - D;          orient = 2; break;
    }
}

static VillageLayout buildOrganicVillage(int vx, int vz, uint32_t seed) {
    VillageLayout lay{};
    VillRNG rng(hash3(vx, (int)(seed ^ 0xC0FFEE00u), vz));

    VillRoad road_queue[32];
    int queue_head = 0, queue_tail = 0;

    auto enqueue = [&](int sx, int sz, VillDir dir, int depth) {
        if (queue_tail >= 32) return;
        int len = 14 + rng.nextInt(15);
        road_queue[queue_tail++] = {sx, sz, dir, len, depth};
    };

    enqueue(vx, vz, VillDir::NORTH, 0);
    enqueue(vx, vz, VillDir::SOUTH, 0);
    enqueue(vx, vz, VillDir::EAST,  0);
    enqueue(vx, vz, VillDir::WEST,  0);

    while (queue_head < queue_tail) {
        if (lay.n_roads >= 32) break;
        VillRoad road = road_queue[queue_head++];
        lay.roads[lay.n_roads++] = road;
        int road_idx = lay.n_roads - 1;

        bool placed_any = false;

        // 左側建物ループ (Java: rand.nextInt(5) から length-8 まで 2+rand(5) 刻み)
        for (int i = rng.nextInt(5); i < road.length - 6; ) {
            VillBType bt = selectBType(rng);  // 衝突時も消費
            int fw = kBFW[(int)bt], fd = kBFD[(int)bt];
            int ox, oz, orient;
            leftBuildingOrigin(road.dir, road.sx, road.sz, i, bt, ox, oz, orient);
            int cx = ox + fw/2, cz = oz + fd/2;
            if (std::abs(cx - vx) <= 58 && std::abs(cz - vz) <= 58
                && !overlapsAny(lay, ox, oz, fw, fd)
                && lay.n_buildings < 48) {
                lay.buildings[lay.n_buildings++] = {ox, oz, bt, (int8_t)orient, (int8_t)road_idx};
                i += std::max(fw, fd) + 2 + rng.nextInt(5);
                placed_any = true;
            } else {
                i += 2 + rng.nextInt(5);
            }
        }

        // 右側建物ループ
        for (int j = rng.nextInt(5); j < road.length - 6; ) {
            VillBType bt = selectBType(rng);  // 衝突時も消費
            int fw = kBFW[(int)bt], fd = kBFD[(int)bt];
            int ox, oz, orient;
            rightBuildingOrigin(road.dir, road.sx, road.sz, j, bt, ox, oz, orient);
            int cx = ox + fw/2, cz = oz + fd/2;
            if (std::abs(cx - vx) <= 58 && std::abs(cz - vz) <= 58
                && !overlapsAny(lay, ox, oz, fw, fd)
                && lay.n_buildings < 48) {
                lay.buildings[lay.n_buildings++] = {ox, oz, bt, (int8_t)orient, (int8_t)road_idx};
                j += std::max(fw, fd) + 2 + rng.nextInt(5);
                placed_any = true;
            } else {
                j += 2 + rng.nextInt(5);
            }
        }

        // 垂直分岐 (Java: placed_any && rand.nextInt(3) > 0、2回独立)
        if (placed_any && road.depth < 2) {
            int ex = road.sx, ez = road.sz;
            switch (road.dir) {
            case VillDir::NORTH: ez -= road.length; break;
            case VillDir::SOUTH: ez += road.length; break;
            case VillDir::EAST:  ex += road.length; break;
            case VillDir::WEST:  ex -= road.length; break;
            }
            if (rng.nextInt(3) > 0) enqueue(ex, ez, perpLeft(road.dir),  road.depth + 1);
            if (rng.nextInt(3) > 0) enqueue(ex, ez, perpRight(road.dir), road.depth + 1);
        }
    }

    return lay;
}

static void renderVillage(Chunk& chunk, const NoiseGen& noise,
                           int chunk_wx, int chunk_wz,
                           const VillageLayout& lay, int vx, int vz) {
    float temp      = noise.getTemperature((float)vx, (float)vz);
    float humid     = noise.getHumidity((float)vx, (float)vz);
    float variation = noise.getVariation((float)vx, (float)vz);
    float wP, wD, wT, wR, wSw, wM, wC, wSp, wAu;
    biomeWeights(temp, humid, variation, wP, wD, wT, wR, wSw, wM, wC, wSp, wAu);
    (void)wP; (void)wD; (void)wR; (void)wSw; (void)wM; (void)wC; (void)wSp; (void)wAu;
    (void)humid; (void)variation;
    VillageMat mat = getVillageMat(wT);

    int base_y = computeTerrainHeight(noise, vx, vz);

    placePlaza(chunk, chunk_wx, chunk_wz, vx, vz, base_y, 3);
    placeWell (chunk, chunk_wx, chunk_wz, vx, vz, base_y);

    // 道路
    for (int ri = 0; ri < lay.n_roads; ++ri) {
        const VillRoad& r = lay.roads[ri];
        int ex = r.sx, ez = r.sz;
        switch (r.dir) {
        case VillDir::NORTH: ez -= r.length; break;
        case VillDir::SOUTH: ez += r.length; break;
        case VillDir::EAST:  ex += r.length; break;
        case VillDir::WEST:  ex -= r.length; break;
        }
        placePath(chunk, noise, chunk_wx, chunk_wz, r.sx, r.sz, ex, ez);
    }

    // 建物
    for (int bi = 0; bi < lay.n_buildings; ++bi) {
        const VillBuilding& b = lay.buildings[bi];
        int fw = kBFW[(int)b.type], fd = kBFD[(int)b.type];

        if (b.ox + fw <= chunk_wx || b.ox >= chunk_wx + CHUNK_SIZE_X) continue;
        if (b.oz + fd <= chunk_wz || b.oz >= chunk_wz + CHUNK_SIZE_Z) continue;

        int bb = computeFootprintBase(noise, b.ox, b.oz, fw, fd);
        levelBuilding(chunk, noise, chunk_wx, chunk_wz, b.ox, b.oz, fw, fd, bb);

        // ドア位置から親道路中点へパス接続
        int door_wx, door_wz;
        switch (b.orient) {
        case 0:  door_wx = b.ox + fw/2;   door_wz = b.oz;          break;
        case 1:  door_wx = b.ox + fw - 1; door_wz = b.oz + fd/2;   break;
        case 2:  door_wx = b.ox + fw/2;   door_wz = b.oz + fd - 1; break;
        default: door_wx = b.ox;          door_wz = b.oz + fd/2;   break;
        }
        if (b.road_idx >= 0 && b.road_idx < lay.n_roads) {
            const VillRoad& r = lay.roads[(int)b.road_idx];
            int mx = r.sx, mz = r.sz;
            int half = r.length / 2;
            switch (r.dir) {
            case VillDir::NORTH: mz -= half; break;
            case VillDir::SOUTH: mz += half; break;
            case VillDir::EAST:  mx += half; break;
            case VillDir::WEST:  mx -= half; break;
            }
            placePath(chunk, noise, chunk_wx, chunk_wz, door_wx, door_wz, mx, mz);
        }

        switch (b.type) {
        case VillBType::SmallHut:
            placeSmallHut  (chunk, chunk_wx, chunk_wz, b.ox, b.oz, bb, b.orient, mat); break;
        case VillBType::House:
            placeHouse     (chunk, chunk_wx, chunk_wz, b.ox, b.oz, bb, b.orient, mat); break;
        case VillBType::LargeHouse:
            placeLargeHouse(chunk, chunk_wx, chunk_wz, b.ox, b.oz, bb, b.orient, mat); break;
        case VillBType::Tower:
            placeTower     (chunk, chunk_wx, chunk_wz, b.ox, b.oz, bb, mat); break;
        case VillBType::Farm:
            placeFarm      (chunk, chunk_wx, chunk_wz, b.ox, b.oz, bb); break;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// placeVillage() — 村全体を配置するマスター関数
// ─────────────────────────────────────────────────────────────────────────────
static void placeVillage(Chunk& chunk, const NoiseGen& noise, uint32_t seed,
                          int chunk_wx, int chunk_wz) {
    constexpr int MAX_REACH = 60;

    int min_cx = (int)std::floor((float)(chunk_wx - MAX_REACH) / VILLAGE_GRID);
    int max_cx = (int)std::floor((float)(chunk_wx + CHUNK_SIZE_X + MAX_REACH) / VILLAGE_GRID);
    int min_cz = (int)std::floor((float)(chunk_wz - MAX_REACH) / VILLAGE_GRID);
    int max_cz = (int)std::floor((float)(chunk_wz + CHUNK_SIZE_Z + MAX_REACH) / VILLAGE_GRID);

    for (int gcx = min_cx; gcx <= max_cx; ++gcx) {
        for (int gcz = min_cz; gcz <= max_cz; ++gcz) {
            int vx, vz;
            if (!getVillageCenter(gcx, gcz, seed, vx, vz)) continue;
            if (!isVillageSuitable(noise, seed, vx, vz))   continue;

            VillageLayout lay = buildOrganicVillage(vx, vz, seed);
            renderVillage(chunk, noise, chunk_wx, chunk_wz, lay, vx, vz);
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
                        float rx, float ry, float rz,
                        const NoiseGen* noise = nullptr,
                        uint32_t seed = 0) {
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

            RiverInfo river;
            bool protect_river_surface = false;
            if (noise) {
                int surface = computeTerrainHeight(*noise, wx, wz);
                river = computeRiver(seed, wx, wz, surface);
                protect_river_surface = river.active;
            }

            for (int wy = min_y; wy <= max_y; ++wy) {
                if (wy <= 1 || wy >= CHUNK_SIZE_Y - 1) continue;
                if (protect_river_surface && wy >= river.land_y - 4)
                    continue;

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
                        3.2f, 2.8f, 3.2f, &noise, seed);

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
                            2.3f + mouth_bonus, 2.1f, 2.3f + mouth_bonus,
                            &noise, seed);
                end_x = cx;
                end_y = cy;
                end_z = cz;
            }

            // 行き止まりに見えないよう、下部に小部屋と横穴を作る。
            carveSphere(chunk, chunk_wx, chunk_wz,
                        end_x + 0.5f, end_y, end_z + 0.5f,
                        4.2f, 3.0f, 4.2f, &noise, seed);
            for (int i = 0; i <= 16; ++i) {
                float t = (float)i / 16.0f;
                carveSphere(chunk, chunk_wx, chunk_wz,
                            end_x + side_x * 13.0f * t + 0.5f,
                            end_y - 1.5f + std::sin(t * 3.1415926f) * 1.2f,
                            end_z + side_z * 13.0f * t + 0.5f,
                            2.0f, 1.8f, 2.0f, &noise, seed);
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
            RiverInfo river = computeRiver(seed_, world_x + x, world_z + z, surface);
            int land_surface = river.active ? river.land_y : surface;

            int max_diff = std::max({
                std::abs(surface - heights[x + 1][z + 2]),
                std::abs(surface - heights[x + 1][z + 0]),
                std::abs(surface - heights[x + 2][z + 1]),
                std::abs(surface - heights[x + 0][z + 1])
            });

            float wx = (float)(world_x + x);
            float wz = (float)(world_z + z);
            float temp      = noise_.getTemperature(wx, wz);
            float humid     = noise_.getHumidity(wx, wz);
            float variation = noise_.getVariation(wx, wz);
            float wP, wD, wT, wR, wSw, wM, wC, wSp, wAu;
            biomeWeights(temp, humid, variation, wP, wD, wT, wR, wSw, wM, wC, wSp, wAu);

            TerrainGenerator::BiomeType biome =
                dominantBiome(wP, wD, wT, wR, wSw, wM, wC, wSp, wAu);
            bool is_desert   = biome == TerrainGenerator::BiomeType::Desert;
            bool is_snowy    = biome == TerrainGenerator::BiomeType::Tundra ||
                               biome == TerrainGenerator::BiomeType::Mountain;
            bool is_swamp    = biome == TerrainGenerator::BiomeType::Swamp;
            bool is_mountain = biome == TerrainGenerator::BiomeType::Mountain;
            bool is_canyon   = biome == TerrainGenerator::BiomeType::Canyon;

            BlockType top;
            if      (is_mountain && surface > 78) top = BlockType::Snow;
            else if (surface > 88)             top = BlockType::Snow;
            else if (surface <= SEA_LEVEL + 3) top = BlockType::Sand;
            else if (is_desert || is_canyon)   top = BlockType::Sand;
            else if (is_snowy && max_diff < 3) top = BlockType::Snow;
            else if (is_swamp  && max_diff < 4) top = BlockType::Dirt;
            else if (max_diff >= 5)            top = BlockType::Stone;
            else if (max_diff >= 2)            top = BlockType::Dirt;
            else                               top = BlockType::Grass;

            int       sub_depth = (is_desert || is_canyon) ? 4 : 3;
            BlockType sub_t     = (is_desert || is_canyon) ? BlockType::Sand : BlockType::Dirt;

            for (int y = 0; y < CHUNK_SIZE_Y; ++y) {
                BlockType t = BlockType::Air;

                if (river.has_water) {
                    if (y == 0) {
                        t = BlockType::Stone;
                    } else if (y < river.bed_y) {
                        int depth = river.bed_y - y;
                        t = (depth <= 3) ? BlockType::Dirt : BlockType::Stone;
                    } else if (y == river.bed_y) {
                        t = BlockType::Sand;
                    } else if (y <= river.water_y) {
                        t = BlockType::Water;
                    }
                } else if (y == 0) {
                    t = BlockType::Stone;
                } else if (y < land_surface) {
                    int depth = land_surface - y;
                    t = (depth <= sub_depth) ? sub_t : BlockType::Stone;
                } else if (y == land_surface) {
                    t = top;
                } else if (y > surface && y <= SEA_LEVEL && surface < SEA_LEVEL) {
                    t = BlockType::Water;
                }

                if (!river.active && t != BlockType::Air && t != BlockType::Water && y > 5) {
                    float fy    = (float)y;
                    float depth = (float)(land_surface - y);

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
                if (!isVillageSuitable(noise_, seed_, vx, vz))  continue;
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
            float temp      = noise_.getTemperature(wx, wz);
            float humid     = noise_.getHumidity(wx, wz);
            float variation = noise_.getVariation(wx, wz);
            float wP, wD, wT, wR, wSw, wM, wC, wSp, wAu;
            biomeWeights(temp, humid, variation, wP, wD, wT, wR, wSw, wM, wC, wSp, wAu);
            TerrainGenerator::BiomeType biome =
                dominantBiome(wP, wD, wT, wR, wSw, wM, wC, wSp, wAu);

            uint32_t chance = hash3(world_x + x, (int)seed_, world_z + z);

            // ── 山岳: 植生なし ────────────────────────────────────────────
            if (biome == TerrainGenerator::BiomeType::Mountain) continue;

            // ── 砂漠: サボテン ────────────────────────────────────────────
            if (biome == TerrainGenerator::BiomeType::Desert) {
                if ((chance % 100u) < 4u && canPlaceCactusAt(chunk, x, z, surface))
                    placeCactus(chunk, x, z, surface, seed_);
                continue;
            }

            // ── 峡谷: サボテンのみ ────────────────────────────────────────
            if (biome == TerrainGenerator::BiomeType::Canyon) {
                if ((chance % 100u) < 3u && canPlaceCactusAt(chunk, x, z, surface))
                    placeCactus(chunk, x, z, surface, seed_);
                continue;
            }

            // 村の半径内は植生をスキップ（全バイオーム共通・Spring/Autumn含む）
            {
                int pwx = world_x + x, pwz = world_z + z;
                bool near_village = false;
                for (int vi = 0; vi < v_count; ++vi) {
                    int ddx = pwx - v_wx[vi], ddz = pwz - v_wz[vi];
                    if (ddx*ddx + ddz*ddz <= TREE_EXCL_R * TREE_EXCL_R) { near_village = true; break; }
                }
                if (near_village) continue;
            }

            // ── 春バイオーム: 桜の木と花 ──────────────────────────────────
            if (biome == TerrainGenerator::BiomeType::Spring) {
                if ((chance % 100u) < 10u && canPlaceTreeAt(chunk, x, z, surface))
                    placeSpringTree(chunk, x, z, surface, seed_);
                else if ((chance >> 8) % 100u < 18u &&
                         canPlaceDecorativeAt(chunk, x, z, surface))
                    placeDecorative(chunk, x, z, surface, BlockType::Flower);
                else if ((chance >> 16) % 100u < 12u &&
                         canPlaceDecorativeAt(chunk, x, z, surface))
                    placeDecorative(chunk, x, z, surface, BlockType::ShortGrass);
                continue;
            }

            // ── 秋バイオーム: 紅葉の木 ────────────────────────────────────
            if (biome == TerrainGenerator::BiomeType::Autumn) {
                if ((chance % 100u) < 8u && canPlaceTreeAt(chunk, x, z, surface))
                    placeAutumnTree(chunk, x, z, surface, seed_);
                else if ((chance >> 8) % 100u < 10u &&
                         canPlaceDecorativeAt(chunk, x, z, surface))
                    placeDecorative(chunk, x, z, surface, BlockType::ShortGrass);
                continue;
            }

            // ── 沼地: 沼の木（低密度）────────────────────────────────────
            if (biome == TerrainGenerator::BiomeType::Swamp) {
                if ((chance % 100u) < 8u && canPlaceTreeAt(chunk, x, z, surface, /*allow_dirt=*/true))
                    placeSwampTree(chunk, x, z, surface, seed_);
                else if ((chance >> 8) % 100u < 2u &&
                         canPlaceDecorativeAt(chunk, x, z, surface, /*allow_dirt=*/true))
                    placeDecorative(chunk, x, z, surface, BlockType::Mushroom);
                else if ((chance >> 16) % 100u < 10u &&
                         canPlaceDecorativeAt(chunk, x, z, surface, /*allow_dirt=*/true))
                    placeDecorative(chunk, x, z, surface, BlockType::ShortGrass);
                continue;
            }

            // ── 寒冷バイオーム(Tundra・Rocky): 松（針葉樹）───────────────
            if (biome == TerrainGenerator::BiomeType::Tundra ||
                biome == TerrainGenerator::BiomeType::Rocky) {
                if ((chance % 100u) < 9u && canPlaceTreeAt(chunk, x, z, surface))
                    placePineTree(chunk, x, z, surface, seed_);
                else if ((chance >> 8) % 100u < 1u &&
                         canPlaceDecorativeAt(chunk, x, z, surface))
                    placeDecorative(chunk, x, z, surface, BlockType::Mushroom);
                else if ((chance >> 16) % 100u < 6u &&
                         canPlaceDecorativeAt(chunk, x, z, surface))
                    placeDecorative(chunk, x, z, surface, BlockType::ShortGrass);
                continue;
            }

            // ── 平原: オーク（広葉樹）────────────────────────────────────
            if (biome != TerrainGenerator::BiomeType::Plains) continue;
            if ((chance % 100u) < 7u && canPlaceTreeAt(chunk, x, z, surface))
                placeTree(chunk, x, z, surface, seed_);
            else if ((chance >> 8) % 100u < 2u &&
                     canPlaceDecorativeAt(chunk, x, z, surface))
                placeDecorative(chunk, x, z, surface, BlockType::Flower);
            else if ((chance >> 16) % 100u < 16u &&
                     canPlaceDecorativeAt(chunk, x, z, surface))
                placeDecorative(chunk, x, z, surface, BlockType::ShortGrass);
        }
    }

    chunk.is_generated = true;
    chunk.is_dirty     = true;
}
