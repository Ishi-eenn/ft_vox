// ─────────────────────────────────────────────────────────────────────────────
// world.cpp — ワールド全体のブロックデータと水のシミュレーションを管理する
//
// 【ワールドの仕組み】
//   世界は「チャンク」と呼ばれる 16×256×16 ブロックの塊に分割されている。
//   全チャンクを `chunks_` マップで管理し、座標（ChunkPos）をキーとして検索する。
//
// 【水のシミュレーション】
//   Minecraft に似た水流ロジックを実装している。
//
//   「水源ブロック」: 永久に存在し、周囲に水を広げる。
//   「流れる水」: 水源から伝わって生まれる一時的な水。以下のルールで動く:
//     ・真上に水がある → レベル0（垂直に落ちる水柱）
//     ・水源/流れから水平に1マス離れるごとに深さ+1
//     ・深さ7を超えると空気になる（水は7マスまでしか広がらない）
//
//   `active_water_` には「次のフレームで処理が必要な水の座標」を積んでおき、
//   stepWater() が呼ばれるたびに周辺ブロックを再計算して変化を伝播させる。
// ─────────────────────────────────────────────────────────────────────────────
#include "world/world.hpp"
#include <cmath>
#include <unordered_set>

// ─────────────────────────────────────────────────────────────────────────────
// コンストラクタ / setSeed()
// ─────────────────────────────────────────────────────────────────────────────
World::World(uint32_t seed) {
    setSeed(seed);
}

void World::setSeed(uint32_t seed) {
    seed_ = seed;
    gen_.setSeed(seed);  // 地形生成器（TerrainGen）にシードを設定
}

// ─────────────────────────────────────────────────────────────────────────────
// ユーティリティ関数
// ─────────────────────────────────────────────────────────────────────────────

// 負の整数でも正しく切り捨て除算する（C++ の / は 0 方向へ丸める）
// 例: floorDiv(-1, 16) = -1 （C++ の -1/16 = 0 とは異なる）
int World::floorDiv(int a, int b) {
    return (int)std::floor((double)a / b);
}

// ワールド座標 (wx, wz) がどのチャンクに属するかを返す
ChunkPos World::worldToChunk(int wx, int wz) {
    return {floorDiv(wx, CHUNK_SIZE_X), floorDiv(wz, CHUNK_SIZE_Z)};
}

// 固体ブロック判定（空気と水以外は固体）
bool World::isSolidBlock(BlockType type) const {
    return type != BlockType::Air && type != BlockType::Water;
}

// チャンク座標が指定範囲内に入っているか判定（水シミュレーション範囲の絞り込みに使う）
bool World::inChunkRange(ChunkPos pos, ChunkPos min_chunk, ChunkPos max_chunk) const {
    return pos.x >= min_chunk.x && pos.x <= max_chunk.x
        && pos.z >= min_chunk.z && pos.z <= max_chunk.z;
}

// 水ブロック判定
bool World::isWaterBlock(int wx, int wy, int wz) const {
    return getWorldBlock(wx, wy, wz) == BlockType::Water;
}

// 水が流れ込める場所かどうか（空気か流れる水なら true）
bool World::canFlowInto(int wx, int wy, int wz) const {
    BlockType t = getWorldBlock(wx, wy, wz);
    return t == BlockType::Air || t == BlockType::Water;
}

// 水源ブロック判定（水ブロックで、かつ `flowing_water_` に登録されていない）
bool World::isSourceWater(int wx, int wy, int wz) const {
    if (getWorldBlock(wx, wy, wz) != BlockType::Water) return false;
    return flowing_water_.find({wx, wy, wz}) == flowing_water_.end();
}

// 流れる水ブロック判定（`flowing_water_` に登録されているか）
// level_out が非 null なら流れの深さを書き込む
bool World::isFlowingWater(int wx, int wy, int wz, uint8_t* level_out) const {
    auto it = flowing_water_.find({wx, wy, wz});
    if (it == flowing_water_.end()) return false;
    if (level_out) *level_out = it->second;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// activateWaterAt() — 指定座標を水シミュレーションの「次の処理対象」に登録する
// ─────────────────────────────────────────────────────────────────────────────
void World::activateWaterAt(int wx, int wy, int wz) {
    if (wy < 0 || wy >= CHUNK_SIZE_Y) return;
    active_water_.insert({wx, wy, wz});
}

// 指定座標とその隣接6方向を全部アクティブにする
void World::activateWaterNeighborhood(int wx, int wy, int wz) {
    static const int OFFSETS[][3] = {
        { 0, 0, 0}, { 1, 0, 0}, {-1, 0, 0}, { 0, 0, 1}, { 0, 0,-1},
        { 0, 1, 0}, { 0,-1, 0}
    };
    for (const auto& o : OFFSETS) {
        activateWaterAt(wx + o[0], wy + o[1], wz + o[2]);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// setExistingWorldBlock() — 既存チャンク内のブロックを変更する（内部用）
//
// チャンクがロード済みの場合だけ変更する。存在しないチャンクは無視する。
// ─────────────────────────────────────────────────────────────────────────────
bool World::setExistingWorldBlock(int wx, int wy, int wz, BlockType type) {
    if (wy < 0 || wy >= CHUNK_SIZE_Y) return false;
    ChunkPos pos = worldToChunk(wx, wz);
    int lx = wx - pos.x * CHUNK_SIZE_X;
    int lz = wz - pos.z * CHUNK_SIZE_Z;
    auto it = chunks_.find(pos);
    if (it == chunks_.end()) return false;
    it->second->setBlock(lx, wy, lz, type);
    // 水ブロックを置くとき、水位が未設定なら最大（8）に設定する
    if (type == BlockType::Water && it->second->getWaterLevel(lx, wy, lz) == 0) {
        it->second->setWaterLevel(lx, wy, lz, 8);
    }
    it->second->is_dirty = true;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// getWorldBlock() — ワールド座標でブロックの種類を取得する
//
// Y 範囲外は Air を返す。チャンクがロードされていなければ Air を返す。
// ─────────────────────────────────────────────────────────────────────────────
BlockType World::getWorldBlock(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= CHUNK_SIZE_Y) return BlockType::Air;
    int cx = floorDiv(wx, CHUNK_SIZE_X);
    int cz = floorDiv(wz, CHUNK_SIZE_Z);
    int lx = wx - cx * CHUNK_SIZE_X;
    int lz = wz - cz * CHUNK_SIZE_Z;
    auto it = chunks_.find({cx, cz});
    if (it == chunks_.end()) return BlockType::Air;
    return it->second->getBlock(lx, wy, lz);
}

// ─────────────────────────────────────────────────────────────────────────────
// setWorldBlock() — ブロックを破壊/設置する（プレイヤー操作から呼ばれる）
//
// ブロックが水に関係する場合は、周辺の水シミュレーションも起動する。
// ─────────────────────────────────────────────────────────────────────────────
bool World::setWorldBlock(int wx, int wy, int wz, BlockType type) {
    if (wy < 0 || wy >= CHUNK_SIZE_Y) return false;
    BlockType old = getWorldBlock(wx, wy, wz);
    if (!setExistingWorldBlock(wx, wy, wz, type)) return false;

    flowing_water_.erase({wx, wy, wz});
    if (type == BlockType::Water || old == BlockType::Water) {
        // 水ブロックを置いた/壊した → 水位を更新して周辺を再計算
        ChunkPos pos = worldToChunk(wx, wz);
        auto it = chunks_.find(pos);
        if (it != chunks_.end()) {
            int lx = wx - pos.x * CHUNK_SIZE_X;
            int lz = wz - pos.z * CHUNK_SIZE_Z;
            it->second->setWaterLevel(lx, wy, lz, type == BlockType::Water ? 8 : 0);
        }
        activateWaterNeighborhood(wx, wy, wz);
    } else {
        // 固体ブロックを置いた/壊した → 隣接する水ブロックが再流動する可能性
        static const int OFFSETS[][3] = {
            { 1, 0, 0}, {-1, 0, 0}, { 0, 0, 1}, { 0, 0,-1}, { 0, 1, 0}
        };
        for (const auto& o : OFFSETS) {
            if (isWaterBlock(wx + o[0], wy + o[1], wz + o[2])) {
                activateWaterNeighborhood(wx + o[0], wy + o[1], wz + o[2]);
            }
        }
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// getOrCreateChunk() — チャンクを取得し、なければ生成して追加する
//
// 地形生成（TerrainGen::generate）は初回アクセス時だけ実行され、
// 以降は同じポインタを返す。
// ─────────────────────────────────────────────────────────────────────────────
Chunk* World::getOrCreateChunk(ChunkPos pos) {
    auto it = chunks_.find(pos);
    if (it != chunks_.end()) return it->second.get();

    auto chunk = std::make_unique<Chunk>();
    chunk->pos = pos;
    gen_.generate(*chunk);  // Perlin ノイズで地形を生成する

    Chunk* raw = chunk.get();
    chunks_[pos] = std::move(chunk);
    return raw;
}

// allocateChunk() — 空チャンクを確保してmapに挿入する（地形生成なし）
//
// 非同期生成用: メインスレッドがチャンクのメモリを確保し、
// ワーカースレッドが後から blocks[] にデータを書き込む。
// メインスレッドからのみ呼ぶこと。
Chunk* World::allocateChunk(ChunkPos pos) {
    auto it = chunks_.find(pos);
    if (it != chunks_.end()) return it->second.get();

    auto chunk = std::make_unique<Chunk>();
    chunk->pos = pos;
    // is_generated = false のまま（ワーカーが後で true にする）
    Chunk* raw = chunk.get();
    chunks_[pos] = std::move(chunk);
    return raw;
}

// findChunk() — 存在するチャンクを返す。なければ nullptr。
//
// 生成副作用なし。メインスレッドからのみ呼ぶこと。
Chunk* World::findChunk(ChunkPos pos) const {
    auto it = chunks_.find(pos);
    return it != chunks_.end() ? it->second.get() : nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// stepWater() — 水シミュレーションを1ステップ進める
//
// 【水位レベルのエンコーディング】
//   flowing_water_ と water_levels に格納される値:
//     0 (stored 8): 真上から水が流れてくる垂直な水柱
//     1-7:          水源から水平方向に広がる流れ（数字が大きいほど浅く遠い）
//   flowing_water_ に登録されていない水 = 永久水源ブロック
//
// 【アルゴリズム】
//   1. active_water_ から処理対象を取り出す
//   2. 各候補の6近傍も候補に追加（変化が波紋状に広がる）
//   3. 各候補について「あるべき深さ」を計算する:
//      a. 真上が水 → 深さ 0（垂直落下）
//      b. 水平隣接から最小深さ+1（横方向への広がり）
//      c. 計算深さが7を超える → 空気にすべき
//   4. 現在の状態と「あるべき状態」が違えばブロックを更新して
//      変化座標を returned vector に追加し、周辺を再 activate する
// ─────────────────────────────────────────────────────────────────────────────
std::vector<WorldPos> World::stepWater(ChunkPos min_chunk, ChunkPos max_chunk) {
    if (active_water_.empty()) return {};

    // レンダリング範囲内のアクティブ水だけを処理する
    std::vector<WorldPos> seeds;
    seeds.reserve(active_water_.size());
    for (const WorldPos& pos : active_water_) {
        if (inChunkRange(worldToChunk(pos.x, pos.z), min_chunk, max_chunk))
            seeds.push_back(pos);
    }
    active_water_.clear();
    if (seeds.empty()) return {};

    // 処理対象の候補集合を作る（各シードの7近傍を追加）
    std::unordered_set<WorldPos, WorldPosHash> candidates;
    static const int OFFSETS[][3] = {
        { 0, 0, 0}, { 1, 0, 0}, {-1, 0, 0}, { 0, 0, 1}, { 0, 0,-1},
        { 0, 1, 0}, { 0,-1, 0}
    };
    for (const WorldPos& pos : seeds) {
        for (const auto& o : OFFSETS) {
            WorldPos p = {pos.x + o[0], pos.y + o[1], pos.z + o[2]};
            if (p.y < 0 || p.y >= CHUNK_SIZE_Y) continue;
            if (!inChunkRange(worldToChunk(p.x, p.z), min_chunk, max_chunk)) continue;
            candidates.insert(p);
        }
    }

    std::vector<WorldPos> changed;
    changed.reserve(candidates.size());

    // 水平4方向のオフセット
    static const int HOFFSETS[][2] = {{ 1, 0}, {-1, 0}, {0, 1}, {0,-1}};

    for (const WorldPos& pos : candidates) {
        BlockType old_type = getWorldBlock(pos.x, pos.y, pos.z);
        uint8_t old_depth = 0;
        bool was_flowing = isFlowingWater(pos.x, pos.y, pos.z, &old_depth);
        bool was_source  = (old_type == BlockType::Water && !was_flowing);

        if (isSolidBlock(old_type)) continue;  // 固体は変えない
        if (was_source) continue;              // 水源は永久不変

        // ── 「あるべき深さ」を計算する ────────────────────────────────────────
        // 255 = 空気にすべき; 0 = 垂直落下; 1-7 = 水平広がり
        uint8_t desired_depth = 255;

        // 優先1: 真上が水 → 垂直に落ちる（深さ0 = 満水）
        if (isWaterBlock(pos.x, pos.y + 1, pos.z)) {
            desired_depth = 0;
        } else {
            // 優先2: 水平隣接から広がる
            // ただし、隣が「真下が空気/水」= 自分も落下中の場合は横に広がらない
            // （Minecraft の動作: 水柱は横に溢れ出さない）
            uint8_t min_depth = 255;
            for (const auto& o : HOFFSETS) {
                int nx = pos.x + o[0], nz = pos.z + o[1];
                if (getWorldBlock(nx, pos.y, nz) != BlockType::Water) continue;

                BlockType below_neighbor = getWorldBlock(nx, pos.y - 1, nz);
                if (below_neighbor == BlockType::Air || below_neighbor == BlockType::Water)
                    continue;  // 隣が落下中 → 横には広がらない

                uint8_t nd;
                bool nf = isFlowingWater(nx, pos.y, nz, &nd);
                if (!nf) nd = 0;  // 水源 = 深さ0 として扱う

                if (nd < min_depth) min_depth = nd;
            }
            if (min_depth < 255 && min_depth + 1 <= 7) {
                desired_depth = min_depth + 1;
            }
        }

        // ── 変化を適用する ────────────────────────────────────────────────────
        bool should_be_water = (desired_depth <= 7);  // 0-7 = 水, 8+ = 空気
        uint8_t stored_level = (desired_depth == 0) ? 8 : desired_depth;

        if (should_be_water) {
            // 状態が変わった場合だけ更新する（不要な再メッシュを避ける）
            bool state_changed = (old_type != BlockType::Water)
                              || (!was_flowing)
                              || (old_depth != desired_depth);
            if (state_changed) {
                if (setExistingWorldBlock(pos.x, pos.y, pos.z, BlockType::Water)) {
                    ChunkPos cpos = worldToChunk(pos.x, pos.z);
                    auto it = chunks_.find(cpos);
                    if (it != chunks_.end()) {
                        int lx = pos.x - cpos.x * CHUNK_SIZE_X;
                        int lz = pos.z - cpos.z * CHUNK_SIZE_Z;
                        it->second->setWaterLevel(lx, pos.y, lz, stored_level);
                    }
                    flowing_water_[pos] = desired_depth;
                    changed.push_back(pos);
                    activateWaterNeighborhood(pos.x, pos.y, pos.z);
                }
            }
        } else if (was_flowing) {
            // 流れる水が消えるべき場合: 空気に戻す
            if (setExistingWorldBlock(pos.x, pos.y, pos.z, BlockType::Air)) {
                flowing_water_.erase(pos);
                changed.push_back(pos);
                activateWaterNeighborhood(pos.x, pos.y, pos.z);
            }
        }
    }

    return changed;
}
