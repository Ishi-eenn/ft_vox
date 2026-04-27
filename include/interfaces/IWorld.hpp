#pragma once
#include "types.hpp"

class IWorld {
public:
    virtual ~IWorld() = default;
    virtual Chunk* getOrCreateChunk(ChunkPos pos) = 0;
    virtual void   setSeed(uint32_t seed)          = 0;
    virtual uint32_t getSeed() const               = 0;
};
