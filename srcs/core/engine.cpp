// ─────────────────────────────────────────────────────────────────────────────
// engine.cpp — ゲーム全体を統括する「エンジン」
//
// 【3Dゲームの基本的な仕組み】
//   3Dゲームは「ゲームループ」という無限ループで動いている。
//   毎フレーム（1/60秒ごとなど）以下を繰り返す:
//     1. 入力を受け取る（キーボード・マウス）
//     2. ゲームの状態を更新する（プレイヤー移動、物理演算など）
//     3. 画面を描画する
//
//   このファイルはそのループを管理し、各システム
//   （World・Player・Renderer・ChunkManager）を連携させる。
// ─────────────────────────────────────────────────────────────────────────────
#include "core/engine.hpp"

// glad MUST be included before GLFW
// GLAD: OpenGLの関数をOSから読み込む。必ずGLFWより先に include する。
#define GLAD_GL_IMPLEMENTATION
#include <glad/gl.h>
#include <GLFW/glfw3.h>

// GLM: 行列・ベクトルの数学ライブラリ
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "renderer/renderer.hpp"
#include "renderer/frustum.hpp"
#include "world/world.hpp"
#include "player/player.hpp"
#include "streaming/chunk_manager.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// レイキャスト（DDA法）— カメラ正面のブロックを検出する
// ─────────────────────────────────────────────────────────────────────────────
// 【DDA法とは？】
//   「Digital Differential Analyzer」の略。
//   光線（レイ）をグリッド（ブロックの格子）に沿って1マスずつ進める方法。
//   x / y / z のうち「次にブロック境界を越えるのがいちばん近い軸」を選んで
//   その軸方向に1マス進む、を繰り返す。これにより高速かつ正確に
//   レイとブロックの交点を求められる。
//
//   例: 視点からまっすぐ前に光線を飛ばす
//     →ブロック境界を1つずつ越えながら「どのブロックに当たったか」を調べる
// ─────────────────────────────────────────────────────────────────────────────

// レイキャストの結果を格納する構造体
struct RayHit {
    bool hit = false;
    int  bx, by, bz;  // 当たったブロックのワールド座標
    int  nx, ny, nz;  // 当たる直前の空気ブロック座標（ブロック設置位置として使う）
};

// origin: レイの始点（カメラの目線位置）
// dir:    レイの方向（カメラの正面ベクトル、長さ1に正規化済み）
// max_dist: 最大到達距離（ブロック数）
static RayHit castRay(const glm::vec3& origin, const glm::vec3& dir,
                      float max_dist, World& world)
{
    RayHit result;

    // 始点のブロック座標（小数点以下を切り捨てて整数化）
    glm::ivec3 ipos = glm::ivec3(glm::floor(origin));
    int ix = ipos.x, iy = ipos.y, iz = ipos.z;

    // 各軸の進む方向（正なら+1, 負なら-1）
    int sx = (dir.x >= 0.f) ? 1 : -1;
    int sy = (dir.y >= 0.f) ? 1 : -1;
    int sz = (dir.z >= 0.f) ? 1 : -1;

    // tdx/tdy/tdz: 各軸方向に1ブロック分進むのに必要な「時間（t値）」
    // dir がほぼ0（その軸方向に進まない）なら無限大にして絶対に選ばれないようにする
    glm::vec3 adir = glm::abs(dir);
    float tdx = (adir.x > 1e-6f) ? glm::abs(1.f / dir.x) : 1e30f;
    float tdy = (adir.y > 1e-6f) ? glm::abs(1.f / dir.y) : 1e30f;
    float tdz = (adir.z > 1e-6f) ? glm::abs(1.f / dir.z) : 1e30f;

    // tx/ty/tz: 現在位置から次のブロック境界（各軸）まで到達するt値
    float tx = (sx > 0) ? ((ix + 1) - origin.x) * tdx : (origin.x - ix) * tdx;
    float ty = (sy > 0) ? ((iy + 1) - origin.y) * tdy : (origin.y - iy) * tdy;
    float tz = (sz > 0) ? ((iz + 1) - origin.z) * tdz : (origin.z - iz) * tdz;

    // px/py/pz: 1ステップ前のブロック座標（ブロック設置先として使う）
    int px = ix, py = iy, pz = iz;

    for (;;) {
        // 現在のブロックを調べる
        BlockType bt = world.getWorldBlock(ix, iy, iz);
        if (bt != BlockType::Air && bt != BlockType::Water) {
            // 空気でも水でもない = 固体ブロック → ヒット！
            result.hit = true;
            result.bx = ix;  result.by = iy;  result.bz = iz;  // 当たったブロック
            result.nx = px;  result.ny = py;  result.nz = pz;  // その手前のマス
            return result;
        }

        // 1ステップ前の座標を記録しておく（ブロック設置位置用）
        px = ix;  py = iy;  pz = iz;

        // tx/ty/tz のうち最小（＝次に境界を越えるのが最も近い軸）を選ぶ
        float tmin = std::min({tx, ty, tz});
        if (tmin > max_dist) break;  // 最大距離を超えたら終了

        // 最も近い軸方向に1マス進む
        if (tx <= ty && tx <= tz) { ix += sx;  tx += tdx; }
        else if (ty <= tz)        { iy += sy;  ty += tdy; }
        else                      { iz += sz;  tz += tdz; }
    }
    return result;  // hit=false のまま返す
}

// 指定ワールド座標のブロックが属するチャンクとその隣接チャンクを再構築する。
// ブロックを壊す・置くとメッシュが変わるため、そのチャンクを作り直す必要がある。
// チャンク境界にあるブロックは隣のチャンクにも影響するため隣も再構築する。
static void rebuildModified(int wx, int wz, ChunkManager& mgr) {
    // ワールド座標 → チャンク座標に変換
    int cx = (int)std::floor((float)wx / CHUNK_SIZE_X);
    int cz = (int)std::floor((float)wz / CHUNK_SIZE_Z);
    // チャンク内のローカル座標
    int lx = wx - cx * CHUNK_SIZE_X;
    int lz = wz - cz * CHUNK_SIZE_Z;

    mgr.rebuildChunkAt({cx, cz});
    // チャンクの端にあるブロックなら隣も再構築
    if (lx == 0)               mgr.rebuildChunkAt({cx - 1, cz});
    if (lx == CHUNK_SIZE_X-1)  mgr.rebuildChunkAt({cx + 1, cz});
    if (lz == 0)               mgr.rebuildChunkAt({cx, cz - 1});
    if (lz == CHUNK_SIZE_Z-1)  mgr.rebuildChunkAt({cx, cz + 1});
}

// ─────────────────────────────────────────────────────────────────────────────
// Engine の内部実装データ（pImpl パターン）
// ─────────────────────────────────────────────────────────────────────────────
// 1日の長さ（リアル時間の秒数）。600秒 = 10分で昼夜1サイクル。
static constexpr float DAY_DURATION = 600.0f;

struct Engine::Impl {
    Renderer      renderer;    // OpenGL描画担当
    World         world;       // ブロックデータ・地形生成担当
    Player        player;      // プレイヤー移動・物理担当
    ChunkManager* chunk_mgr = nullptr;  // チャンクの読み込み・破棄担当

    BlockType selected_block = BlockType::Stone;  // 現在選択中のブロック種類
    float     time_of_day    = 0.35f;  // 時刻（0=深夜, 0.25=日の出, 0.5=正午, 0.75=日の入り）

    ~Impl() { delete chunk_mgr; }
};

// ─────────────────────────────────────────────────────────────────────────────
Engine::Engine()  : impl_(std::make_unique<Impl>()) {}
Engine::~Engine() { shutdown(); }

// ─────────────────────────────────────────────────────────────────────────────
// init() — ウィンドウ・OpenGL・各システムの初期化
// ─────────────────────────────────────────────────────────────────────────────
bool Engine::init(uint32_t seed, int width, int height) {
    width_  = width;
    height_ = height;

    // ── GLFW の初期化 ────────────────────────────────────────────────────────
    // GLFW: ウィンドウ作成・キーボード・マウス入力を担うライブラリ
    if (!glfwInit()) {
        fprintf(stderr, "[Engine] glfwInit failed\n");
        return false;
    }

    // OpenGL 4.1 コアプロファイルを要求する
    // コアプロファイル = 古い非推奨API（固定パイプライン）を使わない近代的なモード
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);  // macOS では必須
#endif
    glfwWindowHint(GLFW_RESIZABLE, GL_TRUE);

    // モニターの解像度を取得してフルスクリーンウィンドウを作る
    GLFWmonitor*       monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode    = glfwGetVideoMode(monitor);
    width_  = mode->width;
    height_ = mode->height;

    window_ = glfwCreateWindow(width_, height_, "ft_vox", monitor, nullptr);
    if (!window_) {
        fprintf(stderr, "[Engine] glfwCreateWindow failed\n");
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);  // VSync ON（フレームレートをモニターのリフレッシュレートに合わせる）

    // Retina/HiDPI ディスプレイでは論理ピクセルと物理ピクセルが異なる。
    // OpenGLは物理ピクセルを使うため、フレームバッファサイズを取得しなおす。
    int fb_w, fb_h;
    glfwGetFramebufferSize(window_, &fb_w, &fb_h);
    width_  = fb_w;
    height_ = fb_h;

    // ── レンダラー初期化（GLAD で OpenGL 関数ポインタをロード）────────────────
    if (!impl_->renderer.init(window_)) {
        fprintf(stderr, "[Engine] Renderer::init failed\n");
        return false;
    }
    impl_->renderer.onResize(width_, height_);

    // ── ワールド（地形）の初期化 ───────────────────────────────────────────────
    impl_->world.setSeed(seed);

    // ── プレイヤー・カメラの初期化 ─────────────────────────────────────────────
    impl_->player.init(window_);
    impl_->player.camera().setAspect((float)width_ / (float)height_);

    // ── チャンクマネージャーの初期化 ───────────────────────────────────────────
    impl_->chunk_mgr = new ChunkManager(impl_->world, impl_->renderer, impl_->world.terrainGen());

    running_ = true;
    fprintf(stderr, "[Engine] init OK  seed=%u  %dx%d\n", seed, width_, height_);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// run() — メインゲームループ
//
// ゲームが終了するまで以下を毎フレーム繰り返す:
//   1. デルタタイム計算（前フレームからの経過時間）
//   2. FPS計測・動的レンダー距離調整
//   3. プレイヤー入力・移動・物理更新
//   4. ブロック操作（クリックで壊す・置く）
//   5. 昼夜サイクル更新
//   6. チャンクのストリーミング（読み込み・破棄）
//   7. 描画（空→地形→水→HUD）
// ─────────────────────────────────────────────────────────────────────────────
void Engine::run() {
    using Clock = std::chrono::steady_clock;

    // ── タイトル画面ループ ───────────────────────────────────────────────────
    // SPACE が押されるまでタイトル画面を表示し続ける。
    // プレイヤーの入力処理やチャンクのストリーミングはまだ行わない。
    {
        auto prev = Clock::now();
        glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        while (running_) {
            auto  now = Clock::now();
            float dt  = std::chrono::duration<float>(now - prev).count();
            prev      = now;
            if (dt > 0.1f) dt = 0.1f;

            glfwPollEvents();
            if (glfwWindowShouldClose(window_) ||
                glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                running_ = false; break;
            }

            // リサイズ対応（タイトル画面中もウィンドウサイズが変わる可能性がある）
            if (impl_->player.wasResized()) {
                width_  = impl_->player.resizeW();
                height_ = impl_->player.resizeH();
                if (width_ > 0 && height_ > 0) {
                    impl_->renderer.onResize(width_, height_);
                    impl_->player.camera().setAspect((float)width_ / (float)height_);
                }
                impl_->player.clearResize();
            }

            bool start = impl_->renderer.drawTitleScreen(dt);
            impl_->renderer.endFrame();

            if (start) break;
        }
        // ゲーム開始: カーソルをキャプチャしてプレイヤー操作に切り替える
        if (running_) {
            glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
    }

    if (!running_) return;

    // ── メインゲームループ ───────────────────────────────────────────────────
    auto prev   = Clock::now();
    float fps_timer       = 0.0f;
    int   fps_frames      = 0;
    int   fps_display     = 0;
    float rd_adjust_timer = 0.0f;  // レンダー距離調整のクールダウンタイマー（秒）

    while (running_) {
        // ── デルタタイム計算 ──────────────────────────────────────────────────
        // デルタタイム = 前フレームからの経過時間（秒）。
        // これを使うことでFPSが変わっても移動速度などが一定になる。
        // 例: 60fps なら dt≈0.016秒, 30fps なら dt≈0.033秒
        auto  now = Clock::now();
        float dt  = std::chrono::duration<float>(now - prev).count();
        prev      = now;
        // dtを上限0.1秒でクランプ。カクついた瞬間に物理が暴走するのを防ぐ。
        if (dt > 0.1f) dt = 0.1f;
        fps_timer       += dt;
        rd_adjust_timer += dt;
        ++fps_frames;
        // 0.25秒ごとにFPSを更新（毎フレーム更新すると数字が激しく変わって読めない）
        if (fps_timer >= 0.25f) {
            fps_display = (int)std::round((float)fps_frames / fps_timer);
            fps_timer  = 0.0f;
            fps_frames = 0;
        }

        // ── 動的レンダー距離 ─────────────────────────────────────────────────
        // FPSに合わせてチャンクの描画距離を自動調整する。
        //   30FPS未満 → 描画距離を縮小（GPU負荷を下げる）
        //   55FPS超過 → 描画距離を拡大（余裕があるので遠くまで見せる）
        // 頻繁に変えると振動するため、1秒に1回だけ調整する（ヒステリシス）。
        if (rd_adjust_timer >= 1.0f) {
            rd_adjust_timer = 0.0f;
            int cur = impl_->chunk_mgr->renderDistance();
            if (fps_display > 0 && fps_display < 30 && cur > RENDER_DISTANCE_MIN) {
                impl_->chunk_mgr->setRenderDistance(cur - 1);
            } else if (fps_display > 55 && cur < RENDER_DISTANCE_MAX) {
                impl_->chunk_mgr->setRenderDistance(cur + 1);
            }
        }

        // ── プレイヤー入力・移動・物理 ────────────────────────────────────────
        // isSolid: 「このブロックは固体か？」を調べる関数。
        // プレイヤーのAABB（当たり判定の箱）が固体ブロックにめり込まないように使う。
        auto isSolid = [&](int x, int y, int z) {
            BlockType t = impl_->world.getWorldBlock(x, y, z);
            return t != BlockType::Air && t != BlockType::Water;
        };
        // isWater: 「このブロックは水か？」を調べる関数。
        // 水中では重力・移動速度が変わる。
        auto isWater = [&](int x, int y, int z) {
            return impl_->world.getWorldBlock(x, y, z) == BlockType::Water;
        };
        impl_->player.update(dt, isSolid, isWater);
        if (impl_->player.shouldClose()) break;

        // ── ウィンドウリサイズ対応 ────────────────────────────────────────────
        if (impl_->player.wasResized()) {
            width_  = impl_->player.resizeW();
            height_ = impl_->player.resizeH();
            if (width_ > 0 && height_ > 0) {
                impl_->renderer.onResize(width_, height_);
                // アスペクト比が変わったのでカメラの射影行列も更新する
                impl_->player.camera().setAspect((float)width_ / (float)height_);
            }
            impl_->player.clearResize();
        }

        // ── ブロック種類の選択・操作 ─────────────────────────────────────────
        {
            InputHandler& inp = impl_->player.input();

            // キー 1〜6 で設置ブロックを切り替え
            for (int k = GLFW_KEY_1; k <= GLFW_KEY_6; ++k) {
                if (inp.isHeld(k)) {
                    impl_->selected_block =
                        static_cast<BlockType>(k - GLFW_KEY_1 + 1);
                    break;
                }
            }

            // カーソルがキャプチャされているときだけブロック操作を受け付ける
            if (inp.isCursorCaptured()) {
                // カメラ正面方向にレイを飛ばして当たったブロックを特定
                glm::vec3 pos   = impl_->player.camera().position();
                glm::vec3 front = impl_->player.camera().front();
                RayHit hit = castRay(pos, front, 6.0f, impl_->world);

                if (hit.hit) {
                    // 左クリック: 当たったブロックを空気に置き換える（壊す）
                    if (inp.wasLeftClicked()) {
                        impl_->world.setWorldBlock(hit.bx, hit.by, hit.bz,
                                                   BlockType::Air);
                        rebuildModified(hit.bx, hit.bz, *impl_->chunk_mgr);
                    }
                    // 右クリック: 当たる直前のマスに選択中ブロックを置く
                    if (inp.wasRightClicked()) {
                        impl_->world.setWorldBlock(hit.nx, hit.ny, hit.nz,
                                                   impl_->selected_block);
                        rebuildModified(hit.nx, hit.nz, *impl_->chunk_mgr);
                    }
                }
            }
        }

        // ── 昼夜サイクル更新 ─────────────────────────────────────────────────
        // time_of_day を少しずつ進め、1.0になったら0に戻す（ループ）。
        // 600秒（10分）で0→1になる速さ。
        impl_->time_of_day += dt / DAY_DURATION;
        if (impl_->time_of_day >= 1.0f) impl_->time_of_day -= 1.0f;
        // レンダラーに時刻を伝え、空の色・太陽方向・光の強さを更新してもらう
        impl_->renderer.setTimeOfDay(impl_->time_of_day);

        // ── チャンクのストリーミング ─────────────────────────────────────────
        // プレイヤーの周囲のチャンクを読み込み、遠いチャンクを破棄する。
        // 毎フレーム数チャンクずつ処理し、ゲームが止まらないようにする。
        glm::vec3 ppos = impl_->player.camera().position();
        impl_->chunk_mgr->update(ppos.x, ppos.z, frame_);

        // ── 行列の計算 ───────────────────────────────────────────────────────
        // 【View行列】: カメラの位置・向きによって「ワールドがどう見えるか」を表す行列。
        //              カメラが動いたり向きを変えるたびに変わる。
        // 【Proj行列】: 遠近感（遠いものが小さく見える）を付ける射影変換の行列。
        //              画角・アスペクト比・近クリップ・遠クリップから計算する。
        float view4x4[16], proj4x4[16];
        float aspect = (height_ > 0) ? (float)width_ / (float)height_ : 1.0f;
        impl_->player.camera().getViewMatrix(view4x4);
        impl_->player.camera().getProjMatrix(proj4x4, aspect);

        // 空（スカイボックス）用のView行列は「回転だけ」を残す。
        // 平行移動成分（どこにいるか）を取り除くことで、
        // どこに移動しても空が常にカメラを包むように見せる。
        glm::mat4 view = glm::make_mat4(view4x4);
        glm::mat4 proj = glm::make_mat4(proj4x4);
        glm::mat4 sky_view = glm::mat4(glm::mat3(view));  // 3x3の回転部分だけ取り出す
        float sky_view4x4[16];
        std::memcpy(sky_view4x4, glm::value_ptr(sky_view), 64);

        // ── フラスタムカリング ───────────────────────────────────────────────
        // 【フラスタムとは？】
        //   カメラの視野を表す立体（視錐台）。カメラから見える範囲を6つの平面で囲った形。
        //   この外にあるチャンクは絶対に画面に映らないので、描画をスキップして高速化する。
        Frustum frustum;
        frustum.extractFromVP(proj * view);  // Proj×View から視錐台の6平面を取り出す

        // ── 描画 ─────────────────────────────────────────────────────────────
        impl_->renderer.beginFrame();  // 画面をクリア

        // 空（スカイボックス）を最初に描画する（最も遠い背景として）
        impl_->renderer.drawSkybox(sky_view4x4, proj4x4);

        // フラスタムカリング済みの可視チャンクリストを取得
        auto visible = impl_->chunk_mgr->getVisibleChunks(frustum);

        // パス1: 不透明なブロック（石・土・草など）を描画
        // 深度バッファに書き込むので、後で描く水がその後ろに隠れる。
        for (Chunk* c : visible) {
            impl_->renderer.drawChunk(c, view4x4, proj4x4);
        }

        // パス2: 水（半透明）を描画
        // 深度バッファには書き込まない（読むだけ）。
        // こうすることで水越しに地形が透けて見える。
        for (Chunk* c : visible) {
            impl_->renderer.drawChunkWater(c, view4x4, proj4x4);
        }

        // 水中にいるなら青いオーバーレイ（フィルター）を全画面に重ねる
        if (impl_->player.isInWater())
            impl_->renderer.drawUnderwaterOverlay();

        // HUD（クロスヘア＋FPS＋座標表示）を最前面に描画
        impl_->renderer.drawHud(fps_display,
                                (int)std::floor(ppos.x),
                                (int)std::floor(ppos.y),
                                (int)std::floor(ppos.z));

        // ミニマップ（左上）を更新して描画
        impl_->renderer.updateMinimap(impl_->world, ppos.x, ppos.z,
                                      impl_->player.camera().getYaw(), dt);
        impl_->renderer.drawMinimap();

        // 描画したバッファを画面に表示する（ダブルバッファリング）
        impl_->renderer.endFrame();
        ++frame_;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// shutdown() — 終了処理
// OpenGLリソースを全て解放してからウィンドウを閉じる。
// ─────────────────────────────────────────────────────────────────────────────
void Engine::shutdown() {
    if (!window_) return;  // 二重呼び出し防止

    // GLリソース（テクスチャ・バッファ・シェーダー）を OpenGLコンテキストが
    // 生きている間に先に解放しなければならない。
    impl_.reset();

    glfwDestroyWindow(window_);
    window_ = nullptr;
    glfwTerminate();

    fprintf(stderr, "[Engine] shutdown complete  frames=%llu\n",
            (unsigned long long)frame_);
}
