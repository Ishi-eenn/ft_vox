#pragma once
#include "types.hpp"

// Neighbor chunks: needed to determine visibility at chunk boundaries.
// Pass nullptr if neighbor is not loaded (treat as solid = no exposed face on that side).
struct ChunkNeighbors {
    const Chunk* north = nullptr;  // -Z neighbor
    const Chunk* south = nullptr;  // +Z neighbor
    const Chunk* east  = nullptr;  // +X neighbor
    const Chunk* west  = nullptr;  // -X neighbor
};

class MeshBuilder {
public:
    // Build mesh into chunk.vertices and chunk.indices.
    // Does NOT upload to GPU.
    static void build(Chunk& chunk, const ChunkNeighbors& neighbors);

private:
    static bool isSolid(BlockType t) { return t != BlockType::Air; }
    static bool isWater(BlockType t) { return t == BlockType::Water; }
    static BlockType getNeighborBlock(
        int nx, int ny, int nz,
        const Chunk& chunk,
        const ChunkNeighbors& neighbors);
    // 隣接ブロック位置の空の明るさ・ブロックの明るさを取得（チャンク境界対応）
    static uint8_t getNeighborSkyLight(
        int nx, int ny, int nz,
        const Chunk& chunk,
        const ChunkNeighbors& neighbors);
    static uint8_t getNeighborBlockLight(
        int nx, int ny, int nz,
        const Chunk& chunk,
        const ChunkNeighbors& neighbors);
    static uint8_t getNeighborWaterLevel(
        int nx, int ny, int nz,
        const Chunk& chunk,
        const ChunkNeighbors& neighbors);
    static float getWaterSurfaceHeight(
        int x, int y, int z,
        const Chunk& chunk,
        const ChunkNeighbors& neighbors);
    static void computeWaterTopHeights(
        float out[4],
        int x, int y, int z,
        const Chunk& chunk,
        const ChunkNeighbors& neighbors);

    static void addFace(
        std::vector<Vertex>& verts,
        std::vector<uint32_t>& indices,
        int x, int y, int z,
        Face face,
        BlockType type,
        const Chunk& chunk,
        const ChunkNeighbors& neighbors);
};
