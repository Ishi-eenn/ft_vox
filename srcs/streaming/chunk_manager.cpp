// ─────────────────────────────────────────────────────────────────────────────
// chunk_manager.cpp — チャンクの読み込み・更新・破棄を管理する
//
// 【チャンクストリーミングとは？】
//   世界全体をメモリに持つのは不可能なので、プレイヤーの周囲だけを読み込む。
//   プレイヤーが移動するたびに「近くのチャンクを追加」「遠くのチャンクを破棄」する。
//   これを「ストリーミング」と呼ぶ。
//
// 【LRUキャッシュ】
//   「最近使っていないものを追い出す」方式のキャッシュ。
//   アクセスされたチャンクは「最近使用リスト」の先頭に移動し、
//   容量が足りなくなったら末尾（一番古い）から削除する。
// ─────────────────────────────────────────────────────────────────────────────
#include "streaming/chunk_manager.hpp"
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

ChunkManager::ChunkManager(IWorld& world, IRenderer& renderer)
    : world_(world)
    , renderer_(renderer)
    , loaded_(MAX_LOADED_CHUNKS)  // LRUキャッシュの容量を設定
{}

ChunkManager::~ChunkManager() {
    destroyAll();
}

// 全チャンクのGPUリソース（VAO・VBO・EBO）を解放する
void ChunkManager::destroyAll() {
    loaded_.forEach([this](const ChunkPos& /*pos*/, Chunk* chunk) {
        if (chunk) renderer_.destroyChunkMesh(chunk);
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// update() — 毎フレーム呼ばれるチャンク管理の主処理
//
// 1. プレイヤー位置から中心チャンクを特定
// 2. 半径 render_distance_ 以内のチャンクを読み込む
// 3. 3フレームに1回、水の流れをシミュレートする
// 4. 待機中のチャンクをGPUにアップロードする（1フレームに数チャンクずつ）
// 5. 容量超過なら古いチャンクを破棄する
// ─────────────────────────────────────────────────────────────────────────────
void ChunkManager::update(float playerX, float playerZ, uint64_t frame) {
    // プレイヤーがいるチャンク座標を計算
    ChunkPos center = {
        (int32_t)std::floor(playerX / (float)CHUNK_SIZE_X),
        (int32_t)std::floor(playerZ / (float)CHUNK_SIZE_Z)
    };

    loadRadius(center);

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

    // 300フレームに1回、ロード済みチャンク数とキュー数をデバッグ出力
    if (frame - last_debug_frame_ >= 300) {
        fprintf(stderr, "[ChunkMgr] frame=%llu loaded=%zu queue=%zu\n",
                (unsigned long long)frame, loaded_.size(), upload_queue_.size());
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
// loadRadius() — 中心チャンクから半径 rd 以内のチャンクを読み込む
//
// 円形（dx²+dz² ≤ rd²）の範囲に限定して、正方形より自然な視野にする。
// 既にロード済みのチャンクは LRU のアクセス順を更新するだけ（再生成しない）。
// 新しいチャンクはメッシュを生成してアップロードキューに追加する。
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

            // 新しいチャンク: 地形生成してキャッシュに追加
            Chunk* chunk = world_.getOrCreateChunk(p);
            if (!chunk) continue;

            // put() は容量オーバー時に追い出されたチャンクのキーを返す
            auto evicted = loaded_.put(p, chunk);
            for (const ChunkPos& ep : evicted) {
                Chunk* dead = world_.getOrCreateChunk(ep);
                if (dead) renderer_.destroyChunkMesh(dead);  // GPUリソースを解放
            }

            // LRUから追い出された後に再び読み込まれたチャンクは
            // gpu.uploaded が false になっているので必ずメッシュを再構築する
            buildMesh(chunk);
            upload_queue_.push(chunk);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// buildMesh() — チャンクのメッシュ（3D形状）を CPU 上で生成する
//
// チャンク境界のブロック判定には隣接チャンクの情報が必要なので、
// 東西南北の隣チャンクを取得してから MeshBuilder に渡す。
// ─────────────────────────────────────────────────────────────────────────────
void ChunkManager::buildMesh(Chunk* chunk) {
    ChunkNeighbors nb;
    nb.north = world_.getOrCreateChunk({chunk->pos.x,     chunk->pos.z - 1});  // -Z方向
    nb.south = world_.getOrCreateChunk({chunk->pos.x,     chunk->pos.z + 1});  // +Z方向
    nb.east  = world_.getOrCreateChunk({chunk->pos.x + 1, chunk->pos.z    });  // +X方向
    nb.west  = world_.getOrCreateChunk({chunk->pos.x - 1, chunk->pos.z    });  // -X方向
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
// ブロックを壊す・置くときに呼ばれる
void ChunkManager::rebuildChunkAt(ChunkPos pos) {
    if (!loaded_.contains(pos)) return;
    Chunk* chunk = world_.getOrCreateChunk(pos);
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
