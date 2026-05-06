#pragma once
#include "interfaces/IChunkManager.hpp"
#include "interfaces/IWorld.hpp"
#include "interfaces/IRenderer.hpp"
#include "streaming/lru_cache.hpp"
#include "world/mesh_builder.hpp"
#include <queue>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <unordered_set>
#include <memory>

class Frustum;
struct AABB;

class ChunkManager : public IChunkManager {
public:
    ChunkManager(IWorld& world, IRenderer& renderer);
    ~ChunkManager() override;

    void update(float playerX, float playerZ, uint64_t frame) override;
    std::vector<Chunk*> getVisibleChunks(const Frustum& frustum) override;
    size_t loadedCount() const override { return loaded_.size(); }

    void destroyAll();
    void rebuildChunkAt(ChunkPos pos);

    void setRenderDistance(int rd);
    int  renderDistance() const { return render_distance_; }

private:
    void loadRadius(ChunkPos center);
    void uploadPending(int max_per_frame);
    void evictIfOverBudget();
    void buildMesh(Chunk* chunk);
    void drainTerrainDone();
    void processMeshQueue(int max_per_frame);
    void workerFunc();

    IWorld&    world_;
    IRenderer& renderer_;

    LRUCache<ChunkPos, Chunk*, ChunkPosHash> loaded_;
    std::queue<Chunk*> upload_queue_;
    uint64_t           last_debug_frame_ = 0;
    int                render_distance_  = RENDER_DISTANCE;

    // ── バックグラウンドスレッド（地形生成のみ）──────────────────────────────
    // メッシュビルドはメインスレッドのみで行い、データ競合を完全に排除する。
    uint32_t                 world_seed_ = 0;
    std::vector<std::thread> workers_;
    std::atomic<bool>        stop_flag_{false};

    // メイン → ワーカー: 地形生成ジョブ
    std::mutex              gen_mutex_;
    std::condition_variable gen_cv_;
    std::queue<ChunkPos>    gen_queue_;

    // ワーカー → メイン: 地形生成完了
    std::mutex                         terrain_done_mutex_;
    std::queue<std::unique_ptr<Chunk>> terrain_done_queue_;

    // メインスレッド専用（mutex 不要）
    std::unordered_set<ChunkPos, ChunkPosHash> gen_in_flight_;
    // メッシュビルドが必要なチャンク（processMeshQueue で消化）
    std::unordered_set<ChunkPos, ChunkPosHash> mesh_queue_;
};
