#include "world/mesh_builder.hpp"
#include <cmath>

// Per-face: 4 vertex offsets (CCW winding viewed from outside)
// Format: {dx, dy, dz} offsets from block origin
static const float FACE_VERTS[6][4][3] = {
    // Top (+Y): normal (0,1,0)
    {{0,1,1},{1,1,1},{1,1,0},{0,1,0}},
    // Bottom (-Y): normal (0,-1,0)
    {{0,0,0},{1,0,0},{1,0,1},{0,0,1}},
    // North (-Z): normal (0,0,-1)
    {{0,1,0},{1,1,0},{1,0,0},{0,0,0}},
    // South (+Z): normal (0,0,1)
    {{1,1,1},{0,1,1},{0,0,1},{1,0,1}},
    // East (+X): normal (1,0,0)
    {{1,1,0},{1,1,1},{1,0,1},{1,0,0}},
    // West (-X): normal (-1,0,0)
    {{0,1,1},{0,1,0},{0,0,0},{0,0,1}},
};

static const float FACE_NORMALS[6][3] = {
    { 0, 1, 0}, { 0,-1, 0},
    { 0, 0,-1}, { 0, 0, 1},
    { 1, 0, 0}, {-1, 0, 0},
};

// UV corners for each vertex of a face (same for all faces)
static const float FACE_UVS[4][2] = {
    {0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}
};

// Index offsets for 2 triangles from 4 verts (CCW)
static const uint32_t QUAD_INDICES[6] = {0, 1, 2, 2, 3, 0};

// Atlas UV helper: given BlockType and Face, return (u_offset, v_offset, tile_w, tile_h)
static void getAtlasUV(BlockType type, Face face, float& u0, float& v0, float& uw, float& vh) {
    const int cols = ATLAS_COLS;  // 8
    const int rows = 4;
    int tile = (int)type;
    if (type == BlockType::Grass) {
        if      (face == Face::Top)    tile = 1;  // green top
        else if (face == Face::Bottom) tile = 2;  // dirt bottom
        else                           tile = 9;  // dirt+strip sides (col=1, row=1)
    }
    u0 = (float)(tile % cols) / (float)cols;
    v0 = (float)(tile / cols) / (float)rows;
    uw = 1.0f / (float)cols;
    vh = 1.0f / (float)rows;
}

// Neighbor block lookup (handles chunk boundaries)
BlockType MeshBuilder::getNeighborBlock(
    int nx, int ny, int nz,
    const Chunk& chunk,
    const ChunkNeighbors& nb)
{
    if (ny < 0 || ny >= CHUNK_SIZE_Y) return BlockType::Air;

    bool out_x_pos = nx >= CHUNK_SIZE_X;
    bool out_x_neg = nx < 0;
    bool out_z_pos = nz >= CHUNK_SIZE_Z;
    bool out_z_neg = nz < 0;

    if (!out_x_pos && !out_x_neg && !out_z_pos && !out_z_neg) {
        return chunk.getBlock(nx, ny, nz);
    }
    // Boundary: look into neighbor chunk
    if (out_x_pos && nb.east)  return nb.east->getBlock(0,              ny, nz);
    if (out_x_neg && nb.west)  return nb.west->getBlock(CHUNK_SIZE_X-1, ny, nz);
    if (out_z_pos && nb.south) return nb.south->getBlock(nx, ny, 0);
    if (out_z_neg && nb.north) return nb.north->getBlock(nx, ny, CHUNK_SIZE_Z-1);
    // Neighbor not loaded -> treat as solid (no exposed face = conservative)
    return BlockType::Stone;
}

uint8_t MeshBuilder::getNeighborWaterLevel(
    int nx, int ny, int nz,
    const Chunk& chunk,
    const ChunkNeighbors& nb)
{
    if (ny < 0 || ny >= CHUNK_SIZE_Y) return 0;

    bool out_x_pos = nx >= CHUNK_SIZE_X;
    bool out_x_neg = nx < 0;
    bool out_z_pos = nz >= CHUNK_SIZE_Z;
    bool out_z_neg = nz < 0;

    if (!out_x_pos && !out_x_neg && !out_z_pos && !out_z_neg) {
        return chunk.getWaterLevel(nx, ny, nz);
    }
    if (out_x_pos && nb.east)  return nb.east->getWaterLevel(0,              ny, nz);
    if (out_x_neg && nb.west)  return nb.west->getWaterLevel(CHUNK_SIZE_X-1, ny, nz);
    if (out_z_pos && nb.south) return nb.south->getWaterLevel(nx, ny, 0);
    if (out_z_neg && nb.north) return nb.north->getWaterLevel(nx, ny, CHUNK_SIZE_Z-1);
    return 0;
}

float MeshBuilder::getWaterSurfaceHeight(
    int x, int y, int z,
    const Chunk& chunk,
    const ChunkNeighbors& neighbors)
{
    if (getNeighborBlock(x, y + 1, z, chunk, neighbors) == BlockType::Water) {
        return 1.0f;
    }

    uint8_t level = getNeighborWaterLevel(x, y, z, chunk, neighbors);
    if (level == 0) return 1.0f;
    if (level >= 8) return 0.95f;
    return 0.12f + (float)(8 - level) * 0.11f;
}

void MeshBuilder::computeWaterTopHeights(
    float out[4],
    int x, int y, int z,
    const Chunk& chunk,
    const ChunkNeighbors& neighbors)
{
    static const int CORNER_OFFSETS[4][2][2] = {
        {{0, 0}, {0, 1}},
        {{1, 0}, {1, 1}},
        {{1,-1}, {1, 0}},
        {{0,-1}, {0, 0}}
    };

    for (int i = 0; i < 4; ++i) {
        float max_h = 0.0f;
        for (int j = 0; j < 2; ++j) {
            int sx = x + CORNER_OFFSETS[i][j][0];
            int sz = z + CORNER_OFFSETS[i][j][1];
            if (getNeighborBlock(sx, y, sz, chunk, neighbors) == BlockType::Water) {
                max_h = std::max(max_h, getWaterSurfaceHeight(sx, y, sz, chunk, neighbors));
            }
            if (getNeighborBlock(sx, y + 1, sz, chunk, neighbors) == BlockType::Water) {
                max_h = 1.0f;
            }
        }
        out[i] = (max_h > 0.0f) ? max_h : getWaterSurfaceHeight(x, y, z, chunk, neighbors);
    }
}

static float getFaceTopHeight(const float water_top[4], Face face, int vertex_index) {
    switch (face) {
        case Face::North: return water_top[(vertex_index == 0) ? 3 : 2];
        case Face::South: return water_top[(vertex_index == 0) ? 1 : 0];
        case Face::East:  return water_top[(vertex_index == 0) ? 2 : 1];
        case Face::West:  return water_top[(vertex_index == 0) ? 0 : 3];
        default:          return 1.0f;
    }
}

static float getFaceAverageHeight(const float water_top[4], Face face) {
    return 0.5f * (getFaceTopHeight(water_top, face, 0) +
                   getFaceTopHeight(water_top, face, 1));
}

void MeshBuilder::addFace(
    std::vector<Vertex>& verts,
    std::vector<uint32_t>& indices,
    int x, int y, int z,
    Face face,
    BlockType type,
    const Chunk& chunk,
    const ChunkNeighbors& neighbors)
{
    float u0, v0, uw, vh;
    getAtlasUV(type, face, u0, v0, uw, vh);

    const float (*fv)[3] = FACE_VERTS[(int)face];
    const float*  fn     = FACE_NORMALS[(int)face];

    uint32_t base = (uint32_t)verts.size();
    float water_top[4] = {1.f, 1.f, 1.f, 1.f};
    if (type == BlockType::Water) {
        computeWaterTopHeights(water_top, x, y, z, chunk, neighbors);
    }

    for (int i = 0; i < 4; ++i) {
        Vertex vtx;
        vtx.x  = (float)x + fv[i][0];
        vtx.y  = (float)y + fv[i][1];
        vtx.z  = (float)z + fv[i][2];
        if (type == BlockType::Water) {
            if (face == Face::Top) {
                vtx.y = (float)y + water_top[i];
            } else if (face == Face::North || face == Face::South || face == Face::East || face == Face::West) {
                if (fv[i][1] > 0.5f) {
                    vtx.y = (float)y + getFaceTopHeight(water_top, face, i);
                }
            }
        }
        vtx.u  = u0 + FACE_UVS[i][0] * uw;
        vtx.v  = v0 + FACE_UVS[i][1] * vh;
        vtx.nx = fn[0];  vtx.ny = fn[1];  vtx.nz = fn[2];
        verts.push_back(vtx);
    }
    for (int i = 0; i < 6; ++i)
        indices.push_back(base + QUAD_INDICES[i]);
}

void MeshBuilder::build(Chunk& chunk, const ChunkNeighbors& neighbors) {
    chunk.vertices.clear();
    chunk.indices.clear();
    chunk.indices_water.clear();

    // Estimate capacity: ~6 faces * some blocks
    chunk.vertices.reserve(4096);
    chunk.indices.reserve(6144);

    static const int DX[6] = { 0, 0, 0, 0, 1,-1};
    static const int DY[6] = { 1,-1, 0, 0, 0, 0};
    static const int DZ[6] = { 0, 0,-1, 1, 0, 0};

    for (int x = 0; x < CHUNK_SIZE_X; ++x) {
        for (int y = 0; y < CHUNK_SIZE_Y; ++y) {
            for (int z = 0; z < CHUNK_SIZE_Z; ++z) {
                BlockType t = chunk.getBlock(x, y, z);
                if (t == BlockType::Air) continue;

                for (int f = 0; f < 6; ++f) {
                    int nx = x + DX[f];
                    int ny = y + DY[f];
                    int nz = z + DZ[f];
                    BlockType nb = getNeighborBlock(nx, ny, nz, chunk, neighbors);
                    if (t == BlockType::Water) {
                        if (f == (int)Face::Top) {
                            if (nb != BlockType::Water) {
                                addFace(chunk.vertices, chunk.indices_water, x, y, z, (Face)f, t, chunk, neighbors);
                            }
                        } else if (f == (int)Face::North || f == (int)Face::South || f == (int)Face::East || f == (int)Face::West) {
                            if (nb == BlockType::Air) {
                                addFace(chunk.vertices, chunk.indices_water, x, y, z, (Face)f, t, chunk, neighbors);
                            } else if (nb == BlockType::Water) {
                                float self_top[4];
                                float neighbor_top[4];
                                computeWaterTopHeights(self_top, x, y, z, chunk, neighbors);
                                computeWaterTopHeights(neighbor_top, nx, ny, nz, chunk, neighbors);

                                Face opposite = Face::North;
                                if (f == (int)Face::North) opposite = Face::South;
                                if (f == (int)Face::South) opposite = Face::North;
                                if (f == (int)Face::East)  opposite = Face::West;
                                if (f == (int)Face::West)  opposite = Face::East;
                                float self_avg = getFaceAverageHeight(self_top, (Face)f);
                                float nb_avg   = getFaceAverageHeight(neighbor_top, opposite);

                                if (self_avg > nb_avg + 0.01f) {
                                    addFace(chunk.vertices, chunk.indices_water, x, y, z, (Face)f, t, chunk, neighbors);
                                }
                            }
                        } else if (nb == BlockType::Air) {
                            addFace(chunk.vertices, chunk.indices_water, x, y, z, (Face)f, t, chunk, neighbors);
                        }
                    } else if (nb == BlockType::Air || nb == BlockType::Water) {
                        addFace(chunk.vertices, chunk.indices, x, y, z, (Face)f, t, chunk, neighbors);
                    }
                }
            }
        }
    }

    chunk.is_dirty = true;
}
