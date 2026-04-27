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

int World::flowSearchCost(int wx, int wy, int wz, int depth, int from_dir) const {
    static const int DX[4] = { 0, 0,-1, 1};
    static const int DZ[4] = {-1, 1, 0, 0};

    if (depth >= 4) return 1000;
    if (!canFlowInto(wx, wy, wz)) return 1000;
    if (getWorldBlock(wx, wy - 1, wz) == BlockType::Air) return depth;

    int best = 1000;
    for (int dir = 0; dir < 4; ++dir) {
        if (from_dir >= 0 && (dir ^ 1) == from_dir) continue;
        int nx = wx + DX[dir];
        int nz = wz + DZ[dir];
        if (!canFlowInto(nx, wy, nz)) continue;
        best = std::min(best, flowSearchCost(nx, wy, nz, depth + 1, dir));
    }
    return best;
}

bool World::canFlowFromTo(int from_x, int from_y, int from_z, int to_x, int to_y, int to_z, uint8_t* out_depth) const {
    static const int DX[4] = { 0, 0,-1, 1};
    static const int DZ[4] = {-1, 1, 0, 0};

    if (!isWaterBlock(from_x, from_y, from_z)) return false;
    if (from_y != to_y) return false;

    uint8_t from_depth = 0;
    bool from_source = isSourceWater(from_x, from_y, from_z);
    bool from_flowing = isFlowingWater(from_x, from_y, from_z, &from_depth);
    if (!from_source && !from_flowing) return false;

    uint8_t base_depth = from_source ? 0 : from_depth;
    if (base_depth >= 7) return false;

    int min_cost = 1000;
    bool chosen[4] = {false, false, false, false};

    for (int dir = 0; dir < 4; ++dir) {
        int nx = from_x + DX[dir];
        int nz = from_z + DZ[dir];
        if (!canFlowInto(nx, from_y, nz)) continue;

        int cost = (getWorldBlock(nx, from_y - 1, nz) == BlockType::Air)
            ? 0
            : flowSearchCost(nx, from_y, nz, 1, dir);

        if (cost < min_cost) {
            min_cost = cost;
            chosen[0] = chosen[1] = chosen[2] = chosen[3] = false;
            chosen[dir] = true;
        } else if (cost == min_cost) {
            chosen[dir] = true;
        }
    }

    for (int dir = 0; dir < 4; ++dir) {
        if (!chosen[dir]) continue;
        if (from_x + DX[dir] == to_x && from_z + DZ[dir] == to_z) {
            if (out_depth) *out_depth = static_cast<uint8_t>(base_depth + 1);
            return true;
        }
    }
    return false;
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

std::vector<WorldPos> World::stepWater(ChunkPos min_chunk, ChunkPos max_chunk) {
    if (active_water_.empty()) return {};

    std::vector<WorldPos> seeds;
    seeds.reserve(active_water_.size());
    for (const WorldPos& pos : active_water_) {
        if (inChunkRange(worldToChunk(pos.x, pos.z), min_chunk, max_chunk)) {
            seeds.push_back(pos);
        }
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

    for (const WorldPos& pos : candidates) {
        BlockType old_type = getWorldBlock(pos.x, pos.y, pos.z);
        uint8_t old_level = 0;
        bool was_flowing = isFlowingWater(pos.x, pos.y, pos.z, &old_level);
        bool was_source = (old_type == BlockType::Water && !was_flowing);

        if (isSolidBlock(old_type)) continue;
        if (was_source) continue;

        uint8_t best_level = 255;

        if (pos.y + 1 < CHUNK_SIZE_Y && isWaterBlock(pos.x, pos.y + 1, pos.z)) {
            best_level = 0;
        } else {
            static const int HOFFSETS[][2] = {{ 1, 0}, {-1, 0}, {0, 1}, {0,-1}};
            for (const auto& o : HOFFSETS) {
                int nx = pos.x + o[0];
                int nz = pos.z + o[1];
                uint8_t candidate = 0;
                if (canFlowFromTo(nx, pos.y, nz, pos.x, pos.y, pos.z, &candidate)) {
                    if (candidate <= 7) best_level = std::min(best_level, candidate);
                }
            }
        }

        if (best_level <= 7) {
            if (old_type != BlockType::Water || !was_flowing || old_level != best_level) {
                if (setExistingWorldBlock(pos.x, pos.y, pos.z, BlockType::Water)) {
                    ChunkPos cpos = worldToChunk(pos.x, pos.z);
                    auto it = chunks_.find(cpos);
                    if (it != chunks_.end()) {
                        int lx = pos.x - cpos.x * CHUNK_SIZE_X;
                        int lz = pos.z - cpos.z * CHUNK_SIZE_Z;
                        it->second->setWaterLevel(lx, pos.y, lz, best_level == 0 ? 8 : best_level);
                    }
                    flowing_water_[pos] = best_level;
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
