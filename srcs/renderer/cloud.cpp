// cloud.cpp — ボクセル雲レイヤーの生成と描画
//
// 【仕組み】
//   64×64 のブール格子（タイル）を FBM ノイズで生成し、
//   各セルに 12×4×12 ブロックのボックスを構築する。
//   隣接セルが空の面だけクワッドを追加して頂点数を削減する。
//
//   描画時は同じタイルメッシュを 3×3 = 9 枚、世界座標オフセットを変えて描く。
//   タイルは時間とともに +X 方向にドリフトし、GRID_W * CELL_SIZE ごとに周回する。
#include "cloud.hpp"
#include <glad/gl.h>
#include <cmath>
#include <vector>

static constexpr float TILE_W = Cloud::GRID_W * Cloud::CELL_SIZE;

// ── ノイズ（タイル生成用） ─────────────────────────────────────────────────────
static float hashf(int x, int z) {
    unsigned n = (unsigned)(x * 1619 + z * 31337);
    n = (n << 13) ^ n;
    n = n * (n * n * 15731u + 789221u) + 1376312589u;
    return (float)(n & 0x7fffffffu) / 2147483648.0f;  // [0, 1)
}

static float smoothNoise(float x, float z) {
    int ix = (int)std::floor(x), iz = (int)std::floor(z);
    float fx = x - (float)ix, fz = z - (float)iz;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fz = fz * fz * (3.0f - 2.0f * fz);
    float a = hashf(ix,   iz),   b = hashf(ix+1, iz);
    float c = hashf(ix,   iz+1), d = hashf(ix+1, iz+1);
    return a + (b-a)*fx + (c-a)*fz + (a-b-c+d)*fx*fz;
}

static float fbm(float x, float z) {
    float v = 0.0f, a = 0.5f;
    for (int i = 0; i < 4; i++) {
        v += a * smoothNoise(x, z);
        x *= 2.0f; z *= 2.0f; a *= 0.5f;
    }
    return v;
}

// ─────────────────────────────────────────────────────────────────────────────
bool Cloud::cellAt(int gx, int gz) const {
    gx = ((gx % GRID_W) + GRID_W) % GRID_W;
    gz = ((gz % GRID_W) + GRID_W) % GRID_W;
    return grid_[gx][gz];
}

// ─────────────────────────────────────────────────────────────────────────────
void Cloud::buildMesh() {
    // 2層ノイズで Minecraft 風の大きな雲の塊を生成する。
    //   base  : 低周波ノイズ → 大きな連続した雲の塊を作る
    //   detail: 高周波 FBM  → 塊の縁をギザギザにして自然に見せる
    for (int gx = 0; gx < GRID_W; gx++) {
        for (int gz = 0; gz < GRID_W; gz++) {
            float base   = smoothNoise(gx * 0.05f, gz * 0.05f);  // 大スケール: 塊の形
            float detail = fbm      (gx * 0.20f, gz * 0.20f);   // 小スケール: 縁のテクスチャ
            float n      = base * 0.70f + detail * 0.30f;
            grid_[gx][gz] = (n > 0.62f);  // 約 30% のカバレッジ
        }
    }

    struct Vertex { float x, y, z, bright; };
    std::vector<Vertex>   verts;
    std::vector<uint32_t> idxs;
    verts.reserve(32768);
    idxs.reserve(65536);

    // クワッドを追加するラムダ（頂点 4 つ + インデックス 6 つ）
    // 裏面カリングを無効にして描画するので巻き順は不問
    auto addQuad = [&](float x0, float y0, float z0,
                       float x1, float y1, float z1,
                       float x2, float y2, float z2,
                       float x3, float y3, float z3,
                       float bright) {
        uint32_t b = (uint32_t)verts.size();
        verts.push_back({x0, y0, z0, bright});
        verts.push_back({x1, y1, z1, bright});
        verts.push_back({x2, y2, z2, bright});
        verts.push_back({x3, y3, z3, bright});
        idxs.insert(idxs.end(), {b, b+1, b+2, b, b+2, b+3});
    };

    for (int gx = 0; gx < GRID_W; gx++) {
        for (int gz = 0; gz < GRID_W; gz++) {
            if (!grid_[gx][gz]) continue;

            const float x0 = gx * CELL_SIZE, x1 = x0 + CELL_SIZE;
            const float y0 = CLOUD_Y,         y1 = CLOUD_Y + CLOUD_H;
            const float z0 = gz * CELL_SIZE,  z1 = z0 + CELL_SIZE;

            // 上面（明るい白）
            addQuad(x0,y1,z0, x1,y1,z0, x1,y1,z1, x0,y1,z1, 1.00f);
            // 下面（やや暗い灰）
            addQuad(x0,y0,z1, x1,y0,z1, x1,y0,z0, x0,y0,z0, 0.75f);
            // 北面 −Z（隣が空のみ）
            if (!cellAt(gx, gz-1))
                addQuad(x1,y0,z0, x0,y0,z0, x0,y1,z0, x1,y1,z0, 0.88f);
            // 南面 +Z
            if (!cellAt(gx, gz+1))
                addQuad(x0,y0,z1, x1,y0,z1, x1,y1,z1, x0,y1,z1, 0.88f);
            // 西面 −X
            if (!cellAt(gx-1, gz))
                addQuad(x0,y0,z0, x0,y0,z1, x0,y1,z1, x0,y1,z0, 0.82f);
            // 東面 +X
            if (!cellAt(gx+1, gz))
                addQuad(x1,y0,z1, x1,y0,z0, x1,y1,z0, x1,y1,z1, 0.82f);
        }
    }

    idx_count_ = (int32_t)idxs.size();

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(verts.size() * sizeof(Vertex)),
                 verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 (GLsizeiptr)(idxs.size() * sizeof(uint32_t)),
                 idxs.data(), GL_STATIC_DRAW);

    // 頂点レイアウト: location 0 = xyz (vec3), location 1 = bright (float)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                          sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE,
                          sizeof(Vertex), (void*)(3 * sizeof(float)));
    glBindVertexArray(0);
}

// ─────────────────────────────────────────────────────────────────────────────
bool Cloud::init() {
    if (!shader_.load("assets/shaders/cloud.vert", "assets/shaders/cloud.frag"))
        return false;

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);

    buildMesh();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
void Cloud::draw(const float* view4x4, const float* proj4x4,
                 float cam_x, float cam_z,
                 float elapsed_s, const float* sun_dir) {
    shader_.use();
    shader_.setMat4("uView",   view4x4);
    shader_.setMat4("uProj",   proj4x4);
    shader_.setVec3("uSunDir", sun_dir[0], sun_dir[1], sun_dir[2]);

    // ドリフトオフセット（TILE_W ごとに周回してシームレスにループ）
    const float drift = std::fmod(elapsed_s * DRIFT_SPEED, TILE_W);

    // カメラを囲む 3×3 タイルのベースインデックスを計算
    const int base_tx = (int)std::floor((cam_x - drift) / TILE_W);
    const int base_tz = (int)std::floor(cam_z          / TILE_W);

    // 雲描画中は裏面カリングを無効にする（上下両面から見るため）
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    glBindVertexArray(vao_);

    for (int dx = -1; dx <= 1; dx++) {
        for (int dz = -1; dz <= 1; dz++) {
            float ox = (float)(base_tx + dx) * TILE_W + drift;
            float oz = (float)(base_tz + dz) * TILE_W;
            shader_.setVec3("uOffset", ox, 0.0f, oz);
            glDrawElements(GL_TRIANGLES, idx_count_, GL_UNSIGNED_INT, nullptr);
        }
    }

    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
}

// ─────────────────────────────────────────────────────────────────────────────
void Cloud::destroy() {
    shader_.destroy();
    if (ebo_) { glDeleteBuffers(1, &ebo_);       ebo_ = 0; }
    if (vbo_) { glDeleteBuffers(1, &vbo_);       vbo_ = 0; }
    if (vao_) { glDeleteVertexArrays(1, &vao_);  vao_ = 0; }
}

Cloud::~Cloud() { destroy(); }
