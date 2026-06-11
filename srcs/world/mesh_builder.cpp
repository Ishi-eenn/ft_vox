// ─────────────────────────────────────────────────────────────────────────────
// mesh_builder.cpp — チャンクのメッシュ（3D形状）を生成する
//
// 【メッシュとは？】
//   3Dオブジェクトの形を表す「頂点（vertex）」と「三角形の面」の集まり。
//   GPUはこのメッシュを受け取って画面に描画する。
//
// 【なぜ見える面だけ生成するのか？】
//   ボクセルゲームでは隣のブロックと接している面は絶対に見えない。
//   見えない面のポリゴンを生成するのは無駄なので、
//   隣が空気または水の面だけポリゴンを生成することで頂点数を大幅に削減する。
//
// 【テクスチャアトラスとは？】
//   複数のテクスチャを1枚の大きな画像にまとめたもの。
//   GPU的には1テクスチャ = 1バインドなので、1枚にまとめると切り替えが不要で高速。
//   UV座標でどのタイルを参照するか指定する。
// ─────────────────────────────────────────────────────────────────────────────
#include "world/mesh_builder.hpp"
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// 各面の4頂点のオフセット（ブロック原点からの相対座標）
// CCW（Counter Clock Wise: 反時計回り）の巻き順で定義する。
// OpenGL はデフォルトで CCW が表面なので、カメラ側から見て反時計回りにする。
// ─────────────────────────────────────────────────────────────────────────────
static const float FACE_VERTS[6][4][3] = {
    // 上面 (+Y): 法線 (0,1,0)
    {{0,1,1},{1,1,1},{1,1,0},{0,1,0}},
    // 下面 (-Y): 法線 (0,-1,0)
    {{0,0,0},{1,0,0},{1,0,1},{0,0,1}},
    // 北面 (-Z): 法線 (0,0,-1)
    {{0,1,0},{1,1,0},{1,0,0},{0,0,0}},
    // 南面 (+Z): 法線 (0,0,1)
    {{1,1,1},{0,1,1},{0,0,1},{1,0,1}},
    // 東面 (+X): 法線 (1,0,0)
    {{1,1,0},{1,1,1},{1,0,1},{1,0,0}},
    // 西面 (-X): 法線 (-1,0,0)
    {{0,1,1},{0,1,0},{0,0,0},{0,0,1}},
};

// 各面の法線ベクトル（光の計算に使う）
static const float FACE_NORMALS[6][3] = {
    { 0, 1, 0}, { 0,-1, 0},
    { 0, 0,-1}, { 0, 0, 1},
    { 1, 0, 0}, {-1, 0, 0},
};

// 面の4頂点それぞれの UV 座標（左下→右下→右上→左上）
// テクスチャアトラスのタイル内での相対位置
static const float FACE_UVS[4][2] = {
    {0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}
};

// 1つの四角形（4頂点）を2つの三角形（6インデックス）に分割する順番
// GPUは三角形しか描けないため、四角形を2枚の三角形に変換する
static const uint32_t QUAD_INDICES[6] = {0, 1, 2, 2, 3, 0};

// ─────────────────────────────────────────────────────────────────────────────
// getAtlasUV() — ブロック種類と面からテクスチャアトラス上の UV を取得
//
// テクスチャアトラスは 8列×4行 のタイルグリッド。
// ブロック種類に応じてタイルを選び、UV座標に変換する。
// 草ブロックは面によってテクスチャが変わる（上=緑、下=土、横=草付き土）。
// ─────────────────────────────────────────────────────────────────────────────
static void getAtlasUV(BlockType type, Face face, float& u0, float& v0, float& uw, float& vh) {
    const int cols = ATLAS_COLS;  // 8列
    const int rows = 4;
    int tile = (int)type;
    if (type == BlockType::Grass) {
        if      (face == Face::Top)    tile = 1;  // 上面: 緑の草
        else if (face == Face::Bottom) tile = 2;  // 下面: 土
        else                           tile = 10; // 側面: 草付き土（col=2, row=1）
    }
    // タイルインデックス → UV座標
    u0 = (float)(tile % cols) / (float)cols;
    v0 = (float)(tile / cols) / (float)rows;
    uw = 1.0f / (float)cols;  // 1タイルのU幅
    vh = 1.0f / (float)rows;  // 1タイルのV高さ
}

static bool isDecorativePlant(BlockType type) {
    return type == BlockType::ShortGrass ||
           type == BlockType::Flower ||
           type == BlockType::Mushroom ||
           type == BlockType::Wheat;
}

static bool isTorchBlock(BlockType type) {
    return type == BlockType::Torch;
}

static bool isFaceTransparent(BlockType type) {
    return type == BlockType::Air ||
           type == BlockType::Water ||
           isDecorativePlant(type) ||
           isTorchBlock(type);
}

static float plantHeight(BlockType type) {
    if (type == BlockType::Mushroom) return 0.55f;
    if (type == BlockType::Flower)   return 0.85f;
    if (type == BlockType::Wheat)    return 0.90f;
    return 0.72f;
}

static void addPlantQuad(std::vector<Vertex>& verts,
                         std::vector<uint32_t>& indices,
                         const float p[4][3],
                         float u0, float v0, float uw, float vh) {
    static const float uv[4][2] = {
        {0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}
    };

    uint32_t b = (uint32_t)verts.size();
    for (int i = 0; i < 4; ++i) {
        Vertex v{};
        v.x = p[i][0]; v.y = p[i][1]; v.z = p[i][2];
        v.u = u0 + uv[i][0] * uw;
        v.v = v0 + uv[i][1] * vh;
        v.nx = 0.0f; v.ny = 1.0f; v.nz = 0.0f;
        verts.push_back(v);
    }

    // 両面から見えるよう、同じ板を表裏両方向の巻き順で描く。
    indices.insert(indices.end(), {b, b+1, b+2, b, b+2, b+3,
                                   b, b+2, b+1, b, b+3, b+2});
}

// ─────────────────────────────────────────────────────────────────────────────
// 任意サイズの直方体ボックス（6面）をメッシュへ追加するヘルパー。
// 松明の「細い棒」と「炎の小箱」をそれぞれボックスで生成するために使う。
// 各面とも同じ UV 矩形 (u0,v0,uw,vh) を貼る（ボックスが小さいため引き伸ばしは問題なし）。
// ─────────────────────────────────────────────────────────────────────────────
static void addBox(std::vector<Vertex>& verts,
                   std::vector<uint32_t>& indices,
                   float minx, float miny, float minz,
                   float maxx, float maxy, float maxz,
                   float u0, float v0, float uw, float vh) {
    // 6面: top, bottom, north(-Z), south(+Z), east(+X), west(-X)
    // 各面の 4 頂点を CCW で定義（外から見て反時計回り）
    const float P[8][3] = {
        {minx, miny, minz}, // 0
        {maxx, miny, minz}, // 1
        {maxx, miny, maxz}, // 2
        {minx, miny, maxz}, // 3
        {minx, maxy, minz}, // 4
        {maxx, maxy, minz}, // 5
        {maxx, maxy, maxz}, // 6
        {minx, maxy, maxz}, // 7
    };
    const int FACES[6][4] = {
        {7, 6, 5, 4}, // top    +Y  (n = 0,+1,0)
        {0, 1, 2, 3}, // bottom -Y
        {4, 5, 1, 0}, // north  -Z
        {6, 7, 3, 2}, // south  +Z
        {5, 6, 2, 1}, // east   +X
        {7, 4, 0, 3}, // west   -X
    };
    const float N[6][3] = {
        { 0, 1, 0}, { 0,-1, 0},
        { 0, 0,-1}, { 0, 0, 1},
        { 1, 0, 0}, {-1, 0, 0},
    };
    const float UV[4][2] = {
        {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}
    };
    for (int f = 0; f < 6; ++f) {
        uint32_t base = (uint32_t)verts.size();
        for (int i = 0; i < 4; ++i) {
            int p = FACES[f][i];
            Vertex v{};
            v.x = P[p][0]; v.y = P[p][1]; v.z = P[p][2];
            v.u = u0 + UV[i][0] * uw;
            v.v = v0 + UV[i][1] * vh;
            v.nx = N[f][0]; v.ny = N[f][1]; v.nz = N[f][2];
            verts.push_back(v);
        }
        indices.insert(indices.end(),
                       {base, base + 1, base + 2,
                        base, base + 2, base + 3});
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// addTorchModel() — 設置された松明の 3D メッシュを生成
//
// 構造（Minecraft の torch モデルを踏襲）:
//   ・棒部分: 2x10x2 の細い角柱（中央寄り、ブロック底〜10/16 高さ）
//             テクスチャは Wood タイルを流用（茶色の木目）。
//   ・炎部分: クロスビルボード = 2 枚の垂直な板を X 字に交差させる。
//             各板は 8/16 × 5/16 の矩形で、Torch タイル上端 4px の炎パターンを
//             透過テクスチャでサンプル。alpha discard で炎の形状だけが残る。
// ─────────────────────────────────────────────────────────────────────────────
static void addTorchModel(std::vector<Vertex>& verts,
                          std::vector<uint32_t>& indices,
                          int x, int y, int z) {
    const int cols = ATLAS_COLS;
    const int rows = 4;
    const float icols = 1.0f / (float)cols;
    const float irows = 1.0f / (float)rows;

    // 棒: Wood タイル (tile=7, col=7, row=0) を流用
    const int wood_tile = (int)BlockType::Wood;
    const float wu0 = (float)(wood_tile % cols) * icols;
    const float wv0 = (float)(wood_tile / cols) * irows;

    // 炎: Torch タイル (tile=20, col=4, row=2) の上端 4px (= 4/16 of tile)
    const int torch_tile = (int)BlockType::Torch;
    const float tu0 = (float)(torch_tile % cols) * icols;
    const float tv0 = (float)(torch_tile / cols) * irows;
    const float flame_vh = irows * (4.0f / 16.0f);

    const float fx = (float)x;
    const float fy = (float)y;
    const float fz = (float)z;

    // 棒: x:[7/16,9/16], y:[0,10/16], z:[7/16,9/16]
    addBox(verts, indices,
           fx + 7.0f/16.0f, fy,                 fz + 7.0f/16.0f,
           fx + 9.0f/16.0f, fy + 10.0f/16.0f,   fz + 9.0f/16.0f,
           wu0, wv0, icols, irows);

    // 炎クロスビルボード: 2 枚の垂直平面を X 字に配置。
    // 各板の幅 = 8/16, 高さ = 5/16, 棒の真上 (y=10/16〜15/16) に中央配置。
    // addPlantQuad は両面描画（CCW + CW 両方）と alpha discard 対応の UV を持つ。
    const float fy0 = fy + 10.0f / 16.0f;   // 棒の上
    const float fy1 = fy + 15.0f / 16.0f;   // 炎の先端（ブロック上端より少し下）
    const float cx  = fx + 8.0f / 16.0f;
    const float cz  = fz + 8.0f / 16.0f;
    const float hw  = 4.0f / 16.0f;          // half-width → 板幅 8/16

    // Quad A: Z = cz 平面に立つ垂直板（X 軸方向に広がる）
    const float qA[4][3] = {
        {cx - hw, fy0, cz},  // p0: 底-左
        {cx + hw, fy0, cz},  // p1: 底-右
        {cx + hw, fy1, cz},  // p2: 頂-右
        {cx - hw, fy1, cz},  // p3: 頂-左
    };
    // Quad B: X = cx 平面に立つ垂直板（Z 軸方向に広がる）
    const float qB[4][3] = {
        {cx, fy0, cz - hw},
        {cx, fy0, cz + hw},
        {cx, fy1, cz + hw},
        {cx, fy1, cz - hw},
    };
    addPlantQuad(verts, indices, qA, tu0, tv0, icols, flame_vh);
    addPlantQuad(verts, indices, qB, tu0, tv0, icols, flame_vh);
}

static void addPlantCross(std::vector<Vertex>& verts,
                          std::vector<uint32_t>& indices,
                          int x, int y, int z,
                          BlockType type) {
    float u0, v0, uw, vh;
    getAtlasUV(type, Face::North, u0, v0, uw, vh);

    const float h = plantHeight(type);
    const float inset = 0.14f;
    const float x0 = (float)x + inset;
    const float x1 = (float)x + 1.0f - inset;
    const float z0 = (float)z + inset;
    const float z1 = (float)z + 1.0f - inset;
    const float y0 = (float)y;
    const float y1 = (float)y + h;

    const float q0[4][3] = {
        {x0, y0, z0}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z0}
    };
    const float q1[4][3] = {
        {x1, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x1, y1, z0}
    };
    addPlantQuad(verts, indices, q0, u0, v0, uw, vh);
    addPlantQuad(verts, indices, q1, u0, v0, uw, vh);
}

// ─────────────────────────────────────────────────────────────────────────────
// getNeighborBlock() — チャンク境界をまたいで隣ブロックの種類を取得
//
// チャンク内なら直接取得、チャンク外なら隣チャンクから取得する。
// 隣チャンクが未ロードなら石として扱う（面が生成されないようにする保守的な対応）。
// ─────────────────────────────────────────────────────────────────────────────
BlockType MeshBuilder::getNeighborBlock(
    int nx, int ny, int nz,
    const Chunk& chunk,
    const ChunkNeighbors& nb)
{
    if (ny < 0 || ny >= CHUNK_SIZE_Y) return BlockType::Air;  // Y方向は範囲外を空気扱い

    bool out_x_pos = nx >= CHUNK_SIZE_X;
    bool out_x_neg = nx < 0;
    bool out_z_pos = nz >= CHUNK_SIZE_Z;
    bool out_z_neg = nz < 0;

    if (!out_x_pos && !out_x_neg && !out_z_pos && !out_z_neg) {
        return chunk.getBlock(nx, ny, nz);  // チャンク内
    }
    // チャンク境界を越えた場合は隣チャンクを参照
    if (out_x_pos && nb.east)  return nb.east->getBlock(0,              ny, nz);
    if (out_x_neg && nb.west)  return nb.west->getBlock(CHUNK_SIZE_X-1, ny, nz);
    if (out_z_pos && nb.south) return nb.south->getBlock(nx, ny, 0);
    if (out_z_neg && nb.north) return nb.north->getBlock(nx, ny, CHUNK_SIZE_Z-1);
    return BlockType::Stone;  // 隣チャンク未ロード → 石として扱う（面を生成しない）
}

// 隣ブロックの水位を取得（チャンク境界対応）
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

// ─────────────────────────────────────────────────────────────────────────────
// getWaterSurfaceHeight() — 水面の高さ（0.0〜1.0）を返す
//
// 満水（water level=8）なら 0.95（わずかに低くして水面を表現）、
// 水が少なければもっと低くなる。上が水ブロックなら 1.0（満杯）。
// ─────────────────────────────────────────────────────────────────────────────
float MeshBuilder::getWaterSurfaceHeight(
    int x, int y, int z,
    const Chunk& chunk,
    const ChunkNeighbors& neighbors)
{
    if (getNeighborBlock(x, y + 1, z, chunk, neighbors) == BlockType::Water) {
        return 1.0f;  // 上も水なら天井まで満水
    }

    uint8_t level = getNeighborWaterLevel(x, y, z, chunk, neighbors);
    if (level == 0) return 1.0f;
    if (level >= 8) return 0.95f;  // 満水（わずかに低くする）
    return 0.12f + (float)(8 - level) * 0.11f;  // 水位に応じた高さ
}

// ─────────────────────────────────────────────────────────────────────────────
// computeWaterTopHeights() — 水面上面の4頂点の高さを計算する
//
// 水面は平坦ではなく、周囲の水位に応じて傾く（Minecraftと同じ処理）。
// 4頂点それぞれについて、その角を共有する2つの水ブロックの水位を見て最大を使う。
// ─────────────────────────────────────────────────────────────────────────────
void MeshBuilder::computeWaterTopHeights(
    float out[4],
    int x, int y, int z,
    const Chunk& chunk,
    const ChunkNeighbors& neighbors)
{
    // 各頂点に影響する隣接ブロックのオフセット（頂点ごとに2つ）
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
                max_h = 1.0f;  // 上も水なら必ず満水
            }
        }
        out[i] = (max_h > 0.0f) ? max_h : getWaterSurfaceHeight(x, y, z, chunk, neighbors);
    }
}

// 水の側面頂点の高さを面・頂点インデックスから返す
static float getFaceTopHeight(const float water_top[4], Face face, int vertex_index) {
    switch (face) {
        case Face::North: return water_top[(vertex_index == 0) ? 3 : 2];
        case Face::South: return water_top[(vertex_index == 0) ? 1 : 0];
        case Face::East:  return water_top[(vertex_index == 0) ? 2 : 1];
        case Face::West:  return water_top[(vertex_index == 0) ? 0 : 3];
        default:          return 1.0f;
    }
}

// 水の側面の平均高さ（高い方を描画するか判断するため使う）
static float getFaceAverageHeight(const float water_top[4], Face face) {
    return 0.5f * (getFaceTopHeight(water_top, face, 0) +
                   getFaceTopHeight(water_top, face, 1));
}

// ─────────────────────────────────────────────────────────────────────────────
// addFace() — 1つのブロック面（4頂点・2三角形）をメッシュに追加する
//
// 頂点データ（位置・UV・法線）を verts に追加し、
// 三角形のインデックスを indices に追加する。
// 水ブロックは水面の高さに応じて頂点Y座標を調整する。
// ─────────────────────────────────────────────────────────────────────────────
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

    const float (*fv)[3] = FACE_VERTS[(int)face];   // 頂点オフセット
    const float*  fn     = FACE_NORMALS[(int)face];  // 法線

    uint32_t base = (uint32_t)verts.size();  // 既存頂点数 = 新規インデックスのオフセット

    // 水面の高さを事前計算
    float water_top[4] = {1.f, 1.f, 1.f, 1.f};
    if (type == BlockType::Water) {
        computeWaterTopHeights(water_top, x, y, z, chunk, neighbors);
    }

    // 4頂点を生成
    for (int i = 0; i < 4; ++i) {
        Vertex vtx;
        vtx.x  = (float)x + fv[i][0];
        vtx.y  = (float)y + fv[i][1];
        vtx.z  = (float)z + fv[i][2];

        // 水ブロックの場合、上端の頂点（fv[i][1] > 0.5）だけ水面の高さに調整する
        if (type == BlockType::Water) {
            if (face == Face::Top) {
                vtx.y = (float)y + water_top[i];  // 上面: 各頂点の水面高さ
            } else if (face == Face::North || face == Face::South || face == Face::East || face == Face::West) {
                if (fv[i][1] > 0.5f) {
                    vtx.y = (float)y + getFaceTopHeight(water_top, face, i);  // 側面上端
                }
            }
        }

        // UV座標（テクスチャのどの位置を使うか）
        vtx.u  = u0 + FACE_UVS[i][0] * uw;
        vtx.v  = v0 + FACE_UVS[i][1] * vh;

        // 法線ベクトル（ライティング計算に使う）
        vtx.nx = fn[0];  vtx.ny = fn[1];  vtx.nz = fn[2];
        verts.push_back(vtx);
    }

    // 4頂点を2つの三角形（6インデックス）として登録
    for (int i = 0; i < 6; ++i)
        indices.push_back(base + QUAD_INDICES[i]);
}

// ─────────────────────────────────────────────────────────────────────────────
// build() — チャンク全体のメッシュを生成する
//
// 全ブロックをスキャンし、各面について「隣が空気または水なら面を生成」する。
// 不透明ブロックと水は別々のインデックスリストに格納する。
// これにより描画時に不透明→水の順で2パスに分けて描画できる。
//
// 【なぜ2パスに分けるのか？】
//   透明・半透明なオブジェクト（水）は、不透明なものを先に描画した後でないと
//   後ろが透けて見えない。OpenGLの深度バッファの仕組み上、順序が重要。
// ─────────────────────────────────────────────────────────────────────────────
void MeshBuilder::build(Chunk& chunk, const ChunkNeighbors& neighbors) {
    chunk.vertices.clear();
    chunk.indices.clear();
    chunk.indices_water.clear();

    // 最初に容量を予約することでメモリの再確保を減らす
    chunk.vertices.reserve(4096);
    chunk.indices.reserve(6144);

    // 各面の方向（隣ブロックを調べるオフセット）
    static const int DX[6] = { 0, 0, 0, 0, 1,-1};
    static const int DY[6] = { 1,-1, 0, 0, 0, 0};
    static const int DZ[6] = { 0, 0,-1, 1, 0, 0};

    // チャンク内の全ブロックをスキャン
    for (int x = 0; x < CHUNK_SIZE_X; ++x) {
        for (int y = 0; y < CHUNK_SIZE_Y; ++y) {
            for (int z = 0; z < CHUNK_SIZE_Z; ++z) {
                BlockType t = chunk.getBlock(x, y, z);
                if (t == BlockType::Air) continue;  // 空気は面を持たない

                if (isDecorativePlant(t)) {
                    addPlantCross(chunk.vertices, chunk.indices, x, y, z, t);
                    continue;
                }

                if (isTorchBlock(t)) {
                    addTorchModel(chunk.vertices, chunk.indices, x, y, z);
                    continue;
                }

                // 6面それぞれについて面を生成するか判断
                for (int f = 0; f < 6; ++f) {
                    int nx = x + DX[f];
                    int ny = y + DY[f];
                    int nz = z + DZ[f];
                    BlockType nb = getNeighborBlock(nx, ny, nz, chunk, neighbors);

                    if (t == BlockType::Water) {
                        // 水ブロックの面生成ルール
                        if (f == (int)Face::Top) {
                            // 上面: 上が水でないとき（水面が見える）
                            if (nb != BlockType::Water) {
                                addFace(chunk.vertices, chunk.indices_water, x, y, z, (Face)f, t, chunk, neighbors);
                            }
                        } else if (f == (int)Face::North || f == (int)Face::South || f == (int)Face::East || f == (int)Face::West) {
                            if (nb == BlockType::Air) {
                                // 側面: 隣が空気なら必ず生成
                                addFace(chunk.vertices, chunk.indices_water, x, y, z, (Face)f, t, chunk, neighbors);
                            } else if (nb == BlockType::Water) {
                                // 隣も水の場合: 自分の水面が高ければ面を生成（段差の表現）
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
                        } else if (nb == BlockType::Air || isDecorativePlant(nb)) {
                            // 下面など: 隣が空気なら生成
                            addFace(chunk.vertices, chunk.indices_water, x, y, z, (Face)f, t, chunk, neighbors);
                        }
                    } else if (isFaceTransparent(nb)) {
                        // 不透明ブロック: 隣が空気か水なら面を生成（不透明インデックスリストに追加）
                        addFace(chunk.vertices, chunk.indices, x, y, z, (Face)f, t, chunk, neighbors);
                    }
                }
            }
        }
    }

    chunk.is_dirty = true;  // GPUへの再アップロードが必要
}
