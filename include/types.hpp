#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include <cstring>

// ─── Block ───────────────────────────────────────────────────────────────────
enum class BlockType : uint8_t {
    Air   = 0,
    Grass = 1,
    Dirt  = 2,
    Stone = 3,
    Sand  = 4,
    Snow  = 5,
    Water = 6,
    Wood  = 7,
    Leaves = 8,
    COUNT
};

enum class Face : uint8_t {
    Top    = 0,
    Bottom = 1,
    North  = 2,   // -Z
    South  = 3,   // +Z
    East   = 4,   // +X
    West   = 5,   // -X
    COUNT
};

// ─── Chunk position ──────────────────────────────────────────────────────────
struct ChunkPos {
    int32_t x, z;
    bool operator==(const ChunkPos& o) const noexcept { return x == o.x && z == o.z; }
    bool operator!=(const ChunkPos& o) const noexcept { return !(*this == o); }
};

struct ChunkPosHash {
    size_t operator()(const ChunkPos& p) const noexcept {
        uint64_t v = ((uint64_t)(uint32_t)p.x << 32) | (uint32_t)p.z;
        return std::hash<uint64_t>()(v);
    }
};

// ─── Vertex (interleaved: pos 12B, uv 8B, normal 12B = 32B) ─────────────────
struct Vertex {
    float x, y, z;
    float u, v;
    float nx, ny, nz;
};

// ─── Constants ───────────────────────────────────────────────────────────────
constexpr int   CHUNK_SIZE_X         = 16;
constexpr int   CHUNK_SIZE_Y         = 256;
constexpr int   CHUNK_SIZE_Z         = 16;
constexpr int   RENDER_DISTANCE      = 10;    // chunks
constexpr int   MAX_LOADED_CHUNKS    = 500;
constexpr float PLAYER_SPEED_NORMAL  = 1.0f;  // blocks/sec
constexpr float PLAYER_SPEED_FAST    = 20.0f; // blocks/sec (20x key)
constexpr float FOV_DEGREES          = 80.0f;
constexpr float NEAR_PLANE           = 0.1f;
constexpr float FAR_PLANE            = 600.0f;
constexpr int   ATLAS_TILE_SIZE      = 16;    // pixels per tile
constexpr int   ATLAS_COLS           = 8;     // tiles per row
constexpr int   CHUNKS_PER_FRAME_GEN = 16;    // max mesh uploads per frame
constexpr int   SEA_LEVEL            = 42;    // y at which water surface sits

// ─── Chunk data ───────────────────────────────────────────────────────────────
struct ChunkGpuMesh {
    uint32_t vao       = 0;
    uint32_t vbo       = 0;
    uint32_t ebo       = 0;
    int32_t  idx_count = 0;
    bool     uploaded  = false;
};

struct Chunk {
    // Block data: [x][y][z]
    BlockType blocks[CHUNK_SIZE_X][CHUNK_SIZE_Y][CHUNK_SIZE_Z];
    ChunkPos  pos;
    bool      is_generated = false;
    bool      is_dirty     = true;

    // CPU mesh (built by MeshBuilder, consumed by Renderer upload)
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;

    // GPU mesh (owned by Renderer)
    ChunkGpuMesh gpu;

    // LRU timestamp
    uint64_t last_access_frame = 0;

    Chunk() { std::memset(blocks, 0, sizeof(blocks)); }

    inline BlockType getBlock(int x, int y, int z) const noexcept {
        if (x < 0 || x >= CHUNK_SIZE_X || y < 0 || y >= CHUNK_SIZE_Y || z < 0 || z >= CHUNK_SIZE_Z)
            return BlockType::Air;
        return blocks[x][y][z];
    }
    inline void setBlock(int x, int y, int z, BlockType t) noexcept {
        if (x < 0 || x >= CHUNK_SIZE_X || y < 0 || y >= CHUNK_SIZE_Y || z < 0 || z >= CHUNK_SIZE_Z)
            return;
        blocks[x][y][z] = t;
    }
};
