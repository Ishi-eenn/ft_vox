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

// Atlas UV helper: given BlockType, return (u_offset, v_offset, tile_w, tile_h)
static void getAtlasUV(BlockType type, float& u0, float& v0, float& uw, float& vh) {
    // Atlas is ATLAS_COLS wide, 4 rows tall, each tile 1/ATLAS_COLS x 1/4
    const int cols = ATLAS_COLS;  // 8
    const int rows = 4;
    const int tile = (int)type;  // Air=0, Grass=1, Dirt=2, Stone=3, Sand=4, Snow=5
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

void MeshBuilder::addFace(
    std::vector<Vertex>& verts,
    std::vector<uint32_t>& indices,
    int x, int y, int z,
    Face face,
    BlockType type)
{
    float u0, v0, uw, vh;
    getAtlasUV(type, u0, v0, uw, vh);

    const float (*fv)[3] = FACE_VERTS[(int)face];
    const float*  fn     = FACE_NORMALS[(int)face];

    uint32_t base = (uint32_t)verts.size();

    for (int i = 0; i < 4; ++i) {
        Vertex vtx;
        vtx.x  = (float)x + fv[i][0];
        vtx.y  = (float)y + fv[i][1];
        vtx.z  = (float)z + fv[i][2];
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
                    if (nb == BlockType::Air) {
                        addFace(chunk.vertices, chunk.indices, x, y, z, (Face)f, t);
                    }
                }
            }
        }
    }

    chunk.is_dirty = true;
}
