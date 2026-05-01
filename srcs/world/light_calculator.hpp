#pragma once
#include "types.hpp"
#include "world/mesh_builder.hpp"  // ChunkNeighbors

// ─────────────────────────────────────────────────────────────────────────────
// LightCalculator — Minecraft準拠の光伝播計算
//
// 【Minecraftの光システム】
//   - 空の明るさ (sky light): 0〜15。空からの太陽光。
//     地表から上は15、不透明ブロックで遮断、水・葉で1ずつ減衰。
//   - ブロックの明るさ (block light): 0〜15。松明などの光源から発せられる光。
//   - 基本の明るさ (combined): max(block_light, sky_light - sky_darken)
//     sky_darken は時間帯に応じて 0（昼）〜11（夜）の値。
//
// 【アルゴリズム】
//   Phase 1: 各列(x,z)について上から下へ直線的に空の明るさを伝播
//   Phase 2: BFSで水平方向に光を広げる（1ブロックごとに1減衰）
// ─────────────────────────────────────────────────────────────────────────────
class LightCalculator {
public:
    // チャンクの光マップを計算する（MeshBuilder::build の前に呼ぶこと）
    static void compute(Chunk& chunk, const ChunkNeighbors& neighbors);

    // ブロックが不透明かどうか（光を完全に遮断する）
    static bool isOpaque(BlockType t) {
        return t != BlockType::Air
            && t != BlockType::Water
            && t != BlockType::Leaves;
    }
};
