#include "player/player.hpp"
#include <cstdio>
#include <cmath>

// Mouse sensitivity: degrees per pixel
static constexpr float SENSITIVITY = 0.1f;

Player::Player() = default;

void Player::init(GLFWwindow* window) {
    input_.init(window);
}

void Player::update(float dt) {
    input_.newFrame();
    glfwPollEvents();

    bool f_down = input_.isHeld(GLFW_KEY_LEFT_SHIFT);
    if (f_down && !f_was_down_) fast_mode_ = !fast_mode_;
    f_was_down_ = f_down;

    float speed = fast_mode_ ? PLAYER_SPEED_FAST : PLAYER_SPEED_NORMAL;

    // ─── Mouse look (only when cursor is captured) ────────────────────────
    if (input_.isCursorCaptured()) {
        float dx = input_.mouseDX() * SENSITIVITY;
        float dy = input_.mouseDY() * SENSITIVITY;
        camera_.setYaw  (camera_.getYaw()   + dx);
        camera_.setPitch(camera_.getPitch() - dy);
    }

    // ─── Keyboard movement (WASD, horizontal only, ignore pitch) ─────────
    glm::vec3 pos    = camera_.position();
    glm::vec3 front  = camera_.front();
    glm::vec3 right  = camera_.right();

    // Project front to XZ plane so vertical look doesn't affect horizontal move
    glm::vec3 hfront = glm::normalize(glm::vec3(front.x, 0.0f, front.z));
    glm::vec3 hright = glm::normalize(glm::vec3(right.x, 0.0f, right.z));

    if (input_.isHeld(GLFW_KEY_W)) pos += hfront * speed * dt;
    if (input_.isHeld(GLFW_KEY_S)) pos -= hfront * speed * dt;
    if (input_.isHeld(GLFW_KEY_D)) pos += hright * speed * dt;
    if (input_.isHeld(GLFW_KEY_A)) pos -= hright * speed * dt;

    // Vertical movement: Space = up, Left Control = down
    if (input_.isHeld(GLFW_KEY_SPACE))        pos.y += speed * dt;
    if (input_.isHeld(GLFW_KEY_LEFT_CONTROL)) pos.y -= speed * dt;

    camera_.setPosition(pos.x, pos.y, pos.z);
}

ChunkPos Player::chunkPos() const {
    glm::vec3 pos = camera_.position();
    int cx = (int)std::floor(pos.x / CHUNK_SIZE_X);
    int cz = (int)std::floor(pos.z / CHUNK_SIZE_Z);
    return {cx, cz};
}
