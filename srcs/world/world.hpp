#pragma once
#include "interfaces/IWorld.hpp"
#include "world/terrain_gen.hpp"
#include <unordered_map>
#include <memory>

class World : public IWorld {
public:
    explicit World(uint32_t seed = 42);
    ~World() override = default;

    Chunk* getOrCreateChunk(ChunkPos pos) override;
    void   setSeed(uint32_t seed) override;
    uint32_t getSeed() const override { return seed_; }

    // Block access in world coordinates (no chunk generation side-effects)
    BlockType getWorldBlock(int wx, int wy, int wz) const;
    bool      setWorldBlock(int wx, int wy, int wz, BlockType type);

    std::unordered_map<ChunkPos, std::unique_ptr<Chunk>, ChunkPosHash>& chunks() {
        return chunks_;
    }

private:
    uint32_t    seed_ = 42;
    TerrainGenerator gen_;
    std::unordered_map<ChunkPos, std::unique_ptr<Chunk>, ChunkPosHash> chunks_;
};
