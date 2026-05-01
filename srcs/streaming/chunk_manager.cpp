// ─────────────────────────────────────────────────────────────────────────────
// chunk_manager.cpp — チャンクの読み込み・更新・破棄を管理する
//
// 【チャンクストリーミングとは？】
//   世界全体をメモリに持つのは不可能なので、プレイヤーの周囲だけを読み込む。
//   プレイヤーが移動するたびに「近くのチャンクを追加」「遠くのチャンクを破棄」する。
//   これを「ストリーミング」と呼ぶ。
//
// 【非同期生成の仕組み】
//   地形生成（Perlinノイズ）はCPU集約的で重いため、ワーカースレッドで実行する。
//   メインスレッドは空チャンクを確保し、ワーカーが blocks[] を埋める。
//   完了したチャンクはメインスレッドでメッシュ構築・GPUアップロードを行う。
//
// 【LRUキャッシュ】
//   「最近使っていないものを追い出す」方式のキャッシュ。
//   アクセスされたチャンクは「最近使用リスト」の先頭に移動し、
//   容量が足りなくなったら末尾（一番古い）から削除する。
// ─────────────────────────────────────────────────────────────────────────────
#include "streaming/chunk_manager.hpp"
#include "world/light_calculator.hpp"
#include "renderer/frustum.hpp"
#include <cstdio>
#include <cmath>
#include <algorithm>

// ブロックが変更されたとき、そのブロックが属するチャンクと
// チャンク境界に隣接していれば隣のチャンクも再構築する。
static void rebuildModifiedBlock(int wx, int wz, ChunkManager& mgr) {
    int cx = (int)std::floor((float)wx / CHUNK_SIZE_X);
    int cz = (int)std::floor((float)wz / CHUNK_SIZE_Z);
    int lx = wx - cx * CHUNK_SIZE_X;
    int lz = wz - cz * CHUNK_SIZE_Z;

    mgr.rebuildChunkAt({cx, cz});
    if (lx == 0)               mgr.rebuildChunkAt({cx - 1, cz});
    if (lx == CHUNK_SIZE_X-1)  mgr.rebuildChunkAt({cx + 1, cz});
    if (lz == 0)               mgr.rebuildChunkAt({cx, cz - 1});
    if (lz == CHUNK_SIZE_Z-1)  mgr.rebuildChunkAt({cx, cz + 1});
}

ChunkManager::ChunkManager(IWorld& world, IRenderer& renderer, const TerrainGenerator& gen)
    : world_(world)
    , renderer_(renderer)
    , gen_(gen)
    , loaded_(MAX_LOADED_CHUNKS)
{
    // ワーカースレッド数: hardware_concurrency / 2 個（最小1、最大4）
    unsigned int hw = std::thread::hardware_concurrency();
    unsigned int nthreads = std::min(4u, std::max(1u, hw / 2));
    for (unsigned int i = 0; i < nthreads; ++i) {
        gen_threads_.emplace_back(&ChunkManager::workerLoop, this);
    }
    fprintf(stderr, "[ChunkMgr] started %u worker thread(s)\n", nthreads);
}

ChunkManager::~ChunkManager() {
    // ワーカースレッドに停止を通知してから join する
    {
        std::lock_guard<std::mutex> lk(gen_mutex_);
        stopping_ = true;
    }
    gen_cv_.notify_all();
    for (auto& t : gen_threads_) {
        if (t.joinable()) t.join();
    }
    destroyAll();
}

// 全チャンクのGPUリソース（VAO・VBO・EBO）を解放する
void ChunkManager::destroyAll() {
    loaded_.forEach([this](const ChunkPos& /*pos*/, Chunk* chunk) {
        if (chunk) renderer_.destroyChunkMesh(chunk);
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// workerLoop() — ワーカースレッドのメインループ
//
// gen_tasks_ キューからチャンクを取り出し、地形を生成して gen_done_queue_ に積む。
// TerrainGenerator::generate() は const メソッドでスレッドセーフ。
// ─────────────────────────────────────────────────────────────────────────────
void ChunkManager::workerLoop() {
    while (true) {
        std::pair<ChunkPos, Chunk*> task;
        {
            std::unique_lock<std::mutex> lk(gen_mutex_);
            gen_cv_.wait(lk, [this] {
                return stopping_ || !gen_tasks_.empty();
            });
            if (stopping_ && gen_tasks_.empty()) break;
            task = std::move(gen_tasks_.front());
            gen_tasks_.pop();
        }

        // ワーカーが blocks[] を書き込む。
        // gen_in_flight_ により同一チャンクへの並列書き込みは発生しない。
        gen_.generate(*task.second);

        {
            std::lock_guard<std::mutex> lk(done_mutex_);
            gen_done_queue_.push(task.first);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// update() — 毎フレーム呼ばれるチャンク管理の主処理
//
// 1. プレイヤー位置から中心チャンクを特定
// 2. 半径 render_distance_ 以内の未読込チャンクをワーカーキューに投入
// 3. 完了したワーカータスクを処理する（メッシュ構築・アップロードキューへ追加）
// 4. 3フレームに1回、水の流れをシミュレートする
// 5. 待機中のチャンクをGPUにアップロードする（1フレームに数チャンクずつ）
// 6. 容量超過なら古いチャンクを破棄する
// ─────────────────────────────────────────────────────────────────────────────
void ChunkManager::update(float playerX, float playerZ, uint64_t frame) {
    // プレイヤーがいるチャンク座標を計算
    ChunkPos center = {
        (int32_t)std::floor(playerX / (float)CHUNK_SIZE_X),
        (int32_t)std::floor(playerZ / (float)CHUNK_SIZE_Z)
    };

    loadRadius(center);
    processGenDone();  // 完了したワーカータスクをメインスレッドで処理する

    // 3フレームに1回だけ水シミュレーションを実行（毎フレームは重すぎる）
    if (frame % 3 == 0) {
        auto changed_water = world_.stepWater(
            {center.x - render_distance_, center.z - render_distance_},
            {center.x + render_distance_, center.z + render_distance_}
        );
        // 水が変化したブロックのチャンクを再構築する
        for (const WorldPos& pos : changed_water) {
            rebuildModifiedBlock(pos.x, pos.z, *this);
        }
    }

    // 待機キューから1フレームあたり最大 CHUNKS_PER_FRAME_GEN 個をGPUへ転送
    uploadPending(CHUNKS_PER_FRAME_GEN);

    // キャッシュが容量を超えていれば古いチャンクを破棄する
    evictIfOverBudget();

    // 300フレームに1回、状態をデバッグ出力
    if (frame - last_debug_frame_ >= 300) {
        size_t in_flight_count;
        {
            std::lock_guard<std::mutex> lk(gen_mutex_);
            in_flight_count = gen_in_flight_.size();
        }
        fprintf(stderr, "[ChunkMgr] frame=%llu loaded=%zu queue=%zu in_flight=%zu\n",
                (unsigned long long)frame, loaded_.size(), upload_queue_.size(), in_flight_count);
        last_debug_frame_ = frame;
    }
}

// レンダー距離を変更する（最小・最大値でクランプ）
void ChunkManager::setRenderDistance(int rd) {
    if (rd < RENDER_DISTANCE_MIN) rd = RENDER_DISTANCE_MIN;
    if (rd > RENDER_DISTANCE_MAX) rd = RENDER_DISTANCE_MAX;
    render_distance_ = rd;
}

// ─────────────────────────────────────────────────────────────────────────────
// loadRadius() — 中心チャンクから半径 rd 以内の未読込チャンクをワーカーに投入する
//
// 既にロード済み or 生成中のチャンクはスキップする。
// 新チャンクはメインスレッドで空確保し、ワーカーキューに投入する。
// ─────────────────────────────────────────────────────────────────────────────
void ChunkManager::loadRadius(ChunkPos center) {
    const int rd = render_distance_;
    for (int dx = -rd; dx <= rd; ++dx) {
        for (int dz = -rd; dz <= rd; ++dz) {
            // 円形の外はスキップ
            if (dx * dx + dz * dz > rd * rd) continue;
            ChunkPos p = {center.x + dx, center.z + dz};

            if (loaded_.contains(p)) {
                loaded_.touch(p);  // LRUリストの先頭に移動（最近使用済みとしてマーク）
                continue;
            }

            // 既にワーカーに投入済みならスキップ
            {
                std::lock_guard<std::mutex> lk(gen_mutex_);
                if (gen_in_flight_.count(p)) continue;
            }

            // メインスレッドで空チャンクを確保（chunks_ mapへの書き込みはメインのみ）
            Chunk* chunk = world_.allocateChunk(p);
            if (!chunk) continue;

            // ワーカーキューに投入
            {
                std::lock_guard<std::mutex> lk(gen_mutex_);
                gen_in_flight_.insert(p);
                gen_tasks_.push({p, chunk});
            }
            gen_cv_.notify_one();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// processGenDone() — 完了したワーカータスクをメインスレッドで処理する
//
// 毎フレーム呼ばれる。gen_done_queue_ から完了チャンクを取り出し、
// LRUに追加・メッシュ構築・GPUアップロードキューへの追加を行う。
// また隣接チャンクのメッシュを再構築して境界面を正しく更新する。
// ─────────────────────────────────────────────────────────────────────────────
void ChunkManager::processGenDone() {
    // done_queue をドレイン（フレームあたりの処理数を制限してスパイクを防ぐ）
    std::vector<ChunkPos> done;
    {
        std::lock_guard<std::mutex> lk(done_mutex_);
        while (!gen_done_queue_.empty() && (int)done.size() < CHUNKS_PER_FRAME_GEN) {
            done.push_back(gen_done_queue_.front());
            gen_done_queue_.pop();
        }
    }

    for (const ChunkPos& pos : done) {
        // in_flight から除去
        {
            std::lock_guard<std::mutex> lk(gen_mutex_);
            gen_in_flight_.erase(pos);
        }

        Chunk* chunk = world_.findChunk(pos);
        if (!chunk || !chunk->is_generated) continue;

        // LRUに追加（溢れたチャンクのGPUリソースを破棄）
        auto evicted = loaded_.put(pos, chunk);
        for (const ChunkPos& ep : evicted) {
            Chunk* dead = world_.findChunk(ep);
            if (dead) renderer_.destroyChunkMesh(dead);
        }

        // メッシュ構築（CPU）とGPUアップロードキューへの追加
        buildMesh(chunk);
        upload_queue_.push(chunk);

        // 隣接チャンクのメッシュ再構築:
        // 新チャンクが追加されると、隣接チャンクが「nil=solid扱い」で描画していた
        // 境界面を正しい面に更新する必要がある。
        static const int NEIGHBOR_OFFSETS[4][2] = {{0,1},{0,-1},{1,0},{-1,0}};
        for (const auto& d : NEIGHBOR_OFFSETS) {
            rebuildChunkAt({pos.x + d[0], pos.z + d[1]});
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// buildMesh() — チャンクのメッシュ（3D形状）を CPU 上で生成する
//
// findChunk() を使い生成副作用なしで隣接チャンクを取得する。
// 未生成の隣接チャンクは nullptr → MeshBuilder が solid 扱いにする。
// ─────────────────────────────────────────────────────────────────────────────
void ChunkManager::buildMesh(Chunk* chunk) {
    ChunkNeighbors nb;
    nb.north = world_.findChunk({chunk->pos.x,     chunk->pos.z - 1});  // -Z方向
    nb.south = world_.findChunk({chunk->pos.x,     chunk->pos.z + 1});  // +Z方向
    nb.east  = world_.findChunk({chunk->pos.x + 1, chunk->pos.z    });  // +X方向
    nb.west  = world_.findChunk({chunk->pos.x - 1, chunk->pos.z    });  // -X方向

    // 未生成チャンクは nullptr として扱う（MeshBuilderが solid として処理）
    if (nb.north && !nb.north->is_generated) nb.north = nullptr;
    if (nb.south && !nb.south->is_generated) nb.south = nullptr;
    if (nb.east  && !nb.east->is_generated)  nb.east  = nullptr;
    if (nb.west  && !nb.west->is_generated)  nb.west  = nullptr;

    // 光の計算はメッシュ構築の前に行う（光レベルを頂点データに埋め込むため）
    LightCalculator::compute(*chunk, nb);
    MeshBuilder::build(*chunk, nb);
}

// ─────────────────────────────────────────────────────────────────────────────
// uploadPending() — 待機中のチャンクをGPUへ転送する
//
// 1フレームで全チャンクをアップロードすると処理が一瞬止まる（スパイク）。
// max_per_frame 個ずつに分けて、毎フレーム少しずつ処理する。
// ─────────────────────────────────────────────────────────────────────────────
void ChunkManager::uploadPending(int max_per_frame) {
    int n = 0;
    while (!upload_queue_.empty() && n < max_per_frame) {
        Chunk* c = upload_queue_.front();
        upload_queue_.pop();
        if (c && c->is_dirty) {
            renderer_.uploadChunkMesh(c);  // 頂点・インデックスをGPUに送る
            ++n;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// evictIfOverBudget() — キャッシュが上限を超えたら古いチャンクを破棄する
//
// LRU方式なので「最も長く使われていないチャンク」から順に破棄される。
// 上限の90%まで削減することで、すぐに再度オーバーしないようにする（ヒステリシス）。
// ─────────────────────────────────────────────────────────────────────────────
void ChunkManager::evictIfOverBudget() {
    const size_t target = (size_t)(MAX_LOADED_CHUNKS * 9 / 10);
    while (loaded_.size() > target) {
        ChunkPos key;
        Chunk*   chunk = nullptr;
        if (!loaded_.evictLRU(key, chunk)) break;
        if (chunk) renderer_.destroyChunkMesh(chunk);
    }
}

// 指定チャンクのメッシュを再構築してアップロードキューに追加する
// ブロックを壊す・置くときや隣接チャンクが到着したときに呼ばれる
void ChunkManager::rebuildChunkAt(ChunkPos pos) {
    if (!loaded_.contains(pos)) return;
    Chunk* chunk = world_.findChunk(pos);
    if (!chunk || !chunk->is_generated) return;
    buildMesh(chunk);
    upload_queue_.push(chunk);
}

// ─────────────────────────────────────────────────────────────────────────────
// getVisibleChunks() — フラスタムカリングで可視チャンクだけを返す
//
// 各チャンクの AABB（最小点〜最大点の直方体）をフラスタムと判定し、
// 視野外のチャンクを除外する。O(チャンク数) × O(6平面) の処理。
// ─────────────────────────────────────────────────────────────────────────────
std::vector<Chunk*> ChunkManager::getVisibleChunks(const Frustum& frustum) {
    std::vector<Chunk*> result;
    result.reserve(loaded_.size());

    loaded_.forEach([&](const ChunkPos& /*pos*/, Chunk* chunk) {
        if (!chunk || !chunk->gpu.uploaded) return;

        // チャンクのワールド座標での AABB を作る
        float wx = (float)(chunk->pos.x * CHUNK_SIZE_X);
        float wz = (float)(chunk->pos.z * CHUNK_SIZE_Z);

        AABB aabb;
        aabb.min = {wx,                        0.0f,                wz                       };
        aabb.max = {wx + (float)CHUNK_SIZE_X,  (float)CHUNK_SIZE_Y, wz + (float)CHUNK_SIZE_Z };

        // フラスタムの外なら描画リストに追加しない（カリング）
        if (frustum.isAABBVisible(aabb)) {
            result.push_back(chunk);
        }
    });

    return result;
}
