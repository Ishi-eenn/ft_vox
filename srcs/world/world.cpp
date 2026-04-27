#include "world/world.hpp"
#include <cmath>

World::World(uint32_t seed) {
    setSeed(seed);
}

void World::setSeed(uint32_t seed) {
    seed_ = seed;
    gen_.setSeed(seed);
}

// Helper: floor-divide for correct negative coordinate handling
static int floorDiv(int a, int b) {
    return (int)std::floor((double)a / b);
}

BlockType World::getWorldBlock(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= CHUNK_SIZE_Y) return BlockType::Air;
    int cx = floorDiv(wx, CHUNK_SIZE_X);
    int cz = floorDiv(wz, CHUNK_SIZE_Z);
    int lx = wx - cx * CHUNK_SIZE_X;
    int lz = wz - cz * CHUNK_SIZE_Z;
    auto it = chunks_.find({cx, cz});
    if (it == chunks_.end()) return BlockType::Air;
    return it->second->getBlock(lx, wy, lz);
}

bool World::setWorldBlock(int wx, int wy, int wz, BlockType type) {
    if (wy < 0 || wy >= CHUNK_SIZE_Y) return false;
    int cx = floorDiv(wx, CHUNK_SIZE_X);
    int cz = floorDiv(wz, CHUNK_SIZE_Z);
    int lx = wx - cx * CHUNK_SIZE_X;
    int lz = wz - cz * CHUNK_SIZE_Z;
    auto it = chunks_.find({cx, cz});
    if (it == chunks_.end()) return false;
    it->second->setBlock(lx, wy, lz, type);
    it->second->is_dirty = true;
    return true;
}

Chunk* World::getOrCreateChunk(ChunkPos pos) {
    auto it = chunks_.find(pos);
    if (it != chunks_.end()) return it->second.get();

    auto chunk = std::make_unique<Chunk>();
    chunk->pos = pos;
    gen_.generate(*chunk);

    Chunk* raw = chunk.get();
    chunks_[pos] = std::move(chunk);
    return raw;
}
