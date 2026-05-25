// ─────────────────────────────────────────────────────────────────────────────
// chunk_manager.cpp — チャンクの読み込み・更新・破棄を管理する
// ─────────────────────────────────────────────────────────────────────────────
#include "streaming/chunk_manager.hpp"
#include "renderer/frustum.hpp"
#include "world/terrain_gen.hpp"
#include "world/mesh_builder.hpp"
#include <cstdio>
#include <cmath>
#include <algorithm>

static void rebuildModifiedBlock(int wx, int wz, ChunkManager& mgr) {
    int cx = (int)std::floor((float)wx / CHUNK_SIZE_X);
    int cz = (int)std::floor((float)wz / CHUNK_SIZE_Z);
    int lx = wx - cx * CHUNK_SIZE_X;
    int lz = wz - cz * CHUNK_SIZE_Z;

    mgr.rebuildChunkAt({cx, cz});
    if (lx == 0)              mgr.rebuildChunkAt({cx - 1, cz});
    if (lx == CHUNK_SIZE_X-1) mgr.rebuildChunkAt({cx + 1, cz});
    if (lz == 0)              mgr.rebuildChunkAt({cx, cz - 1});
    if (lz == CHUNK_SIZE_Z-1) mgr.rebuildChunkAt({cx, cz + 1});
}

// ─────────────────────────────────────────────────────────────────────────────
// コンストラクタ / デストラクタ
// ─────────────────────────────────────────────────────────────────────────────
ChunkManager::ChunkManager(IWorld& world, IRenderer& renderer)
    : world_(world)
    , renderer_(renderer)
    , loaded_(MAX_LOADED_CHUNKS)
    , world_seed_(world.getSeed())
{
    // ワーカーは地形生成のみ。CPU 競合を最小化するため最大 2 スレッド。
    int n = std::max(1, std::min(2, (int)std::thread::hardware_concurrency() - 1));
    workers_.reserve(n);
    for (int i = 0; i < n; ++i)
        workers_.emplace_back(&ChunkManager::workerFunc, this);
    fprintf(stderr, "[ChunkMgr] %d terrain worker thread(s)\n", n);
}

ChunkManager::~ChunkManager() {
    stop_flag_.store(true, std::memory_order_relaxed);
    gen_cv_.notify_all();
    for (auto& t : workers_) t.join();
    destroyAll();
}

void ChunkManager::destroyAll() {
    loaded_.forEach([this](const ChunkPos& /*pos*/, Chunk* chunk) {
        if (chunk) renderer_.destroyChunkMesh(chunk);
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// workerFunc() — 地形生成のみ。メッシュには一切触れない。
// ─────────────────────────────────────────────────────────────────────────────
void ChunkManager::workerFunc() {
    TerrainGenerator local_gen;
    local_gen.setSeed(world_seed_);

    while (true) {
        ChunkPos pos;
        {
            std::unique_lock<std::mutex> lock(gen_mutex_);
            gen_cv_.wait(lock, [this] {
                return !gen_queue_.empty() ||
                       stop_flag_.load(std::memory_order_relaxed);
            });
            if (stop_flag_.load(std::memory_order_relaxed)) break;
            pos = gen_queue_.front();
            gen_queue_.pop();
        }

        auto chunk = std::make_unique<Chunk>();
        chunk->pos = pos;
        local_gen.generate(*chunk);

        {
            std::lock_guard<std::mutex> lock(terrain_done_mutex_);
            terrain_done_queue_.push(std::move(chunk));
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// drainTerrainDone() — 地形完了チャンクを World に登録し mesh_queue_ へ追加
// ─────────────────────────────────────────────────────────────────────────────
void ChunkManager::drainTerrainDone() {
    while (true) {
        std::unique_ptr<Chunk> chunk;
        {
            std::lock_guard<std::mutex> lock(terrain_done_mutex_);
            if (terrain_done_queue_.empty()) break;
            chunk = std::move(terrain_done_queue_.front());
            terrain_done_queue_.pop();
        }

        ChunkPos pos = chunk->pos;
        gen_in_flight_.erase(pos);

        if (world_.getChunk(pos)) continue;  // 二重登録ガード

        world_.applyMods(chunk.get());
        Chunk* raw = world_.registerChunk(std::move(chunk));
        if (!raw) continue;

        auto evicted = loaded_.put(pos, raw);
        for (const ChunkPos& ep : evicted) {
            if (Chunk* dead = world_.getChunk(ep)) renderer_.destroyChunkMesh(dead);
            world_.unregisterChunk(ep);
        }

        // 新チャンク自身と隣接チャンクをメッシュ再構築対象に追加
        mesh_queue_.insert(pos);
        for (ChunkPos np : {ChunkPos{pos.x, pos.z - 1}, ChunkPos{pos.x, pos.z + 1},
                            ChunkPos{pos.x + 1, pos.z}, ChunkPos{pos.x - 1, pos.z}}) {
            if (loaded_.contains(np)) {
                mesh_queue_.insert(np);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// processMeshQueue() — メインスレッドでメッシュを構築（上限付き）
// ─────────────────────────────────────────────────────────────────────────────
void ChunkManager::processMeshQueue(int max_per_frame) {
    int n = 0;
    auto it = mesh_queue_.begin();
    while (it != mesh_queue_.end() && n < max_per_frame) {
        ChunkPos pos = *it;
        it = mesh_queue_.erase(it);

        Chunk* chunk = world_.getChunk(pos);
        if (!chunk || !chunk->is_generated) continue;
        if (!loaded_.contains(pos)) continue;

        buildMesh(chunk);
        upload_queue_.push(chunk);
        ++n;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// update() — 毎フレーム呼ばれるチャンク管理の主処理
// ─────────────────────────────────────────────────────────────────────────────
void ChunkManager::update(float playerX, float playerZ, uint64_t frame) {
    ChunkPos center = {
        (int32_t)std::floor(playerX / (float)CHUNK_SIZE_X),
        (int32_t)std::floor(playerZ / (float)CHUNK_SIZE_Z)
    };

    drainTerrainDone();       // 地形完了 → World 登録 → mesh_queue_ へ
    processMeshQueue(4);      // メインスレッドでメッシュ構築（最大 4/frame）
    loadRadius(center);       // 未生成チャンクを gen_queue_ へ投入

    if (frame % 3 == 0) {
        auto changed_water = world_.stepWater(
            {center.x - render_distance_, center.z - render_distance_},
            {center.x + render_distance_, center.z + render_distance_}
        );
        // 変化ブロックの影響チャンクを先に集約してから1回だけ再構築する
        // （同じチャンクへの rebuildModifiedBlock 重複呼び出しを防ぐ）
        std::unordered_set<ChunkPos, ChunkPosHash> dirty_chunks;
        for (const WorldPos& pos : changed_water) {
            int cx = (int)std::floor((float)pos.x / CHUNK_SIZE_X);
            int cz = (int)std::floor((float)pos.z / CHUNK_SIZE_Z);
            int lx = pos.x - cx * CHUNK_SIZE_X;
            int lz = pos.z - cz * CHUNK_SIZE_Z;
            dirty_chunks.insert({cx, cz});
            if (lx == 0)               dirty_chunks.insert({cx - 1, cz});
            if (lx == CHUNK_SIZE_X-1)  dirty_chunks.insert({cx + 1, cz});
            if (lz == 0)               dirty_chunks.insert({cx,     cz - 1});
            if (lz == CHUNK_SIZE_Z-1)  dirty_chunks.insert({cx,     cz + 1});
        }
        for (const ChunkPos& cp : dirty_chunks)
            rebuildChunkAt(cp);
    }

    uploadPending(CHUNKS_PER_FRAME_GEN);
    evictIfOverBudget();

    if (frame - last_debug_frame_ >= 300) {
        size_t gen_q_size;
        {
            std::lock_guard<std::mutex> lock(gen_mutex_);
            gen_q_size = gen_queue_.size();
        }
        fprintf(stderr, "[ChunkMgr] frame=%llu loaded=%zu gen_q=%zu mesh_q=%zu upload=%zu\n",
                (unsigned long long)frame, loaded_.size(),
                gen_q_size, mesh_queue_.size(), upload_queue_.size());
        last_debug_frame_ = frame;
    }
}

void ChunkManager::setRenderDistance(int rd) {
    if (rd < 1)               rd = 1;
    if (rd > RENDER_DISTANCE) rd = RENDER_DISTANCE;
    render_distance_ = rd;
}

// ─────────────────────────────────────────────────────────────────────────────
// loadRadius() — 中心チャンクから半径 rd 以内のチャンクを確保する
// ─────────────────────────────────────────────────────────────────────────────
void ChunkManager::loadRadius(ChunkPos center) {
    const int rd = render_distance_;
    for (int dx = -rd; dx <= rd; ++dx) {
        for (int dz = -rd; dz <= rd; ++dz) {
            if (dx * dx + dz * dz > rd * rd) continue;
            ChunkPos p = {center.x + dx, center.z + dz};

            if (loaded_.contains(p)) {
                loaded_.touch(p);
                continue;
            }

            // World に登録済み（LRU 追い出し後に再度視野に入った）
            Chunk* existing = world_.getChunk(p);
            if (existing) {
                auto evicted = loaded_.put(p, existing);
                for (const ChunkPos& ep : evicted) {
                    if (Chunk* dead = world_.getChunk(ep)) renderer_.destroyChunkMesh(dead);
                    world_.unregisterChunk(ep);
                }
                mesh_queue_.insert(p);
                continue;
            }

            // 未生成かつ未依頼 → TERRAIN ジョブとして投入
            if (!gen_in_flight_.count(p)) {
                gen_in_flight_.insert(p);
                {
                    std::lock_guard<std::mutex> lock(gen_mutex_);
                    gen_queue_.push(p);
                }
                gen_cv_.notify_one();
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// buildMesh() — メインスレッド専用のメッシュ構築
// ─────────────────────────────────────────────────────────────────────────────
void ChunkManager::buildMesh(Chunk* chunk) {
    ChunkNeighbors nb;
    nb.north = world_.getChunk({chunk->pos.x,     chunk->pos.z - 1});
    nb.south = world_.getChunk({chunk->pos.x,     chunk->pos.z + 1});
    nb.east  = world_.getChunk({chunk->pos.x + 1, chunk->pos.z    });
    nb.west  = world_.getChunk({chunk->pos.x - 1, chunk->pos.z    });
    MeshBuilder::build(*chunk, nb);
}

// ─────────────────────────────────────────────────────────────────────────────
// uploadPending()
// ─────────────────────────────────────────────────────────────────────────────
void ChunkManager::uploadPending(int max_per_frame) {
    int n = 0;
    while (!upload_queue_.empty() && n < max_per_frame) {
        Chunk* c = upload_queue_.front();
        upload_queue_.pop();
        if (c && c->is_dirty) {
            renderer_.uploadChunkMesh(c);
            ++n;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// evictIfOverBudget()
// ─────────────────────────────────────────────────────────────────────────────
void ChunkManager::evictIfOverBudget() {
    const size_t target = (size_t)(MAX_LOADED_CHUNKS * 9 / 10);
    while (loaded_.size() > target) {
        ChunkPos key;
        Chunk*   chunk = nullptr;
        if (!loaded_.evictLRU(key, chunk)) break;
        if (chunk) renderer_.destroyChunkMesh(chunk);
        world_.unregisterChunk(key);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// rebuildChunkAt() — ブロック操作・水シミュレーション後の同期メッシュ再構築
// ─────────────────────────────────────────────────────────────────────────────
void ChunkManager::rebuildChunkAt(ChunkPos pos) {
    if (!loaded_.contains(pos)) return;
    Chunk* chunk = world_.getChunk(pos);
    if (!chunk || !chunk->is_generated) return;
    mesh_queue_.erase(pos);  // 重複を避ける
    buildMesh(chunk);
    upload_queue_.push(chunk);
}

// ─────────────────────────────────────────────────────────────────────────────
// getAllLoadedChunks() — GPU メッシュを持つ全チャンクを返す（シャドウパス用）
// ─────────────────────────────────────────────────────────────────────────────
std::vector<Chunk*> ChunkManager::getAllLoadedChunks() const {
    std::vector<Chunk*> result;
    result.reserve(loaded_.size());
    loaded_.forEach([&](const ChunkPos& /*pos*/, Chunk* chunk) {
        if (chunk && chunk->gpu.uploaded)
            result.push_back(chunk);
    });
    return result;
}

// getVisibleChunks() — フラスタムカリングで可視チャンクだけを返す
// ─────────────────────────────────────────────────────────────────────────────
std::vector<Chunk*> ChunkManager::getVisibleChunks(const Frustum& frustum) {
    std::vector<Chunk*> result;
    result.reserve(loaded_.size());

    loaded_.forEach([&](const ChunkPos& /*pos*/, Chunk* chunk) {
        if (!chunk || !chunk->gpu.uploaded) return;

        float wx = (float)(chunk->pos.x * CHUNK_SIZE_X);
        float wz = (float)(chunk->pos.z * CHUNK_SIZE_Z);

        AABB aabb;
        aabb.min = {wx,                       0.0f,                wz                      };
        aabb.max = {wx + (float)CHUNK_SIZE_X, (float)CHUNK_SIZE_Y, wz + (float)CHUNK_SIZE_Z};

        if (frustum.isAABBVisible(aabb)) {
            result.push_back(chunk);
        }
    });

    return result;
}
