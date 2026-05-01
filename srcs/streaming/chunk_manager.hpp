#pragma once
#include "interfaces/IChunkManager.hpp"
#include "interfaces/IWorld.hpp"
#include "interfaces/IRenderer.hpp"
#include "streaming/lru_cache.hpp"
#include "world/mesh_builder.hpp"
#include "world/terrain_gen.hpp"
#include <queue>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unordered_set>

class Frustum;
struct AABB;

class ChunkManager : public IChunkManager {
public:
    ChunkManager(IWorld& world, IRenderer& renderer, const TerrainGenerator& gen);
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
    void processGenDone();  // 完了したワーカータスクをメインスレッドで処理する
    void workerLoop();      // ワーカースレッドのメインループ

    IWorld&                   world_;
    IRenderer&                renderer_;
    const TerrainGenerator&   gen_;  // ワーカーが使う地形生成器（const: スレッドセーフ）

    // chunk ptr is owned by World; ChunkManager only borrows it
    LRUCache<ChunkPos, Chunk*, ChunkPosHash> loaded_;

    std::queue<Chunk*> upload_queue_;
    uint64_t           last_debug_frame_ = 0;
    int                render_distance_  = RENDER_DISTANCE;

    // ── ワーカースレッド関連 ────────────────────────────────────────────────────
    std::vector<std::thread>  gen_threads_;

    // gen_tasks_ と gen_in_flight_ を保護する mutex
    std::mutex                gen_mutex_;
    std::condition_variable   gen_cv_;
    std::queue<std::pair<ChunkPos, Chunk*>> gen_tasks_;
    std::unordered_set<ChunkPos, ChunkPosHash> gen_in_flight_;
    bool                      stopping_ = false;

    // ワーカー → メイン への完了通知キュー
    std::mutex                done_mutex_;
    std::queue<ChunkPos>      gen_done_queue_;
};
