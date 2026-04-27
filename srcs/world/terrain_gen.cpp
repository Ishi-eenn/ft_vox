#include "world/terrain_gen.hpp"
#include <algorithm>
#include <cmath>

void TerrainGenerator::setSeed(uint32_t seed) {
    seed_ = seed;
    noise_.setSeed(seed);
}

// ── Biome parameters ─────────────────────────────────────────────────────────
//   Axes: temperature (hot/cold) × humidity (wet/dry)
//
//              Dry              Wet
//   Hot:    Desert           Plains
//   Cold:   Rocky            Tundra
//
struct BiomeParams { float base, amp, valley; };
static constexpr BiomeParams kPlains = {56.0f, 38.0f, 22.0f};
static constexpr BiomeParams kDesert = {48.0f, 10.0f,  3.0f};
static constexpr BiomeParams kTundra = {54.0f, 30.0f, 18.0f};
static constexpr BiomeParams kRocky  = {72.0f, 55.0f, 32.0f};

// Bilinear blend of a single float parameter across four biome corners.
static float blendBiome(float wP, float wD, float wT, float wR,
                        float vP, float vD, float vT, float vR) {
    return wP*vP + wD*vD + wT*vT + wR*vR;
}

// Compute biome weights from raw temperature/humidity noise values.
static void biomeWeights(float temp, float humid,
                         float& wP, float& wD, float& wT, float& wR) {
    float t01 = temp  * 0.5f + 0.5f;   // 0=cold, 1=hot
    float h01 = humid * 0.5f + 0.5f;   // 0=dry,  1=wet
    wP = t01 * h01;                     // Plains  (hot+wet)
    wD = t01 * (1.0f - h01);           // Desert  (hot+dry)
    wT = (1.0f - t01) * h01;           // Tundra  (cold+wet)
    wR = (1.0f - t01) * (1.0f - h01); // Rocky   (cold+dry)
}

void TerrainGenerator::generate(Chunk& chunk) const {
    const int world_x = chunk.pos.x * CHUNK_SIZE_X;
    const int world_z = chunk.pos.z * CHUNK_SIZE_Z;

    // Pre-compute surface heights with 1-block border for slope detection.
    // Biome blending is baked into the height formula.
    int heights[CHUNK_SIZE_X + 2][CHUNK_SIZE_Z + 2];

    auto computeHeight = [&](int lx, int lz) -> int {
        float wx = (float)(world_x + lx);
        float wz = (float)(world_z + lz);

        float temp  = noise_.getTemperature(wx, wz);
        float humid = noise_.getHumidity(wx, wz);
        float wP, wD, wT, wR;
        biomeWeights(temp, humid, wP, wD, wT, wR);

        float base   = blendBiome(wP, wD, wT, wR, kPlains.base,   kDesert.base,   kTundra.base,   kRocky.base);
        float amp    = blendBiome(wP, wD, wT, wR, kPlains.amp,    kDesert.amp,    kTundra.amp,    kRocky.amp);
        float valley = blendBiome(wP, wD, wT, wR, kPlains.valley, kDesert.valley, kTundra.valley, kRocky.valley);

        float n   = noise_.getHeight(wx, wz);
        float v   = noise_.getValley(wx, wz);
        float cut = std::max(0.0f, v) * valley;
        int   s   = (int)(base + n * amp - cut);
        return std::clamp(s, 2, CHUNK_SIZE_Y - 2);
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

            // Biome weights at this column
            float wx = (float)(world_x + x);
            float wz = (float)(world_z + z);
            float temp  = noise_.getTemperature(wx, wz);
            float humid = noise_.getHumidity(wx, wz);
            float wP, wD, wT, wR;
            biomeWeights(temp, humid, wP, wD, wT, wR);

            bool is_desert = wD > 0.35f;
            bool is_snowy  = (wT + wR) > 0.55f;

            // Surface block selection
            BlockType top;
            if      (surface > 88)             top = BlockType::Snow;   // high altitude
            else if (surface <= SEA_LEVEL + 3) top = BlockType::Sand;   // beach / underwater
            else if (is_desert)                top = BlockType::Sand;
            else if (is_snowy && max_diff < 3) top = BlockType::Snow;
            else if (max_diff >= 5)            top = BlockType::Stone;
            else if (max_diff >= 2)            top = BlockType::Dirt;
            else                               top = BlockType::Grass;

            // Sub-surface depth: desert has 4 sand layers, others 3 dirt layers
            int  sub_depth  = is_desert ? 4 : 3;
            BlockType sub_t = is_desert ? BlockType::Sand : BlockType::Dirt;

            for (int y = 0; y < CHUNK_SIZE_Y; ++y) {
                BlockType t = BlockType::Air;

                if (y == 0) {
                    t = BlockType::Stone;
                } else if (y < surface) {
                    int depth = surface - y;   // 1 = just below surface
                    t = (depth <= sub_depth) ? sub_t : BlockType::Stone;
                } else if (y == surface) {
                    t = top;
                } else if (y > surface && y <= SEA_LEVEL && surface < SEA_LEVEL) {
                    t = BlockType::Water;
                }

                // Cave carving (only deep solid rock, never water zone)
                if (t != BlockType::Air && t != BlockType::Water
                        && y > 5 && y < surface - 3) {
                    if (noise_.getCave(wx, (float)y, wz) > 0.55f)
                        t = BlockType::Air;
                }

                chunk.setBlock(x, y, z, t);
            }
        }
    }

    chunk.is_generated = true;
    chunk.is_dirty     = true;
}
