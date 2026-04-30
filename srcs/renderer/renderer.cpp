// ─────────────────────────────────────────────────────────────────────────────
// renderer.cpp — OpenGL を使って画面に絵を描く「描画係」
//
// 【OpenGL とは？】
//   GPU（グラフィックスチップ）に命令を送るための API。
//   「この頂点データを GPU に転送して」「このシェーダで描いて」という
//   指示を C++ から出す仕組み。
//
// 【このファイルがやること】
//   1. OpenGL の初期化（深度テスト・カリング・ブレンドを有効化）
//   2. チャンクのメッシュを GPU に転送する（uploadChunkMesh）
//   3. 毎フレーム、不透明チャンク→水チャンク→スカイボックス→HUD の順に描画
//   4. 昼夜サイクルに合わせて太陽方向・空の色・ライト強度を更新（setTimeOfDay）
// ─────────────────────────────────────────────────────────────────────────────
#include "renderer/renderer.hpp"
#include "types.hpp"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <array>
#include <cstdio>
#include <iostream>
#include <cstring>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// コンストラクタ / デストラクタ
// ─────────────────────────────────────────────────────────────────────────────
Renderer::Renderer() = default;

Renderer::~Renderer() {
    // シェーダープログラム・GPU バッファをすべて解放する
    chunk_shader_.destroy();
    sky_shader_.destroy();
    hud_shader_.destroy();
    if (hud_vao_)     { glDeleteVertexArrays(1, &hud_vao_);     hud_vao_     = 0; }
    if (hud_vbo_)     { glDeleteBuffers(1, &hud_vbo_);          hud_vbo_     = 0; }
    if (overlay_vao_) { glDeleteVertexArrays(1, &overlay_vao_); overlay_vao_ = 0; }
    if (overlay_vbo_) { glDeleteBuffers(1, &overlay_vbo_);      overlay_vbo_ = 0; }
    atlas_.destroy();
    skybox_.destroy();
}

// ─────────────────────────────────────────────────────────────────────────────
// init() — レンダラーの初期化
//
// GLAD で OpenGL 関数ポインタを取得し、各種レンダリング状態を設定する。
// シェーダー・テクスチャアトラス・スカイボックスもここで準備する。
// ─────────────────────────────────────────────────────────────────────────────
bool Renderer::init(GLFWwindow* window) {
    window_ = window;

    // GLAD で OpenGL 関数ポインタを取得する（コンテキスト作成後に必須）
    if (!gladLoadGL(glfwGetProcAddress)) {
        std::cerr << "[Renderer] gladLoadGL failed\n";
        return false;
    }

    std::cerr << "[Renderer] OpenGL " << glGetString(GL_VERSION)
              << "  GLSL " << glGetString(GL_SHADING_LANGUAGE_VERSION) << "\n";

    // ── レンダリング状態の設定 ───────────────────────────────────────────────
    glEnable(GL_DEPTH_TEST);   // 深度テスト: 近いものが遠いものを隠す
    glEnable(GL_CULL_FACE);    // 裏面カリング: 見えない裏面ポリゴンをスキップ
    glCullFace(GL_BACK);       // 裏面 = 頂点が時計回りになる面
    glEnable(GL_BLEND);        // アルファブレンド: 透明水の描画に必要
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);  // 標準的な半透明合成

    // ── シェーダーの読み込み ──────────────────────────────────────────────────
    if (!chunk_shader_.load("assets/shaders/chunk.vert", "assets/shaders/chunk.frag")) {
        std::cerr << "[Renderer] Failed to load chunk shaders\n";
        return false;
    }
    if (!sky_shader_.load("assets/shaders/skybox.vert", "assets/shaders/skybox.frag")) {
        std::cerr << "[Renderer] Failed to load skybox shaders\n";
        return false;
    }

    // ── テクスチャアトラスの生成 ───────────────────────────────────────────────
    // 全ブロックの絵柄を1枚のテクスチャにまとめたもの（アトラス）
    if (!atlas_.generate()) {
        std::cerr << "[Renderer] Failed to generate texture atlas\n";
        return false;
    }

    // ── スカイボックスの初期化 ────────────────────────────────────────────────
    // 世界を取り囲む巨大な立方体として空を描く
    if (!skybox_.init()) {
        std::cerr << "[Renderer] Failed to initialise skybox\n";
        return false;
    }

    // ── HUD（照準＋FPS表示）の初期化 ──────────────────────────────────────────
    initHud();

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// initHud() — 照準（クロスヘア）と水中オーバーレイの GPU バッファを準備する
//
// 【HUD とは？】
//   Heads-Up Display の略。ゲーム内の UI（照準・FPS など）をさす。
//   3D ワールドの上に2D で重ね描きする。
//
// 【NDC（Normalized Device Coordinates）】
//   OpenGL の画面座標系。X/Y ともに -1.0〜+1.0 で画面全体を表す。
//   左下が(-1,-1)、右上が(+1,+1)。
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::initHud() {
    hud_shader_.load("assets/shaders/hud.vert", "assets/shaders/hud.frag");

    // 照準 + FPS 数字の線分データ（動的に更新するので GL_DYNAMIC_DRAW）
    glGenVertexArrays(1, &hud_vao_);
    glGenBuffers(1, &hud_vbo_);
    glBindVertexArray(hud_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, hud_vbo_);
    glBufferData(GL_ARRAY_BUFFER, 256 * 2 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glBindVertexArray(0);

    // 水中オーバーレイ: 画面全体を覆う2枚の三角形（NDC 座標）
    // 画面四隅 (-1,-1), (1,-1), (1,1), (-1,1) で正方形を作る
    static const float quad[] = {
        -1.f, -1.f,  1.f, -1.f,  1.f,  1.f,
        -1.f, -1.f,  1.f,  1.f, -1.f,  1.f,
    };
    glGenVertexArrays(1, &overlay_vao_);
    glGenBuffers(1, &overlay_vbo_);
    glBindVertexArray(overlay_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, overlay_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glBindVertexArray(0);
}

// ─────────────────────────────────────────────────────────────────────────────
// appendLine() / appendDigit() / appendNumber() — HUD 用線分ヘルパー
//
// 7セグメントディスプレイ風に FPS 数字を描く。
// SEGMENTS の各ビットが7本の線分のうちどれを点灯させるかを示す。
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::appendLine(float* verts, int& count, float x0, float y0, float x1, float y1) const {
    verts[count++] = x0;
    verts[count++] = y0;
    verts[count++] = x1;
    verts[count++] = y1;
}

void Renderer::appendDigit(float* verts, int& count, int digit, float left, float top, float w, float h) const {
    // 7セグメント表示のビットマスク（上から時計回りで各セグメントに対応）
    static const uint8_t SEGMENTS[10] = {
        0b1111110, 0b0110000, 0b1101101, 0b1111001, 0b0110011,
        0b1011011, 0b1011111, 0b1110000, 0b1111111, 0b1111011
    };

    if (digit < 0 || digit > 9) return;

    const float right = left + w;
    const float mid   = top - h * 0.5f;
    const float bot   = top - h;
    const uint8_t mask = SEGMENTS[digit];

    // 各ビットに対応する線分を追加する
    if (mask & 0b1000000) appendLine(verts, count, left, top, right, top);   // 上横
    if (mask & 0b0100000) appendLine(verts, count, right, top, right, mid);  // 右上縦
    if (mask & 0b0010000) appendLine(verts, count, right, mid, right, bot);  // 右下縦
    if (mask & 0b0001000) appendLine(verts, count, left, bot, right, bot);   // 下横
    if (mask & 0b0000100) appendLine(verts, count, left, mid, left, bot);    // 左下縦
    if (mask & 0b0000010) appendLine(verts, count, left, top, left, mid);    // 左上縦
    if (mask & 0b0000001) appendLine(verts, count, left, mid, right, mid);   // 中横
}

void Renderer::appendNumber(float* verts, int& count, int value, float right, float top, float w, float h, float gap) const {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", value);
    int len = 0;
    while (buf[len] != '\0') ++len;

    // 右揃えで描くために全体幅を計算してから左端を決める
    float total = len * w + (len > 0 ? (len - 1) * gap : 0.0f);
    float x = right - total;
    for (int i = 0; i < len; ++i) {
        appendDigit(verts, count, buf[i] - '0', x, top, w, h);
        x += w + gap;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// drawHud() — 照準とFPS数字を画面に描く
//
// 毎フレーム呼ばれ、照準（十字線）と右上のFPS数字をラインとして描画する。
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawHud(int fps) {
    std::array<float, 256> verts{};
    int count = 0;

    // 照準の十字線（画面中央の小さな十字）
    float cx = 20.0f / (float)(width_  / 2);
    float cy = 20.0f / (float)(height_ / 2);
    appendLine(verts.data(), count, -cx, 0.f, cx, 0.f);
    appendLine(verts.data(), count, 0.f, -cy, 0.f, cy);

    // FPS 数字（右上に表示）
    float px = 14.0f / (float)(width_ / 2);
    float py = 18.0f / (float)(height_ / 2);
    float digit_w = 14.0f / (float)(width_ / 2);
    float digit_h = 24.0f / (float)(height_ / 2);
    float gap = 6.0f / (float)(width_ / 2);
    appendNumber(verts.data(), count, fps, 1.0f - px, 1.0f - py, digit_w, digit_h, gap);

    // GPU に線分データを転送して描画
    glBindBuffer(GL_ARRAY_BUFFER, hud_vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(float), verts.data());

    glDisable(GL_DEPTH_TEST);  // HUD は常に最前面に表示
    hud_shader_.use();
    hud_shader_.setVec4("uColor", 1.0f, 1.0f, 1.0f, 0.9f);  // 白色
    glBindVertexArray(hud_vao_);
    glDrawArrays(GL_LINES, 0, count / 2);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

// ─────────────────────────────────────────────────────────────────────────────
// drawUnderwaterOverlay() — 水中にいるとき画面を青くするオーバーレイ
//
// 深度テストを無効にして画面全体の四角形を半透明の青で塗る。
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawUnderwaterOverlay() {
    glDisable(GL_DEPTH_TEST);
    hud_shader_.use();
    hud_shader_.setVec4("uColor", 0.0f, 0.25f, 0.65f, 0.40f);  // 半透明の青
    glBindVertexArray(overlay_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

// ─────────────────────────────────────────────────────────────────────────────
// uploadChunkMesh() — CPU で生成したメッシュを GPU へ転送する
//
// 【VAO・VBO・EBO とは？】
//   VAO (Vertex Array Object): 頂点データの「フォーマット設定」を記憶する入れ物
//   VBO (Vertex Buffer Object): 頂点座標・UV・法線などのデータを持つ GPU バッファ
//   EBO (Element Buffer Object): 三角形を構成するインデックスを持つ GPU バッファ
//
// 不透明インデックス（土・石など）と水インデックスを1つの EBO に詰めて、
// drawChunk と drawChunkWater がそれぞれの範囲だけを描画できるようにする。
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::uploadChunkMesh(Chunk* chunk) {
    if (chunk->vertices.empty()) {
        // 頂点データがない（完全に空のチャンク）→ ダーティフラグだけ落とす
        chunk->is_dirty = false;
        return;
    }

    // 既に GPU にデータがあれば先に解放する（再アップロードの場合）
    if (chunk->gpu.uploaded) {
        destroyChunkMesh(chunk);
    }

    glGenVertexArrays(1, &chunk->gpu.vao);
    glGenBuffers(1, &chunk->gpu.vbo);
    glGenBuffers(1, &chunk->gpu.ebo);

    glBindVertexArray(chunk->gpu.vao);

    // 頂点データをGPUに転送
    glBindBuffer(GL_ARRAY_BUFFER, chunk->gpu.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(chunk->vertices.size() * sizeof(Vertex)),
                 chunk->vertices.data(),
                 GL_STATIC_DRAW);

    // 不透明インデックス + 水インデックスを1本のバッファに連結する。
    // drawChunk は [0, idx_count) を、drawChunkWater はその後ろを参照する。
    const size_t n_opaque = chunk->indices.size();
    const size_t n_water  = chunk->indices_water.size();
    std::vector<uint32_t> all_indices;
    all_indices.reserve(n_opaque + n_water);
    all_indices.insert(all_indices.end(), chunk->indices.begin(),       chunk->indices.end());
    all_indices.insert(all_indices.end(), chunk->indices_water.begin(), chunk->indices_water.end());

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, chunk->gpu.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(all_indices.size() * sizeof(uint32_t)),
                 all_indices.data(),
                 GL_STATIC_DRAW);

    // 頂点属性レイアウト — struct Vertex のメモリ配置に合わせる:
    //   オフセット  0: 位置 (x, y, z) — location 0 に対応
    //   オフセット 12: UV  (u, v)     — location 1 に対応
    //   オフセット 20: 法線 (nx,ny,nz)— location 2 に対応
    //   ストライド: sizeof(Vertex) = 32 バイト
    const GLsizei stride = static_cast<GLsizei>(sizeof(Vertex));

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                          (void*)offsetof(Vertex, x));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                          (void*)offsetof(Vertex, u));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride,
                          (void*)offsetof(Vertex, nx));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    // 注意: VAO をバインド解除した後に EBO を解除してはいけない（記録済みのため）

    chunk->gpu.idx_count       = static_cast<int32_t>(n_opaque);
    chunk->gpu.idx_count_water = static_cast<int32_t>(n_water);
    chunk->gpu.uploaded        = true;

    // GPU に転送したので CPU 側のメッシュデータを解放してメモリを節約する
    chunk->vertices.clear();
    chunk->vertices.shrink_to_fit();
    chunk->indices.clear();
    chunk->indices.shrink_to_fit();
    chunk->indices_water.clear();
    chunk->indices_water.shrink_to_fit();

    chunk->is_dirty = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// destroyChunkMesh() — チャンクの GPU リソース（VAO/VBO/EBO）を解放する
//
// チャンクが視野外に出て LRU キャッシュから追い出されるときに呼ばれる。
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::destroyChunkMesh(Chunk* chunk) {
    if (!chunk->gpu.uploaded) return;

    glDeleteVertexArrays(1, &chunk->gpu.vao);
    glDeleteBuffers(1, &chunk->gpu.vbo);
    glDeleteBuffers(1, &chunk->gpu.ebo);

    chunk->gpu.vao             = 0;
    chunk->gpu.vbo             = 0;
    chunk->gpu.ebo             = 0;
    chunk->gpu.idx_count       = 0;
    chunk->gpu.idx_count_water = 0;
    chunk->gpu.uploaded        = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// beginFrame() — フレームの開始処理（画面をクリアする）
//
// 空の地平線色で背景を塗りつぶすことで、チャンクが欠けていても
// スカイボックスとの継ぎ目が見えにくくなる。
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::beginFrame() {
    glClearColor(sky_horizon_[0], sky_horizon_[1], sky_horizon_[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

// ─────────────────────────────────────────────────────────────────────────────
// buildMVP() — MVP 行列を生成する共通ヘルパー
//
// 【MVP 行列とは？】
//   3D ワールドの点を2D 画面上の点に変換するための行列の積。
//   Model: ワールド座標へ配置（チャンクの位置オフセット）
//   View:  カメラ視点への変換（カメラを原点として世界を回す）
//   Projection: 遠近感を付けて画面座標へ変換
//   MVP = Projection × View × Model の順に掛ける。
// ─────────────────────────────────────────────────────────────────────────────
static glm::mat4 buildMVP(const Chunk* chunk,
                           const float* view4x4, const float* proj4x4) {
    // チャンクのワールド位置への平行移動行列
    glm::mat4 model = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(
            static_cast<float>(chunk->pos.x) * static_cast<float>(CHUNK_SIZE_X),
            0.0f,
            static_cast<float>(chunk->pos.z) * static_cast<float>(CHUNK_SIZE_Z)
        )
    );
    glm::mat4 view = glm::make_mat4(view4x4);
    glm::mat4 proj = glm::make_mat4(proj4x4);
    return proj * view * model;
}

// ─────────────────────────────────────────────────────────────────────────────
// setChunkLightingUniforms() — ライティング用ユニフォームをシェーダーに送る
//
// 【ユニフォーム変数とは？】
//   シェーダー（GPU プログラム）に渡す定数。毎頂点同じ値を使う場合に使用する。
//   太陽の方向・環境光・太陽光の強さを渡して、シェーダー内で明暗計算させる。
// ─────────────────────────────────────────────────────────────────────────────
static void setChunkLightingUniforms(Shader& shader,
                                     const float sun_dir[3],
                                     float ambient, float sun_strength) {
    shader.setVec3 ("uSunDir",      sun_dir[0],   sun_dir[1],   sun_dir[2]);
    shader.setFloat("uAmbient",     ambient);
    shader.setFloat("uSunStrength", sun_strength);
}

// ─────────────────────────────────────────────────────────────────────────────
// drawChunk() — 不透明チャンクを描画する（パス1）
//
// 不透明ブロック（土・石・草など）だけのインデックスを使って描画する。
// 水の半透明描画は drawChunkWater() で別途行う（順番が大事）。
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawChunk(const Chunk* chunk, const float* view4x4, const float* proj4x4) {
    if (!chunk->gpu.uploaded || chunk->gpu.idx_count == 0) return;

    glm::mat4 mvp = buildMVP(chunk, view4x4, proj4x4);

    chunk_shader_.use();
    chunk_shader_.setMat4("uMVP", glm::value_ptr(mvp));
    setChunkLightingUniforms(chunk_shader_, sun_dir_, ambient_, sun_strength_);
    atlas_.bind(0);                    // テクスチャスロット 0 にアトラスを bind
    chunk_shader_.setInt("uAtlas", 0);

    glBindVertexArray(chunk->gpu.vao);
    glDrawElements(GL_TRIANGLES,
                   static_cast<GLsizei>(chunk->gpu.idx_count),
                   GL_UNSIGNED_INT,
                   nullptr);           // EBO の先頭から idx_count 個分
    glBindVertexArray(0);
}

// ─────────────────────────────────────────────────────────────────────────────
// drawChunkWater() — 水チャンクを描画する（パス2、半透明）
//
// 【深度書き込みを無効にする理由】
//   水は半透明なので、後ろに見えるブロックも描画される必要がある。
//   深度バッファへの書き込みをオフにすることで、水の奥にある地形を
//   上書きしないようにする。深度テスト（読み込み）は引き続き有効なので
//   水は手前の固体ブロックには隠れる。
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawChunkWater(const Chunk* chunk, const float* view4x4, const float* proj4x4) {
    if (!chunk->gpu.uploaded || chunk->gpu.idx_count_water == 0) return;

    glm::mat4 mvp = buildMVP(chunk, view4x4, proj4x4);

    chunk_shader_.use();
    chunk_shader_.setMat4("uMVP", glm::value_ptr(mvp));
    setChunkLightingUniforms(chunk_shader_, sun_dir_, ambient_, sun_strength_);
    atlas_.bind(0);
    chunk_shader_.setInt("uAtlas", 0);

    glDepthMask(GL_FALSE);  // 深度バッファへの書き込みを止める

    glBindVertexArray(chunk->gpu.vao);
    // 水インデックスは EBO の中で不透明インデックスの後ろに続いている
    const GLintptr water_offset =
        static_cast<GLintptr>(chunk->gpu.idx_count) * static_cast<GLintptr>(sizeof(uint32_t));
    glDrawElements(GL_TRIANGLES,
                   static_cast<GLsizei>(chunk->gpu.idx_count_water),
                   GL_UNSIGNED_INT,
                   reinterpret_cast<const void*>(water_offset));
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);   // 深度書き込みを元に戻す
}

// ─────────────────────────────────────────────────────────────────────────────
// drawSkybox() — スカイボックスを描画する
//
// スカイボックスは「無限遠にある空の箱」として描かれる。
// View 行列から移動成分を取り除いた 3×3 部分行列を使うことで
// カメラが移動しても空が常に同じ場所に見える。
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawSkybox(const float* view3x3, const float* proj4x4) {
    sky_shader_.use();
    sky_shader_.setVec3("uSkyZenith",   sky_zenith_[0],  sky_zenith_[1],  sky_zenith_[2]);
    sky_shader_.setVec3("uSkyHorizon",  sky_horizon_[0], sky_horizon_[1], sky_horizon_[2]);
    sky_shader_.setVec3("uGroundColor", sky_ground_[0],  sky_ground_[1],  sky_ground_[2]);
    sky_shader_.setVec3("uSunDir",      sun_dir_[0],     sun_dir_[1],     sun_dir_[2]);
    sky_shader_.setVec3("uSunColor",    sun_color_[0],   sun_color_[1],   sun_color_[2]);
    skybox_.draw(view3x3, proj4x4, sky_shader_);
}

// ─────────────────────────────────────────────────────────────────────────────
// endFrame() — フレームの終了処理（ダブルバッファリング）
//
// 【ダブルバッファリングとは？】
//   描画中の画面をそのまま表示すると「描きかけ」が見えてしまう。
//   裏画面（バックバッファ）に描き終わってから表画面と入れ替えることで
//   ちらつきのないスムーズな表示を実現する。
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::endFrame() {
    glfwSwapBuffers(window_);  // バックバッファとフロントバッファを入れ替える
}

// ─────────────────────────────────────────────────────────────────────────────
// onResize() — ウィンドウサイズが変わったときのリサイズ処理
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::onResize(int w, int h) {
    glViewport(0, 0, w, h);  // 描画領域をウィンドウ全体に合わせる
    width_  = w;
    height_ = h;
}

// ─────────────────────────────────────────────────────────────────────────────
// setTimeOfDay() — 時刻に応じて空の色と太陽の方向を更新する
//
// t: 0.0〜1.0 の1日の進行度（0.0=深夜, 0.25=日の出, 0.5=正午, 0.75=日の入り）
//
// 【太陽の動き】
//   t に応じて角度を計算し、sin/cos で太陽方向ベクトルを作る。
//   elev（仰角）が +1 なら真上、-1 なら真下（地平線の下）。
//
// 【空の色のブレンド】
//   夜・夜明け/黄昏・昼 の3段階で基準色を定義し、
//   仰角を使って線形補間（lerp）でなめらかに遷移させる。
//
// 【環境光と太陽光強度】
//   ambient: 影の部分にも当たる最低限の明るさ（月明かりのイメージ）
//   sun_strength: 太陽が当たる面への追加光（昼は強く、夜は0）
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::setTimeOfDay(float t) {
    // 太陽方向ベクトルを計算する
    // angle=0 で深夜（太陽は真下）、angle=π で正午（太陽は真上）
    const float angle = 2.0f * static_cast<float>(M_PI) * t;
    const float raw_x = std::sin(angle);
    const float raw_y = -std::cos(angle);
    const float raw_z = 0.30f;  // 北半球の太陽の傾き（視覚的な面白さのため）
    const float len   = std::sqrt(raw_x*raw_x + raw_y*raw_y + raw_z*raw_z);
    sun_dir_[0] = raw_x / len;
    sun_dir_[1] = raw_y / len;
    sun_dir_[2] = raw_z / len;

    const float elev = sun_dir_[1];  // -1=水平線下, +1=真上

    // RGB 色を線形補間するヘルパー
    auto lerpCol = [](float* dst,
                      const float a[3], const float b[3], float f) {
        for (int i = 0; i < 3; ++i)
            dst[i] = a[i] + (b[i] - a[i]) * f;
    };
    auto clamp01 = [](float x) { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); };

    // ── 空の基準色テーブル ─────────────────────────────────────────────────────
    static const float kNightZenith [3] = {0.01f, 0.02f, 0.08f};  // 夜の天頂（濃紺）
    static const float kNightHorizon[3] = {0.02f, 0.04f, 0.12f};  // 夜の地平線
    static const float kNightGround [3] = {0.01f, 0.01f, 0.03f};  // 夜の地面反射

    static const float kDawnZenith  [3] = {0.12f, 0.22f, 0.52f};  // 夜明け天頂
    static const float kDawnHorizon [3] = {0.90f, 0.40f, 0.10f};  // 夜明け地平（オレンジ）
    static const float kDawnGround  [3] = {0.20f, 0.15f, 0.10f};  // 夜明け地面

    static const float kDayZenith   [3] = {0.08f, 0.25f, 0.65f};  // 昼の天頂（青空）
    static const float kDayHorizon  [3] = {0.55f, 0.72f, 0.90f};  // 昼の地平（水色）
    static const float kDayGround   [3] = {0.35f, 0.30f, 0.25f};  // 昼の地面

    static const float kSunColorDawn[3] = {1.00f, 0.60f, 0.20f};  // 朝日（橙）
    static const float kSunColorDay [3] = {1.00f, 0.98f, 0.85f};  // 昼の太陽（白に近い）
    static const float kSunColorDusk[3] = {1.00f, 0.50f, 0.10f};  // 夕日（深橙）

    // ── 仰角でブレンドして空の色を決める ─────────────────────────────────────
    if (elev <= -0.15f) {
        // 完全な夜
        for (int i = 0; i < 3; ++i) {
            sky_zenith_ [i] = kNightZenith [i];
            sky_horizon_[i] = kNightHorizon[i];
            sky_ground_ [i] = kNightGround [i];
            sun_color_  [i] = kSunColorDay [i];
        }
        ambient_      = 0.10f;  // 最低限の月明かり
        sun_strength_ = 0.0f;   // 太陽光なし
    } else if (elev < 0.15f) {
        // 夜明け・夕暮れの移行期間
        const float f = clamp01((elev + 0.15f) / 0.30f);  // 0=夜, 1=昼方向
        lerpCol(sky_zenith_,  kNightZenith,  kDawnZenith,  f);
        lerpCol(sky_horizon_, kNightHorizon, kDawnHorizon, f);
        lerpCol(sky_ground_,  kNightGround,  kDawnGround,  f);
        // 日の出か日の入りかで太陽色を変える
        const float* sunrise_col = (t < 0.5f) ? kSunColorDawn : kSunColorDusk;
        lerpCol(sun_color_, kSunColorDay, sunrise_col, 1.0f - f);
        ambient_      = 0.10f + 0.08f * f;
        sun_strength_ = 0.30f * f;
    } else {
        // 昼間 — 太陽が高くなるほど空が深い青になる
        const float day_f = clamp01((elev - 0.15f) / 0.85f);  // 0=低い太陽, 1=真上
        lerpCol(sky_zenith_,  kDawnZenith,  kDayZenith,  day_f);
        lerpCol(sky_horizon_, kDawnHorizon, kDayHorizon, day_f);
        lerpCol(sky_ground_,  kDawnGround,  kDayGround,  day_f);
        lerpCol(sun_color_,   kSunColorDawn, kSunColorDay, day_f);
        ambient_      = 0.18f + 0.14f * day_f;
        sun_strength_ = 0.30f + 0.35f * day_f;
    }

    // 太陽が水平線より下のときは拡散光（太陽光）を出さない
    if (elev < 0.0f) sun_strength_ = 0.0f;
}
