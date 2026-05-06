// ─────────────────────────────────────────────────────────────────────────────
// chunk_manager.cpp — チャンクの読み込み・更新・破棄を管理する
//
// 【チャンクストリーミングとは？】
//   世界全体をメモリに持つのは不可能なので、プレイヤーの周囲だけを読み込む。
//   プレイヤーが移動するたびに「近くのチャンクを追加」「遠くのチャンクを破棄」する。
//   これを「ストリーミング」と呼ぶ。
//
// 【非同期生成・メッシュ構築の仕組み】
//   地形生成（Perlinノイズ）とメッシュ構築（光計算+ポリゴン生成）はCPU集約的で重いため、
//   どちらもワーカースレッドで実行する。メインスレッドはGPUアップロードのみ担当する。
//
//   gen worker:   generate() → gen_done_queue_
//   main thread:  processGenDone() → enqueueMeshBuild() [neighbors解決済み]
//   mesh worker:  LightCalculator::compute() + MeshBuilder::build() → mesh_done_queue_
//   main thread:  processMeshDone() → LRU管理 + upload_queue_ + upload_pending_
//   main thread:  uploadPending() → GL upload → upload_pending_ から除去
//   main thread:  processRebuildDirty() → 延期された隣接再ビルド処理
//
// 【スレッドセーフティ設計】
//   - gen workers: blocks[] を書き込む。生成完了後は不変。
//   - mesh workers: vertices/indices/light_map を書き込む。
//                   neighbors は enqueueMeshBuild でメインスレッドが解決済み。
//   - mesh_in_flight_ は processMeshDone で erase される（meshWorkerLoop ではしない）。
//     → upload_queue_ 内のチャンクが再ビルドされるのを防ぐ。
//   - upload_pending_ でさらに upload_queue_ 内チャンクの再ビルドを防ぐ。
//   - rebuild_dirty_ で隣接再ビルドを uploadPending 後に延期して競合を回避する。
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
    unsigned int hw    = std::thread::hardware_concurrency();
    // gen + mesh の合計が hw/2 になるよう分配（最小各1）
    // 例: hw=8 → total=4 → gen=2, mesh=2
    unsigned int total  = std::max(2u, hw / 2);
    unsigned int n_gen  = std::max(1u, total / 2);
    unsigned int n_mesh = std::max(1u, total - n_gen);

    for (unsigned int i = 0; i < n_gen; ++i)
        gen_threads_.emplace_back(&ChunkManager::workerLoop, this);
    for (unsigned int i = 0; i < n_mesh; ++i)
        mesh_threads_.emplace_back(&ChunkManager::meshWorkerLoop, this);

    fprintf(stderr, "[ChunkMgr] started %u gen + %u mesh worker thread(s)\n", n_gen, n_mesh);
}

ChunkManager::~ChunkManager() {
    // gen/mesh 両ワーカーに停止を通知してから join する
    stopping_.store(true);
    gen_cv_.notify_all();
    mesh_cv_.notify_all();

    for (auto& t : gen_threads_)  if (t.joinable()) t.join();
    for (auto& t : mesh_threads_) if (t.joinable()) t.join();

    destroyAll();
}

// 全チャンクのGPUリソース（VAO・VBO・EBO）を解放する
void ChunkManager::destroyAll() {
    loaded_.forEach([this](const ChunkPos& /*pos*/, Chunk* chunk) {
        if (chunk) renderer_.destroyChunkMesh(chunk);
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// workerLoop() — 地形生成ワーカースレッドのメインループ
//
// gen_tasks_ キューからチャンクを取り出し、地形を生成して gen_done_queue_ に積む。
// TerrainGenerator::generate() は const メソッドでスレッドセーフ。
// gen_in_flight_ の erase もここで行う（メインスレッド側での erase 処理が不要）。
// ─────────────────────────────────────────────────────────────────────────────
void ChunkManager::workerLoop() {
    while (true) {
        std::pair<ChunkPos, Chunk*> task;
        {
            std::unique_lock<std::mutex> lk(gen_mutex_);
            gen_cv_.wait(lk, [this] {
                return stopping_.load() || !gen_tasks_.empty();
            });
            if (stopping_.load() && gen_tasks_.empty()) break;
            task = std::move(gen_tasks_.front());
            gen_tasks_.pop();
        }

        // ワーカーが blocks[] を書き込む。
        // gen_in_flight_ により同一チャンクへの並列書き込みは発生しない。
        gen_.generate(*task.second);

        // in_flight から除去（メインスレッドでの erase が不要になる）
        {
            std::lock_guard<std::mutex> lk(gen_mutex_);
            gen_in_flight_.erase(task.first);
        }

        // gen完了を通知（メインスレッドがneighbors解決してmesh_tasks_に投入する）
        {
            std::lock_guard<std::mutex> lk(done_mutex_);
            gen_done_queue_.push(task.first);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// meshWorkerLoop() — メッシュ構築ワーカースレッドのメインループ
//
// mesh_tasks_ キューからタスクを取り出し、光計算とポリゴン生成を行う。
// neighbors はメインスレッドで解決済みのため、ここでは findChunk を呼ばない
// （World::chunks_ map への並行読み書きを避けるため）。
//
// 【mesh_in_flight_ の erase について】
//   mesh_in_flight_ の erase は meshWorkerLoop ではなく processMeshDone（メインスレッド）
//   で行う。これにより、buildMesh 完了から processMeshDone での upload_queue_ 追加まで
//   の間、チャンクが mesh_in_flight_ に残り続けるため、upload 中の再ビルド競合を防ぐ。
// ─────────────────────────────────────────────────────────────────────────────
void ChunkManager::meshWorkerLoop() {
    while (true) {
        MeshTask task;
        {
            std::unique_lock<std::mutex> lk(mesh_mutex_);
            mesh_cv_.wait(lk, [this] {
                return stopping_.load() || !mesh_tasks_.empty();
            });
            if (stopping_.load() && mesh_tasks_.empty()) break;
            task = std::move(mesh_tasks_.front());
            mesh_tasks_.pop();
        }

        // is_generated フラグが false なら処理しない（稀な保険）
        // この場合は erase せず mesh_done_queue_ にも積まない
        // （processMeshDone が in_flight を erase するので、これはスキップして
        //   "zombie in_flight" になるが、次フレームでのenqueueが抑制されるだけ
        //   で機能的には問題ない。より堅牢にするなら done_queue に failed marker を送る）
        if (!task.chunk || !task.chunk->is_generated) {
            // 例外的: erase はここで行って zombie を防ぐ
            std::lock_guard<std::mutex> lk(mesh_mutex_);
            mesh_in_flight_.erase(task.pos);
            continue;
        }

        // メッシュ構築（CPU）: neighbors は既にメインスレッドで解決済み
        buildMesh(task.chunk, task.neighbors);

        // 完了を通知（mesh_in_flight_ の erase は processMeshDone で行う）
        {
            std::lock_guard<std::mutex> lk(mesh_done_mutex_);
            mesh_done_queue_.push(task.pos);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// enqueueMeshBuild() — メッシュ構築タスクをキューに投入する（メインスレッド専用）
//
// neighbors の解決はメインスレッドで行う。
// world_.findChunk() は World::chunks_ map の読み取りだが、
// allocateChunk() はメインスレッドのみ書き込むため、メインスレッド内での読み取りは安全。
//
// 重複投入防止:
//   - mesh_in_flight_: buildMesh 実行中または mesh_done_queue_ 待機中
//   - upload_pending_: upload_queue_ に入っており GPU に送信待ち
//   upload_pending_ の場合は rebuild_dirty_ に追加して uploadPending 後に再試行する。
// ─────────────────────────────────────────────────────────────────────────────
void ChunkManager::enqueueMeshBuild(ChunkPos pos, Chunk* chunk) {
    // mesh_in_flight_ チェック（buildMesh 中または done_queue 待機中）
    // ブロック編集などからの再ビルド要求が失われないよう rebuild_dirty_ に追加して次フレームで再試行
    {
        std::lock_guard<std::mutex> lk(mesh_mutex_);
        if (mesh_in_flight_.count(pos)) {
            rebuild_dirty_.insert(pos);
            return;
        }
    }

    // upload_pending_ チェック（upload_queue_ に入っており頂点データが読み取られる可能性）
    if (upload_pending_.count(pos)) {
        // アップロード完了後に再試行するよう延期セットに追加
        rebuild_dirty_.insert(pos);
        return;
    }

    // mesh_in_flight_ に追加（ここからワーカー完了＋processMeshDone処理まで保護される）
    {
        std::lock_guard<std::mutex> lk(mesh_mutex_);
        // もう一度チェック（上記 check から ここまでの間に別チャンクから投入される稀なケース）
        if (mesh_in_flight_.count(pos)) return;
        mesh_in_flight_.insert(pos);
    }

    // neighbors 解決（メインスレッドのみ: findChunk競合回避）
    MeshTask task;
    task.pos   = pos;
    task.chunk = chunk;
    task.neighbors.north = world_.findChunk({pos.x,     pos.z - 1});
    task.neighbors.south = world_.findChunk({pos.x,     pos.z + 1});
    task.neighbors.east  = world_.findChunk({pos.x + 1, pos.z    });
    task.neighbors.west  = world_.findChunk({pos.x - 1, pos.z    });
    // 未生成チャンクは nullptr として扱う（MeshBuilderが solid として処理）
    if (task.neighbors.north && !task.neighbors.north->is_generated) task.neighbors.north = nullptr;
    if (task.neighbors.south && !task.neighbors.south->is_generated) task.neighbors.south = nullptr;
    if (task.neighbors.east  && !task.neighbors.east->is_generated)  task.neighbors.east  = nullptr;
    if (task.neighbors.west  && !task.neighbors.west->is_generated)  task.neighbors.west  = nullptr;

    {
        std::lock_guard<std::mutex> lk(mesh_mutex_);
        mesh_tasks_.push(std::move(task));
    }
    mesh_cv_.notify_one();
}

// ─────────────────────────────────────────────────────────────────────────────
// update() — 毎フレーム呼ばれるチャンク管理の主処理
//
// 1. プレイヤー位置から中心チャンクを特定
// 2. 半径 render_distance_ 以内の未読込チャンクをワーカーキューに投入
// 3. gen完了チャンクをメッシュ構築キューに投入（軽量）
// 4. メッシュ構築完了チャンクをLRU管理・アップロードキューへ追加
// 5. 3フレームに1回、水の流れをシミュレートする
// 6. 待機中のチャンクをGPUにアップロードする（1フレームに数チャンクずつ）
// 7. upload_pending_ から外れた隣接再ビルドを処理する
// 8. 容量超過なら古いチャンクを破棄する
// ─────────────────────────────────────────────────────────────────────────────
void ChunkManager::update(float playerX, float playerZ, uint64_t frame) {
    // プレイヤーがいるチャンク座標を計算
    ChunkPos center = {
        (int32_t)std::floor(playerX / (float)CHUNK_SIZE_X),
        (int32_t)std::floor(playerZ / (float)CHUNK_SIZE_Z)
    };

    loadRadius(center);
    processGenDone();   // gen完了チャンクをmesh_tasks_に投入（軽量）
    processMeshDone();  // メッシュ完了チャンクのLRU管理・upload_queue追加

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

    // uploadPending後: アップロード完了で upload_pending_ から外れたチャンクの
    // 隣接再ビルドを処理する（uploadPending前に延期されていたもの）
    processRebuildDirty();

    // キャッシュが容量を超えていれば古いチャンクを破棄する
    evictIfOverBudget();

    // 300フレームに1回、状態をデバッグ出力
    if (frame - last_debug_frame_ >= 300) {
        size_t gen_in_flight_count;
        size_t mesh_in_flight_count;
        {
            std::lock_guard<std::mutex> lk(gen_mutex_);
            gen_in_flight_count = gen_in_flight_.size();
        }
        {
            std::lock_guard<std::mutex> lk(mesh_mutex_);
            mesh_in_flight_count = mesh_in_flight_.size();
        }
        fprintf(stderr, "[ChunkMgr] frame=%llu loaded=%zu upload_q=%zu gen_flight=%zu mesh_flight=%zu\n",
                (unsigned long long)frame, loaded_.size(), upload_queue_.size(),
                gen_in_flight_count, mesh_in_flight_count);
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
// 既にロード済み or 生成中 or メッシュ構築中のチャンクはスキップする。
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

            // 既にワーカーに投入済みならスキップ（gen または mesh の処理中）
            {
                std::lock_guard<std::mutex> lk(gen_mutex_);
                if (gen_in_flight_.count(p)) continue;
            }
            {
                std::lock_guard<std::mutex> lk(mesh_mutex_);
                if (mesh_in_flight_.count(p)) continue;
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
// processGenDone() — gen完了チャンクをメッシュ構築キューに投入する（軽量）
//
// gen_done_queue_ からチャンクポジションを取り出し、
// neighbors をメインスレッドで解決してから mesh_tasks_ に投入する。
// 重いメッシュ構築処理はmeshワーカースレッドで行う。
// ─────────────────────────────────────────────────────────────────────────────
void ChunkManager::processGenDone() {
    std::vector<ChunkPos> done;
    {
        std::lock_guard<std::mutex> lk(done_mutex_);
        while (!gen_done_queue_.empty() && (int)done.size() < CHUNKS_PER_FRAME_GEN) {
            done.push_back(gen_done_queue_.front());
            gen_done_queue_.pop();
        }
    }

    for (const ChunkPos& pos : done) {
        // gen_in_flight_ の erase は gen worker 側で実施済み
        Chunk* chunk = world_.findChunk(pos);
        if (!chunk || !chunk->is_generated) continue;

        // メッシュ構築タスクをキューに積む（neighbors解決はenqueueMeshBuild内で行う）
        enqueueMeshBuild(pos, chunk);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// processMeshDone() — メッシュ構築完了チャンクのLRU管理とアップロードキューへの追加
//
// mesh_done_queue_ から完了チャンクを取り出し、LRUに追加・upload_queue_への追加を行う。
// mesh_in_flight_ の erase もここで行う（ワーカー側では行わない）。
// 隣接チャンクの再ビルドは rebuild_dirty_ に追加して uploadPending 後に処理する。
// ─────────────────────────────────────────────────────────────────────────────
void ChunkManager::processMeshDone() {
    std::vector<ChunkPos> done;
    {
        std::lock_guard<std::mutex> lk(mesh_done_mutex_);
        while (!mesh_done_queue_.empty()) {
            done.push_back(mesh_done_queue_.front());
            mesh_done_queue_.pop();
        }
    }

    for (const ChunkPos& pos : done) {
        // mesh_in_flight_ から除去（ワーカーで行わず、ここで行う）
        // これにより upload_queue_ 追加前後にチャンクが再ビルドされるのを防ぐ
        {
            std::lock_guard<std::mutex> lk(mesh_mutex_);
            mesh_in_flight_.erase(pos);
        }

        Chunk* chunk = world_.findChunk(pos);
        if (!chunk) continue;

        // LRUに追加（溢れたチャンクのGPUリソースを破棄）
        auto evicted = loaded_.put(pos, chunk);
        for (const ChunkPos& ep : evicted) {
            Chunk* dead = world_.findChunk(ep);
            if (dead) renderer_.destroyChunkMesh(dead);
            upload_pending_.erase(ep);  // evict済みは upload_pending_ からも除去
        }

        // upload_queue_ への追加と upload_pending_ への登録
        upload_queue_.push(chunk);
        upload_pending_.insert(pos);

        // 隣接チャンクの再ビルドは rebuild_dirty_ に追加して uploadPending 後に処理する
        // （upload_queue_ にあるチャンクの頂点データ競合を避けるため即時投入しない）
        static const int NEIGHBOR_OFFSETS[4][2] = {{0,1},{0,-1},{1,0},{-1,0}};
        for (const auto& d : NEIGHBOR_OFFSETS) {
            ChunkPos np = {pos.x + d[0], pos.z + d[1]};
            if (!loaded_.contains(np)) continue;
            rebuild_dirty_.insert(np);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// processRebuildDirty() — 延期された隣接再ビルドを uploadPending 後に処理する
//
// rebuild_dirty_ に追加されたチャンクを enqueueMeshBuild に投入する。
// uploadPending の後に呼ばれるため、今フレームでアップロード済みのチャンクは
// upload_pending_ から外れており再ビルドが安全に投入できる。
// ─────────────────────────────────────────────────────────────────────────────
void ChunkManager::processRebuildDirty() {
    // std::move でローカルに退避してからイテレートする。
    // enqueueMeshBuild が内部で rebuild_dirty_ に再追加する場合があるため、
    // イテレート対象と追加先を分離して clear() による消失を防ぐ。
    auto dirty = std::move(rebuild_dirty_);
    for (const ChunkPos& pos : dirty) {
        if (!loaded_.contains(pos)) continue;
        Chunk* chunk = world_.findChunk(pos);
        if (!chunk || !chunk->is_generated) continue;
        enqueueMeshBuild(pos, chunk);
        // チャンクが mesh_in_flight_ または upload_pending_ の場合は
        // enqueueMeshBuild 内で rebuild_dirty_ に再追加されるため次フレームで再試行される
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// buildMesh() — チャンクのメッシュ（3D形状）を CPU 上で生成する
//
// neighbors は呼び出し元（enqueueMeshBuild）でメインスレッドが解決済み。
// meshWorkerLoop から呼ばれるため、OpenGL コールを含めてはならない。
// ─────────────────────────────────────────────────────────────────────────────
void ChunkManager::buildMesh(Chunk* chunk, const ChunkNeighbors& nb) {
    // 光の計算はメッシュ構築の前に行う（光レベルを頂点データに埋め込むため）
    LightCalculator::compute(*chunk, nb);
    MeshBuilder::build(*chunk, nb);
}

// ─────────────────────────────────────────────────────────────────────────────
// uploadPending() — 待機中のチャンクをGPUへ転送する
//
// 1フレームで全チャンクをアップロードすると処理が一瞬止まる（スパイク）。
// max_per_frame 個ずつに分けて、毎フレーム少しずつ処理する。
// アップロード後は upload_pending_ からチャンクを除去して再ビルドを許可する。
// evict済みチャンク（loaded_ に存在しない）はスキップしてGPU破壊後のアクセスを防ぐ。
// ─────────────────────────────────────────────────────────────────────────────
void ChunkManager::uploadPending(int max_per_frame) {
    int n = 0;
    while (!upload_queue_.empty() && n < max_per_frame) {
        Chunk* c = upload_queue_.front();
        upload_queue_.pop();
        if (!c) continue;

        // evict済みチャンクはスキップ（GPUリソース破壊後のアップロードを防ぐ）
        if (!loaded_.contains(c->pos)) {
            upload_pending_.erase(c->pos);
            continue;
        }

        if (c->is_dirty) {
            renderer_.uploadChunkMesh(c);  // 頂点・インデックスをGPUに送る
            ++n;
        }
        // アップロード済み（またはis_dirty=false）: upload_pending_ から除去して再ビルド許可
        upload_pending_.erase(c->pos);
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
        upload_pending_.erase(key);  // evict済みは upload_pending_ からも除去
    }
}

// 指定チャンクのメッシュを非同期で再構築してアップロードキューに追加する
// ブロックを壊す・置くときや隣接チャンクが到着したときに呼ばれる
void ChunkManager::rebuildChunkAt(ChunkPos pos) {
    if (!loaded_.contains(pos)) return;
    Chunk* chunk = world_.findChunk(pos);
    if (!chunk || !chunk->is_generated) return;
    enqueueMeshBuild(pos, chunk);
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
