// ─────────────────────────────────────────────────────────────────────────────
// player.cpp — プレイヤーの移動・物理・水中処理
//
// 【3Dゲームの物理の基本】
//   現実と同じように、プレイヤーには毎フレーム重力が加わり続ける。
//   ジャンプすると上向きの速度が与えられ、重力によってだんだん減速→落下する。
//
// 【AABB当たり判定】
//   AABB = Axis-Aligned Bounding Box（軸平行境界ボックス）
//   プレイヤーを「横0.6×縦1.8ブロックの見えない箱」として扱い、
//   箱がブロックにめり込む方向への移動を禁止することで壁や床を実現する。
//   X / Y / Z 軸を独立して処理するため、壁に斜めから当たっても
//   壁に沿ってスライドできる（X が詰まっても Z は動ける）。
// ─────────────────────────────────────────────────────────────────────────────
#include "player/player.hpp"
#include <cstdio>
#include <cmath>

// ─── 調整定数 ────────────────────────────────────────────────────────────────
static constexpr float WORLD_BOTTOM      = -200.0f; // Y座標の下限（これより低くは行けない）
static constexpr float SENSITIVITY       = 0.1f;    // マウス感度（度/ピクセル）
static constexpr float GRAVITY           = 28.0f;   // 重力加速度（ブロック/秒²）
static constexpr float JUMP_VELOCITY     = 9.0f;    // ジャンプ初速度（ブロック/秒）
static constexpr float TERMINAL_VELOCITY = -50.0f;  // 落下速度の上限（ブロック/秒）
static constexpr float DBL_TAP_WINDOW    = 0.30f;   // ダブルタップとみなす時間窓（秒）

// 水中物理の定数
static constexpr float WATER_GRAVITY      = 6.0f;   // 水中での沈む力（空気中の1/4程度）
static constexpr float WATER_TERMINAL_VEL = -3.0f;  // 水中での沈む速度の上限
static constexpr float WATER_SWIM_SPEED   = 4.5f;   // Space/Ctrl での上下移動速度
static constexpr float WATER_SPEED_FACTOR = 0.5f;   // 水中での水平移動速度の倍率（遅くなる）

// ─── プレイヤーの体のサイズ ───────────────────────────────────────────────────
// Minecraft 準拠: 幅0.6 × 高さ1.8ブロック、目線は足元から1.62ブロック上
static constexpr float PLAYER_HALF_W = 0.30f;   // 横方向の半幅（XとZ）
static constexpr float PLAYER_HEIGHT = 1.80f;   // 体全体の高さ
static constexpr float EYE_HEIGHT    = 1.62f;   // カメラY = 足元Y + EYE_HEIGHT

// ─────────────────────────────────────────────────────────────────────────────
Player::Player() = default;

void Player::init(GLFWwindow* window) {
    input_.init(window);
}

// ─────────────────────────────────────────────────────────────────────────────
// overlapsAny() — 指定位置にプレイヤーAABBを置いたとき固体ブロックと重なるか判定
//
// px/py/pz: カメラ（目線）の座標。足元は py - EYE_HEIGHT。
// AABBが1つでも固体ブロックに重なれば true を返す。
//
// EPS（微小量）: ブロックの辺にぴったり触れたとき、浮動小数点誤差で
// 隣のブロックに少しはみ出す問題を防ぐための内側への余白。
// ─────────────────────────────────────────────────────────────────────────────
bool Player::overlapsAny(float px, float py, float pz,
                          const std::function<bool(int,int,int)>& isSolid) {
    static constexpr float EPS = 1e-4f;

    // AABBの足元と頭の高さを計算
    const float feet = py - EYE_HEIGHT;
    const float head = py + (PLAYER_HEIGHT - EYE_HEIGHT);

    // AABBが接触しうるブロック座標の範囲を計算
    const int x0 = static_cast<int>(std::floor(px - PLAYER_HALF_W + EPS));
    const int x1 = static_cast<int>(std::floor(px + PLAYER_HALF_W - EPS));
    const int y0 = static_cast<int>(std::floor(feet + EPS));
    const int y1 = static_cast<int>(std::floor(head - EPS));
    const int z0 = static_cast<int>(std::floor(pz - PLAYER_HALF_W + EPS));
    const int z1 = static_cast<int>(std::floor(pz + PLAYER_HALF_W - EPS));

    // 範囲内のブロックを全部チェック
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z)
                if (isSolid(x, y, z)) return true;
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// update() — 毎フレーム呼ばれるプレイヤー更新処理
//
// dt: 前フレームからの経過時間（秒）。移動量に掛けてフレームレートに依存しないようにする。
// isSolid: 座標(x,y,z)のブロックが固体かどうかを返す関数
// isWater: 座標(x,y,z)のブロックが水かどうかを返す関数
// ─────────────────────────────────────────────────────────────────────────────
void Player::update(float dt,
                    const std::function<bool(int,int,int)>& isSolid,
                    const std::function<bool(int,int,int)>& isWater) {
    input_.newFrame();    // 前フレームのクリック状態などをリセット
    glfwPollEvents();     // OSからキー・マウスイベントを受け取る

    // ── 高速モード切り替え（左Shiftシングルタップ） ──────────────────────────────
    {
        bool shift_down = input_.isHeld(GLFW_KEY_LEFT_SHIFT);
        if (shift_down && !shift_was_down_) fast_mode_ = !fast_mode_;
        shift_was_down_ = shift_down;
    }

    // ── 飛行モード切り替え（Spaceダブルタップ） ──────────────────────────────────
    // 0.3秒以内に2回Spaceを押すと飛行モードのON/OFFが切り替わる。
    {
        bool space_down = input_.isHeld(GLFW_KEY_SPACE);
        if (space_down && !space_was_down_) {
            space_tap_count_++;
            space_tap_timer_ = DBL_TAP_WINDOW;  // タイムウィンドウをリセット
            if (space_tap_count_ >= 2) {
                fly_mode_        = !fly_mode_;
                velocity_y_      = 0.0f;
                on_ground_       = false;
                space_tap_count_ = 0;
                space_tap_timer_ = 0.0f;
                fprintf(stderr, "[Player] %s mode\n", fly_mode_ ? "FLIGHT" : "NORMAL");
            }
        }
        space_was_down_ = space_down;

        // タイムウィンドウが切れたらタップカウントをリセット
        if (space_tap_timer_ > 0.0f) {
            space_tap_timer_ -= dt;
            if (space_tap_timer_ <= 0.0f) space_tap_count_ = 0;
        }
    }

    float speed = fast_mode_ ? PLAYER_SPEED_FAST : PLAYER_SPEED_NORMAL;

    // ── マウスで視点を操作 ────────────────────────────────────────────────────
    if (input_.isCursorCaptured()) {
        // マウスの移動量（ピクセル）× 感度 = 回転角度（度）
        float dx = input_.mouseDX() * SENSITIVITY;
        float dy = input_.mouseDY() * SENSITIVITY;
        camera_.setYaw  (camera_.getYaw()   + dx);
        camera_.setPitch(camera_.getPitch() - dy);  // Y軸は反転（上に動かすと見上げる）
    }

    glm::vec3 pos    = camera_.position();
    glm::vec3 front  = camera_.front();
    glm::vec3 right  = camera_.right();

    // 水平移動用のベクトル（ピッチを無視してY成分をゼロにする）。
    // カメラが上を向いていても水平方向にだけ移動するようにする。
    glm::vec3 hfront = glm::normalize(glm::vec3(front.x, 0.0f, front.z));
    glm::vec3 hright = glm::normalize(glm::vec3(right.x, 0.0f, right.z));

    // 目線位置（カメラ座標）のブロックが水かどうかで水中判定
    glm::ivec3 ipos = glm::ivec3(glm::floor(pos));
    in_water_ = isWater(ipos.x, ipos.y, ipos.z);

    // ─────────────────────────────────────────────────────────────────────────
    // 飛行モード: 重力なし・当たり判定なし。自由に空を飛べる。
    // ─────────────────────────────────────────────────────────────────────────
    if (fly_mode_) {
        if (input_.isHeld(GLFW_KEY_W)) pos += hfront * speed * dt;
        if (input_.isHeld(GLFW_KEY_S)) pos -= hfront * speed * dt;
        if (input_.isHeld(GLFW_KEY_D)) pos += hright * speed * dt;
        if (input_.isHeld(GLFW_KEY_A)) pos -= hright * speed * dt;
        if (input_.isHeld(GLFW_KEY_SPACE))        pos.y += speed * dt;
        if (input_.isHeld(GLFW_KEY_LEFT_CONTROL)) pos.y -= speed * dt;
        velocity_y_ = 0.0f;
        on_ground_  = false;

        if (pos.y < WORLD_BOTTOM) pos.y = WORLD_BOTTOM;
        camera_.setPosition(pos.x, pos.y, pos.z);
        return;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 水中モード: 弱い重力（沈む）+ Space/Ctrl で泳ぐ
    // ─────────────────────────────────────────────────────────────────────────
    if (in_water_) {
        float wsp = speed * WATER_SPEED_FACTOR;  // 水中は水平移動が遅い
        glm::vec3 dp(0.0f);
        if (input_.isHeld(GLFW_KEY_W)) dp += hfront * wsp * dt;
        if (input_.isHeld(GLFW_KEY_S)) dp -= hfront * wsp * dt;
        if (input_.isHeld(GLFW_KEY_D)) dp += hright * wsp * dt;
        if (input_.isHeld(GLFW_KEY_A)) dp -= hright * wsp * dt;

        if (input_.isHeld(GLFW_KEY_SPACE)) {
            // 水面（目線の1つ上が水でない）ならジャンプ速度で飛び出す。
            // 水中深くなら泳ぎ速度で上昇するだけ。
            glm::ivec3 eye = glm::ivec3(glm::floor(pos));
            bool at_surface = !isWater(eye.x, eye.y + 1, eye.z);
            velocity_y_ = at_surface ? JUMP_VELOCITY : WATER_SWIM_SPEED;
        } else if (input_.isHeld(GLFW_KEY_LEFT_CONTROL)) {
            velocity_y_ = -WATER_SWIM_SPEED;  // 下に泳ぐ
        } else {
            // 何もしないと少しずつ沈む（水中重力）
            velocity_y_ -= WATER_GRAVITY * dt;
            if (velocity_y_ < WATER_TERMINAL_VEL) velocity_y_ = WATER_TERMINAL_VEL;
        }
        on_ground_ = false;

        // AABB当たり判定をしながら移動（X/Z/Y の順で独立して解決）
        if (dp.x != 0.0f) {
            float nx = pos.x + dp.x;
            if (!overlapsAny(nx, pos.y, pos.z, isSolid)) pos.x = nx;
        }
        if (dp.z != 0.0f) {
            float nz = pos.z + dp.z;
            if (!overlapsAny(pos.x, pos.y, nz, isSolid)) pos.z = nz;
        }
        {
            float ny = pos.y + velocity_y_ * dt;
            if (!overlapsAny(pos.x, ny, pos.z, isSolid)) {
                pos.y = ny;
            } else {
                if (velocity_y_ < 0.0f) on_ground_ = true;  // 水底に着いた
                velocity_y_ = 0.0f;
            }
        }

        if (pos.y < WORLD_BOTTOM) { pos.y = WORLD_BOTTOM; velocity_y_ = 0.0f; }
        camera_.setPosition(pos.x, pos.y, pos.z);
        return;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 通常モード: 重力あり + AABB当たり判定
    // ─────────────────────────────────────────────────────────────────────────

    // 水平方向の移動量を計算（まだ実際には動かさない）
    glm::vec3 dp(0.0f);
    if (input_.isHeld(GLFW_KEY_W)) dp += hfront * speed * dt;
    if (input_.isHeld(GLFW_KEY_S)) dp -= hfront * speed * dt;
    if (input_.isHeld(GLFW_KEY_D)) dp += hright * speed * dt;
    if (input_.isHeld(GLFW_KEY_A)) dp -= hright * speed * dt;

    // 地面にいるときだけジャンプできる
    if (input_.isHeld(GLFW_KEY_SPACE) && on_ground_) {
        velocity_y_ = JUMP_VELOCITY;  // 上向きの初速度を与える
        on_ground_  = false;
    }

    // 毎フレーム重力を速度に加算する（上向き速度がだんだん減り、落下に転じる）
    velocity_y_ -= GRAVITY * dt;
    if (velocity_y_ < TERMINAL_VELOCITY) velocity_y_ = TERMINAL_VELOCITY;

    // ── X方向の当たり判定と移動 ───────────────────────────────────────────────
    // 「移動後の位置でAABBが固体ブロックと重なるか？」を試してから移動する。
    // 重なるなら動かない（壁に阻まれる）。
    if (dp.x != 0.0f) {
        float nx = pos.x + dp.x;
        if (!overlapsAny(nx, pos.y, pos.z, isSolid))
            pos.x = nx;
    }

    // ── Z方向の当たり判定と移動 ───────────────────────────────────────────────
    if (dp.z != 0.0f) {
        float nz = pos.z + dp.z;
        if (!overlapsAny(pos.x, pos.y, nz, isSolid))
            pos.z = nz;
    }

    // ── Y方向（重力・ジャンプ）の当たり判定と移動 ────────────────────────────
    {
        float ny = pos.y + velocity_y_ * dt;
        if (!overlapsAny(pos.x, ny, pos.z, isSolid)) {
            pos.y      = ny;
            on_ground_ = false;   // 空中にいる
        } else {
            // 床または天井に当たった
            if (velocity_y_ < 0.0f) on_ground_ = true;  // 落下中に当たった → 着地
            velocity_y_ = 0.0f;  // 速度をリセット
        }
    }

    if (pos.y < WORLD_BOTTOM) { pos.y = WORLD_BOTTOM; velocity_y_ = 0.0f; }
    camera_.setPosition(pos.x, pos.y, pos.z);
}

// プレイヤーが現在いるチャンク座標を返す
ChunkPos Player::chunkPos() const {
    glm::vec3 pos = camera_.position();
    int cx = static_cast<int>(std::floor(pos.x / CHUNK_SIZE_X));
    int cz = static_cast<int>(std::floor(pos.z / CHUNK_SIZE_Z));
    return {cx, cz};
}
