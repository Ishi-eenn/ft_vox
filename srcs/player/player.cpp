#include "player/player.hpp"
#include <cstdio>
#include <cmath>

// ─── Tuning constants ─────────────────────────────────────────────────────────
static constexpr float SENSITIVITY       = 0.1f;
static constexpr float GRAVITY           = 28.0f;
static constexpr float JUMP_VELOCITY     = 9.0f;
static constexpr float TERMINAL_VELOCITY = -50.0f;
static constexpr float DBL_TAP_WINDOW    = 0.30f;

// Water physics
static constexpr float WATER_GRAVITY      = 6.0f;   // gentle sinking force
static constexpr float WATER_TERMINAL_VEL = -3.0f;  // slow sink cap
static constexpr float WATER_SWIM_SPEED   = 4.5f;   // Space / Ctrl vertical swim speed
static constexpr float WATER_SPEED_FACTOR = 0.5f;   // horizontal speed multiplier

// ─── Player AABB ──────────────────────────────────────────────────────────────
// Minecraft standard: 0.6 wide × 1.8 tall, eyes at 1.62 from feet.
static constexpr float PLAYER_HALF_W = 0.30f;   // half-width (X and Z)
static constexpr float PLAYER_HEIGHT = 1.80f;
static constexpr float EYE_HEIGHT    = 1.62f;   // camera Y offset above feet

// ─────────────────────────────────────────────────────────────────────────────
Player::Player() = default;

void Player::init(GLFWwindow* window) {
    input_.init(window);
}

// Returns true if the player AABB centred at (px, py, pz) (camera coords)
// overlaps any solid block according to isSolid.
bool Player::overlapsAny(float px, float py, float pz,
                          const std::function<bool(int,int,int)>& isSolid) {
    // A tiny inward epsilon keeps the player from "sticking" at exact block edges.
    static constexpr float EPS = 1e-4f;

    const float feet = py - EYE_HEIGHT;
    const float head = py + (PLAYER_HEIGHT - EYE_HEIGHT);

    const int x0 = static_cast<int>(std::floor(px - PLAYER_HALF_W + EPS));
    const int x1 = static_cast<int>(std::floor(px + PLAYER_HALF_W - EPS));
    const int y0 = static_cast<int>(std::floor(feet + EPS));
    const int y1 = static_cast<int>(std::floor(head - EPS));
    const int z0 = static_cast<int>(std::floor(pz - PLAYER_HALF_W + EPS));
    const int z1 = static_cast<int>(std::floor(pz + PLAYER_HALF_W - EPS));

    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z)
                if (isSolid(x, y, z)) return true;
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
void Player::update(float dt,
                    const std::function<bool(int,int,int)>& isSolid,
                    const std::function<bool(int,int,int)>& isWater) {
    input_.newFrame();
    glfwPollEvents();

    // ── Fast mode: single-tap Left Shift ──────────────────────────────────────
    {
        bool shift_down = input_.isHeld(GLFW_KEY_LEFT_SHIFT);
        if (shift_down && !shift_was_down_) fast_mode_ = !fast_mode_;
        shift_was_down_ = shift_down;
    }

    // ── Flight toggle: double-tap Space ───────────────────────────────────────
    {
        bool space_down = input_.isHeld(GLFW_KEY_SPACE);
        if (space_down && !space_was_down_) {
            space_tap_count_++;
            space_tap_timer_ = DBL_TAP_WINDOW;
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

        if (space_tap_timer_ > 0.0f) {
            space_tap_timer_ -= dt;
            if (space_tap_timer_ <= 0.0f) space_tap_count_ = 0;
        }
    }

    float speed = fast_mode_ ? PLAYER_SPEED_FAST : PLAYER_SPEED_NORMAL;

    // ── Mouse look ────────────────────────────────────────────────────────────
    if (input_.isCursorCaptured()) {
        float dx = input_.mouseDX() * SENSITIVITY;
        float dy = input_.mouseDY() * SENSITIVITY;
        camera_.setYaw  (camera_.getYaw()   + dx);
        camera_.setPitch(camera_.getPitch() - dy);
    }

    glm::vec3 pos    = camera_.position();
    glm::vec3 front  = camera_.front();
    glm::vec3 right  = camera_.right();

    // Horizontal basis vectors (ignore camera pitch for movement)
    glm::vec3 hfront = glm::normalize(glm::vec3(front.x, 0.0f, front.z));
    glm::vec3 hright = glm::normalize(glm::vec3(right.x, 0.0f, right.z));

    // Detect water at eye level (camera pos = eye pos)
    in_water_ = isWater((int)std::floor(pos.x), (int)std::floor(pos.y), (int)std::floor(pos.z));

    if (fly_mode_) {
        // ── Creative flight: no gravity, no collision ─────────────────────────
        if (input_.isHeld(GLFW_KEY_W)) pos += hfront * speed * dt;
        if (input_.isHeld(GLFW_KEY_S)) pos -= hfront * speed * dt;
        if (input_.isHeld(GLFW_KEY_D)) pos += hright * speed * dt;
        if (input_.isHeld(GLFW_KEY_A)) pos -= hright * speed * dt;
        if (input_.isHeld(GLFW_KEY_SPACE))        pos.y += speed * dt;
        if (input_.isHeld(GLFW_KEY_LEFT_CONTROL)) pos.y -= speed * dt;
        velocity_y_ = 0.0f;
        on_ground_  = false;

        camera_.setPosition(pos.x, pos.y, pos.z);
        return;
    }

    if (in_water_) {
        // ── Underwater: buoyancy + swim controls ──────────────────────────────
        float wsp = speed * WATER_SPEED_FACTOR;
        glm::vec3 dp(0.0f);
        if (input_.isHeld(GLFW_KEY_W)) dp += hfront * wsp * dt;
        if (input_.isHeld(GLFW_KEY_S)) dp -= hfront * wsp * dt;
        if (input_.isHeld(GLFW_KEY_D)) dp += hright * wsp * dt;
        if (input_.isHeld(GLFW_KEY_A)) dp -= hright * wsp * dt;

        if (input_.isHeld(GLFW_KEY_SPACE)) {
            // At the water surface (block above eye is not water): jump out with full velocity
            int eye_x = (int)std::floor(pos.x);
            int eye_y = (int)std::floor(pos.y);
            int eye_z = (int)std::floor(pos.z);
            bool at_surface = !isWater(eye_x, eye_y + 1, eye_z);
            velocity_y_ = at_surface ? JUMP_VELOCITY : WATER_SWIM_SPEED;
        } else if (input_.isHeld(GLFW_KEY_LEFT_CONTROL)) {
            velocity_y_ = -WATER_SWIM_SPEED;
        } else {
            velocity_y_ -= WATER_GRAVITY * dt;
            if (velocity_y_ < WATER_TERMINAL_VEL) velocity_y_ = WATER_TERMINAL_VEL;
        }
        on_ground_ = false;

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
                if (velocity_y_ < 0.0f) on_ground_ = true;
                velocity_y_ = 0.0f;
            }
        }

        camera_.setPosition(pos.x, pos.y, pos.z);
        return;
    }

    // ── Normal mode: gravity + AABB collision ─────────────────────────────────

    glm::vec3 dp(0.0f);
    if (input_.isHeld(GLFW_KEY_W)) dp += hfront * speed * dt;
    if (input_.isHeld(GLFW_KEY_S)) dp -= hfront * speed * dt;
    if (input_.isHeld(GLFW_KEY_D)) dp += hright * speed * dt;
    if (input_.isHeld(GLFW_KEY_A)) dp -= hright * speed * dt;

    // Jump (only when on solid ground)
    if (input_.isHeld(GLFW_KEY_SPACE) && on_ground_) {
        velocity_y_ = JUMP_VELOCITY;
        on_ground_  = false;
    }

    // Gravity
    velocity_y_ -= GRAVITY * dt;
    if (velocity_y_ < TERMINAL_VELOCITY) velocity_y_ = TERMINAL_VELOCITY;

    // ── Resolve X ─────────────────────────────────────────────────────────────
    if (dp.x != 0.0f) {
        float nx = pos.x + dp.x;
        if (!overlapsAny(nx, pos.y, pos.z, isSolid))
            pos.x = nx;
    }

    // ── Resolve Z ─────────────────────────────────────────────────────────────
    if (dp.z != 0.0f) {
        float nz = pos.z + dp.z;
        if (!overlapsAny(pos.x, pos.y, nz, isSolid))
            pos.z = nz;
    }

    // ── Resolve Y (gravity / jump) ────────────────────────────────────────────
    {
        float ny = pos.y + velocity_y_ * dt;
        if (!overlapsAny(pos.x, ny, pos.z, isSolid)) {
            pos.y      = ny;
            on_ground_ = false;
        } else {
            if (velocity_y_ < 0.0f) on_ground_ = true;
            velocity_y_ = 0.0f;
        }
    }

    camera_.setPosition(pos.x, pos.y, pos.z);
}

// ─────────────────────────────────────────────────────────────────────────────
ChunkPos Player::chunkPos() const {
    glm::vec3 pos = camera_.position();
    int cx = static_cast<int>(std::floor(pos.x / CHUNK_SIZE_X));
    int cz = static_cast<int>(std::floor(pos.z / CHUNK_SIZE_Z));
    return {cx, cz};
}
