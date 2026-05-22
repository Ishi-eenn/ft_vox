#pragma once
#include <cstdint>

struct Zombie {
    // Feet position (y = bottom of body)
    float x = 0, y = 0, z = 0;
    float yaw = 0;           // facing angle (degrees, same convention as Camera)
    float velocity_y = 0;
    bool  on_ground  = false;
    float health     = 20.0f;
    float walk_phase = 0.0f;
    float attack_cooldown = 0.0f;

    enum class State : uint8_t { Idle, Chase, Attack } state = State::Idle;
    float state_timer  = 0.0f;

    // Idle wandering
    float wander_yaw   = 0.0f;
    float wander_timer = 0.0f;

    bool alive() const { return health > 0.0f; }
};
