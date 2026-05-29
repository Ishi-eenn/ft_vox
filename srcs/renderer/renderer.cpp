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
#include "world/world.hpp"
#include "network/client.hpp"
#include "mob/zombie.hpp"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <array>
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <cstring>
#include <cmath>
#include <random>

// ─────────────────────────────────────────────────────────────────────────────
// コンストラクタ / デストラクタ
// ─────────────────────────────────────────────────────────────────────────────
Renderer::Renderer() = default;

Renderer::~Renderer() {
    // シェーダープログラム・GPU バッファをすべて解放する
    title_screen_.destroy();
    minimap_.destroy();
    chunk_shader_.destroy();
    shadow_shader_.destroy();
    sky_shader_.destroy();
    if (shadow_fbo_)       { glDeleteFramebuffers(1, &shadow_fbo_);  shadow_fbo_       = 0; }
    if (shadow_depth_tex_) { glDeleteTextures(1, &shadow_depth_tex_); shadow_depth_tex_ = 0; }
    hud_shader_.destroy();
    entity_shader_.destroy();
    if (entity_vao_)  { glDeleteVertexArrays(1, &entity_vao_);  entity_vao_  = 0; }
    if (entity_vbo_)  { glDeleteBuffers(1, &entity_vbo_);       entity_vbo_  = 0; }
    if (entity_ebo_)  { glDeleteBuffers(1, &entity_ebo_);       entity_ebo_  = 0; }
    if (hud_vao_)     { glDeleteVertexArrays(1, &hud_vao_);     hud_vao_     = 0; }
    if (hud_vbo_)     { glDeleteBuffers(1, &hud_vbo_);          hud_vbo_     = 0; }
    if (overlay_vao_) { glDeleteVertexArrays(1, &overlay_vao_); overlay_vao_ = 0; }
    if (overlay_vbo_) { glDeleteBuffers(1, &overlay_vbo_);      overlay_vbo_ = 0; }
    if (hotbar_vao_)      { glDeleteVertexArrays(1, &hotbar_vao_);      hotbar_vao_      = 0; }
    if (hotbar_vbo_)      { glDeleteBuffers(1, &hotbar_vbo_);          hotbar_vbo_      = 0; }
    hotbar_shader_.destroy();
    if (hotbar_tex_vao_)  { glDeleteVertexArrays(1, &hotbar_tex_vao_); hotbar_tex_vao_  = 0; }
    if (hotbar_tex_vbo_)  { glDeleteBuffers(1, &hotbar_tex_vbo_);      hotbar_tex_vbo_  = 0; }
    atlas_.destroy();
    skybox_.destroy();
    cloud_.destroy();

    // SSAO リソース解放
    gbuffer_shader_.destroy();
    ssao_shader_.destroy();
    ssao_blur_shader_.destroy();
    if (gbuffer_fbo_)        { glDeleteFramebuffers(1, &gbuffer_fbo_);       gbuffer_fbo_        = 0; }
    if (gbuffer_normal_tex_) { glDeleteTextures(1, &gbuffer_normal_tex_);    gbuffer_normal_tex_ = 0; }
    if (gbuffer_depth_tex_)  { glDeleteTextures(1, &gbuffer_depth_tex_);     gbuffer_depth_tex_  = 0; }
    if (ssao_fbo_)           { glDeleteFramebuffers(1, &ssao_fbo_);          ssao_fbo_           = 0; }
    if (ssao_color_tex_)     { glDeleteTextures(1, &ssao_color_tex_);        ssao_color_tex_     = 0; }
    if (ssao_blur_fbo_)      { glDeleteFramebuffers(1, &ssao_blur_fbo_);     ssao_blur_fbo_      = 0; }
    if (ssao_blur_tex_)      { glDeleteTextures(1, &ssao_blur_tex_);         ssao_blur_tex_      = 0; }
    if (ssao_noise_tex_)     { glDeleteTextures(1, &ssao_noise_tex_);        ssao_noise_tex_     = 0; }
    if (ssao_quad_vao_)      { glDeleteVertexArrays(1, &ssao_quad_vao_);     ssao_quad_vao_      = 0; }
    if (ssao_quad_vbo_)      { glDeleteBuffers(1, &ssao_quad_vbo_);          ssao_quad_vbo_      = 0; }
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
    if (!shadow_shader_.load("assets/shaders/shadow.vert", "assets/shaders/shadow.frag")) {
        std::cerr << "[Renderer] Failed to load shadow shaders\n";
        return false;
    }
    if (!sky_shader_.load("assets/shaders/skybox.vert", "assets/shaders/skybox.frag")) {
        std::cerr << "[Renderer] Failed to load skybox shaders\n";
        return false;
    }

    // ── シャドウマップ FBO の生成 ──────────────────────────────────────────────
    // SHADOW_MAP_SIZE × SHADOW_MAP_SIZE の深度テクスチャに太陽視点の深度を記録する。
    glGenFramebuffers(1, &shadow_fbo_);

    glGenTextures(1, &shadow_depth_tex_);
    glBindTexture(GL_TEXTURE_2D, shadow_depth_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                 SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    // シャドウマップのサンプリング設定
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // 範囲外は「影なし」として扱う (border = 1.0)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float border_color[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border_color);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, shadow_fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D, shadow_depth_tex_, 0);
    glDrawBuffer(GL_NONE);  // カラー出力なし (深度のみ)
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "[Renderer] Shadow FBO incomplete\n";
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

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
    if (!cloud_.init()) {
        std::cerr << "[Renderer] Failed to initialise clouds\n";
        return false;
    }

    // ── HUD（照準＋FPS表示）の初期化 ──────────────────────────────────────────
    initHud();

    // ── タイトル画面の初期化 ────────────────────────────────────────────────
    title_screen_.init(atlas_, chunk_shader_, hud_shader_);

    // ── ミニマップの初期化 ──────────────────────────────────────────────────
    if (!minimap_.init()) {
        std::cerr << "[Renderer] Failed to initialise minimap\n";
        return false;
    }

    // ── エンティティ（リモートプレイヤー）シェーダーと VAO ────────────────────
    if (!entity_shader_.load("assets/shaders/entity.vert",
                              "assets/shaders/entity.frag")) {
        std::cerr << "[Renderer] Failed to load entity shaders\n";
        return false;
    }
    {
        // Unit box: (−0.5,−0.5,−0.5) to (+0.5,+0.5,+0.5).
        // 6 faces × 4 vertices, each vertex has pos(3) + normal(3).
        static const float verts[] = {
            // +Y face
            -0.5f, 0.5f,-0.5f,  0, 1, 0,
             0.5f, 0.5f,-0.5f,  0, 1, 0,
             0.5f, 0.5f, 0.5f,  0, 1, 0,
            -0.5f, 0.5f, 0.5f,  0, 1, 0,
            // -Y face
            -0.5f,-0.5f,-0.5f,  0,-1, 0,
             0.5f,-0.5f,-0.5f,  0,-1, 0,
             0.5f,-0.5f, 0.5f,  0,-1, 0,
            -0.5f,-0.5f, 0.5f,  0,-1, 0,
            // +Z face
            -0.5f,-0.5f, 0.5f,  0, 0, 1,
             0.5f,-0.5f, 0.5f,  0, 0, 1,
             0.5f, 0.5f, 0.5f,  0, 0, 1,
            -0.5f, 0.5f, 0.5f,  0, 0, 1,
            // -Z face
            -0.5f,-0.5f,-0.5f,  0, 0,-1,
             0.5f,-0.5f,-0.5f,  0, 0,-1,
             0.5f, 0.5f,-0.5f,  0, 0,-1,
            -0.5f, 0.5f,-0.5f,  0, 0,-1,
            // +X face
             0.5f,-0.5f,-0.5f,  1, 0, 0,
             0.5f,-0.5f, 0.5f,  1, 0, 0,
             0.5f, 0.5f, 0.5f,  1, 0, 0,
             0.5f, 0.5f,-0.5f,  1, 0, 0,
            // -X face
            -0.5f,-0.5f,-0.5f, -1, 0, 0,
            -0.5f,-0.5f, 0.5f, -1, 0, 0,
            -0.5f, 0.5f, 0.5f, -1, 0, 0,
            -0.5f, 0.5f,-0.5f, -1, 0, 0,
        };
        static const uint32_t idx[] = {
             0, 1, 2,  0, 2, 3,    // +Y
             4, 6, 5,  4, 7, 6,    // -Y
             8, 9,10,  8,10,11,    // +Z
            12,14,13, 12,15,14,    // -Z
            16,17,18, 16,18,19,    // +X
            20,22,21, 20,23,22,    // -X
        };
        glGenVertexArrays(1, &entity_vao_);
        glGenBuffers(1, &entity_vbo_);
        glGenBuffers(1, &entity_ebo_);
        glBindVertexArray(entity_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, entity_vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, entity_ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                              (void*)(3 * sizeof(float)));
        glBindVertexArray(0);
    }

    // ── SSAO の初期化 ──────────────────────────────────────────────────────────
    initSSAO();

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// drawTitleScreen() — タイトル画面を描画し、SPACEが押されたら true を返す
// ─────────────────────────────────────────────────────────────────────────────
bool Renderer::drawTitleScreen(float dt) {
    return title_screen_.render(dt, window_, width_, height_);
}

void Renderer::updateMinimap(World& world, float px, float pz, float yaw_deg, float dt) {
    minimap_.update(world, px, pz, yaw_deg, dt);
}

void Renderer::drawMinimap() {
    minimap_.draw(hud_shader_, width_, height_);
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
    glBufferData(GL_ARRAY_BUFFER, 1024 * 2 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glBindVertexArray(0);

    // ホットバー背景用 VAO (2D のみ, 1スロット分を逐次更新)
    glGenVertexArrays(1, &hotbar_vao_);
    glGenBuffers(1, &hotbar_vbo_);
    glBindVertexArray(hotbar_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, hotbar_vbo_);
    glBufferData(GL_ARRAY_BUFFER, 12 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);

    // ホットバーテクスチャアイコン用 VAO (pos.xy + uv.xy, 9スロット一括)
    hotbar_shader_.load("assets/shaders/hotbar.vert", "assets/shaders/hotbar.frag");
    glGenVertexArrays(1, &hotbar_tex_vao_);
    glGenBuffers(1, &hotbar_tex_vbo_);
    glBindVertexArray(hotbar_tex_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, hotbar_tex_vbo_);
    // 9スロット × 6頂点 × 4floats = 216 floats
    glBufferData(GL_ARRAY_BUFFER, 9 * 6 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void*)(2 * sizeof(float)));
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
// appendLetter() — X / Y / Z のラベル文字を線分で描く
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::appendLetter(float* verts, int& count, char letter,
                            float left, float top, float w, float h) const {
    const float right = left + w;
    const float mid   = top - h * 0.5f;
    const float bot   = top - h;
    switch (letter) {
        case 'P':
            appendLine(verts, count, left,  top, right, top);
            appendLine(verts, count, left,  mid, right, mid);
            appendLine(verts, count, left,  top, left,  bot);
            appendLine(verts, count, right, top, right, mid);
            break;
        case 'A':
            appendLine(verts, count, left,  bot, left,  mid);
            appendLine(verts, count, right, bot, right, mid);
            appendLine(verts, count, left,  mid, (left + right) * 0.5f, top);
            appendLine(verts, count, right, mid, (left + right) * 0.5f, top);
            appendLine(verts, count, left + w * 0.25f, mid, right - w * 0.25f, mid);
            break;
        case 'B':
            appendLine(verts, count, left, top, left, bot);
            appendLine(verts, count, left, top, right, top);
            appendLine(verts, count, left, mid, right, mid);
            appendLine(verts, count, left, bot, right, bot);
            appendLine(verts, count, right, top, right, mid);
            appendLine(verts, count, right, mid, right, bot);
            break;
        case 'C':
            appendLine(verts, count, left, top, right, top);
            appendLine(verts, count, left, top, left, bot);
            appendLine(verts, count, left, bot, right, bot);
            break;
        case 'D':
            appendLine(verts, count, left, top, left, bot);
            appendLine(verts, count, left, top, right - w * 0.15f, top);
            appendLine(verts, count, right - w * 0.15f, top, right, mid);
            appendLine(verts, count, right, mid, right - w * 0.15f, bot);
            appendLine(verts, count, right - w * 0.15f, bot, left, bot);
            break;
        case 'E':
            appendLine(verts, count, left, top, left, bot);
            appendLine(verts, count, left, top, right, top);
            appendLine(verts, count, left, mid, right * 0.92f + left * 0.08f, mid);
            appendLine(verts, count, left, bot, right, bot);
            break;
        case 'F':
            appendLine(verts, count, left, top, left, bot);
            appendLine(verts, count, left, top, right, top);
            appendLine(verts, count, left, mid, right * 0.92f + left * 0.08f, mid);
            break;
        case 'H':
            appendLine(verts, count, left, top, left, bot);
            appendLine(verts, count, right, top, right, bot);
            appendLine(verts, count, left, mid, right, mid);
            break;
        case 'I':
            appendLine(verts, count, left, top, right, top);
            appendLine(verts, count, (left + right) * 0.5f, top, (left + right) * 0.5f, bot);
            appendLine(verts, count, left, bot, right, bot);
            break;
        case 'K':
            appendLine(verts, count, left, top, left, bot);
            appendLine(verts, count, left, mid, right, top);
            appendLine(verts, count, left, mid, right, bot);
            break;
        case 'L':
            appendLine(verts, count, left, top, left, bot);
            appendLine(verts, count, left, bot, right, bot);
            break;
        case 'G':
            appendLine(verts, count, left, top, right, top);
            appendLine(verts, count, left, top, left, bot);
            appendLine(verts, count, left, bot, right, bot);
            appendLine(verts, count, right, bot, right, mid);
            appendLine(verts, count, right, mid, left + w * 0.55f, mid);
            break;
        case 'M':
            appendLine(verts, count, left, top, left, bot);
            appendLine(verts, count, right, top, right, bot);
            appendLine(verts, count, left, top, (left + right) * 0.5f, mid);
            appendLine(verts, count, (left + right) * 0.5f, mid, right, top);
            break;
        case 'N':
            appendLine(verts, count, left, top, left, bot);
            appendLine(verts, count, right, top, right, bot);
            appendLine(verts, count, left, top, right, bot);
            break;
        case 'R':
            appendLine(verts, count, left, top, left, bot);
            appendLine(verts, count, left, top, right, top);
            appendLine(verts, count, left, mid, right, mid);
            appendLine(verts, count, right, top, right, mid);
            appendLine(verts, count, left + w * 0.4f, mid, right, bot);
            break;
        case 'S':
            appendLine(verts, count, left, top, right, top);
            appendLine(verts, count, left, top, left, mid);
            appendLine(verts, count, left, mid, right, mid);
            appendLine(verts, count, right, mid, right, bot);
            appendLine(verts, count, left, bot, right, bot);
            break;
        case 'T':
            appendLine(verts, count, left, top, right, top);
            appendLine(verts, count, (left + right) * 0.5f, top, (left + right) * 0.5f, bot);
            break;
        case 'O':
            appendLine(verts, count, left, top, right, top);
            appendLine(verts, count, right, top, right, bot);
            appendLine(verts, count, left, bot, right, bot);
            appendLine(verts, count, left, top, left, bot);
            break;
        case 'U':
            appendLine(verts, count, left, top, left, bot);
            appendLine(verts, count, right, top, right, bot);
            appendLine(verts, count, left, bot, right, bot);
            break;
        case 'W':
            appendLine(verts, count, left, top, left + w * 0.18f, bot);
            appendLine(verts, count, left + w * 0.18f, bot, (left + right) * 0.5f, mid);
            appendLine(verts, count, (left + right) * 0.5f, mid, right - w * 0.18f, bot);
            appendLine(verts, count, right - w * 0.18f, bot, right, top);
            break;
        case 'X':
            appendLine(verts, count, left, top,   right, bot);   // 左上→右下
            appendLine(verts, count, right, top,  left,  bot);   // 右上→左下
            break;
        case 'Y':
            appendLine(verts, count, left,  top,  (left+right)*0.5f, mid); // 左上→中央
            appendLine(verts, count, right, top,  (left+right)*0.5f, mid); // 右上→中央
            appendLine(verts, count, (left+right)*0.5f, mid, (left+right)*0.5f, bot); // 中央→下
            break;
        case 'Z':
            appendLine(verts, count, left,  top,  right, top);   // 上横
            appendLine(verts, count, right, top,  left,  bot);   // 斜め
            appendLine(verts, count, left,  bot,  right, bot);   // 下横
            break;
        default:
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// appendSignedNumberLeft() — 符号付き整数を左揃えで描く
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::appendSignedNumberLeft(float* verts, int& count, int value,
                                      float left, float top, float w, float h,
                                      float gap) const {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", value < 0 ? -value : value);
    int len = 0;
    while (buf[len] != '\0') ++len;

    float x = left;
    if (value < 0) {
        // マイナス符号（中横線）
        float mid = top - h * 0.5f;
        appendLine(verts, count, x, mid, x + w, mid);
        x += w + gap;
    }
    for (int i = 0; i < len; ++i) {
        appendDigit(verts, count, buf[i] - '0', x, top, w, h);
        x += w + gap;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// appendHeart() — HUD 用の簡易ハート形状をラインで描く
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::appendHeart(float* verts, int& count,
                           float left, float top, float w, float h) const {
    const float x0 = left;
    const float x1 = left + w * 0.22f;
    const float x2 = left + w * 0.50f;
    const float x3 = left + w * 0.78f;
    const float x4 = left + w;

    const float y0 = top - h * 0.34f;
    const float y1 = top;
    const float y2 = top - h * 0.22f;
    const float y4 = top - h;

    appendLine(verts, count, x2, y4, x0, y2);
    appendLine(verts, count, x0, y2, x1, y1);
    appendLine(verts, count, x1, y1, x2, y0);
    appendLine(verts, count, x2, y0, x3, y1);
    appendLine(verts, count, x3, y1, x4, y2);
    appendLine(verts, count, x4, y2, x2, y4);
    appendLine(verts, count, x1, y2, x3, y2);
}

// ─────────────────────────────────────────────────────────────────────────────
// drawHud() — 照準・FPS・座標を画面に描く
//
// 毎フレーム呼ばれ、照準・FPS・座標・HPをラインとして描画する。
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawHud(int fps, int px, int py, int pz,
                       float health, float max_health) {
    std::array<float, 2048> verts{};
    int count = 0;

    // NDC 変換係数（ピクセル値 → NDC）
    const float hw = static_cast<float>(width_)  * 0.5f;
    const float hh = static_cast<float>(height_) * 0.5f;

    // 照準の十字線（画面中央の小さな十字）
    float cx = 20.0f / hw;
    float cy = 20.0f / hh;
    appendLine(verts.data(), count, -cx, 0.f, cx, 0.f);
    appendLine(verts.data(), count, 0.f, -cy, 0.f, cy);

    // FPS 数字（右上に表示）
    float fps_pad_x  = 14.0f / hw;
    float fps_pad_y  = 18.0f / hh;
    float digit_w    = 14.0f / hw;
    float digit_h    = 24.0f / hh;
    float gap        =  6.0f / hw;
    appendNumber(verts.data(), count, fps,
                 1.0f - fps_pad_x, 1.0f - fps_pad_y,
                 digit_w, digit_h, gap);

    // 座標表示（左上: X val | Y val | Z val を1行でコンパクトに表示）
    float coord_x  = -1.0f + 10.0f / hw;   // 左端マージン
    float coord_y  =  1.0f - 16.0f / hh;   // 上端マージン
    float cw       =  9.0f / hw;            // 文字・数字幅
    float ch       = 14.0f / hh;            // 文字・数字高さ
    float clgap    =  5.0f / hw;            // ラベル→数値の隙間
    float cngap    =  3.0f / hw;            // 数字桁間隔
    float csep     =  9.0f / hw;            // グループ区切りスペース

    struct { char label; int val; } coords[3] = {
        { 'X', px }, { 'Y', py }, { 'Z', pz }
    };
    for (int i = 0; i < 3; ++i) {
        appendLetter(verts.data(), count, coords[i].label, coord_x, coord_y, cw, ch);
        coord_x += cw + clgap;

        char nbuf[16];
        int v = coords[i].val;
        std::snprintf(nbuf, sizeof(nbuf), "%d", v < 0 ? -v : v);
        int nlen = 0;
        while (nbuf[nlen]) ++nlen;
        int nchars = nlen + (v < 0 ? 1 : 0);

        appendSignedNumberLeft(verts.data(), count, v, coord_x, coord_y, cw, ch, cngap);
        coord_x += static_cast<float>(nchars) * (cw + cngap);

        if (i < 2) {
            coord_x += csep;
            // 縦線セパレータ
            appendLine(verts.data(), count, coord_x, coord_y + ch * 0.15f, coord_x, coord_y - ch * 1.15f);
            coord_x += csep;
        }
    }

    // GPU に線分データを転送して描画
    glBindBuffer(GL_ARRAY_BUFFER, hud_vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(float), verts.data());

    glDisable(GL_DEPTH_TEST);  // HUD は常に最前面に表示
    hud_shader_.use();
    hud_shader_.setVec4("uColor", 1.0f, 1.0f, 1.0f, 0.9f);  // 白色
    glBindVertexArray(hud_vao_);
    glDrawArrays(GL_LINES, 0, count / 2);

    // HP ハート表示（Minecraft 風にホットバー直上へ配置）
    const float safe_max_health = std::max(max_health, 1.0f);
    const float hp = std::clamp(health, 0.0f, safe_max_health);
    const int max_hearts = std::max(1, std::min(10,
        static_cast<int>(std::ceil(safe_max_health * 0.5f))));
    const int filled_hearts = std::min(max_hearts,
        static_cast<int>(std::ceil(hp * 0.5f)));

    std::array<float, 512> heart_verts{};
    int heart_count = 0;
    constexpr float SLOT_PX = 52.0f;
    constexpr float GAP_PX = 5.0f;
    constexpr float BOTTOM_PX = 14.0f;
    constexpr float HOTBAR_SLOTS = 9.0f;
    const float hotbar_w_px = HOTBAR_SLOTS * SLOT_PX + (HOTBAR_SLOTS - 1.0f) * GAP_PX;
    const float hotbar_left_px = (static_cast<float>(width_) - hotbar_w_px) * 0.5f;
    const float heart_w = 15.0f / hw;
    const float heart_h = 14.0f / hh;
    const float heart_gap = 3.0f / hw;
    const float heart_left = hotbar_left_px / hw - 1.0f;
    const float heart_top = -1.0f + (BOTTOM_PX + SLOT_PX + 28.0f) / hh;

    for (int i = 0; i < max_hearts; ++i) {
        appendHeart(heart_verts.data(), heart_count,
                    heart_left + static_cast<float>(i) * (heart_w + heart_gap),
                    heart_top, heart_w, heart_h);
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, heart_count * sizeof(float), heart_verts.data());
    hud_shader_.setVec4("uColor", 0.0f, 0.0f, 0.0f, 0.82f);
    glDrawArrays(GL_LINES, 0, heart_count / 2);

    heart_count = 0;
    for (int i = 0; i < filled_hearts; ++i) {
        appendHeart(heart_verts.data(), heart_count,
                    heart_left + static_cast<float>(i) * (heart_w + heart_gap),
                    heart_top, heart_w, heart_h);
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, heart_count * sizeof(float), heart_verts.data());
    hud_shader_.setVec4("uColor", 0.95f, 0.08f, 0.10f, 0.95f);
    glDrawArrays(GL_LINES, 0, heart_count / 2);

    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

// ─────────────────────────────────────────────────────────────────────────────
// drawStats() — FPS / triangles / cubes / chunks を表示する
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawStats(int fps, int triangles, int cubes,
                         int visible_chunks, int loaded_chunks,
                         bool minimap_visible, const char* biome_name) {
    const float hw = static_cast<float>(width_)  * 0.5f;
    const float hh = static_cast<float>(height_) * 0.5f;

    const float x0 = -1.0f + 12.0f / hw;
    const float top_px = minimap_visible ? 320.0f : 44.0f;
    const float y1 =  1.0f - top_px / hh;
    const float x1 = x0 + 260.0f / hw;
    const float y0 = y1 - 156.0f / hh;

    auto drawQuad = [&](float qx0, float qy0, float qx1, float qy1,
                        float r, float g, float b, float a) {
        float q[12] = {
            qx0, qy0, qx1, qy0, qx1, qy1,
            qx0, qy0, qx1, qy1, qx0, qy1
        };
        glBindBuffer(GL_ARRAY_BUFFER, hotbar_vbo_);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(q), q);
        hud_shader_.setVec4("uColor", r, g, b, a);
        glBindVertexArray(hotbar_vao_);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    };

    glDisable(GL_DEPTH_TEST);
    hud_shader_.use();
    drawQuad(x0, y0, x1, y1, 0.02f, 0.03f, 0.04f, 0.62f);
    drawQuad(x0, y1 - 4.0f / hh, x1, y1, 0.95f, 0.72f, 0.25f, 0.90f);

    std::array<float, 2048> verts{};
    int count = 0;
    const float lw = 9.0f / hw;
    const float lh = 14.0f / hh;
    const float gap = 4.0f / hw;

    auto appendWord = [&](const char* word, float x, float y) {
        for (int i = 0; word[i] != '\0'; ++i) {
            appendLetter(verts.data(), count, word[i], x, y, lw, lh);
            x += lw + gap;
        }
    };
    auto appendRow = [&](const char* label, int value, int row) {
        float y = y1 - (24.0f + 24.0f * static_cast<float>(row)) / hh;
        appendWord(label, x0 + 14.0f / hw, y);
        appendNumber(verts.data(), count, value,
                     x1 - 16.0f / hw, y, lw, lh, gap);
    };
    auto appendTextRow = [&](const char* label, const char* value, int row) {
        float y = y1 - (24.0f + 24.0f * static_cast<float>(row)) / hh;
        appendWord(label, x0 + 14.0f / hw, y);
        appendWord(value ? value : "UNKNOWN", x0 + 88.0f / hw, y);
    };

    appendRow("FPS", fps, 0);
    appendRow("TRI", triangles, 1);
    appendRow("CUB", cubes, 2);
    appendRow("CHK", visible_chunks, 3);
    appendRow("LOAD", loaded_chunks, 4);
    appendTextRow("BIOME", biome_name, 5);

    glBindBuffer(GL_ARRAY_BUFFER, hud_vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(float), verts.data());
    hud_shader_.setVec4("uColor", 0.96f, 0.92f, 0.78f, 0.94f);
    glBindVertexArray(hud_vao_);
    glDrawArrays(GL_LINES, 0, count / 2);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

// ─────────────────────────────────────────────────────────────────────────────
// drawPlayerList() — 接続中プレイヤーID一覧を表示する
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawPlayerList(uint8_t local_id,
                              const std::map<uint8_t, RemotePlayer>& players,
                              bool multiplayer) {
    const float hw = static_cast<float>(width_)  * 0.5f;
    const float hh = static_cast<float>(height_) * 0.5f;

    std::vector<uint8_t> ids;
    ids.push_back(local_id == 0 ? 1 : local_id);
    if (multiplayer) {
        for (const auto& [id, _] : players) {
            if (id != ids.front()) ids.push_back(id);
            if (ids.size() >= 8) break;
        }
    }

    const float panel_w = 190.0f / hw;
    const float panel_h = (72.0f + 28.0f * static_cast<float>(ids.size())) / hh;
    const float x0 = 1.0f - panel_w - 24.0f / hw;
    const float y1 = 1.0f - 44.0f / hh;
    const float x1 = x0 + panel_w;
    const float y0 = y1 - panel_h;

    auto drawQuad = [&](float qx0, float qy0, float qx1, float qy1,
                        float r, float g, float b, float a) {
        float q[12] = {
            qx0, qy0, qx1, qy0, qx1, qy1,
            qx0, qy0, qx1, qy1, qx0, qy1
        };
        glBindBuffer(GL_ARRAY_BUFFER, hotbar_vbo_);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(q), q);
        hud_shader_.setVec4("uColor", r, g, b, a);
        glBindVertexArray(hotbar_vao_);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    };

    glDisable(GL_DEPTH_TEST);
    hud_shader_.use();

    // Panel base and accent strip.
    drawQuad(x0, y0, x1, y1, 0.02f, 0.03f, 0.04f, 0.62f);
    drawQuad(x0, y1 - 4.0f / hh, x1, y1, 0.18f, 0.78f, 0.95f, 0.85f);

    // Row backgrounds and status chips.
    for (size_t i = 0; i < ids.size(); ++i) {
        float top = y1 - (46.0f + 28.0f * static_cast<float>(i)) / hh;
        float bot = top - 22.0f / hh;
        float tint = (i == 0) ? 0.15f : 0.08f;
        drawQuad(x0 + 10.0f / hw, bot, x1 - 10.0f / hw, top,
                 tint, tint + 0.02f, tint + 0.03f, 0.55f);
        drawQuad(x0 + 16.0f / hw, bot + 5.0f / hh,
                 x0 + 24.0f / hw, top - 5.0f / hh,
                 i == 0 ? 0.35f : 0.20f,
                 i == 0 ? 0.95f : 0.75f,
                 i == 0 ? 0.70f : 0.95f,
                 0.95f);
    }
    glBindVertexArray(0);

    std::array<float, 2048> verts{};
    int count = 0;

    auto appendWord = [&](const char* word, float x, float y, float w, float h, float gap) {
        for (int i = 0; word[i] != '\0'; ++i) {
            appendLetter(verts.data(), count, word[i], x, y, w, h);
            x += w + gap;
        }
    };

    const float lw = 9.0f / hw;
    const float lh = 14.0f / hh;
    const float gap = 4.0f / hw;

    appendWord("PLAYERS", x0 + 14.0f / hw, y1 - 16.0f / hh, lw, lh, gap);
    appendNumber(verts.data(), count, static_cast<int>(ids.size()),
                 x1 - 18.0f / hw, y1 - 16.0f / hh, lw, lh, gap);

    for (size_t i = 0; i < ids.size(); ++i) {
        float y = y1 - (52.0f + 28.0f * static_cast<float>(i)) / hh;
        float x = x0 + 34.0f / hw;
        appendLetter(verts.data(), count, 'P', x, y, lw, lh);
        x += lw + gap;
        appendNumber(verts.data(), count, ids[i], x + 20.0f / hw, y, lw, lh, gap);
        if (i == 0) appendWord("YOU", x1 - 50.0f / hw, y, lw, lh, gap);
    }

    glBindBuffer(GL_ARRAY_BUFFER, hud_vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(float), verts.data());

    hud_shader_.setVec4("uColor", 0.92f, 0.98f, 1.0f, 0.92f);
    glBindVertexArray(hud_vao_);
    glDrawArrays(GL_LINES, 0, count / 2);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

// ─────────────────────────────────────────────────────────────────────────────
// drawDeathScreen() — 死亡時の暗転オーバーレイと復活ボタンを描く
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawDeathScreen() {
    const float hw = static_cast<float>(width_)  * 0.5f;
    const float hh = static_cast<float>(height_) * 0.5f;

    auto drawQuad = [&](float qx0, float qy0, float qx1, float qy1,
                        float r, float g, float b, float a) {
        float q[12] = {
            qx0, qy0, qx1, qy0, qx1, qy1,
            qx0, qy0, qx1, qy1, qx0, qy1
        };
        glBindBuffer(GL_ARRAY_BUFFER, hotbar_vbo_);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(q), q);
        hud_shader_.setVec4("uColor", r, g, b, a);
        glBindVertexArray(hotbar_vao_);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    };

    glDisable(GL_DEPTH_TEST);
    hud_shader_.use();

    drawQuad(-1.0f, -1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.68f);

    const float btn_w = 300.0f / hw;
    const float btn_h = 56.0f / hh;
    const float btn_x0 = -btn_w * 0.5f;
    const float btn_x1 =  btn_w * 0.5f;
    const float btn_y1 = -0.10f;
    const float btn_y0 = btn_y1 - btn_h;
    drawQuad(btn_x0, btn_y0, btn_x1, btn_y1, 0.13f, 0.14f, 0.16f, 0.92f);
    drawQuad(btn_x0, btn_y1 - 4.0f / hh, btn_x1, btn_y1,
             0.75f, 0.10f, 0.10f, 0.95f);

    std::array<float, 2048> verts{};
    int count = 0;

    auto appendWordCentered = [&](const char* word, float center_x, float top,
                                  float w, float h, float gap) {
        int visible = 0;
        for (int i = 0; word[i] != '\0'; ++i)
            if (word[i] != ' ') ++visible;
        int spaces = 0;
        for (int i = 0; word[i] != '\0'; ++i)
            if (word[i] == ' ') ++spaces;
        float total = static_cast<float>(visible) * w
            + static_cast<float>(visible + spaces - 1) * gap
            + static_cast<float>(spaces) * w * 0.55f;
        float x = center_x - total * 0.5f;
        for (int i = 0; word[i] != '\0'; ++i) {
            if (word[i] == ' ') {
                x += w * 0.55f + gap;
                continue;
            }
            appendLetter(verts.data(), count, word[i], x, top, w, h);
            x += w + gap;
        }
    };

    appendWordCentered("YOU DIED", 0.0f, 0.42f,
                       34.0f / hw, 54.0f / hh, 12.0f / hw);
    appendWordCentered("RESPAWN", 0.0f, btn_y0 + btn_h * 0.66f,
                       16.0f / hw, 24.0f / hh, 7.0f / hw);

    glBindBuffer(GL_ARRAY_BUFFER, hud_vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(float), verts.data());
    hud_shader_.setVec4("uColor", 0.95f, 0.08f, 0.10f, 0.96f);
    glBindVertexArray(hud_vao_);
    glDrawArrays(GL_LINES, 0, count / 2);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

// ─────────────────────────────────────────────────────────────────────────────
// drawHotbar() — 画面下中央にホットバー（9スロット）を描画する
//
// パス1: hud_shader_    — スロット背景
// パス2: hotbar_shader_ — 3Dアイソメトリックブロック + 平面アイテム
// パス3: hud_shader_    — 枠線 + 選択ハイライト + 個数（7セグ）
//
// 【アイソメトリックキューブの頂点レイアウト】
//
//          T
//         / \
//       TL   TR       ← "赤道"ライン（上面と側面の境界）
//       |\ / |
//       | M  |        ← 3面の内側交点
//       BL   BR       ← 底の赤道ライン
//         \ /
//          B
//
//   上面: T, TR, M, TL  — 最も明るい（uBright=1.00）
//   左面: TL, M, B, BL  — 中くらい    （uBright=0.74）
//   右面: TR, BR, B, M  — 最も暗い    （uBright=0.58）
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawHotbar(const Inventory& inv) {
    const float hw = (float)width_  * 0.5f;
    const float hh = (float)height_ * 0.5f;

    constexpr float SLOT_PX   = 52.0f;
    constexpr float GAP_PX    =  5.0f;
    constexpr float BOTTOM_PX = 14.0f;

    const float total_w = HOTBAR_SIZE * SLOT_PX + (HOTBAR_SIZE - 1) * GAP_PX;
    const float sx0_px  = ((float)width_ - total_w) * 0.5f;

    auto nx = [&](float px) { return px / hw - 1.0f; };
    auto ny = [&](float py) { return py / hh - 1.0f; };

    const float sy0 = ny(BOTTOM_PX);
    const float sy1 = ny(BOTTOM_PX + SLOT_PX);

    // アイソメトリックキューブの寸法（ピクセル→NDC）
    // 2:1 アイソメトリック比: 上面の高さ = 水平幅 / 2
    const float W  = 21.0f / hw;    // 半幅
    const float HT = 10.5f / hh;    // 上面の半高さ（2:1比 → HT = W*hw/hh/2）
    const float HS = 12.0f / hh;    // 側面の高さ

    glDisable(GL_DEPTH_TEST);

    // ── パス1: スロット背景 ────────────────────────────────────────────────
    hud_shader_.use();
    auto drawBgQuad = [&](float x0, float y0, float x1, float y1) {
        float v[12] = { x0, y0,  x1, y0,  x1, y1,
                        x0, y0,  x1, y1,  x0, y1 };
        glBindBuffer(GL_ARRAY_BUFFER, hotbar_vbo_);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(v), v);
        glBindVertexArray(hotbar_vao_);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    };
    for (int i = 0; i < HOTBAR_SIZE; ++i) {
        float x0 = nx(sx0_px + i * (SLOT_PX + GAP_PX));
        float x1 = nx(sx0_px + i * (SLOT_PX + GAP_PX) + SLOT_PX);
        float bg = (i == inv.selected) ? 0.55f : 0.22f;
        hud_shader_.setVec4("uColor", bg * 0.6f, bg * 0.6f, bg * 0.6f, 0.85f);
        drawBgQuad(x0, sy0, x1, sy1);
    }

    // ── パス2: 3Dアイソメトリックブロック ────────────────────────────────
    // face=0: 上面(bright=1.00)  face=1: 左面(bright=0.74)  face=2: 右面(bright=0.58)
    static constexpr float kFaceBright[3] = { 1.00f, 0.74f, 0.58f };

    glDisable(GL_CULL_FACE);
    hotbar_shader_.use();
    hotbar_shader_.setInt("uAtlas", 0);
    atlas_.bind(0);
    glBindVertexArray(hotbar_tex_vao_);

    for (int face = 0; face < 3; ++face) {
        hotbar_shader_.setFloat("uBright", kFaceBright[face]);

        float verts[9 * 6 * 4] = {};
        int   vc = 0, drawn = 0;

        for (int i = 0; i < HOTBAR_SIZE; ++i) {
            const ItemStack& s = inv.slots[i];
            if (s.type == BlockType::Air || s.count <= 0) continue;
            if (s.type == BlockType::Bow) continue;

            AtlasUV uv = atlas_.getUV(s.type);
            float uc = (uv.u0 + uv.u1) * 0.5f;
            float vc2 = (uv.v0 + uv.v1) * 0.5f;

            // スロット垂直中心（アイコン基準点）
            float cx = nx(sx0_px + i * (SLOT_PX + GAP_PX) + SLOT_PX * 0.5f);
            // キューブ中心を少し上寄りにオフセット（底の隙間を減らす）
            float cy = ny(BOTTOM_PX + SLOT_PX * 0.5f) + HS * 0.1f;

            // 7頂点
            float T_x = cx,    T_y = cy + HT + HS;
            float TL_x = cx-W, TL_y = cy + HS;
            float TR_x = cx+W, TR_y = cy + HS;
            float M_x  = cx,   M_y  = cy;
            float BL_x = cx-W, BL_y = cy - HS;
            float BR_x = cx+W, BR_y = cy - HS;
            float B_x  = cx,   B_y  = cy - HT - HS;

            // 各面を2三角形で定義: [x, y, u, v] × 6頂点
            float tri[6][4];

            if (face == 0) {
                // 上面(ダイヤモンドUV: テクスチャを45°回転して貼る)
                tri[0][0]=T_x;  tri[0][1]=T_y;  tri[0][2]=uc;     tri[0][3]=uv.v0;
                tri[1][0]=TR_x; tri[1][1]=TR_y; tri[1][2]=uv.u1;  tri[1][3]=vc2;
                tri[2][0]=M_x;  tri[2][1]=M_y;  tri[2][2]=uc;     tri[2][3]=uv.v1;
                tri[3][0]=T_x;  tri[3][1]=T_y;  tri[3][2]=uc;     tri[3][3]=uv.v0;
                tri[4][0]=M_x;  tri[4][1]=M_y;  tri[4][2]=uc;     tri[4][3]=uv.v1;
                tri[5][0]=TL_x; tri[5][1]=TL_y; tri[5][2]=uv.u0;  tri[5][3]=vc2;
            } else if (face == 1) {
                // 左面
                tri[0][0]=TL_x; tri[0][1]=TL_y; tri[0][2]=uv.u0;  tri[0][3]=uv.v0;
                tri[1][0]=M_x;  tri[1][1]=M_y;  tri[1][2]=uv.u1;  tri[1][3]=uv.v0;
                tri[2][0]=B_x;  tri[2][1]=B_y;  tri[2][2]=uv.u1;  tri[2][3]=uv.v1;
                tri[3][0]=TL_x; tri[3][1]=TL_y; tri[3][2]=uv.u0;  tri[3][3]=uv.v0;
                tri[4][0]=B_x;  tri[4][1]=B_y;  tri[4][2]=uv.u1;  tri[4][3]=uv.v1;
                tri[5][0]=BL_x; tri[5][1]=BL_y; tri[5][2]=uv.u0;  tri[5][3]=uv.v1;
            } else {
                // 右面
                tri[0][0]=TR_x; tri[0][1]=TR_y; tri[0][2]=uv.u0;  tri[0][3]=uv.v0;
                tri[1][0]=BR_x; tri[1][1]=BR_y; tri[1][2]=uv.u0;  tri[1][3]=uv.v1;
                tri[2][0]=B_x;  tri[2][1]=B_y;  tri[2][2]=uv.u1;  tri[2][3]=uv.v1;
                tri[3][0]=TR_x; tri[3][1]=TR_y; tri[3][2]=uv.u0;  tri[3][3]=uv.v0;
                tri[4][0]=B_x;  tri[4][1]=B_y;  tri[4][2]=uv.u1;  tri[4][3]=uv.v1;
                tri[5][0]=M_x;  tri[5][1]=M_y;  tri[5][2]=uv.u1;  tri[5][3]=uv.v0;
            }

            for (int v = 0; v < 6; ++v)
                for (int c = 0; c < 4; ++c)
                    verts[vc++] = tri[v][c];
            ++drawn;
        }

        if (drawn > 0) {
            glBindBuffer(GL_ARRAY_BUFFER, hotbar_tex_vbo_);
            glBufferSubData(GL_ARRAY_BUFFER, 0, vc * sizeof(float), verts);
            glDrawArrays(GL_TRIANGLES, 0, drawn * 6);
        }
    }

    // 弓はブロックではないので、Bow.png の平面アイコンとして表示する。
    {
        hotbar_shader_.setFloat("uBright", 1.0f);
        float verts[HOTBAR_SIZE * 6 * 4] = {};
        int vc = 0, drawn = 0;

        for (int i = 0; i < HOTBAR_SIZE; ++i) {
            const ItemStack& s = inv.slots[i];
            if (s.type != BlockType::Bow || s.count <= 0) continue;

            AtlasUV uv = atlas_.getUV(BlockType::Bow);
            const float cx = nx(sx0_px + i * (SLOT_PX + GAP_PX) + SLOT_PX * 0.5f);
            const float cy = ny(BOTTOM_PX + SLOT_PX * 0.5f);
            const float iw = 34.0f / hw;
            const float ih = 34.0f / hh;
            const float x0 = cx - iw * 0.5f;
            const float x1 = cx + iw * 0.5f;
            const float y0 = cy - ih * 0.5f;
            const float y1 = cy + ih * 0.5f;

            const float tri[6][4] = {
                {x0, y0, uv.u0, uv.v1},
                {x1, y0, uv.u1, uv.v1},
                {x1, y1, uv.u1, uv.v0},
                {x0, y0, uv.u0, uv.v1},
                {x1, y1, uv.u1, uv.v0},
                {x0, y1, uv.u0, uv.v0},
            };
            for (int v = 0; v < 6; ++v)
                for (int c = 0; c < 4; ++c)
                    verts[vc++] = tri[v][c];
            ++drawn;
        }

        if (drawn > 0) {
            glBindBuffer(GL_ARRAY_BUFFER, hotbar_tex_vbo_);
            glBufferSubData(GL_ARRAY_BUFFER, 0, vc * sizeof(float), verts);
            glDrawArrays(GL_TRIANGLES, 0, drawn * 6);
        }
    }

    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);

    // ── パス3: 枠線 + 選択ハイライト + 個数 ──────────────────────────────
    hud_shader_.use();
    std::array<float, 2048> verts{};
    int cnt = 0;

    for (int i = 0; i < HOTBAR_SIZE; ++i) {
        float x0 = nx(sx0_px + i * (SLOT_PX + GAP_PX));
        float x1 = nx(sx0_px + i * (SLOT_PX + GAP_PX) + SLOT_PX);

        // 通常枠線（暗め）→ 後で白を上書きするのでバッチへ追加するだけ
        appendLine(verts.data(), cnt, x0, sy0, x1, sy0);
        appendLine(verts.data(), cnt, x0, sy1, x1, sy1);
        appendLine(verts.data(), cnt, x0, sy0, x0, sy1);
        appendLine(verts.data(), cnt, x1, sy0, x1, sy1);

        // 個数（2個以上のとき右下に表示）
        const ItemStack& s = inv.slots[i];
        if (s.type != BlockType::Air && s.count > 1) {
            float dw = 8.0f / hw, dh = 11.0f / hh, dg = 2.0f / hw;
            appendNumber(verts.data(), cnt, s.count,
                         x1 - 3.0f / hw, sy0 + dh + 3.0f / hh, dw, dh, dg);
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, hud_vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, cnt * sizeof(float), verts.data());
    hud_shader_.setVec4("uColor", 0.75f, 0.75f, 0.75f, 0.85f);
    glBindVertexArray(hud_vao_);
    glDrawArrays(GL_LINES, 0, cnt / 2);

    // 選択スロットだけ明るい白で上書き描画
    {
        int i = inv.selected;
        float x0 = nx(sx0_px + i * (SLOT_PX + GAP_PX));
        float x1 = nx(sx0_px + i * (SLOT_PX + GAP_PX) + SLOT_PX);
        float sel[16] = {
            x0, sy0, x1, sy0,
            x0, sy1, x1, sy1,
            x0, sy0, x0, sy1,
            x1, sy0, x1, sy1,
        };
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(sel), sel);
        hud_shader_.setVec4("uColor", 1.0f, 1.0f, 1.0f, 1.0f);
        glDrawArrays(GL_LINES, 0, 8);
    }
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

static void setFogUniforms(Shader& shader, const float sky_horizon[3],
                           bool underwater) {
    if (underwater) {
        shader.setVec3 ("uFogColor",  0.03f, 0.24f, 0.34f);
        shader.setFloat("uFogStart",  7.0f);
        shader.setFloat("uFogEnd",    38.0f);
        return;
    }
    shader.setVec3 ("uFogColor",  sky_horizon[0], sky_horizon[1], sky_horizon[2]);
    shader.setFloat("uFogStart",  170.0f);
    shader.setFloat("uFogEnd",    310.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// drawChunk() — 不透明チャンクを描画する（パス1）
//
// 不透明ブロック（土・石・草など）だけのインデックスを使って描画する。
// 水の半透明描画は drawChunkWater() で別途行う（順番が大事）。
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawChunk(const Chunk* chunk, const float* view4x4, const float* proj4x4) {
    if (!chunk->gpu.uploaded || chunk->gpu.idx_count == 0) return;

    glm::mat4 model = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(
            static_cast<float>(chunk->pos.x) * static_cast<float>(CHUNK_SIZE_X),
            0.0f,
            static_cast<float>(chunk->pos.z) * static_cast<float>(CHUNK_SIZE_Z)));
    glm::mat4 mvp = glm::make_mat4(proj4x4) * glm::make_mat4(view4x4) * model;

    chunk_shader_.use();
    chunk_shader_.setMat4("uMVP",          glm::value_ptr(mvp));
    chunk_shader_.setMat4("uModel",        glm::value_ptr(model));
    chunk_shader_.setMat4("uView",         view4x4);
    chunk_shader_.setMat4("uLightSpaceMat", light_space_mat_);
    setChunkLightingUniforms(chunk_shader_, sun_dir_, ambient_, sun_strength_);
    setFogUniforms(chunk_shader_, sky_horizon_, underwater_);
    chunk_shader_.setFloat("uSunStrength", sun_strength_);

    atlas_.bind(0);
    chunk_shader_.setInt("uAtlas", 0);

    // シャドウマップをテクスチャスロット 1 にバインド
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadow_depth_tex_);
    chunk_shader_.setInt("uShadowMap", 1);

    // SSAOマップをテクスチャスロット 2 にバインド
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, ssao_blur_tex_);
    chunk_shader_.setInt("uSSAOMap", 2);
    chunk_shader_.setVec2("uScreenSize", (float)width_, (float)height_);

    glActiveTexture(GL_TEXTURE0);

    glBindVertexArray(chunk->gpu.vao);
    glDrawElements(GL_TRIANGLES,
                   static_cast<GLsizei>(chunk->gpu.idx_count),
                   GL_UNSIGNED_INT,
                   nullptr);
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

    glm::mat4 model = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(
            static_cast<float>(chunk->pos.x) * static_cast<float>(CHUNK_SIZE_X),
            0.0f,
            static_cast<float>(chunk->pos.z) * static_cast<float>(CHUNK_SIZE_Z)));
    glm::mat4 mvp = glm::make_mat4(proj4x4) * glm::make_mat4(view4x4) * model;

    chunk_shader_.use();
    chunk_shader_.setMat4("uMVP",          glm::value_ptr(mvp));
    chunk_shader_.setMat4("uModel",        glm::value_ptr(model));
    chunk_shader_.setMat4("uView",         view4x4);
    chunk_shader_.setMat4("uLightSpaceMat", light_space_mat_);
    setChunkLightingUniforms(chunk_shader_, sun_dir_, ambient_, sun_strength_);
    setFogUniforms(chunk_shader_, sky_horizon_, underwater_);
    chunk_shader_.setFloat("uSunStrength", sun_strength_);

    atlas_.bind(0);
    chunk_shader_.setInt("uAtlas", 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadow_depth_tex_);
    chunk_shader_.setInt("uShadowMap", 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, ssao_blur_tex_);
    chunk_shader_.setInt("uSSAOMap", 2);
    chunk_shader_.setVec2("uScreenSize", (float)width_, (float)height_);

    glActiveTexture(GL_TEXTURE0);

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
// drawClouds() — 3D 雲レイヤーを描画する
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawClouds(const float* view4x4, const float* proj4x4,
                          float cam_x, float cam_z, float elapsed_s) {
    cloud_.draw(view4x4, proj4x4, cam_x, cam_z, elapsed_s, sun_dir_);
}

// ─────────────────────────────────────────────────────────────────────────────
// updateShadowMatrix() — 光源空間行列 (LightProj × LightView) を計算する
//
// 太陽方向 (sun_dir_) とプレイヤー位置からオーソグラフィック射影行列を作る。
// この行列でチャンクを描くと「太陽から見た深度マップ」になる。
//
// 【オーソグラフィック射影】
//   遠近感がない平行投影。太陽は無限遠にあるので、この投影が適切。
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::updateShadowMatrix(float px, float py, float pz) {
    glm::vec3 sunDir  = glm::normalize(glm::vec3(sun_dir_[0], sun_dir_[1], sun_dir_[2]));
    glm::vec3 playerPos = glm::vec3(px, py, pz);

    // 光源位置: 太陽方向にプレイヤーから 350 ブロック離れた点
    glm::vec3 lightPos = playerPos + sunDir * 350.0f;

    // 上ベクトル: 太陽が真上/真下に近いときは別軸を使う
    glm::vec3 up = (std::abs(sunDir.y) > 0.98f)
                   ? glm::vec3(1.0f, 0.0f, 0.0f)
                   : glm::vec3(0.0f, 1.0f, 0.0f);
    glm::mat4 lightView = glm::lookAt(lightPos, playerPos, up);

    // 描画距離 (160ブロック) をカバーする直交ボックス
    constexpr float RANGE = 200.0f;
    glm::mat4 lightProj   = glm::ortho(-RANGE, RANGE, -RANGE, RANGE, 1.0f, 750.0f);

    glm::mat4 lsMat = lightProj * lightView;
    std::memcpy(light_space_mat_, glm::value_ptr(lsMat), 16 * sizeof(float));
}

// ─────────────────────────────────────────────────────────────────────────────
// beginShadowPass() — シャドウパスの開始
//
// シャドウ用 FBO に切り替え、太陽視点で深度のみ描画する準備をする。
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::beginShadowPass() {
    glBindFramebuffer(GL_FRAMEBUFFER, shadow_fbo_);
    glViewport(0, 0, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
    glClear(GL_DEPTH_BUFFER_BIT);

    // ポリゴンオフセット: シャドウマップの深度値を傾きに応じてわずかにずらす。
    // これにより surface 自身が自分の影を描くself-shadowingを防ぐ。
    // ボクセルメッシュでは glCullFace(GL_FRONT) を使うと地形の上面(影の元)が
    // シャドウマップから除外されてしまうため、通常の GL_BACK カリングのままにする。
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// drawChunkShadow() — シャドウパスでのチャンク描画
//
// shadow_shader_ (position のみ) で描画し、深度バッファに書き込む。
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawChunkShadow(const Chunk* chunk) {
    if (!chunk->gpu.uploaded || chunk->gpu.idx_count == 0) return;

    glm::mat4 model = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(
            static_cast<float>(chunk->pos.x) * static_cast<float>(CHUNK_SIZE_X),
            0.0f,
            static_cast<float>(chunk->pos.z) * static_cast<float>(CHUNK_SIZE_Z)));
    glm::mat4 lightMVP = glm::make_mat4(light_space_mat_) * model;

    shadow_shader_.use();
    shadow_shader_.setMat4("uLightMVP", glm::value_ptr(lightMVP));

    glBindVertexArray(chunk->gpu.vao);
    glDrawElements(GL_TRIANGLES,
                   static_cast<GLsizei>(chunk->gpu.idx_count),
                   GL_UNSIGNED_INT,
                   nullptr);
    glBindVertexArray(0);
}

// ─────────────────────────────────────────────────────────────────────────────
// endShadowPass() — シャドウパスの終了
//
// デフォルト FBO に戻し、ビューポートをウィンドウサイズに復元する。
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::endShadowPass() {
    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width_, height_);
}

// ─────────────────────────────────────────────────────────────────────────────
// initSSAO() — SSAOに必要なリソースをすべて生成する
//
// 処理:
//   1. シェーダーロード（gbuffer / ssao / ssao_blur）
//   2. ヘミスフィアサンプルカーネル生成（64点, 原点近くに集中）
//   3. 4x4 ランダム回転ノイズテクスチャ生成
//   4. フルスクリーンクアッド VAO 生成
//   5. FBO を resizeSSAOBuffers で生成
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::initSSAO() {
    if (!gbuffer_shader_.load("assets/shaders/gbuffer.vert",
                               "assets/shaders/gbuffer.frag")) {
        std::cerr << "[Renderer] Failed to load GBuffer shaders\n";
        return;
    }
    if (!ssao_shader_.load("assets/shaders/ssao.vert",
                            "assets/shaders/ssao.frag")) {
        std::cerr << "[Renderer] Failed to load SSAO shaders\n";
        return;
    }
    if (!ssao_blur_shader_.load("assets/shaders/ssao.vert",
                                 "assets/shaders/ssao_blur.frag")) {
        std::cerr << "[Renderer] Failed to load SSAO blur shaders\n";
        return;
    }

    // ── サンプルカーネル生成 ───────────────────────────────────────────────────
    // ビュー空間ヘミスフィア上の64点。原点付近に密集させることで
    // 近接ジオメトリによるオクルージョンを強調する。
    std::default_random_engine gen(42);
    std::uniform_real_distribution<float> rnd(0.0f, 1.0f);
    for (int i = 0; i < SSAO_SAMPLES; ++i) {
        glm::vec3 s(
            rnd(gen) * 2.0f - 1.0f,
            rnd(gen) * 2.0f - 1.0f,
            rnd(gen)           // z ∈ [0,1] → 上半球
        );
        s = glm::normalize(s) * rnd(gen);
        float scale = static_cast<float>(i) / static_cast<float>(SSAO_SAMPLES);
        scale = 0.1f + scale * scale * 0.9f;  // 二次関数で原点付近に集中
        ssao_kernel_[i] = s * scale;
    }

    // ── 4x4 ランダム回転ノイズテクスチャ ─────────────────────────────────────
    // 各ピクセルに異なる TBN 基底を与え、パターンノイズを防ぐ。
    // z=0 の XY 平面内のベクトルのみ（接線空間の回転なので Z 成分不要）。
    glm::vec3 noise_data[16];
    for (int i = 0; i < 16; ++i) {
        noise_data[i] = glm::normalize(glm::vec3(
            rnd(gen) * 2.0f - 1.0f,
            rnd(gen) * 2.0f - 1.0f,
            0.0f
        ));
    }
    glGenTextures(1, &ssao_noise_tex_);
    glBindTexture(GL_TEXTURE_2D, ssao_noise_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 4, 4, 0,
                 GL_RGB, GL_FLOAT, noise_data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);

    // ── フルスクリーンクアッド VAO ────────────────────────────────────────────
    static const float quad[] = {
        -1.0f, -1.0f,   1.0f, -1.0f,   1.0f,  1.0f,
        -1.0f, -1.0f,   1.0f,  1.0f,  -1.0f,  1.0f,
    };
    glGenVertexArrays(1, &ssao_quad_vao_);
    glGenBuffers(1, &ssao_quad_vbo_);
    glBindVertexArray(ssao_quad_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, ssao_quad_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);

    // ── FBO 生成 ─────────────────────────────────────────────────────────────
    resizeSSAOBuffers(width_, height_);
}

// ─────────────────────────────────────────────────────────────────────────────
// resizeSSAOBuffers() — ウィンドウリサイズ時に SSAO 用 FBO をサイズに合わせて再生成する
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::resizeSSAOBuffers(int w, int h) {
    if (!ssao_noise_tex_) return;  // initSSAO がまだ走っていない場合はスキップ

    // ── GBuffer FBO（法線 + 深度） ────────────────────────────────────────────
    if (gbuffer_fbo_) {
        glDeleteFramebuffers(1, &gbuffer_fbo_);
        glDeleteTextures(1, &gbuffer_normal_tex_);
        glDeleteTextures(1, &gbuffer_depth_tex_);
    }
    glGenFramebuffers(1, &gbuffer_fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, gbuffer_fbo_);

    glGenTextures(1, &gbuffer_normal_tex_);
    glBindTexture(GL_TEXTURE_2D, gbuffer_normal_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, gbuffer_normal_tex_, 0);

    glGenTextures(1, &gbuffer_depth_tex_);
    glBindTexture(GL_TEXTURE_2D, gbuffer_depth_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D, gbuffer_depth_tex_, 0);

    GLuint gbuf_attach = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &gbuf_attach);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "[Renderer] GBuffer FBO incomplete\n";

    // ── SSAO FBO（オクルージョン係数, R16F） ──────────────────────────────────
    if (ssao_fbo_) {
        glDeleteFramebuffers(1, &ssao_fbo_);
        glDeleteTextures(1, &ssao_color_tex_);
    }
    glGenFramebuffers(1, &ssao_fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, ssao_fbo_);

    glGenTextures(1, &ssao_color_tex_);
    glBindTexture(GL_TEXTURE_2D, ssao_color_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, ssao_color_tex_, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "[Renderer] SSAO FBO incomplete\n";

    // ── SSAO Blur FBO（ブラー後, RGBA16F） ───────────────────────────────────
    if (ssao_blur_fbo_) {
        glDeleteFramebuffers(1, &ssao_blur_fbo_);
        glDeleteTextures(1, &ssao_blur_tex_);
    }
    glGenFramebuffers(1, &ssao_blur_fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, ssao_blur_fbo_);

    glGenTextures(1, &ssao_blur_tex_);
    glBindTexture(GL_TEXTURE_2D, ssao_blur_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, ssao_blur_tex_, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "[Renderer] SSAO Blur FBO incomplete\n";

    // 初期値を 1.0（オクルージョンなし）でクリアしておく
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// beginGBufferPass() — GBufferパス開始（法線と深度を書き込む）
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::beginGBufferPass() {
    if (!gbuffer_fbo_) return;
    glBindFramebuffer(GL_FRAMEBUFFER, gbuffer_fbo_);
    glViewport(0, 0, width_, height_);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

// ─────────────────────────────────────────────────────────────────────────────
// drawChunkGBuffer() — GBufferパスでチャンクを描画（法線出力のみ）
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawChunkGBuffer(const Chunk* chunk,
                                const float* view4x4, const float* proj4x4) {
    if (!chunk->gpu.uploaded || chunk->gpu.idx_count == 0) return;

    glm::mat4 model = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(
            static_cast<float>(chunk->pos.x) * static_cast<float>(CHUNK_SIZE_X),
            0.0f,
            static_cast<float>(chunk->pos.z) * static_cast<float>(CHUNK_SIZE_Z)));
    glm::mat4 view = glm::make_mat4(view4x4);
    glm::mat4 proj = glm::make_mat4(proj4x4);
    glm::mat4 mvp        = proj * view * model;
    glm::mat4 model_view = view * model;

    gbuffer_shader_.use();
    gbuffer_shader_.setMat4("uMVP",       glm::value_ptr(mvp));
    gbuffer_shader_.setMat4("uModelView", glm::value_ptr(model_view));

    glBindVertexArray(chunk->gpu.vao);
    glDrawElements(GL_TRIANGLES,
                   static_cast<GLsizei>(chunk->gpu.idx_count),
                   GL_UNSIGNED_INT,
                   nullptr);
    glBindVertexArray(0);
}

// ─────────────────────────────────────────────────────────────────────────────
// endGBufferPass() — GBufferパス終了
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::endGBufferPass() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width_, height_);
}

// ─────────────────────────────────────────────────────────────────────────────
// computeSSAO() — SSAOパスとブラーパスを実行して ssao_blur_tex_ に書き込む
//
// 1. SSAO FBO にオクルージョン係数を計算
// 2. Blur FBO で 4x4 ボックスブラーをかけてノイズを除去
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::computeSSAO(const float* proj4x4) {
    if (!ssao_fbo_ || !gbuffer_fbo_) return;

    glm::mat4 proj    = glm::make_mat4(proj4x4);
    glm::mat4 inv_proj = glm::inverse(proj);

    glDisable(GL_DEPTH_TEST);

    // ── SSAOパス ─────────────────────────────────────────────────────────────
    glBindFramebuffer(GL_FRAMEBUFFER, ssao_fbo_);
    glClear(GL_COLOR_BUFFER_BIT);

    ssao_shader_.use();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gbuffer_normal_tex_);
    ssao_shader_.setInt("gNormal", 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gbuffer_depth_tex_);
    ssao_shader_.setInt("gDepth", 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, ssao_noise_tex_);
    ssao_shader_.setInt("uNoiseTex", 2);

    ssao_shader_.setMat4("uProjection",    glm::value_ptr(proj));
    ssao_shader_.setMat4("uInvProjection", glm::value_ptr(inv_proj));
    ssao_shader_.setVec2("uNoiseScale",
                         (float)width_ / 4.0f, (float)height_ / 4.0f);

    for (int i = 0; i < SSAO_SAMPLES; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "uSamples[%d]", i);
        ssao_shader_.setVec3(name,
                             ssao_kernel_[i].x,
                             ssao_kernel_[i].y,
                             ssao_kernel_[i].z);
    }

    glBindVertexArray(ssao_quad_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    // ── Blur パス ─────────────────────────────────────────────────────────────
    glBindFramebuffer(GL_FRAMEBUFFER, ssao_blur_fbo_);
    glClear(GL_COLOR_BUFFER_BIT);

    ssao_blur_shader_.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ssao_color_tex_);
    ssao_blur_shader_.setInt("uSSAOInput", 0);

    glBindVertexArray(ssao_quad_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_DEPTH_TEST);
    glActiveTexture(GL_TEXTURE0);
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
    resizeSSAOBuffers(w, h);
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

// ─────────────────────────────────────────────────────────────────────────────
// drawStevePart() — ユニットボックスに MVP とカラーを渡して1パーツ描画
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawStevePart(const glm::mat4& mvp, const glm::mat4& model,
                               const float* color) {
    entity_shader_.setMat4("uMVP",   glm::value_ptr(mvp));
    entity_shader_.setMat4("uModel", glm::value_ptr(model));
    entity_shader_.setVec3("uColor", color[0], color[1], color[2]);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// drawRemotePlayers() — Steve 風キャラクターで他プレイヤーを描画する
//
// Steve のパーツ（足元 y=0 基準、ユニット: 1ブロック）:
//   Head:  中心 (0, 1.55, 0)、0.5×0.5×0.5
//   Torso: 中心 (0, 0.975, 0)、0.5×0.75×0.25
//   Arms:  肩 (±0.35, 1.30, 0) をピボットに垂れ下がる 0.20×0.65×0.20
//   Legs:  股関節 (±0.185, 0.65, 0) をピボットに垂れ下がる 0.23×0.65×0.23
//
// アニメーション: walk_phase に応じて腕・脚を X 軸回転でスイング。
//   左脚 ＝ 右腕が同位相、右脚 ＝ 左腕が同位相（自然な歩き）。
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawRemotePlayers(const std::map<uint8_t, RemotePlayer>& players,
                                  const float* view4x4, const float* proj4x4) {
    if (players.empty() || !entity_vao_) return;

    // Torso colors per player (head/arms = skin, legs = jeans)
    static const float kTorsoColors[][3] = {
        {0.25f, 0.35f, 0.75f},  // blue
        {0.70f, 0.22f, 0.22f},  // red
        {0.20f, 0.58f, 0.20f},  // green
        {0.75f, 0.50f, 0.08f},  // orange
        {0.55f, 0.20f, 0.72f},  // purple
        {0.12f, 0.55f, 0.60f},  // teal
    };
    static const float kSkin[]  = {0.83f, 0.66f, 0.52f};
    static const float kJeans[] = {0.20f, 0.22f, 0.50f};

    const glm::mat4 view = glm::make_mat4(view4x4);
    const glm::mat4 proj = glm::make_mat4(proj4x4);
    const glm::mat4 vp   = proj * view;

    entity_shader_.use();
    entity_shader_.setVec3("uSunDir",
                            sun_dir_[0], sun_dir_[1], sun_dir_[2]);
    entity_shader_.setFloat("uAmbient",     ambient_);
    entity_shader_.setFloat("uSunStrength", sun_strength_);
    entity_shader_.setMat4("uView", view4x4);
    setFogUniforms(entity_shader_, sky_horizon_, underwater_);

    glBindVertexArray(entity_vao_);
    glDisable(GL_CULL_FACE);

    for (auto& [id, rp] : players) {
        // Camera position → feet position (eye is 1.62 blocks above feet)
        static constexpr float EYE_H = 1.62f;
        const bool dead = (rp.state_flags & 0x04u) != 0 || rp.health <= 0.0f;
        const bool attacking = (rp.state_flags & 0x02u) != 0 ||
                               rp.attack_timer > 0.0f;

        // Global transform: translate to feet, then rotate body to face yaw.
        // yaw=0 → front=(1,0,0) (+X); model default forward is +Z, so yaw-90°.
        glm::mat4 global = glm::translate(glm::mat4(1.0f),
                                           glm::vec3(rp.x, rp.y - EYE_H, rp.z));
        global = glm::rotate(global,
                              glm::radians(rp.yaw - 90.0f),
                              glm::vec3(0.f, 1.f, 0.f));
        if (dead) {
            global = glm::translate(global, glm::vec3(0.f, 0.18f, 0.f));
            global = glm::rotate(global,
                                  glm::radians(90.0f),
                                  glm::vec3(1.f, 0.f, 0.f));
        }

        // Walking animation: ±30° limb swing
        const float swing = dead ? 0.0f : glm::radians(sinf(rp.walk_phase) * 30.0f);
        const float attack_swing = (!dead && attacking) ? glm::radians(-85.0f) : 0.0f;
        const float* tc   = kTorsoColors[id % 6];

        // ── Head (no animation) ─────────────────────────────────────────────
        {
            glm::vec3 sz(0.50f, 0.50f, 0.50f);
            glm::mat4 m = glm::translate(global, glm::vec3(0.f, 1.55f, 0.f));
            m = glm::scale(m, sz);
            drawStevePart(vp * m, m, kSkin);
        }

        // ── Torso (no animation) ────────────────────────────────────────────
        {
            glm::vec3 sz(0.50f, 0.75f, 0.25f);
            glm::mat4 m = glm::translate(global, glm::vec3(0.f, 0.975f, 0.f));
            m = glm::scale(m, sz);
            drawStevePart(vp * m, m, tc);
        }

        // ── Left Arm  (pivot = left shoulder, swings same as right leg) ────
        {
            glm::vec3 sz(0.20f, 0.65f, 0.20f);
            glm::mat4 m = glm::translate(global, glm::vec3(-0.35f, 1.30f, 0.f));
            m = glm::rotate(m, -swing, glm::vec3(1.f, 0.f, 0.f));
            m = glm::translate(m, glm::vec3(0.f, -sz.y * 0.5f, 0.f));
            m = glm::scale(m, sz);
            drawStevePart(vp * m, m, kSkin);
        }

        // ── Right Arm (pivot = right shoulder, swings same as left leg) ────
        {
            glm::vec3 sz(0.20f, 0.65f, 0.20f);
            glm::mat4 m = glm::translate(global, glm::vec3( 0.35f, 1.30f, 0.f));
            m = glm::rotate(m,  swing + attack_swing, glm::vec3(1.f, 0.f, 0.f));
            m = glm::translate(m, glm::vec3(0.f, -sz.y * 0.5f, 0.f));
            m = glm::scale(m, sz);
            drawStevePart(vp * m, m, kSkin);
        }

        // ── Left Leg (pivot = left hip) ─────────────────────────────────────
        {
            glm::vec3 sz(0.23f, 0.65f, 0.23f);
            glm::mat4 m = glm::translate(global, glm::vec3(-0.185f, 0.65f, 0.f));
            m = glm::rotate(m,  swing, glm::vec3(1.f, 0.f, 0.f));
            m = glm::translate(m, glm::vec3(0.f, -sz.y * 0.5f, 0.f));
            m = glm::scale(m, sz);
            drawStevePart(vp * m, m, kJeans);
        }

        // ── Right Leg (pivot = right hip) ───────────────────────────────────
        {
            glm::vec3 sz(0.23f, 0.65f, 0.23f);
            glm::mat4 m = glm::translate(global, glm::vec3( 0.185f, 0.65f, 0.f));
            m = glm::rotate(m, -swing, glm::vec3(1.f, 0.f, 0.f));
            m = glm::translate(m, glm::vec3(0.f, -sz.y * 0.5f, 0.f));
            m = glm::scale(m, sz);
            drawStevePart(vp * m, m, kJeans);
        }
    }

    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);
}

// ─────────────────────────────────────────────────────────────────────────────
// drawMobs() — ゾンビを描画する
//
// ゾンビのポーズ（Steve との違い）:
//   ・胴体を約 20° 前傾させる（hunched）
//   ・腕を前方約 70° 上げる（zombie arms）
//   ・皮膚色: 緑がかった灰色
//   ・胴体: 暗いティール
//   ・脚: 褐色グレー
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawMobs(const std::vector<Zombie>& zombies,
                         const float* view4x4, const float* proj4x4) {
    if (zombies.empty() || !entity_vao_) return;

    static const float kSkin[]  = {0.35f, 0.52f, 0.28f};  // green-grey
    static const float kShirt[] = {0.13f, 0.22f, 0.18f};  // dark teal
    static const float kPants[] = {0.22f, 0.18f, 0.14f};  // mud brown

    const glm::mat4 view = glm::make_mat4(view4x4);
    const glm::mat4 proj = glm::make_mat4(proj4x4);
    const glm::mat4 vp   = proj * view;

    entity_shader_.use();
    entity_shader_.setVec3("uSunDir",
                            sun_dir_[0], sun_dir_[1], sun_dir_[2]);
    entity_shader_.setFloat("uAmbient",     ambient_);
    entity_shader_.setFloat("uSunStrength", sun_strength_);
    entity_shader_.setMat4("uView", view4x4);
    setFogUniforms(entity_shader_, sky_horizon_, underwater_);

    glBindVertexArray(entity_vao_);
    glDisable(GL_CULL_FACE);

    for (const Zombie& z : zombies) {
        // Zombie position is already feet. No EYE_H offset needed.
        // Body faces yaw direction (same convention as camera: yaw-90°).
        glm::mat4 global = glm::translate(glm::mat4(1.0f),
                                           glm::vec3(z.x, z.y, z.z));
        global = glm::rotate(global,
                              glm::radians(z.yaw - 90.0f),
                              glm::vec3(0.f, 1.f, 0.f));

        if (z.type == MobType::Creeper) {
            const float pulse = (z.fuse_timer > 0.0f)
                ? (0.5f + 0.5f * sinf(z.fuse_timer * 36.0f))
                : 0.0f;
            const float flash = glm::clamp(z.fuse_timer / 1.5f, 0.0f, 1.0f) * pulse;
            const float kBaseBody[] = {0.22f, 0.66f, 0.18f};
            const float kBaseDark[] = {0.04f, 0.16f, 0.04f};
            float body[] = {
                kBaseBody[0] + (1.0f - kBaseBody[0]) * flash,
                kBaseBody[1] + (1.0f - kBaseBody[1]) * flash,
                kBaseBody[2] + (1.0f - kBaseBody[2]) * flash,
            };
            float dark[] = {
                kBaseDark[0] + (1.0f - kBaseDark[0]) * flash,
                kBaseDark[1] + (1.0f - kBaseDark[1]) * flash,
                kBaseDark[2] + (1.0f - kBaseDark[2]) * flash,
            };

            const float leg_swing = glm::radians(sinf(z.walk_phase) * 18.0f);

            // Body
            {
                glm::vec3 sz(0.62f, 0.90f, 0.36f);
                glm::mat4 m = glm::translate(global, glm::vec3(0.f, 0.82f, 0.f));
                m = glm::scale(m, sz);
                drawStevePart(vp * m, m, body);
            }

            // Head
            {
                glm::vec3 sz(0.66f, 0.66f, 0.66f);
                glm::mat4 m = glm::translate(global, glm::vec3(0.f, 1.50f, 0.f));
                m = glm::scale(m, sz);
                drawStevePart(vp * m, m, body);
            }

            // Face: eyes and mouth on the +Z side of the head.
            {
                glm::vec3 sz(0.12f, 0.12f, 0.035f);
                glm::mat4 m = glm::translate(global, glm::vec3(-0.17f, 1.58f, 0.345f));
                m = glm::scale(m, sz);
                drawStevePart(vp * m, m, dark);
            }
            {
                glm::vec3 sz(0.12f, 0.12f, 0.035f);
                glm::mat4 m = glm::translate(global, glm::vec3(0.17f, 1.58f, 0.345f));
                m = glm::scale(m, sz);
                drawStevePart(vp * m, m, dark);
            }
            {
                glm::vec3 sz(0.14f, 0.22f, 0.035f);
                glm::mat4 m = glm::translate(global, glm::vec3(0.f, 1.39f, 0.345f));
                m = glm::scale(m, sz);
                drawStevePart(vp * m, m, dark);
            }

            // Four legs in a 2x2 grid with diagonal (trot) gait:
            // FL+BR swing together, FR+BL swing together.
            const glm::vec3 leg_sz(0.24f, 0.48f, 0.24f);
            // (x, z, swing_sign): FL, FR, BL, BR
            const float leg_x[4]    = {-0.19f,  0.19f, -0.19f,  0.19f};
            const float leg_z[4]    = { 0.10f,  0.10f, -0.10f, -0.10f};
            const float leg_sign[4] = {  1.f,   -1.f,   -1.f,    1.f };
            for (int i = 0; i < 4; ++i) {
                glm::mat4 m = glm::translate(global, glm::vec3(leg_x[i], 0.48f, leg_z[i]));
                m = glm::rotate(m, leg_sign[i] * leg_swing, glm::vec3(1.f, 0.f, 0.f));
                m = glm::translate(m, glm::vec3(0.f, -leg_sz.y * 0.5f, 0.f));
                m = glm::scale(m, leg_sz);
                drawStevePart(vp * m, m, body);
            }
            continue;
        }

        // Walk swing for legs; arms are mostly raised, with smaller swing
        const float leg_swing = glm::radians(sinf(z.walk_phase) * 28.0f);
        const float arm_raise = glm::radians(-70.0f);   // zombie arms forward
        const float arm_swing = glm::radians(sinf(z.walk_phase) * 12.0f);

        // ── Torso: hunched ~20° forward ─────────────────────────────────
        {
            glm::vec3 sz(0.50f, 0.70f, 0.25f);
            // Pivot at torso centre for tilt
            glm::mat4 m = glm::translate(global, glm::vec3(0.f, 0.975f, 0.f));
            m = glm::rotate(m, glm::radians(-5.0f), glm::vec3(1.f, 0.f, 0.f));
            m = glm::scale(m, sz);
            drawStevePart(vp * m, m, kShirt);
        }

        // ── Head: sits on top of hunched torso ──────────────────────────
        {
            glm::vec3 sz(0.50f, 0.50f, 0.50f);
            // Offset from torso top, corrected for tilt
            glm::mat4 m = glm::translate(global, glm::vec3(0.f, 1.30f, 0.02f));
            m = glm::scale(m, sz);
            drawStevePart(vp * m, m, kSkin);
        }

        // ── Left Arm: raised forward ~70° ───────────────────────────────
        {
            glm::vec3 sz(0.20f, 0.65f, 0.20f);
            glm::mat4 m = glm::translate(global, glm::vec3(-0.35f, 1.30f, 0.f));
            m = glm::rotate(m, arm_raise + arm_swing, glm::vec3(1.f, 0.f, 0.f));
            m = glm::translate(m, glm::vec3(0.f, -sz.y * 0.5f, 0.f));
            m = glm::scale(m, sz);
            drawStevePart(vp * m, m, kSkin);
        }

        // ── Right Arm: raised forward ~70° (opposite swing) ─────────────
        {
            glm::vec3 sz(0.20f, 0.65f, 0.20f);
            glm::mat4 m = glm::translate(global, glm::vec3( 0.35f, 1.30f, 0.f));
            m = glm::rotate(m, arm_raise - arm_swing, glm::vec3(1.f, 0.f, 0.f));
            m = glm::translate(m, glm::vec3(0.f, -sz.y * 0.5f, 0.f));
            m = glm::scale(m, sz);
            drawStevePart(vp * m, m, kSkin);
        }

        // ── Left Leg ─────────────────────────────────────────────────────
        {
            glm::vec3 sz(0.23f, 0.65f, 0.23f);
            glm::mat4 m = glm::translate(global, glm::vec3(-0.185f, 0.65f, 0.f));
            m = glm::rotate(m,  leg_swing, glm::vec3(1.f, 0.f, 0.f));
            m = glm::translate(m, glm::vec3(0.f, -sz.y * 0.5f, 0.f));
            m = glm::scale(m, sz);
            drawStevePart(vp * m, m, kPants);
        }

        // ── Right Leg ────────────────────────────────────────────────────
        {
            glm::vec3 sz(0.23f, 0.65f, 0.23f);
            glm::mat4 m = glm::translate(global, glm::vec3( 0.185f, 0.65f, 0.f));
            m = glm::rotate(m, -leg_swing, glm::vec3(1.f, 0.f, 0.f));
            m = glm::translate(m, glm::vec3(0.f, -sz.y * 0.5f, 0.f));
            m = glm::scale(m, sz);
            drawStevePart(vp * m, m, kPants);
        }
    }

    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);
}

// ─────────────────────────────────────────────────────────────────────────────
// drawArrows() — 飛行中の矢を細長い直方体で描画する
//
// 速度ベクトル v から yaw / pitch を算出し、矢の長軸が進行方向と一致するよう
// 回転を掛ける。刺さって停止中の矢は最後の速度方向を保持して描画する。
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawArrows(const std::vector<Arrow>& arrows,
                           const float* view4x4, const float* proj4x4) {
    if (arrows.empty() || !entity_vao_) return;

    const glm::mat4 view = glm::make_mat4(view4x4);
    const glm::mat4 proj = glm::make_mat4(proj4x4);
    const glm::mat4 vp   = proj * view;

    entity_shader_.use();
    entity_shader_.setVec3("uSunDir", sun_dir_[0], sun_dir_[1], sun_dir_[2]);
    entity_shader_.setFloat("uAmbient",     ambient_);
    entity_shader_.setFloat("uSunStrength", sun_strength_);
    entity_shader_.setMat4("uView", view4x4);
    setFogUniforms(entity_shader_, sky_horizon_, underwater_);

    glBindVertexArray(entity_vao_);
    glDisable(GL_CULL_FACE);

    // 茶色のシャフトと灰色の矢じりで構成
    static const float kShaft[]    = {0.55f, 0.36f, 0.18f};
    static const float kFletching[] = {0.85f, 0.85f, 0.85f};

    for (const Arrow& a : arrows) {
        if (!a.alive) continue;

        // 進行方向から yaw / pitch を算出（ベクトルが極端に小さい場合は前方扱い）
        float vx = a.vx, vy = a.vy, vz = a.vz;
        float len2 = vx*vx + vy*vy + vz*vz;
        if (len2 < 1e-6f) { vx = 1.0f; vy = 0.0f; vz = 0.0f; }

        const float horiz = std::sqrt(vx*vx + vz*vz);
        const float yaw   = std::atan2(vz, vx);    // ラジアン
        const float pitch = std::atan2(vy, horiz);

        // ピボット = 矢の先端。中心軸を +X 方向にしておき、yaw / pitch で回す。
        glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(a.x, a.y, a.z));
        m = glm::rotate(m, yaw,   glm::vec3(0.f, -1.f, 0.f));
        m = glm::rotate(m, pitch, glm::vec3(0.f, 0.f, 1.f));

        // シャフト本体（長さ 0.6、太さ 0.05）
        {
            glm::mat4 mm = glm::translate(m, glm::vec3(-0.30f, 0.0f, 0.0f));
            mm = glm::scale(mm, glm::vec3(0.6f, 0.05f, 0.05f));
            drawStevePart(vp * mm, mm, kShaft);
        }
        // 矢じり（先端、長さ 0.10）
        {
            glm::mat4 mm = glm::translate(m, glm::vec3(0.05f, 0.0f, 0.0f));
            mm = glm::scale(mm, glm::vec3(0.10f, 0.08f, 0.08f));
            drawStevePart(vp * mm, mm, kFletching);
        }
        // 矢羽（末端、十字に配置）
        {
            glm::mat4 mm = glm::translate(m, glm::vec3(-0.58f, 0.0f, 0.0f));
            mm = glm::scale(mm, glm::vec3(0.12f, 0.02f, 0.18f));
            drawStevePart(vp * mm, mm, kFletching);
        }
        {
            glm::mat4 mm = glm::translate(m, glm::vec3(-0.58f, 0.0f, 0.0f));
            mm = glm::scale(mm, glm::vec3(0.12f, 0.18f, 0.02f));
            drawStevePart(vp * mm, mm, kFletching);
        }
    }

    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);
}

// ─────────────────────────────────────────────────────────────────────────────
// drawFirstPersonHand() — ローカルプレイヤーの一人称ハンドアニメーション
//
// 画面右下に腕を表示する。歩行時にボブ、攻撃時にスイングアニメーション。
// 全3Dコンテンツの後・HUD の前に呼ぶ。深度テスト無効で必ず最前面に表示。
//
// walk_phase       : 移動中に加算される位相値（radians）
// attack_timer_norm: attack_sync_timer / 0.28f（1.0=攻撃直後, 0.0=待機）
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawFirstPersonHand(float walk_phase, float attack_timer_norm,
                                    bool bow_equipped, float bow_charge) {
    if (!entity_vao_) return;

    static const float kSkin[]      = {0.83f, 0.66f, 0.52f};
    static const float kBowWood[]   = {0.50f, 0.30f, 0.08f};
    static const float kBowDark[]   = {0.05f, 0.04f, 0.02f};
    static const float kBowGrip[]   = {0.72f, 0.72f, 0.68f};

    const float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    const float h = tanf(glm::radians(70.0f) * 0.5f) * 1.5f;  // ≈ 1.050
    const float w = h * aspect;
    const glm::mat4 proj = glm::ortho(-w, w, -h, h, 0.05f, 10.0f);
    const glm::mat4 view = glm::mat4(1.0f);

    // 歩行ボブ
    const float bob_y = sinf(walk_phase)        * 0.06f;
    const float bob_x = sinf(walk_phase * 0.5f) * 0.03f;

    // 攻撃スイング（弓装備中は無効）
    const float progress  = 1.0f - std::clamp(attack_timer_norm, 0.0f, 1.0f);
    const float swing_z   = bow_equipped
        ? 0.0f
        : sinf(progress * glm::pi<float>()) * glm::radians(45.0f);

    const float charge = std::clamp(bow_charge, 0.0f, 1.0f);

    entity_shader_.use();
    entity_shader_.setVec3 ("uSunDir",      sun_dir_[0], sun_dir_[1], sun_dir_[2]);
    entity_shader_.setFloat("uAmbient",     std::max(ambient_, 0.35f));
    entity_shader_.setFloat("uSunStrength", sun_strength_ * 0.6f);
    entity_shader_.setMat4 ("uView",        glm::value_ptr(view));
    entity_shader_.setVec3 ("uFogColor",    sky_horizon_[0], sky_horizon_[1], sky_horizon_[2]);
    entity_shader_.setFloat("uFogStart",    9999.0f);
    entity_shader_.setFloat("uFogEnd",      10000.0f);

    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(entity_vao_);

    if (!bow_equipped) {
        // ── 通常の腕（弓を持っていないとき） ─────────────────────────────
        glm::mat4 arm = glm::translate(glm::mat4(1.0f),
            glm::vec3(0.82f + bob_x, -1.30f + bob_y, -1.5f));
        arm = glm::rotate(arm, glm::radians(-40.0f),          glm::vec3(0.f, 1.f, 0.f));
        arm = glm::rotate(arm, glm::radians(15.0f),           glm::vec3(1.f, 0.f, 0.f));
        arm = glm::rotate(arm, glm::radians(10.0f) + swing_z, glm::vec3(0.f, 0.f, 1.f));
        glm::mat4 m = glm::scale(arm, glm::vec3(0.40f, 1.20f, 0.28f));
        drawStevePart(proj * m, m, kSkin);
        (void)kBowWood; (void)kBowDark; (void)kBowGrip;
    } else {
        // ── 弓装備中: BOW2.png を 1 ブロック厚の 3D ボクセルとして描く ────
        // 16x16 のドットマップをそのまま展開し、minecraft-yumi.png に寄せて
        // 画面右側に傾けて置く。
        static const float kBowPal[10][3] = {
            {1.000f, 1.000f, 1.000f},   // a: white arrow tip
            {0.286f, 0.212f, 0.082f},   // b: dark wood
            {0.694f, 0.694f, 0.694f},   // c: light gray
            {0.847f, 0.847f, 0.847f},   // d: lighter gray
            {0.537f, 0.404f, 0.153f},   // e: medium wood
            {0.408f, 0.306f, 0.118f},   // f: darker medium wood
            {0.157f, 0.118f, 0.043f},   // g: near-black bowstring
            {0.420f, 0.420f, 0.420f},   // h: medium gray
            {0.267f, 0.267f, 0.267f},   // i: arrow shaft dark gray
            {0.588f, 0.588f, 0.588f},   // j: light gray highlight
        };
        static const char* kBowPat[16] = {
            "................",
            "................",
            "...a....bbbbbbb.",
            "...cd.bbefeeefeg",
            "....gehfggggggg.",
            "....bgeh......i.",
            "...bhjge......i.",
            "...bfh.ge....i..",
            "..beg...ge...i..",
            "..bfg....ge..i..",
            "..beg.....ge.i..",
            "..beg......ge...",
            "..beg......i....",
            "..bfg..iiii.....",
            "..begii.........",
            "...g............",
        };

        // 弓だけは透視投影に切り替える。これで斜めに構えた弓の奥側ブロックが
        // 画面上で本当に小さく見え、手前のブロックが大きく見える。
        const float bow_fov  = 45.0f;
        const float bow_z    = -3.0f;
        glm::mat4 bow_proj   = glm::perspective(
            glm::radians(bow_fov), aspect, 0.05f, 20.0f);
        const float tan_half = tanf(glm::radians(bow_fov) * 0.5f);
        const float h_bow    = std::abs(bow_z) * tan_half;   // bow_z面でのワールド半高

        const float p     = 0.165f;             // 1ブロックのワールドサイズ
        const float pull  = charge * 0.06f;     // チャージ中はわずかに手前へ引く

        // 弓全体のルート: 手前に大きく置き、右下を画面外にはみ出させる
        //   Z +45°: BOW2.png の上限右上→下限左下のナナメを「縦に立った弓」に揃える
        //   X  +6°: 軽く前傾（弓面に沿った調整）
        //   Y -75°: 弓面を斜めから見せ、矢の延長線がクロスヘア方向へ
        //  WX+12°: 最終的にワールドX軸でピッチアップ → 弓全体が若干上向きに
        // 位置は NDC 比率 (0.75, -0.30) を bow_z 面のワールド座標に変換
        glm::mat4 root = glm::translate(glm::mat4(1.0f),
            glm::vec3(0.75f * h_bow * aspect + bob_x - pull,
                     -0.30f * h_bow         + bob_y,
                      bow_z));
        root = glm::rotate(root, glm::radians( 12.0f), glm::vec3(1.f, 0.f, 0.f));
        root = glm::rotate(root, glm::radians(-75.0f), glm::vec3(0.f, 1.f, 0.f));
        root = glm::rotate(root, glm::radians(  6.0f), glm::vec3(1.f, 0.f, 0.f));
        root = glm::rotate(root, glm::radians( 45.0f), glm::vec3(0.f, 0.f, 1.f));

        for (int cy = 0; cy < 16; ++cy) {
            for (int cx = 0; cx < 16; ++cx) {
                const char ch = kBowPat[cy][cx];
                if (ch < 'a' || ch > 'j') continue;
                const int idx = ch - 'a';
                // ローカル: 中心(7.5, 7.5) を原点に。Yは画像座標を反転して上向き＋
                const float lx = (cx - 7.5f) * p;
                const float ly = (7.5f - cy) * p;
                glm::mat4 m = glm::translate(root, glm::vec3(lx, ly, 0.0f));
                m = glm::scale(m, glm::vec3(p, p, p));
                drawStevePart(bow_proj * m, m, kBowPal[idx]);
            }
        }
        (void)kSkin; (void)kBowWood; (void)kBowDark; (void)kBowGrip;
    }

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);
}
