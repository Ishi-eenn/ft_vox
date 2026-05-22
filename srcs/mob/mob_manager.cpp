#include "mob/mob_manager.hpp"
#include "world/world.hpp"
#include "types.hpp"
#include <cmath>
#include <cstdlib>
#include <algorithm>

// ── Physics constants ─────────────────────────────────────────────────────────
static constexpr float GRAVITY        = 28.0f;
static constexpr float TERMINAL_VEL  = -50.0f;
static constexpr float ZOMBIE_HALF_W = 0.30f;
static constexpr float ZOMBIE_HEIGHT = 1.80f;

// ── AI constants ──────────────────────────────────────────────────────────────
static constexpr float ZOMBIE_SPEED   = 0.9f;   // blocks/sec
static constexpr float DETECT_RANGE  = 16.0f;  // start chasing
static constexpr float LOSE_RANGE    = 26.0f;  // give up chasing
static constexpr float ATTACK_RANGE  = 1.5f;   // deal damage
static constexpr float ATTACK_DAMAGE = 2.0f;   // HP per hit
static constexpr float ATTACK_PERIOD = 1.0f;   // seconds between hits
static constexpr float PLAYER_EYE_H  = 1.62f;

// ── AABB collision ────────────────────────────────────────────────────────────
bool MobManager::overlapsAny(float x, float y, float z,
                               const std::function<bool(int,int,int)>& isSolid) {
    static constexpr float EPS = 1e-4f;
    const int x0 = (int)std::floor(x - ZOMBIE_HALF_W + EPS);
    const int x1 = (int)std::floor(x + ZOMBIE_HALF_W - EPS);
    const int y0 = (int)std::floor(y + EPS);
    const int y1 = (int)std::floor(y + ZOMBIE_HEIGHT - EPS);
    const int z0 = (int)std::floor(z - ZOMBIE_HALF_W + EPS);
    const int z1 = (int)std::floor(z + ZOMBIE_HALF_W - EPS);
    for (int bx = x0; bx <= x1; ++bx)
        for (int by = y0; by <= y1; ++by)
            for (int bz = z0; bz <= z1; ++bz)
                if (isSolid(bx, by, bz)) return true;
    return false;
}

// Find the y-coordinate of the highest solid block at (x,z), returns -1 if none.
int MobManager::findGroundY(float x, float z, const World& world) const {
    const int bx = (int)std::floor(x);
    const int bz = (int)std::floor(z);
    for (int y = 200; y >= 0; --y) {
        BlockType bt = world.getWorldBlock(bx, y, bz);
        if (bt != BlockType::Air && bt != BlockType::Water)
            return y + 1;
    }
    return -1;
}

// ── Per-zombie update ─────────────────────────────────────────────────────────
void MobManager::updateZombie(Zombie& z, float dt,
                               float px, float py, float pz,
                               const std::function<bool(int,int,int)>& isSolid) {
    // Player feet position
    const float pfx = px;
    const float pfy = py - PLAYER_EYE_H;
    const float pfz = pz;

    const float dx     = pfx - z.x;
    const float dz     = pfz - z.z;
    const float horiz  = std::sqrt(dx * dx + dz * dz);

    // ── State transitions ─────────────────────────────────────────────────
    z.state_timer += dt;
    switch (z.state) {
    case Zombie::State::Idle:
        if (horiz < DETECT_RANGE) {
            z.state = Zombie::State::Chase;
            z.state_timer = 0;
        }
        break;
    case Zombie::State::Chase:
        if (horiz > LOSE_RANGE) {
            z.state = Zombie::State::Idle;
        } else if (horiz < ATTACK_RANGE) {
            z.state = Zombie::State::Attack;
            z.state_timer = 0;
        }
        break;
    case Zombie::State::Attack:
        if (horiz > ATTACK_RANGE * 1.6f)
            z.state = Zombie::State::Chase;
        break;
    }

    // ── Horizontal movement ───────────────────────────────────────────────
    float move_x = 0, move_z = 0;
    if (z.state == Zombie::State::Chase || z.state == Zombie::State::Attack) {
        if (horiz > 0.02f) {
            const float inv = 1.0f / horiz;
            move_x = dx * inv * ZOMBIE_SPEED * dt;
            move_z = dz * inv * ZOMBIE_SPEED * dt;
            z.yaw  = std::atan2(dz, dx) * (180.0f / 3.14159f);
        }
    } else {
        // Idle: wander slowly
        z.wander_timer -= dt;
        if (z.wander_timer <= 0) {
            z.wander_timer = 2.0f + ((float)rand() / RAND_MAX) * 2.0f;
            z.wander_yaw  += -60.0f + ((float)rand() / RAND_MAX) * 120.0f;
        }
        const float wr = z.wander_yaw * (3.14159f / 180.0f);
        move_x = std::cos(wr) * ZOMBIE_SPEED * 0.35f * dt;
        move_z = std::sin(wr) * ZOMBIE_SPEED * 0.35f * dt;
        z.yaw  = z.wander_yaw;
    }

    // ── Gravity ───────────────────────────────────────────────────────────
    z.velocity_y -= GRAVITY * dt;
    if (z.velocity_y < TERMINAL_VEL) z.velocity_y = TERMINAL_VEL;

    // ── AABB: X ───────────────────────────────────────────────────────────
    if (move_x != 0 && !overlapsAny(z.x + move_x, z.y, z.z, isSolid))
        z.x += move_x;

    // ── AABB: Z ───────────────────────────────────────────────────────────
    if (move_z != 0 && !overlapsAny(z.x, z.y, z.z + move_z, isSolid))
        z.z += move_z;

    // ── AABB: Y ───────────────────────────────────────────────────────────
    {
        const float ny = z.y + z.velocity_y * dt;
        if (!overlapsAny(z.x, ny, z.z, isSolid)) {
            z.y        = ny;
            z.on_ground = false;
        } else {
            if (z.velocity_y < 0) z.on_ground = true;
            z.velocity_y = 0;
        }
    }

    // ── Walk animation ────────────────────────────────────────────────────
    const bool moving = (move_x != 0 || move_z != 0) && z.on_ground;
    const float spd   = (z.state == Zombie::State::Idle) ? 0.4f : 1.0f;
    if (moving) z.walk_phase += dt * 7.0f * spd;

    // ── Attack cooldown ───────────────────────────────────────────────────
    if (z.attack_cooldown > 0) z.attack_cooldown -= dt;
}

// ── Spawning ──────────────────────────────────────────────────────────────────
void MobManager::trySpawn(float px, float pz,
                           const World& world, float time_of_day) {
    if ((int)zombies_.size() >= MAX_ZOMBIES) return;

    // Spawn at night: time_of_day wraps 0=midnight, 0.5=noon
    const bool is_night = (time_of_day > 0.75f || time_of_day < 0.25f);
    if (!is_night) return;

    const float angle = ((float)rand() / RAND_MAX) * 6.28318f;
    const float dist  = 12.0f + ((float)rand() / RAND_MAX) * 10.0f;
    const float sx = px + std::cos(angle) * dist;
    const float sz = pz + std::sin(angle) * dist;

    const int sy = findGroundY(sx, sz, world);
    if (sy < 0) return;

    Zombie z;
    z.x          = sx;
    z.y          = (float)sy;
    z.z          = sz;
    z.yaw        = angle * (180.0f / 3.14159f);
    z.wander_yaw = z.yaw;
    zombies_.push_back(z);
}

// ── Public API ────────────────────────────────────────────────────────────────
float MobManager::update(float dt,
                          float px, float py, float pz,
                          float time_of_day,
                          const std::function<bool(int,int,int)>& isSolid,
                          const World& world) {
    spawn_timer_ -= dt;
    if (spawn_timer_ <= 0) {
        spawn_timer_ = SPAWN_INTERVAL;
        trySpawn(px, pz, world, time_of_day);
    }

    float damage = 0;
    for (auto& z : zombies_) {
        if (!z.alive()) continue;
        updateZombie(z, dt, px, py, pz, isSolid);
        if (z.state == Zombie::State::Attack && z.attack_cooldown <= 0) {
            damage += ATTACK_DAMAGE;
            z.attack_cooldown = ATTACK_PERIOD;
        }
    }

    zombies_.erase(
        std::remove_if(zombies_.begin(), zombies_.end(),
                       [](const Zombie& z) { return !z.alive(); }),
        zombies_.end());

    return damage;
}

int MobManager::playerMeleeAttack(float px, float py, float pz,
                                   float front_x, float front_z) {
    static constexpr float MELEE_RANGE  = 4.0f;
    static constexpr float MELEE_DAMAGE = 5.0f;

    const float pfx = px, pfy = py - PLAYER_EYE_H, pfz = pz;
    float  best_dist = MELEE_RANGE;
    int    best_idx  = -1;

    for (int i = 0; i < (int)zombies_.size(); ++i) {
        const Zombie& z = zombies_[i];
        if (!z.alive()) continue;
        const float dx = z.x - pfx, dz = z.z - pfz;
        const float dist = std::sqrt(dx * dx + dz * dz);
        if (dist >= best_dist) continue;
        // Must be roughly in front of the player
        const float dot = dx * front_x + dz * front_z;
        if (dot < 0) continue;
        best_dist = dist;
        best_idx  = i;
    }

    if (best_idx >= 0) {
        zombies_[best_idx].health -= MELEE_DAMAGE;
    }
    return best_idx;
}
