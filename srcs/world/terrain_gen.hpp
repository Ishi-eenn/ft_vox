#pragma once
#include "types.hpp"
#include "world/noise.hpp"

class TerrainGenerator {
public:
    void setSeed(uint32_t seed);
    // Fill chunk.blocks based on chunk.pos and seed. Sets chunk.is_generated=true, chunk.is_dirty=true.
    void generate(Chunk& chunk) const;

private:
    uint32_t seed_ = 42;
    NoiseGen noise_;
};
