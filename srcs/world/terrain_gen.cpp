#include "world/terrain_gen.hpp"
#include <algorithm>
#include <cmath>

void TerrainGenerator::setSeed(uint32_t seed) {
    seed_ = seed;
    noise_.setSeed(seed);
}

void TerrainGenerator::generate(Chunk& chunk) const {
    const int world_x = chunk.pos.x * CHUNK_SIZE_X;
    const int world_z = chunk.pos.z * CHUNK_SIZE_Z;

    // Pre-compute surface heights with 1-block border for slope detection
    int heights[CHUNK_SIZE_X + 2][CHUNK_SIZE_Z + 2];

    auto computeHeight = [&](int lx, int lz) -> int {
        float wx = (float)(world_x + lx);
        float wz = (float)(world_z + lz);
        float n  = noise_.getHeight(wx, wz);          // [-1, 1]
        float v  = noise_.getValley(wx, wz);          // Ridged [0,~1]: high = carve valley
        float cut = std::max(0.0f, v) * 22.0f;
        int   s   = (int)(55.0f + n * 45.0f - cut);  // wider amplitude + valley carving
        return std::clamp(s, 2, CHUNK_SIZE_Y - 2);
    };

    for (int x = -1; x <= CHUNK_SIZE_X; ++x)
        for (int z = -1; z <= CHUNK_SIZE_Z; ++z)
            heights[x + 1][z + 1] = computeHeight(x, z);

    for (int x = 0; x < CHUNK_SIZE_X; ++x) {
        for (int z = 0; z < CHUNK_SIZE_Z; ++z) {
            int surface = heights[x + 1][z + 1];

            // Max height diff with 4 cardinal neighbors — drives slope-based block selection
            int max_diff = std::max({
                std::abs(surface - heights[x + 1][z + 2]),
                std::abs(surface - heights[x + 1][z + 0]),
                std::abs(surface - heights[x + 2][z + 1]),
                std::abs(surface - heights[x + 0][z + 1])
            });

            // Top surface block
            BlockType top;
            if      (surface > 73)              top = BlockType::Snow;
            else if (surface <= SEA_LEVEL + 3)  top = BlockType::Sand;   // underwater + beach
            else if (max_diff >= 5)             top = BlockType::Stone;  // steep cliff
            else if (max_diff >= 2)             top = BlockType::Dirt;   // gentle slope
            else                                top = BlockType::Grass;

            float wx = (float)(world_x + x);
            float wz = (float)(world_z + z);

            for (int y = 0; y < CHUNK_SIZE_Y; ++y) {
                BlockType t = BlockType::Air;

                if (y == 0) {
                    t = BlockType::Stone;
                } else if (y < surface - 3) {
                    t = BlockType::Stone;
                } else if (y < surface) {
                    t = BlockType::Dirt;
                } else if (y == surface) {
                    t = top;
                } else if (y > surface && y <= SEA_LEVEL && surface < SEA_LEVEL) {
                    t = BlockType::Water;
                }

                // Cave carving (only in deep solid rock, never in water zone)
                if (t != BlockType::Air && t != BlockType::Water && y > 5 && y < surface - 3) {
                    float cv = noise_.getCave(wx, (float)y, wz);
                    if (cv > 0.55f) t = BlockType::Air;
                }

                chunk.setBlock(x, y, z, t);
            }
        }
    }

    chunk.is_generated = true;
    chunk.is_dirty     = true;
}
