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
    Wood   = 7,
    Leaves = 8,
    Cactus = 9,
    GoldOre = 11,
    DiamondOre = 12,
    ShortGrass = 13,
    Flower = 14,
    Mushroom = 15,
    PinkLeaves   = 16,  // 桜（春バイオーム）
    OrangeLeaves = 17,  // 紅葉オレンジ（秋バイオーム）
    Bow          = 18,  // 弓（アイテム: 設置不可・スタック上限1）
    Stick        = 19,  // 棒（アイテム: 設置不可・クラフト素材）
    Torch        = 20,  // 松明（設置可: 細い柱型 3D メッシュ）
    DragonEgg    = 21,  // ドラゴンエッグ（アイテム: 右クリックでエンダードラゴンを召喚）
    // ─── 村用ブロック ─────────────────────────────────────────────────────────
    Cobblestone  = 22,  // 石レンガ（村の建物の壁）
    Planks       = 23,  // 木の板材（村の屋根・床）
    Farmland     = 24,  // 農耕地（耕した土）
    GravelPath   = 25,  // 砂利道（村の道）
    Wheat        = 26,  // 小麦（農地に生える作物、クロスモデル）
    COUNT
};

// ブロックではなく "アイテム" として扱う BlockType を判定する。
// アイテムは右クリックで設置されず、種別固有の挙動を発火する。
inline bool isItem(BlockType t) {
    return t == BlockType::Bow || t == BlockType::Stick ||
           t == BlockType::DragonEgg;
}

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
constexpr int   RENDER_DISTANCE      = 17;    // chunks (= 272 blocks, subject requires 260+)
constexpr int   MAX_LOADED_CHUNKS    = 1300;  // (2*17+1)^2 = 1225 chunks max in view
constexpr float PLAYER_SPEED_NORMAL  = 1.0f;  // blocks/sec (walk)
constexpr float PLAYER_SPEED_SPRINT  = 2.0f;  // blocks/sec (sprint on ground)
constexpr float PLAYER_SPEED_FLY     = 20.0f; // blocks/sec (fly = 20x walk, subject V.3)
constexpr float FOV_DEGREES          = 80.0f;
constexpr float NEAR_PLANE           = 0.1f;
constexpr float FAR_PLANE            = 600.0f;
constexpr int   ATLAS_TILE_SIZE      = 16;    // pixels per tile
constexpr int   ATLAS_COLS           = 8;     // tiles per row
constexpr int   CHUNKS_PER_FRAME_GEN = 16;    // max mesh uploads per frame
constexpr int   SEA_LEVEL            = 42;    // y at which water surface sits
constexpr int   HOTBAR_SIZE          = 9;
constexpr int   STACK_MAX            = 64;

struct ItemStack {
    BlockType type  = BlockType::Air;
    int       count = 0;
};

struct Inventory {
    ItemStack slots[HOTBAR_SIZE] = {};
    int       selected           = 0;  // 0–8
};

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
        std::memset(blocks, 0, sizeof(blocks));
        std::memset(water_levels, 0, sizeof(water_levels));
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
