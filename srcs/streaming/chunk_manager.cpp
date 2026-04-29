#include "streaming/chunk_manager.hpp"
#include "renderer/frustum.hpp"
#include <cstdio>
#include <cmath>
#include <algorithm>

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

ChunkManager::ChunkManager(IWorld& world, IRenderer& renderer)
    : world_(world)
    , renderer_(renderer)
    , loaded_(MAX_LOADED_CHUNKS)
{}

ChunkManager::~ChunkManager() {
    destroyAll();
}

void ChunkManager::destroyAll() {
    loaded_.forEach([this](const ChunkPos& /*pos*/, Chunk* chunk) {
        if (chunk) renderer_.destroyChunkMesh(chunk);
    });
}

// ─── update ──────────────────────────────────────────────────────────────────

void ChunkManager::update(float playerX, float playerZ, uint64_t frame) {
    ChunkPos center = {
        (int32_t)std::floor(playerX / (float)CHUNK_SIZE_X),
        (int32_t)std::floor(playerZ / (float)CHUNK_SIZE_Z)
    };

    loadRadius(center);
    if (frame % 3 == 0) {
        auto changed_water = world_.stepWater(
            {center.x - render_distance_, center.z - render_distance_},
            {center.x + render_distance_, center.z + render_distance_}
        );
        for (const WorldPos& pos : changed_water) {
            rebuildModifiedBlock(pos.x, pos.z, *this);
        }
    }
    uploadPending(CHUNKS_PER_FRAME_GEN);
    evictIfOverBudget();

    if (frame - last_debug_frame_ >= 300) {
        fprintf(stderr, "[ChunkMgr] frame=%llu loaded=%zu queue=%zu\n",
                (unsigned long long)frame, loaded_.size(), upload_queue_.size());
        last_debug_frame_ = frame;
    }
}

void ChunkManager::setRenderDistance(int rd) {
    if (rd < RENDER_DISTANCE_MIN) rd = RENDER_DISTANCE_MIN;
    if (rd > RENDER_DISTANCE_MAX) rd = RENDER_DISTANCE_MAX;
    render_distance_ = rd;
}

void ChunkManager::loadRadius(ChunkPos center) {
    const int rd = render_distance_;
    for (int dx = -rd; dx <= rd; ++dx) {
        for (int dz = -rd; dz <= rd; ++dz) {
            if (dx * dx + dz * dz > rd * rd) continue;
            ChunkPos p = {center.x + dx, center.z + dz};

            if (loaded_.contains(p)) {
                loaded_.touch(p);
                continue;
            }

            Chunk* chunk = world_.getOrCreateChunk(p);
            if (!chunk) continue;

            // put() returns evicted keys when over capacity; retrieve ptr from World
            auto evicted = loaded_.put(p, chunk);
            for (const ChunkPos& ep : evicted) {
                Chunk* dead = world_.getOrCreateChunk(ep);
                if (dead) renderer_.destroyChunkMesh(dead);
            }

            // Always rebuild: is_dirty may be false on chunks re-entering
            // the cache after LRU eviction (gpu.uploaded was destroyed then).
            buildMesh(chunk);
            upload_queue_.push(chunk);
        }
    }
}

void ChunkManager::buildMesh(Chunk* chunk) {
    ChunkNeighbors nb;
    nb.north = world_.getOrCreateChunk({chunk->pos.x,     chunk->pos.z - 1});
    nb.south = world_.getOrCreateChunk({chunk->pos.x,     chunk->pos.z + 1});
    nb.east  = world_.getOrCreateChunk({chunk->pos.x + 1, chunk->pos.z    });
    nb.west  = world_.getOrCreateChunk({chunk->pos.x - 1, chunk->pos.z    });
    MeshBuilder::build(*chunk, nb);
}

void ChunkManager::uploadPending(int max_per_frame) {
    int n = 0;
    while (!upload_queue_.empty() && n < max_per_frame) {
        Chunk* c = upload_queue_.front();
        upload_queue_.pop();
        if (c && c->is_dirty) {
            renderer_.uploadChunkMesh(c);
            ++n;
        }
    }
}

void ChunkManager::evictIfOverBudget() {
    // Evict down to 90% of budget when over limit
    const size_t target = (size_t)(MAX_LOADED_CHUNKS * 9 / 10);
    while (loaded_.size() > target) {
        ChunkPos key;
        Chunk*   chunk = nullptr;
        if (!loaded_.evictLRU(key, chunk)) break;
        if (chunk) renderer_.destroyChunkMesh(chunk);
    }
}

// ─── rebuildChunkAt ──────────────────────────────────────────────────────────

void ChunkManager::rebuildChunkAt(ChunkPos pos) {
    if (!loaded_.contains(pos)) return;
    Chunk* chunk = world_.getOrCreateChunk(pos);
    if (!chunk || !chunk->is_generated) return;
    buildMesh(chunk);
    upload_queue_.push(chunk);
}

// ─── getVisibleChunks ────────────────────────────────────────────────────────

std::vector<Chunk*> ChunkManager::getVisibleChunks(const Frustum& frustum) {
    std::vector<Chunk*> result;
    result.reserve(loaded_.size());

    loaded_.forEach([&](const ChunkPos& /*pos*/, Chunk* chunk) {
        if (!chunk || !chunk->gpu.uploaded) return;

        float wx = (float)(chunk->pos.x * CHUNK_SIZE_X);
        float wz = (float)(chunk->pos.z * CHUNK_SIZE_Z);

        AABB aabb;
        aabb.min = {wx,                        0.0f,                     wz                       };
        aabb.max = {wx + (float)CHUNK_SIZE_X,  (float)CHUNK_SIZE_Y,      wz + (float)CHUNK_SIZE_Z };

        if (frustum.isAABBVisible(aabb)) {
            result.push_back(chunk);
        }
    });

    return result;
}
