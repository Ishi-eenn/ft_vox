#pragma once
#include "types.hpp"
#include <memory>

class IWorld {
public:
    virtual ~IWorld() = default;
    virtual Chunk* getOrCreateChunk(ChunkPos pos) = 0;
    // Read-only lookup — returns nullptr if the chunk has not been generated yet.
    virtual Chunk* getChunk(ChunkPos pos) = 0;
    // Register a chunk that was generated off the main thread.
    virtual Chunk* registerChunk(std::unique_ptr<Chunk> chunk) = 0;
    // Remove a chunk from World storage and free its water simulation state.
    // Must be called after destroyChunkMesh, never before.
    virtual void   unregisterChunk(ChunkPos pos)   = 0;
    virtual void   setSeed(uint32_t seed)          = 0;
    virtual uint32_t getSeed() const               = 0;
    virtual std::vector<WorldPos> stepWater(ChunkPos min_chunk, ChunkPos max_chunk) = 0;
    virtual void applyMods(Chunk* /*chunk*/) const {}
};
