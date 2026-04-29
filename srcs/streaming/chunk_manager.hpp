#pragma once
#include "interfaces/IChunkManager.hpp"
#include "interfaces/IWorld.hpp"
#include "interfaces/IRenderer.hpp"
#include "streaming/lru_cache.hpp"
#include "world/mesh_builder.hpp"
#include <queue>
#include <vector>

class Frustum;
struct AABB;

class ChunkManager : public IChunkManager {
public:
    ChunkManager(IWorld& world, IRenderer& renderer);
    ~ChunkManager() override;

    void update(float playerX, float playerZ, uint64_t frame) override;
    std::vector<Chunk*> getVisibleChunks(const Frustum& frustum) override;
    size_t loadedCount() const override { return loaded_.size(); }

    // Destroy all GPU resources. Call before OpenGL context teardown.
    void destroyAll();

    // Rebuild the mesh for a single chunk (e.g. after a block edit).
    // Also rebuilds the 4 cardinal neighbors if the edited block was on a border.
    void rebuildChunkAt(ChunkPos pos);

    // Dynamic render distance — clamped to [RENDER_DISTANCE_MIN, RENDER_DISTANCE_MAX].
    void setRenderDistance(int rd);
    int  renderDistance() const { return render_distance_; }

private:
    void loadRadius(ChunkPos center);
    void uploadPending(int max_per_frame);
    void evictIfOverBudget();
    void buildMesh(Chunk* chunk);

    IWorld&    world_;
    IRenderer& renderer_;

    // chunk ptr is owned by World; ChunkManager only borrows it
    LRUCache<ChunkPos, Chunk*, ChunkPosHash> loaded_;

    std::queue<Chunk*> upload_queue_;
    uint64_t           last_debug_frame_ = 0;
    int                render_distance_  = RENDER_DISTANCE;
};
