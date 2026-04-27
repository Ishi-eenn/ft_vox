#pragma once
#include "types.hpp"
#include <vector>

class Frustum;

class IChunkManager {
public:
    virtual ~IChunkManager() = default;
    virtual void update(float playerX, float playerZ, uint64_t frame) = 0;
    virtual std::vector<Chunk*> getVisibleChunks(const Frustum& frustum) = 0;
    virtual size_t loadedCount() const = 0;
};
