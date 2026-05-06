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
#include <atomic>

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
    void buildMesh(Chunk* chunk, const ChunkNeighbors& nb);
    void processGenDone();       // gen完了チャンクをメッシュタスクキューに投入する（軽量）
    void processMeshDone();      // メッシュ構築完了チャンクをLRU管理・アップロードキューへ追加
    void processRebuildDirty();  // 延期された隣接チャンク再構築をアップロード後に処理する
    void workerLoop();           // 地形生成ワーカースレッドのメインループ
    void meshWorkerLoop();       // メッシュ構築ワーカースレッドのメインループ
    void enqueueMeshBuild(ChunkPos pos, Chunk* chunk);  // メインスレッドのみ呼ぶ

    IWorld&                   world_;
    IRenderer&                renderer_;
    const TerrainGenerator&   gen_;  // ワーカーが使う地形生成器（const: スレッドセーフ）

    // chunk ptr is owned by World; ChunkManager only borrows it
    LRUCache<ChunkPos, Chunk*, ChunkPosHash> loaded_;

    std::queue<Chunk*> upload_queue_;
    uint64_t           last_debug_frame_ = 0;
    int                render_distance_  = RENDER_DISTANCE;

    // ── 地形生成ワーカースレッド関連 ─────────────────────────────────────────────
    std::vector<std::thread>  gen_threads_;

    // gen_tasks_ と gen_in_flight_ を保護する mutex
    std::mutex                gen_mutex_;
    std::condition_variable   gen_cv_;
    std::queue<std::pair<ChunkPos, Chunk*>> gen_tasks_;
    std::unordered_set<ChunkPos, ChunkPosHash> gen_in_flight_;

    // gen/mesh 両ワーカーが参照するため atomic
    std::atomic<bool>         stopping_{false};

    // 地形生成完了 → メインスレッドへの通知キュー
    std::mutex                done_mutex_;
    std::queue<ChunkPos>      gen_done_queue_;

    // ── メッシュ構築ワーカースレッド関連 ──────────────────────────────────────────
    // MeshTask: neighborはメインスレッドで解決済み（findChunk競合回避）
    struct MeshTask {
        ChunkPos       pos;
        Chunk*         chunk;
        ChunkNeighbors neighbors;
    };

    std::vector<std::thread>  mesh_threads_;
    std::mutex                mesh_mutex_;
    std::condition_variable   mesh_cv_;
    std::queue<MeshTask>      mesh_tasks_;
    std::unordered_set<ChunkPos, ChunkPosHash> mesh_in_flight_;

    // メッシュ構築完了 → メインスレッドへの通知キュー
    std::mutex                mesh_done_mutex_;
    std::queue<ChunkPos>      mesh_done_queue_;

    // ── メインスレッド専用（ロック不要） ─────────────────────────────────────────
    // upload_queue_ に入っているチャンクを追跡する（再ビルド競合防止）
    std::unordered_set<ChunkPos, ChunkPosHash> upload_pending_;

    // uploadPending後に処理する隣接再ビルドの延期セット
    std::unordered_set<ChunkPos, ChunkPosHash> rebuild_dirty_;
};
