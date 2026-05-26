#pragma once
#include "types.hpp"
#include "world/noise.hpp"

class TerrainGenerator {
public:
    enum class BiomeType {
        Plains,
        Desert,
        Tundra,
        Rocky,
        Swamp,
        Mountain,
        Canyon,
        Spring,
        Autumn,
    };

    void setSeed(uint32_t seed);
    // Fill chunk.blocks based on chunk.pos and seed. Sets chunk.is_generated=true, chunk.is_dirty=true.
    void generate(Chunk& chunk) const;
    BiomeType getBiomeAt(float wx, float wz) const;
    const char* getBiomeNameAt(float wx, float wz) const;
    static const char* biomeName(BiomeType biome);

private:
    uint32_t seed_ = 42;
    NoiseGen noise_;
};
