#include "world/world.hpp"
#include <cmath>
#include <unordered_set>

World::World(uint32_t seed) {
    setSeed(seed);
}

void World::setSeed(uint32_t seed) {
    seed_ = seed;
    gen_.setSeed(seed);
}

int World::floorDiv(int a, int b) {
    return (int)std::floor((double)a / b);
}

ChunkPos World::worldToChunk(int wx, int wz) {
    return {floorDiv(wx, CHUNK_SIZE_X), floorDiv(wz, CHUNK_SIZE_Z)};
}

bool World::isSolidBlock(BlockType type) const {
    return type != BlockType::Air && type != BlockType::Water;
}

bool World::inChunkRange(ChunkPos pos, ChunkPos min_chunk, ChunkPos max_chunk) const {
    return pos.x >= min_chunk.x && pos.x <= max_chunk.x
        && pos.z >= min_chunk.z && pos.z <= max_chunk.z;
}

bool World::isWaterBlock(int wx, int wy, int wz) const {
    return getWorldBlock(wx, wy, wz) == BlockType::Water;
}

bool World::canFlowInto(int wx, int wy, int wz) const {
    BlockType t = getWorldBlock(wx, wy, wz);
    return t == BlockType::Air || t == BlockType::Water;
}

bool World::isSourceWater(int wx, int wy, int wz) const {
    if (getWorldBlock(wx, wy, wz) != BlockType::Water) return false;
    return flowing_water_.find({wx, wy, wz}) == flowing_water_.end();
}

bool World::isFlowingWater(int wx, int wy, int wz, uint8_t* level_out) const {
    auto it = flowing_water_.find({wx, wy, wz});
    if (it == flowing_water_.end()) return false;
    if (level_out) *level_out = it->second;
    return true;
}


void World::activateWaterAt(int wx, int wy, int wz) {
    if (wy < 0 || wy >= CHUNK_SIZE_Y) return;
    active_water_.insert({wx, wy, wz});
}

void World::activateWaterNeighborhood(int wx, int wy, int wz) {
    static const int OFFSETS[][3] = {
        { 0, 0, 0}, { 1, 0, 0}, {-1, 0, 0}, { 0, 0, 1}, { 0, 0,-1},
        { 0, 1, 0}, { 0,-1, 0}
    };
    for (const auto& o : OFFSETS) {
        activateWaterAt(wx + o[0], wy + o[1], wz + o[2]);
    }
}

bool World::setExistingWorldBlock(int wx, int wy, int wz, BlockType type) {
    if (wy < 0 || wy >= CHUNK_SIZE_Y) return false;
    ChunkPos pos = worldToChunk(wx, wz);
    int lx = wx - pos.x * CHUNK_SIZE_X;
    int lz = wz - pos.z * CHUNK_SIZE_Z;
    auto it = chunks_.find(pos);
    if (it == chunks_.end()) return false;
    it->second->setBlock(lx, wy, lz, type);
    if (type == BlockType::Water && it->second->getWaterLevel(lx, wy, lz) == 0) {
        it->second->setWaterLevel(lx, wy, lz, 8);
    }
    it->second->is_dirty = true;
    return true;
}

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

bool World::setWorldBlock(int wx, int wy, int wz, BlockType type) {
    if (wy < 0 || wy >= CHUNK_SIZE_Y) return false;
    BlockType old = getWorldBlock(wx, wy, wz);
    if (!setExistingWorldBlock(wx, wy, wz, type)) return false;

    flowing_water_.erase({wx, wy, wz});
    if (type == BlockType::Water || old == BlockType::Water) {
        ChunkPos pos = worldToChunk(wx, wz);
        auto it = chunks_.find(pos);
        if (it != chunks_.end()) {
            int lx = wx - pos.x * CHUNK_SIZE_X;
            int lz = wz - pos.z * CHUNK_SIZE_Z;
            it->second->setWaterLevel(lx, wy, lz, type == BlockType::Water ? 8 : 0);
        }
        activateWaterNeighborhood(wx, wy, wz);
    } else {
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

Chunk* World::getOrCreateChunk(ChunkPos pos) {
    auto it = chunks_.find(pos);
    if (it != chunks_.end()) return it->second.get();

    auto chunk = std::make_unique<Chunk>();
    chunk->pos = pos;
    gen_.generate(*chunk);

    Chunk* raw = chunk.get();
    chunks_[pos] = std::move(chunk);
    return raw;
}

// Minecraft-style water propagation.
//
// Level encoding in flowing_water_ and water_levels:
//   depth 0  → stored level 8  (falling water: directly below source/water)
//   depth 1  → stored level 1  (1 block horizontal from source, tallest flowing)
//   depth 7  → stored level 7  (7 blocks horizontal from source, flattest)
//   not in flowing_water_  →  permanent source block
//
// Each non-source block's desired depth:
//   • Has water directly above → depth 0 (full falling column)
//   • Otherwise → min(horizontal_neighbor_depth) + 1, capped at 7
//
// If desired depth > 7: block should be air (too far from any source).
// No directional-preference logic → stable, no oscillation.
std::vector<WorldPos> World::stepWater(ChunkPos min_chunk, ChunkPos max_chunk) {
    if (active_water_.empty()) return {};

    std::vector<WorldPos> seeds;
    seeds.reserve(active_water_.size());
    for (const WorldPos& pos : active_water_) {
        if (inChunkRange(worldToChunk(pos.x, pos.z), min_chunk, max_chunk))
            seeds.push_back(pos);
    }
    active_water_.clear();
    if (seeds.empty()) return {};

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

    static const int HOFFSETS[][2] = {{ 1, 0}, {-1, 0}, {0, 1}, {0,-1}};

    for (const WorldPos& pos : candidates) {
        BlockType old_type = getWorldBlock(pos.x, pos.y, pos.z);
        uint8_t old_depth = 0;
        bool was_flowing = isFlowingWater(pos.x, pos.y, pos.z, &old_depth);
        bool was_source  = (old_type == BlockType::Water && !was_flowing);

        if (isSolidBlock(old_type)) continue;
        if (was_source) continue;  // permanent source blocks are never modified

        // ── Compute desired flowing depth ─────────────────────────────────────
        // 255 = should become air; 0 = falling (full column); 1-7 = horizontal spread
        uint8_t desired_depth = 255;

        // Priority 1: water directly above → full falling column
        if (isWaterBlock(pos.x, pos.y + 1, pos.z)) {
            desired_depth = 0;
        } else {
            // Priority 2: propagate from horizontal neighbors.
            // A neighbor only contributes horizontal flow if it has solid ground
            // beneath it — water with air/water below falls straight down instead
            // of spreading sideways (Minecraft behavior).
            uint8_t min_depth = 255;
            for (const auto& o : HOFFSETS) {
                int nx = pos.x + o[0], nz = pos.z + o[1];
                if (getWorldBlock(nx, pos.y, nz) != BlockType::Water) continue;

                BlockType below_neighbor = getWorldBlock(nx, pos.y - 1, nz);
                if (below_neighbor == BlockType::Air || below_neighbor == BlockType::Water)
                    continue;  // neighbor is falling — doesn't spread sideways

                uint8_t nd;
                bool nf = isFlowingWater(nx, pos.y, nz, &nd);
                if (!nf) nd = 0;  // source block = depth 0

                if (nd < min_depth) min_depth = nd;
            }
            if (min_depth < 255 && min_depth + 1 <= 7) {
                desired_depth = min_depth + 1;
            }
        }

        // ── Apply changes ─────────────────────────────────────────────────────
        bool should_be_water = (desired_depth <= 7);  // 0-7 inclusive
        uint8_t stored_level = (desired_depth == 0) ? 8 : desired_depth;

        if (should_be_water) {
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
            if (setExistingWorldBlock(pos.x, pos.y, pos.z, BlockType::Air)) {
                flowing_water_.erase(pos);
                changed.push_back(pos);
                activateWaterNeighborhood(pos.x, pos.y, pos.z);
            }
        }
    }

    return changed;
}
