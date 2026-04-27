#pragma once
#include "interfaces/IWorld.hpp"
#include "world/terrain_gen.hpp"
#include <unordered_set>
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
    std::vector<WorldPos> stepWater(ChunkPos min_chunk, ChunkPos max_chunk) override;

    std::unordered_map<ChunkPos, std::unique_ptr<Chunk>, ChunkPosHash>& chunks() {
        return chunks_;
    }

private:
    static int floorDiv(int a, int b);
    static ChunkPos worldToChunk(int wx, int wz);
    bool setExistingWorldBlock(int wx, int wy, int wz, BlockType type);
    bool isSourceWater(int wx, int wy, int wz) const;
    bool isFlowingWater(int wx, int wy, int wz, uint8_t* level_out = nullptr) const;
    bool isWaterBlock(int wx, int wy, int wz) const;
    bool isSolidBlock(BlockType type) const;
    bool canFlowInto(int wx, int wy, int wz) const;
    bool inChunkRange(ChunkPos pos, ChunkPos min_chunk, ChunkPos max_chunk) const;
    int  flowSearchCost(int wx, int wy, int wz, int depth, int from_dir) const;
    bool canFlowFromTo(int from_x, int from_y, int from_z, int to_x, int to_y, int to_z, uint8_t* out_depth = nullptr) const;
    void activateWaterAt(int wx, int wy, int wz);
    void activateWaterNeighborhood(int wx, int wy, int wz);

    uint32_t    seed_ = 42;
    TerrainGenerator gen_;
    std::unordered_map<ChunkPos, std::unique_ptr<Chunk>, ChunkPosHash> chunks_;
    std::unordered_map<WorldPos, uint8_t, WorldPosHash> flowing_water_;
    std::unordered_set<WorldPos, WorldPosHash> active_water_;
};
