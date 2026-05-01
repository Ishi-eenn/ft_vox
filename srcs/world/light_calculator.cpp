// ─────────────────────────────────────────────────────────────────────────────
// light_calculator.cpp — Minecraft準拠の光伝播計算
//
// 【光伝播の2フェーズ】
//
// Phase 1: 直線伝播（空→地面方向）
//   各列(x,z)について y=255 から y=0 へ向かって空の明るさを伝播させる。
//   - Air: 明るさをそのまま通す（減衰なし）
//   - Water / Leaves: 通過するたびに明るさを1減らす
//   - その他（不透明ブロック）: 明るさを0にしてそこで止める
//
// Phase 2: BFS横方向拡散
//   Phase 1 で明るさ > 0 になったブロック + 隣チャンクの境界から
//   幅優先探索（BFS）で横方向に光を広げる。
//   6方向への伝播で1ずつ減衰する。
// ─────────────────────────────────────────────────────────────────────────────
#include "world/light_calculator.hpp"
#include <queue>
#include <cstring>

// BFSキューのエントリ
struct LightNode {
    int16_t x, y, z;
    uint8_t level;
};

// 6方向のオフセット（BFS拡散用）
static const int DX6[6] = { 1,-1, 0, 0, 0, 0};
static const int DY6[6] = { 0, 0, 1,-1, 0, 0};
static const int DZ6[6] = { 0, 0, 0, 0, 1,-1};

// ─────────────────────────────────────────────────────────────────────────────
// compute() — チャンクの光マップを計算する
// ─────────────────────────────────────────────────────────────────────────────
void LightCalculator::compute(Chunk& chunk, const ChunkNeighbors& neighbors) {
    // 光マップをリセット（sky=0, block=0）
    std::memset(chunk.light_map, 0, sizeof(chunk.light_map));

    std::queue<LightNode> sky_queue;

    // ─── Phase 1: 上から下への直線伝播 ─────────────────────────────────────────
    for (int x = 0; x < CHUNK_SIZE_X; ++x) {
        for (int z = 0; z < CHUNK_SIZE_Z; ++z) {
            uint8_t sky = MAX_LIGHT_LEVEL;  // y=255の上は常に15

            for (int y = CHUNK_SIZE_Y - 1; y >= 0; --y) {
                BlockType b = chunk.getBlock(x, y, z);

                if (isOpaque(b)) {
                    // 不透明ブロック: 光を遮断、この列はここで終了
                    sky = 0;
                    chunk.setSkyLight(x, y, z, 0);
                } else {
                    // 半透明ブロック（水・葉）は通過するたびに1減衰
                    if (b == BlockType::Water || b == BlockType::Leaves) {
                        if (sky > 0) --sky;
                    }
                    if (sky > 0) {
                        chunk.setSkyLight(x, y, z, sky);
                        sky_queue.push({(int16_t)x, (int16_t)y, (int16_t)z, sky});
                    }
                }
            }
        }
    }

    // ─── Phase 2: BFS拡散 ────────────────────────────────────────────────────

    // 隣チャンクの境界からシードを追加するヘルパー
    // neighbor_chunk: 隣チャンク、our_x/our_z: このチャンク内の境界座標
    // nb_x/nb_z: 隣チャンク内の境界座標
    auto seedBorder = [&](const Chunk* neighbor,
                          int our_x_start, int our_x_end,
                          int our_z_start, int our_z_end,
                          int nb_x_start, int nb_z_start) {
        if (!neighbor) return;
        for (int ox = our_x_start; ox <= our_x_end; ++ox) {
            for (int oz = our_z_start; oz <= our_z_end; ++oz) {
                for (int y = 0; y < CHUNK_SIZE_Y; ++y) {
                    int nbx = nb_x_start + (ox - our_x_start);
                    int nbz = nb_z_start + (oz - our_z_start);

                    // 隣の境界ブロックが不透明なら光は伝わらない
                    // （未計算チャンクの light_map=0xF0 による誤った光漏れを防ぐ）
                    if (isOpaque(neighbor->getBlock(nbx, y, nbz))) continue;

                    uint8_t nb_sl = neighbor->getSkyLight(nbx, y, nbz);
                    if (nb_sl <= 1) continue;
                    uint8_t prop = nb_sl - 1;

                    if (!isOpaque(chunk.getBlock(ox, y, oz))
                            && chunk.getSkyLight(ox, y, oz) < prop) {
                        chunk.setSkyLight(ox, y, oz, prop);
                        sky_queue.push({(int16_t)ox, (int16_t)y, (int16_t)oz, prop});
                    }
                }
            }
        }
    };

    // 北(z=0): 隣チャンクのz=CHUNK_SIZE_Z-1と隣接
    seedBorder(neighbors.north,
               0, CHUNK_SIZE_X - 1, 0, 0,
               0, CHUNK_SIZE_Z - 1);
    // 南(z=CHUNK_SIZE_Z-1): 隣チャンクのz=0と隣接
    seedBorder(neighbors.south,
               0, CHUNK_SIZE_X - 1, CHUNK_SIZE_Z - 1, CHUNK_SIZE_Z - 1,
               0, 0);
    // 東(x=CHUNK_SIZE_X-1): 隣チャンクのx=0と隣接
    seedBorder(neighbors.east,
               CHUNK_SIZE_X - 1, CHUNK_SIZE_X - 1, 0, CHUNK_SIZE_Z - 1,
               0, 0);
    // 西(x=0): 隣チャンクのx=CHUNK_SIZE_X-1と隣接
    seedBorder(neighbors.west,
               0, 0, 0, CHUNK_SIZE_Z - 1,
               CHUNK_SIZE_X - 1, 0);

    // BFS本体: キューが空になるまで6方向に伝播
    while (!sky_queue.empty()) {
        auto [qx, qy, qz, lvl] = sky_queue.front();
        sky_queue.pop();

        // 古いエントリのスキップ（より高い光レベルで既に更新済み）
        if (chunk.getSkyLight(qx, qy, qz) != lvl) continue;
        if (lvl == 0) continue;

        uint8_t next = lvl - 1;

        for (int d = 0; d < 6; ++d) {
            int nx = qx + DX6[d];
            int ny = qy + DY6[d];
            int nz = qz + DZ6[d];

            // チャンク範囲外はBFS対象外（隣チャンクへの伝播はseedBorderで対応）
            if (nx < 0 || nx >= CHUNK_SIZE_X) continue;
            if (ny < 0 || ny >= CHUNK_SIZE_Y) continue;
            if (nz < 0 || nz >= CHUNK_SIZE_Z) continue;

            if (isOpaque(chunk.getBlock(nx, ny, nz))) continue;

            if (chunk.getSkyLight(nx, ny, nz) < next) {
                chunk.setSkyLight(nx, ny, nz, next);
                sky_queue.push({(int16_t)nx, (int16_t)ny, (int16_t)nz, next});
            }
        }
    }

    // ─── ブロック光: 現時点では光源ブロックなし（将来の松明等に備えた枠組み）───
    // block_light は memset で 0 初期化済みなので何もしない
}
