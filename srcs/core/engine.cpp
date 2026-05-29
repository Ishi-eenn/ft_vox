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
#include "network/client.hpp"
#include "mob/mob_manager.hpp"
#include "mob/arrow_manager.hpp"

#include <chrono>
#include <thread>
#include <string>
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
// インベントリ操作ヘルパー
// ─────────────────────────────────────────────────────────────────────────────

// ブロックをインベントリに1個追加する。既存スタックに積む → 空きスロットを探す。
static void inventoryAdd(Inventory& inv, BlockType type) {
    if (type == BlockType::Air || type == BlockType::Water ||
        type == BlockType::ShortGrass || isItem(type)) return;
    for (int i = 0; i < HOTBAR_SIZE; ++i) {
        if (inv.slots[i].type == type && inv.slots[i].count < STACK_MAX) {
            ++inv.slots[i].count;
            return;
        }
    }
    for (int i = 0; i < HOTBAR_SIZE; ++i) {
        if (inv.slots[i].type == BlockType::Air) {
            inv.slots[i] = {type, 1};
            return;
        }
    }
    // インベントリ満杯: 無音で捨てる
}

// 選択スロットから1個消費する。成功なら true を返す。
static bool inventoryConsume(Inventory& inv) {
    ItemStack& s = inv.slots[inv.selected];
    if (s.type == BlockType::Air || s.count <= 0) return false;
    --s.count;
    if (s.count == 0) s = {};
    return true;
}

static void applyMobExplosion(const MobExplosion& ex, World& world,
                              ChunkManager& chunk_mgr,
                              NetworkClient* net_client) {
    const int radius = (int)std::ceil(ex.radius);
    const float r2 = ex.radius * ex.radius;
    const int cx = (int)std::floor(ex.x);
    const int cy = (int)std::floor(ex.y);
    const int cz = (int)std::floor(ex.z);
    std::vector<ChunkPos> dirty_chunks;
    auto addDirtyChunk = [&](ChunkPos pos) {
        if (std::find(dirty_chunks.begin(), dirty_chunks.end(), pos) ==
            dirty_chunks.end())
            dirty_chunks.push_back(pos);
    };

    for (int x = cx - radius; x <= cx + radius; ++x) {
        for (int y = cy - radius; y <= cy + radius; ++y) {
            for (int z = cz - radius; z <= cz + radius; ++z) {
                const float dx = (x + 0.5f) - ex.x;
                const float dy = (y + 0.5f) - ex.y;
                const float dz = (z + 0.5f) - ex.z;
                if (dx * dx + dy * dy + dz * dz > r2) continue;

                BlockType bt = world.getWorldBlock(x, y, z);
                if (bt == BlockType::Air || bt == BlockType::Water) continue;
                if (!world.setWorldBlock(x, y, z, BlockType::Air)) continue;

                int ccx = (int)std::floor((float)x / CHUNK_SIZE_X);
                int ccz = (int)std::floor((float)z / CHUNK_SIZE_Z);
                int lx = x - ccx * CHUNK_SIZE_X;
                int lz = z - ccz * CHUNK_SIZE_Z;
                addDirtyChunk({ccx, ccz});
                if (lx == 0)              addDirtyChunk({ccx - 1, ccz});
                if (lx == CHUNK_SIZE_X-1) addDirtyChunk({ccx + 1, ccz});
                if (lz == 0)              addDirtyChunk({ccx, ccz - 1});
                if (lz == CHUNK_SIZE_Z-1) addDirtyChunk({ccx, ccz + 1});
                if (net_client)
                    net_client->sendBlockChange(
                        x, y, z, static_cast<uint8_t>(BlockType::Air));
            }
        }
    }
    for (ChunkPos pos : dirty_chunks)
        chunk_mgr.rebuildChunkAt(pos);
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

    Inventory     inventory;
    float         time_of_day    = 0.35f;
    bool          show_minimap_  = true;
    bool          show_player_list_ = false;
    bool          show_stats_ = false;

    // Multiplayer
    NetworkClient net_client;
    bool          multiplayer    = false;
    float         net_pos_timer  = 0.0f;
    float         mob_sync_timer = 0.0f;   // ホスト用: mob送信インターバル

    // Mobs
    MobManager    mob_mgr;
    ArrowManager  arrow_mgr;
    float         bow_charge_time   = 0.0f;   // 右クリック保持時間（秒）
    bool          bow_charging      = false;
    float         player_health     = 20.0f;
    float         player_max_health = 20.0f;
    bool          player_dead       = false;
    float         attack_sync_timer = 0.0f;
    float         death_sync_timer  = 0.0f;
    float         local_walk_phase  = 0.0f;

    ~Impl() { delete chunk_mgr; }
};

// ─────────────────────────────────────────────────────────────────────────────
Engine::Engine()  : impl_(std::make_unique<Impl>()) {}
Engine::~Engine() { shutdown(); }

bool Engine::connectToServer(const char* host, uint16_t port) {
    if (!impl_->net_client.connect(host, port)) {
        fprintf(stderr, "[Engine] Failed to connect to %s:%u\n", host, port);
        return false;
    }
    // Wait briefly for WELCOME packet (server sends it synchronously on connect).
    // poll() a few times to receive it before the game loop starts.
    for (int i = 0; i < 50 && !impl_->net_client.isConnected(); ++i) {
        std::vector<NetworkEvent> ev;
        impl_->net_client.poll(ev);
        for (auto& e : ev) {
            if (e.kind == NetworkEvent::Kind::BlockChange) {
                auto bt = static_cast<BlockType>(e.block_type);
                impl_->world.recordWorldBlockMod(e.bx, e.by, e.bz, bt);
                if (impl_->chunk_mgr)
                    rebuildModified(e.bx, e.bz, *impl_->chunk_mgr);
            } else if (e.kind == NetworkEvent::Kind::TimeSync) {
                impl_->time_of_day = e.time_of_day;
            }
        }
        // short busy-wait; acceptable once at startup
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(10ms);
    }
    if (!impl_->net_client.isConnected()) {
        fprintf(stderr, "[Engine] Did not receive WELCOME from server\n");
        return false;
    }
    // Override world seed so terrain matches server's world.
    impl_->world.setSeed(impl_->net_client.seed());
    impl_->multiplayer = true;
    fprintf(stderr, "[Engine] Multiplayer ready  seed=%u\n",
            impl_->net_client.seed());
    return true;
}

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
    impl_->world.setSaveDir("saves/" + std::to_string(seed));

    // ── プレイヤー・カメラの初期化 ─────────────────────────────────────────────
    impl_->player.init(window_);
    impl_->player.camera().setAspect((float)width_ / (float)height_);

    // ── チャンクマネージャーの初期化 ───────────────────────────────────────────
    impl_->chunk_mgr = new ChunkManager(impl_->world, impl_->renderer);

    // 弓を最後のホットバースロットに常備（無限矢扱い）
    impl_->inventory.slots[HOTBAR_SIZE - 1] = {BlockType::Bow, 1};

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
    float fps_timer   = 0.0f;
    int   fps_frames  = 0;
    int   fps_display = 0;
    float elapsed_s   = 0.0f;  // 起動からの経過秒数（雲アニメーション用）

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
        fps_timer += dt;
        elapsed_s += dt;
        ++fps_frames;
        // 0.25秒ごとにFPSを更新（毎フレーム更新すると数字が激しく変わって読めない）
        if (fps_timer >= 0.25f) {
            fps_display = (int)std::round((float)fps_frames / fps_timer);
            fps_timer  = 0.0f;
            fps_frames = 0;
        }

        // ── プレイヤー入力・移動・物理 ────────────────────────────────────────
        // isSolid: 「このブロックは固体か？」を調べる関数。
        // プレイヤーのAABB（当たり判定の箱）が固体ブロックにめり込まないように使う。
        auto isSolid = [&](int x, int y, int z) {
            BlockType t = impl_->world.getWorldBlock(x, y, z);
            return t != BlockType::Air
                && t != BlockType::Water
                && t != BlockType::ShortGrass
                && t != BlockType::Flower
                && t != BlockType::Mushroom;
        };
        // isWater: 「このブロックは水か？」を調べる関数。
        // 水中では重力・移動速度が変わる。
        auto isWater = [&](int x, int y, int z) {
            return impl_->world.getWorldBlock(x, y, z) == BlockType::Water;
        };
        if (impl_->player_dead) {
            InputHandler& inp = impl_->player.input();
            inp.setCaptureOnClick(false);
            if (inp.isCursorCaptured())
                inp.releaseCursor();
            impl_->player.pollInputOnly();
        } else {
            impl_->player.input().setCaptureOnClick(true);
            impl_->player.update(dt, isSolid, isWater);

            // 歩行ボブ位相: WASD 入力中のみ加算
            if (!impl_->player_dead) {
                InputHandler& inp = impl_->player.input();
                bool moving = inp.isHeld(GLFW_KEY_W) || inp.isHeld(GLFW_KEY_S)
                           || inp.isHeld(GLFW_KEY_A) || inp.isHeld(GLFW_KEY_D);
                if (moving) impl_->local_walk_phase += dt * 8.0f;
            }
        }
        if (impl_->player.shouldClose()) break;

        auto applyPlayerDamage = [&](float dmg, const char* source) {
            if (dmg <= 0.0f || impl_->player_dead) return;
            impl_->player_health -= dmg;
            fprintf(stderr, "[Game] Player took %.1f damage (%s).\n",
                    dmg, source);
            if (impl_->player_health <= 0.0f) {
                impl_->player_health = 0.0f;
                impl_->player_dead = true;
                impl_->death_sync_timer = 1.0f;
                InputHandler& inp = impl_->player.input();
                inp.setCaptureOnClick(false);
                if (inp.isCursorCaptured())
                    inp.releaseCursor();
                fprintf(stderr, "[Game] Player died.\n");
            }
        };

        if (impl_->player_dead) {
            InputHandler& inp = impl_->player.input();
            if (inp.wasFreeLeftClicked()) {
                const double x = inp.cursorX();
                const double y = inp.cursorY();
                const double bx0 = static_cast<double>(width_) * 0.5 - 150.0;
                const double bx1 = static_cast<double>(width_) * 0.5 + 150.0;
                const double by0 = static_cast<double>(height_) * 0.55;
                const double by1 = by0 + 56.0;
                if (x >= bx0 && x <= bx1 && y >= by0 && y <= by1) {
                    impl_->player.respawnAtInitial();
                    impl_->player_health = impl_->player_max_health;
                    impl_->player_dead = false;
                    impl_->death_sync_timer = 0.0f;
                    inp.setCaptureOnClick(true);
                    inp.captureCursor();
                    fprintf(stderr, "[Game] Player respawned.\n");
                }
            }
        }

        // 検証用ダメージ: P キーで2ハート分減る。
        if (!impl_->player_dead) {
            InputHandler& inp = impl_->player.input();
            static bool prev_p = false;
            bool cur_p = inp.isHeld(GLFW_KEY_P);
            if (cur_p && !prev_p)
                applyPlayerDamage(4.0f, "debug");
            prev_p = cur_p;
        }

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

        // ── M キー: ミニマップ表示切り替え ───────────────────────────────────
        {
            InputHandler& inp = impl_->player.input();
            static bool prev_m = false;
            bool cur_m = inp.isHeld(GLFW_KEY_M);
            if (cur_m && !prev_m) impl_->show_minimap_ = !impl_->show_minimap_;
            prev_m = cur_m;
        }

        // ── Tab キー: 接続中プレイヤー一覧表示切り替え ───────────────────────
        {
            InputHandler& inp = impl_->player.input();
            static bool prev_tab = false;
            bool cur_tab = inp.isHeld(GLFW_KEY_TAB);
            if (cur_tab && !prev_tab)
                impl_->show_player_list_ = !impl_->show_player_list_;
            prev_tab = cur_tab;
        }

        // ── F3 キー: FPS / triangles / cubes / chunks 表示切り替え ───────────
        {
            InputHandler& inp = impl_->player.input();
            static bool prev_f3 = false;
            bool cur_f3 = inp.isHeld(GLFW_KEY_F3);
            if (cur_f3 && !prev_f3)
                impl_->show_stats_ = !impl_->show_stats_;
            prev_f3 = cur_f3;
        }

        // ── ホットバー選択・ブロック操作 ─────────────────────────────────────
        {
            InputHandler& inp = impl_->player.input();
            Inventory&    inv = impl_->inventory;

            // キー 1〜9: ホットバースロットを選択（ワンショット: 前フレームと差分）
            static bool prev_num[9] = {};
            for (int k = 0; k < 9; ++k) {
                bool cur = inp.isHeld(GLFW_KEY_1 + k);
                if (cur && !prev_num[k]) inv.selected = k;
                prev_num[k] = cur;
            }

            // スクロールホイールでスロットを循環
            float sw = inp.scrollY();
            if (sw < -0.5f)
                inv.selected = (inv.selected + 1) % HOTBAR_SIZE;
            else if (sw > 0.5f)
                inv.selected = (inv.selected + HOTBAR_SIZE - 1) % HOTBAR_SIZE;

            // カーソルがキャプチャされているときだけブロック操作を受け付ける
            if (inp.isCursorCaptured()) {
                glm::vec3 pos   = impl_->player.camera().position();
                glm::vec3 front = impl_->player.camera().front();
                RayHit hit = castRay(pos, front, 6.0f, impl_->world);
                BlockType held = inv.slots[inv.selected].type;
                const bool bow_equipped = (held == BlockType::Bow);

                // 左クリック: ブロックを壊してインベントリに追加 OR ゾンビを攻撃
                if (inp.wasLeftClicked()) {
                    impl_->attack_sync_timer = 0.28f;
                    if (hit.hit) {
                        BlockType broken = impl_->world.getWorldBlock(
                            hit.bx, hit.by, hit.bz);
                        impl_->world.setWorldBlock(hit.bx, hit.by, hit.bz,
                                                   BlockType::Air);
                        rebuildModified(hit.bx, hit.bz, *impl_->chunk_mgr);
                        inventoryAdd(inv, broken);
                        if (impl_->multiplayer)
                            impl_->net_client.sendBlockChange(
                                hit.bx, hit.by, hit.bz,
                                static_cast<uint8_t>(BlockType::Air));
                    } else {
                        impl_->mob_mgr.playerMeleeAttack(
                            pos.x, pos.y, pos.z, front.x, front.z);
                    }
                }

                // 弓装備中: 右クリック長押しでチャージ、離して発射。
                // それ以外: 右クリックでブロックを設置。
                const bool rmb_held =
                    glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

                if (bow_equipped) {
                    if (rmb_held) {
                        impl_->bow_charging    = true;
                        impl_->bow_charge_time += dt;
                        if (impl_->bow_charge_time > 1.5f)
                            impl_->bow_charge_time = 1.5f;
                    } else if (impl_->bow_charging) {
                        // 離した瞬間: 矢を発射
                        // チャージ 0.1秒以下なら最低威力、1.0秒以上で最大威力
                        float t = (impl_->bow_charge_time - 0.1f) / 0.9f;
                        if (t < 0.0f) t = 0.0f;
                        if (t > 1.0f) t = 1.0f;
                        const float speed = 8.0f + 28.0f * t;
                        // 矢の発射位置: カメラ少し前方・少し下
                        const glm::vec3 spawn = pos + front * 0.4f
                                                    - glm::vec3(0.0f, 0.15f, 0.0f);
                        impl_->arrow_mgr.spawn(spawn.x, spawn.y, spawn.z,
                                               front.x * speed,
                                               front.y * speed,
                                               front.z * speed);
                        impl_->attack_sync_timer = 0.20f;
                        impl_->bow_charging    = false;
                        impl_->bow_charge_time = 0.0f;
                    }
                } else {
                    // 弓を持っていないときはチャージ状態をリセット
                    impl_->bow_charging    = false;
                    impl_->bow_charge_time = 0.0f;

                    // 右クリック: 選択スロットのブロックを設置（在庫があるときのみ）
                    if (hit.hit && inp.wasRightClicked()) {
                        BlockType to_place = held;
                        if (to_place != BlockType::Air && !isItem(to_place) &&
                            inventoryConsume(inv)) {
                            impl_->world.setWorldBlock(hit.nx, hit.ny, hit.nz,
                                                       to_place);
                            rebuildModified(hit.nx, hit.nz, *impl_->chunk_mgr);
                            if (impl_->multiplayer)
                                impl_->net_client.sendBlockChange(
                                    hit.nx, hit.ny, hit.nz,
                                    static_cast<uint8_t>(to_place));
                        }
                    }
                }
            } else {
                // カーソル解放中はチャージを中断
                impl_->bow_charging    = false;
                impl_->bow_charge_time = 0.0f;
            }
        }

        // ── 昼夜サイクル更新 ─────────────────────────────────────────────────
        // マルチプレイ時はサーバーからの TimeSync で時刻が上書きされるが、
        // 受信間隔（5秒）の間は各クライアントが自分で進める。
        impl_->time_of_day += dt / DAY_DURATION;
        if (impl_->time_of_day >= 1.0f) impl_->time_of_day -= 1.0f;
        impl_->renderer.setTimeOfDay(impl_->time_of_day);

        // ── マルチプレイ: ネットワーク送受信 ────────────────────────────────────
        glm::vec3 ppos = impl_->player.camera().position();
        if (impl_->attack_sync_timer > 0.0f)
            impl_->attack_sync_timer -= dt;
        if (impl_->death_sync_timer > 0.0f)
            impl_->death_sync_timer -= dt;
        const bool is_mob_host = !impl_->multiplayer ||
                                  impl_->net_client.playerId() == 1;
        if (impl_->multiplayer) {
            impl_->net_pos_timer += dt;
            if (impl_->net_pos_timer >= 0.05f) {   // ~20 Hz
                impl_->net_pos_timer = 0.0f;
                uint8_t flags = 0;
                if (impl_->attack_sync_timer > 0.0f) flags |= 0x02u;
                if (impl_->player_dead || impl_->death_sync_timer > 0.0f)
                    flags |= 0x04u;
                float synced_health = (impl_->player_dead || impl_->death_sync_timer > 0.0f)
                    ? 0.0f : impl_->player_health;
                impl_->net_client.updatePosition(
                    ppos.x, ppos.y, ppos.z,
                    impl_->player.camera().getYaw(),
                    impl_->player.camera().getPitch(),
                    synced_health,
                    flags);
            }
            std::vector<NetworkEvent> events;
            impl_->net_client.poll(events);
            for (auto& ev : events) {
                if (ev.kind == NetworkEvent::Kind::BlockChange) {
                    auto bt = static_cast<BlockType>(ev.block_type);
                    impl_->world.recordWorldBlockMod(ev.bx, ev.by, ev.bz, bt);
                    rebuildModified(ev.bx, ev.bz, *impl_->chunk_mgr);
                } else if (ev.kind == NetworkEvent::Kind::TimeSync) {
                    // サーバーの時刻に合わせる（小さなジャンプは許容）
                    impl_->time_of_day = ev.time_of_day;
                } else if (ev.kind == NetworkEvent::Kind::MobUpdate) {
                    if (!is_mob_host)
                        impl_->mob_mgr.setZombies(std::move(ev.mobs));
                }
            }
            // Update walking animation phase for each remote player.
            for (auto& [id, rp] : impl_->net_client.remotePlayers()) {
                if (!rp.initialized) {
                    rp.prev_x     = rp.x;
                    rp.prev_z     = rp.z;
                    rp.initialized = true;
                }
                float dx    = rp.x - rp.prev_x;
                float dz    = rp.z - rp.prev_z;
                float speed = std::sqrt(dx * dx + dz * dz) / dt;
                if (speed > 0.3f)
                    rp.walk_phase += dt * 8.0f;
                if (rp.attack_timer > 0.0f)
                    rp.attack_timer -= dt;
                rp.prev_x = rp.x;
                rp.prev_z = rp.z;
            }
        }

        // ── モブ更新 ────────────────────────────────────────────────────────
        // ホスト（シングルプレイまたはplayer_id==1）のみ物理・AIを実行。
        // 非ホストはサーバー経由で受け取ったzombies_をそのまま描画する。
        {
            float dmg = 0.0f;
            if (is_mob_host) {
                // ホスト: mob AI を動かし、マルチプレイ中は状態を送信する
                dmg = impl_->mob_mgr.update(
                    dt,
                    ppos.x, ppos.y, ppos.z,
                    impl_->time_of_day,
                    isSolid,
                    impl_->world);
                if (impl_->multiplayer) {
                    impl_->mob_sync_timer -= dt;
                    if (impl_->mob_sync_timer <= 0.0f) {
                        impl_->mob_sync_timer = 0.1f;  // 10 Hz
                        const auto& zs = impl_->mob_mgr.zombies();
                        uint8_t count = (uint8_t)std::min((int)zs.size(), 255);
                        std::vector<uint8_t> buf;
                        buf.reserve(1 + count * sizeof(PktMobEntry));
                        buf.push_back(count);
                        for (uint8_t i = 0; i < count; ++i) {
                            PktMobEntry e;
                            e.x = zs[i].x; e.y = zs[i].y; e.z = zs[i].z;
                            e.yaw = zs[i].yaw; e.health = zs[i].health;
                            e.fuse_timer = zs[i].fuse_timer;
                            e.type = (uint8_t)zs[i].type;
                            e.state = (uint8_t)zs[i].state;
                            buf.insert(buf.end(),
                                       reinterpret_cast<uint8_t*>(&e),
                                       reinterpret_cast<uint8_t*>(&e) + sizeof(e));
                        }
                        impl_->net_client.sendRaw(PacketType::MobUpdate,
                                                   buf.data(), (uint16_t)buf.size());
                    }
                }
            }
            if (is_mob_host) {
                auto explosions = impl_->mob_mgr.consumeExplosions();
                for (const MobExplosion& ex : explosions) {
                    applyMobExplosion(
                        ex, impl_->world, *impl_->chunk_mgr,
                        impl_->multiplayer ? &impl_->net_client : nullptr);
                }
            }
            applyPlayerDamage(dmg, "mob");
        }

        // ── 矢の物理更新（命中判定はホストのみ実行） ─────────────────────────
        // 非ホストではモブ状態はサーバーから配信されるため矢の更新は行わない（MVP）。
        if (is_mob_host) {
            impl_->arrow_mgr.update(dt, isSolid, impl_->mob_mgr.zombiesMut());
        }

        // ── チャンクのストリーミング ─────────────────────────────────────────
        // プレイヤーの周囲のチャンクを読み込み、遠いチャンクを破棄する。
        // 毎フレーム数チャンクずつ処理し、ゲームが止まらないようにする。
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
        impl_->renderer.setUnderwater(impl_->player.isInWater());

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

        // パス0: シャドウマップ生成 (太陽視点で深度のみ描画)
        // カメラ外の地形(洞窟の天井など)も影を落とすため、全ロード済みチャンクを使う
        impl_->renderer.updateShadowMatrix(ppos.x, ppos.y, ppos.z);
        impl_->renderer.beginShadowPass();
        {
            auto all_chunks = impl_->chunk_mgr->getAllLoadedChunks();
            for (Chunk* c : all_chunks)
                impl_->renderer.drawChunkShadow(c);
        }
        impl_->renderer.endShadowPass();

        // フラスタムカリング済みの可視チャンクリストを取得（後続パスで共用）
        auto visible = impl_->chunk_mgr->getVisibleChunks(frustum);
        int visible_triangles = 0;
        int visible_cubes = 0;
        if (impl_->show_stats_) {
            for (const Chunk* c : visible) {
                int indices = c->gpu.idx_count + c->gpu.idx_count_water;
                visible_triangles += indices / 3;
                visible_cubes += indices / 36;
            }
        }

        // パス1: GBufferパス（ビュー空間法線 + 深度をテクスチャに書き込む）
        impl_->renderer.beginGBufferPass();
        for (Chunk* c : visible)
            impl_->renderer.drawChunkGBuffer(c, view4x4, proj4x4);
        impl_->renderer.endGBufferPass();

        // パス2: SSAO + ブラー計算（ssao_blur_tex_ に結果を書き込む）
        impl_->renderer.computeSSAO(proj4x4);

        impl_->renderer.beginFrame();  // 画面をクリア

        // 空（スカイボックス）を最初に描画する（最も遠い背景として）
        impl_->renderer.drawSkybox(sky_view4x4, proj4x4);

        // パス3: 不透明なブロック（石・土・草など）を描画
        // 深度バッファに書き込むので、後で描く水がその後ろに隠れる。
        for (Chunk* c : visible) {
            impl_->renderer.drawChunk(c, view4x4, proj4x4);
        }

        // リモートプレイヤーを Steve モデルで描画
        if (impl_->multiplayer)
            impl_->renderer.drawRemotePlayers(
                impl_->net_client.remotePlayers(), view4x4, proj4x4);

        // モブ（ゾンビ）を描画
        impl_->renderer.drawMobs(impl_->mob_mgr.zombies(), view4x4, proj4x4);

        // 矢を描画
        impl_->renderer.drawArrows(impl_->arrow_mgr.arrows(), view4x4, proj4x4);

        // 3D 雲レイヤーを描画（不透明ブロックの後、水の前）
        impl_->renderer.drawClouds(view4x4, proj4x4, ppos.x, ppos.z, elapsed_s);

        // パス2: 水（半透明）を描画
        // 深度バッファには書き込まない（読むだけ）。
        // こうすることで水越しに地形が透けて見える。
        for (Chunk* c : visible) {
            impl_->renderer.drawChunkWater(c, view4x4, proj4x4);
        }

        // 水中にいるなら青いオーバーレイ（フィルター）を全画面に重ねる
        if (impl_->player.isInWater())
            impl_->renderer.drawUnderwaterOverlay();

        // 一人称ハンドアニメーション（死亡時は非表示）
        if (!impl_->player_dead) {
            const bool bow_held =
                impl_->inventory.slots[impl_->inventory.selected].type == BlockType::Bow;
            const float charge_ratio = std::min(impl_->bow_charge_time / 1.0f, 1.0f);
            impl_->renderer.drawFirstPersonHand(
                impl_->local_walk_phase,
                impl_->attack_sync_timer / 0.28f,
                bow_held,
                charge_ratio);
        }

        // HUD（クロスヘア＋FPS＋座標表示）を最前面に描画
        impl_->renderer.drawHud(fps_display,
                                (int)std::floor(ppos.x),
                                (int)std::floor(ppos.y),
                                (int)std::floor(ppos.z),
                                impl_->player_health,
                                impl_->player_max_health);

        if (impl_->show_stats_) {
            const char* biome_name = impl_->world.getBiomeNameAt(ppos.x, ppos.z);
            impl_->renderer.drawStats(
                fps_display,
                visible_triangles,
                visible_cubes,
                static_cast<int>(visible.size()),
                static_cast<int>(impl_->chunk_mgr->loadedCount()),
                impl_->show_minimap_,
                biome_name);
        }

        if (impl_->show_player_list_) {
            impl_->renderer.drawPlayerList(
                impl_->net_client.playerId(),
                impl_->net_client.remotePlayers(),
                impl_->multiplayer);
        }

        // ホットバー（画面下中央）を描画
        impl_->renderer.drawHotbar(impl_->inventory);

        // ミニマップ（左上）を更新して描画（M キーでトグル）
        if (impl_->show_minimap_) {
            impl_->renderer.updateMinimap(impl_->world, ppos.x, ppos.z,
                                          impl_->player.camera().getYaw(), dt);
            impl_->renderer.drawMinimap();
        }

        if (impl_->player_dead)
            impl_->renderer.drawDeathScreen();

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
