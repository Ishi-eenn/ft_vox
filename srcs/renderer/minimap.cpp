#include "renderer/minimap.hpp"
#include "world/world.hpp"
#include "types.hpp"
#include <glad/gl.h>
#include <cmath>
#include <cstring>
#include <algorithm>

// ── ブロックタイプ→RGBA カラーテーブル（BlockType のインデックスと対応） ─────
static constexpr uint8_t kBlockRGB[9][3] = {
    {  25,  25,  25},  // Air
    {  88, 148,  64},  // Grass
    { 134,  96,  67},  // Dirt
    { 118, 118, 116},  // Stone
    { 215, 190, 130},  // Sand
    { 235, 245, 255},  // Snow
    {  44, 102, 195},  // Water
    { 100,  73,  47},  // Wood
    {  50,  95,  35},  // Leaves
};

// ─────────────────────────────────────────────────────────────────────────────
bool Minimap::init() {
    if (!shader_.load("assets/shaders/minimap.vert", "assets/shaders/minimap.frag"))
        return false;

    // テクスチャを生成（RGBA, Nearest フィルタリング）
    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    std::memset(pixels_, 20, sizeof(pixels_));        // 暗いグレーで初期化
    for (int i = 3; i < kSize * kSize * 4; i += 4)
        pixels_[i] = 255;  // Alpha を全て不透明に
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kSize, kSize, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    // テクスチャ付き四角形 VAO（pos + UV, 動的更新）
    glGenVertexArrays(1, &tex_vao_);
    glGenBuffers(1, &tex_vbo_);
    glBindVertexArray(tex_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, tex_vbo_);
    glBufferData(GL_ARRAY_BUFFER, 4 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void*)(2 * sizeof(float)));
    glBindVertexArray(0);

    // 背景・ボーダー用 VAO（pos のみ, 動的更新）
    glGenVertexArrays(1, &dyn_vao_);
    glGenBuffers(1, &dyn_vbo_);
    glBindVertexArray(dyn_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, dyn_vbo_);
    glBufferData(GL_ARRAY_BUFFER, 32 * 2 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
void Minimap::update(World& world, float px, float pz, float yaw_deg, float dt) {
    update_timer_ += dt;
    const float dx = std::abs(px - last_px_);
    const float dz = std::abs(pz - last_pz_);

    if (update_timer_ < 0.25f && dx < 2.0f && dz < 2.0f) return;
    update_timer_ = 0.f;
    last_px_ = px;
    last_pz_ = pz;

    // Origin: world block at pixel (col=0, row=0)
    const int origin_wx = static_cast<int>(std::floor(px)) - kHalf;
    const int origin_wz = static_cast<int>(std::floor(pz)) - kHalf;

    // ── テクスチャピクセルを地形カラーで埋める（チャンク単位でハッシュ検索）──
    // row 0 = 北（world -Z 方向）、row kSize-1 = 南（world +Z 方向）
    // col 0 = 西（world -X 方向）、col kSize-1 = 東（world +X 方向）
    std::memset(pixels_, 25, sizeof(pixels_));   // 未ロードチャンクはグレーに
    for (int i = 3; i < kSize * kSize * 4; i += 4) pixels_[i] = 255;

    auto& chunks = world.chunks();
    const int chx0 = static_cast<int>(std::floor(static_cast<float>(origin_wx) / CHUNK_SIZE_X));
    const int chx1 = static_cast<int>(std::floor(static_cast<float>(origin_wx + kSize - 1) / CHUNK_SIZE_X));
    const int chz0 = static_cast<int>(std::floor(static_cast<float>(origin_wz) / CHUNK_SIZE_Z));
    const int chz1 = static_cast<int>(std::floor(static_cast<float>(origin_wz + kSize - 1) / CHUNK_SIZE_Z));

    for (int chz = chz0; chz <= chz1; ++chz) {
        for (int chx = chx0; chx <= chx1; ++chx) {
            auto it = chunks.find({chx, chz});
            if (it == chunks.end() || !it->second) continue;
            const Chunk* chunk = it->second.get();

            // Pixel column/row range covered by this chunk
            const int col0 = std::max(0,     chx * CHUNK_SIZE_X - origin_wx);
            const int col1 = std::min(kSize,  (chx + 1) * CHUNK_SIZE_X - origin_wx);
            const int row0 = std::max(0,     chz * CHUNK_SIZE_Z - origin_wz);
            const int row1 = std::min(kSize,  (chz + 1) * CHUNK_SIZE_Z - origin_wz);

            for (int row = row0; row < row1; ++row) {
                const int lz = (origin_wz + row) - chz * CHUNK_SIZE_Z;
                for (int col = col0; col < col1; ++col) {
                    const int lx = (origin_wx + col) - chx * CHUNK_SIZE_X;
                    BlockType bt = BlockType::Air;
                    for (int y = 128; y >= 4; --y) {
                        BlockType b = chunk->getBlock(lx, y, lz);
                        if (b != BlockType::Air) { bt = b; break; }
                    }
                    int bi = static_cast<int>(bt);
                    if (bi < 0 || bi >= 9) bi = 0;
                    int idx = (row * kSize + col) * 4;
                    pixels_[idx + 0] = kBlockRGB[bi][0];
                    pixels_[idx + 1] = kBlockRGB[bi][1];
                    pixels_[idx + 2] = kBlockRGB[bi][2];
                    pixels_[idx + 3] = 255;
                }
            }
        }
    }

    // ── プレイヤードット（3×3 白）────────────────────────────────────────────
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx2 = -1; dx2 <= 1; dx2++) {
            int r = kHalf + dy;
            int c = kHalf + dx2;
            if (r >= 0 && r < kSize && c >= 0 && c < kSize) {
                int idx = (r * kSize + c) * 4;
                pixels_[idx+0] = 255;
                pixels_[idx+1] = 255;
                pixels_[idx+2] = 255;
                pixels_[idx+3] = 255;
            }
        }
    }

    // ── 進行方向矢印（5ピクセル、黄色）──────────────────────────────────────
    // yaw=0 → front.x=cos(0)=1, front.z=sin(0)=0 → 東（col+）
    // minimap: col+ = +X = 東, row+ = +Z = 南
    const float rad     = yaw_deg * (3.14159265f / 180.0f);
    const float dir_col = std::cos(rad);   // +X（東）
    const float dir_row = std::sin(rad);   // +Z（南）
    for (int i = 3; i <= 7; ++i) {
        int r = kHalf + static_cast<int>(std::round(dir_row * i));
        int c = kHalf + static_cast<int>(std::round(dir_col * i));
        if (r >= 0 && r < kSize && c >= 0 && c < kSize) {
            int idx = (r * kSize + c) * 4;
            pixels_[idx+0] = 255;
            pixels_[idx+1] = 215;
            pixels_[idx+2] = 0;
            pixels_[idx+3] = 255;
        }
    }

    // GPU テクスチャを更新
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kSize, kSize,
                    GL_RGBA, GL_UNSIGNED_BYTE, pixels_);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
void Minimap::draw(Shader& hud_shader, int sw, int sh) {
    // ピクセル → NDC 変換ヘルパー
    auto ndcX = [sw](float px) { return -1.0f + 2.0f * px / static_cast<float>(sw); };
    auto ndcY = [sh](float py) { return  1.0f - 2.0f * py / static_cast<float>(sh); };

    constexpr float kMarginX = 14.0f;  // 左端からの余白（px）
    constexpr float kMarginY = 46.0f;  // 上端からの余白（座標HUDの下に配置）
    constexpr float kPad     = 4.0f;   // フレームとマップの間隔（px）
    constexpr float kMapSize = 256.0f; // マップ表示サイズ（px）

    // フレーム外枠の NDC 座標
    const float bx0 = ndcX(kMarginX);
    const float bx1 = ndcX(kMarginX + kPad * 2 + kMapSize);
    const float by1 = ndcY(kMarginY);
    const float by0 = ndcY(kMarginY + kPad * 2 + kMapSize);

    // マップ本体の NDC 座標
    const float mx0 = ndcX(kMarginX + kPad);
    const float mx1 = ndcX(kMarginX + kPad + kMapSize);
    const float my1 = ndcY(kMarginY + kPad);
    const float my0 = ndcY(kMarginY + kPad + kMapSize);

    glDisable(GL_DEPTH_TEST);

    // ── 1. 半透明の暗い背景フレーム ─────────────────────────────────────────
    {
        float bg[] = {
            bx0, by0,  bx1, by0,  bx1, by1,
            bx0, by0,  bx1, by1,  bx0, by1,
        };
        glBindBuffer(GL_ARRAY_BUFFER, dyn_vbo_);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(bg), bg);
        glBindVertexArray(dyn_vao_);
        hud_shader.use();
        hud_shader.setVec4("uColor", 0.0f, 0.0f, 0.0f, 0.65f);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    // ── 2. ミニマップテクスチャ ──────────────────────────────────────────────
    // UV.y=0 が GL テクスチャ底辺 = 私の row 0 = 北方向
    // 北が画面上になるよう UV を反転配置する
    {
        float quad[] = {
            mx0, my0,  0.0f, 1.0f,   // 画面左下 → テクスチャ南（UV.y=1）
            mx1, my0,  1.0f, 1.0f,   // 画面右下 → テクスチャ南
            mx1, my1,  1.0f, 0.0f,   // 画面右上 → テクスチャ北（UV.y=0）
            mx0, my1,  0.0f, 0.0f,   // 画面左上 → テクスチャ北
        };
        glBindBuffer(GL_ARRAY_BUFFER, tex_vbo_);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(quad), quad);
        glBindVertexArray(tex_vao_);
        shader_.use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex_);
        shader_.setInt("uMap", 0);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // ── 3. 白いボーダーライン ────────────────────────────────────────────────
    {
        float border[] = {
            bx0, by1,  bx1, by1,   // 上辺
            bx1, by1,  bx1, by0,   // 右辺
            bx1, by0,  bx0, by0,   // 下辺
            bx0, by0,  bx0, by1,   // 左辺
        };
        glBindBuffer(GL_ARRAY_BUFFER, dyn_vbo_);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(border), border);
        glBindVertexArray(dyn_vao_);
        hud_shader.use();
        hud_shader.setVec4("uColor", 0.75f, 0.75f, 0.75f, 0.85f);
        glDrawArrays(GL_LINES, 0, 8);
    }

    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

// ─────────────────────────────────────────────────────────────────────────────
void Minimap::destroy() {
    shader_.destroy();
    if (tex_)     { glDeleteTextures(1, &tex_);              tex_     = 0; }
    if (tex_vao_) { glDeleteVertexArrays(1, &tex_vao_);      tex_vao_ = 0; }
    if (tex_vbo_) { glDeleteBuffers(1, &tex_vbo_);           tex_vbo_ = 0; }
    if (dyn_vao_) { glDeleteVertexArrays(1, &dyn_vao_);      dyn_vao_ = 0; }
    if (dyn_vbo_) { glDeleteBuffers(1, &dyn_vbo_);           dyn_vbo_ = 0; }
}
