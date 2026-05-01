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

struct WorldPos {
    int32_t x, y, z;
    bool operator==(const WorldPos& o) const noexcept {
        return x == o.x && y == o.y && z == o.z;
    }
};

struct WorldPosHash {
    size_t operator()(const WorldPos& p) const noexcept {
        uint64_t a = ((uint64_t)(uint32_t)p.x << 32) | (uint32_t)p.y;
        uint64_t b = ((uint64_t)(uint32_t)p.z << 32) ^ a;
        return std::hash<uint64_t>()(b);
    }
};

// ─── Vertex (interleaved: pos 12B, uv 8B, normal 12B, light 8B = 40B) ───────
struct Vertex {
    float x, y, z;
    float u, v;
    float nx, ny, nz;
    float sky_light;    // 空の明るさ（0.0〜1.0、0/15〜15/15に対応）
    float block_light;  // ブロックの明るさ（0.0〜1.0）
};

// ─── Constants ───────────────────────────────────────────────────────────────
constexpr int   CHUNK_SIZE_X         = 16;
constexpr int   CHUNK_SIZE_Y         = 256;
constexpr int   CHUNK_SIZE_Z         = 16;
constexpr int   RENDER_DISTANCE_MIN  = 1;     // chunks (= 16 blocks, satisfies 14-cube floor)
constexpr int   RENDER_DISTANCE_MAX  = 10;    // chunks (= 160 blocks)
constexpr int   RENDER_DISTANCE      = RENDER_DISTANCE_MAX;  // initial value
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

// ─── 光レベルの最大値（Minecraft準拠: 0〜15の16段階）────────────────────────
constexpr uint8_t MAX_LIGHT_LEVEL = 15;

// ─── Chunk data ───────────────────────────────────────────────────────────────
struct ChunkGpuMesh {
    uint32_t vao             = 0;
    uint32_t vbo             = 0;
    uint32_t ebo             = 0;        // opaque + water indices (packed)
    int32_t  idx_count       = 0;        // opaque index count
    int32_t  idx_count_water = 0;        // water index count (after opaque in EBO)
    bool     uploaded        = false;
};

struct Chunk {
    // Block data: [x][y][z]
    BlockType blocks[CHUNK_SIZE_X][CHUNK_SIZE_Y][CHUNK_SIZE_Z];
    uint8_t   water_levels[CHUNK_SIZE_X][CHUNK_SIZE_Y][CHUNK_SIZE_Z];
    // 光マップ: [x][y][z]  上位4bit=空の明るさ(0-15)、下位4bit=ブロックの明るさ(0-15)
    uint8_t   light_map[CHUNK_SIZE_X][CHUNK_SIZE_Y][CHUNK_SIZE_Z];
    ChunkPos  pos;
    bool      is_generated = false;
    bool      is_dirty     = true;

    // CPU mesh (built by MeshBuilder, consumed by Renderer upload)
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;        // opaque faces
    std::vector<uint32_t> indices_water;  // transparent (water) faces

    // GPU mesh (owned by Renderer)
    ChunkGpuMesh gpu;

    // LRU timestamp
    uint64_t last_access_frame = 0;

    Chunk() {
        std::memset(blocks,      0,    sizeof(blocks));
        std::memset(water_levels, 0,   sizeof(water_levels));
        // 初期値: 空の明るさ=15（0xF0）、ブロックの明るさ=0 → 0xF0
        std::memset(light_map,   0,    sizeof(light_map));
    }

    // ── 光レベルのアクセサ ──────────────────────────────────────────────────────
    inline uint8_t getSkyLight(int x, int y, int z) const noexcept {
        if (x < 0 || x >= CHUNK_SIZE_X || y < 0 || y >= CHUNK_SIZE_Y || z < 0 || z >= CHUNK_SIZE_Z)
            return MAX_LIGHT_LEVEL;  // チャンク外（上空）は最大輝度
        return (light_map[x][y][z] >> 4) & 0x0F;
    }
    inline uint8_t getBlockLight(int x, int y, int z) const noexcept {
        if (x < 0 || x >= CHUNK_SIZE_X || y < 0 || y >= CHUNK_SIZE_Y || z < 0 || z >= CHUNK_SIZE_Z)
            return 0;
        return light_map[x][y][z] & 0x0F;
    }
    inline void setSkyLight(int x, int y, int z, uint8_t v) noexcept {
        if (x < 0 || x >= CHUNK_SIZE_X || y < 0 || y >= CHUNK_SIZE_Y || z < 0 || z >= CHUNK_SIZE_Z)
            return;
        light_map[x][y][z] = (light_map[x][y][z] & 0x0F) | ((v & 0x0F) << 4);
    }
    inline void setBlockLight(int x, int y, int z, uint8_t v) noexcept {
        if (x < 0 || x >= CHUNK_SIZE_X || y < 0 || y >= CHUNK_SIZE_Y || z < 0 || z >= CHUNK_SIZE_Z)
            return;
        light_map[x][y][z] = (light_map[x][y][z] & 0xF0) | (v & 0x0F);
    }

    inline BlockType getBlock(int x, int y, int z) const noexcept {
        if (x < 0 || x >= CHUNK_SIZE_X || y < 0 || y >= CHUNK_SIZE_Y || z < 0 || z >= CHUNK_SIZE_Z)
            return BlockType::Air;
        return blocks[x][y][z];
    }
    inline void setBlock(int x, int y, int z, BlockType t) noexcept {
        if (x < 0 || x >= CHUNK_SIZE_X || y < 0 || y >= CHUNK_SIZE_Y || z < 0 || z >= CHUNK_SIZE_Z)
            return;
        blocks[x][y][z] = t;
        if (t != BlockType::Water) water_levels[x][y][z] = 0;
    }
    inline uint8_t getWaterLevel(int x, int y, int z) const noexcept {
        if (x < 0 || x >= CHUNK_SIZE_X || y < 0 || y >= CHUNK_SIZE_Y || z < 0 || z >= CHUNK_SIZE_Z)
            return 0;
        return water_levels[x][y][z];
    }
    inline void setWaterLevel(int x, int y, int z, uint8_t level) noexcept {
        if (x < 0 || x >= CHUNK_SIZE_X || y < 0 || y >= CHUNK_SIZE_Y || z < 0 || z >= CHUNK_SIZE_Z)
            return;
        water_levels[x][y][z] = level;
    }
};
