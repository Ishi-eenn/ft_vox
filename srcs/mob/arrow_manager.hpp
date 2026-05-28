#pragma once
#include "mob/arrow.hpp"
#include "mob/zombie.hpp"
#include <functional>
#include <vector>

class ArrowManager {
public:
    void spawn(float x, float y, float z,
               float vx, float vy, float vz);

    // 全矢を更新。命中したゾンビにダメージを与える。
    void update(float dt,
                const std::function<bool(int,int,int)>& isSolid,
                std::vector<Zombie>& zombies);

    const std::vector<Arrow>& arrows() const { return arrows_; }

private:
    std::vector<Arrow> arrows_;

    static constexpr float MAX_LIFETIME = 10.0f;
    static constexpr float STUCK_LIFETIME = 4.0f;   // 刺さってから消えるまで
    static constexpr float ARROW_DAMAGE = 100.0f;  // 確実に一撃で倒す
    static constexpr float HIT_RADIUS = 0.55f;
};
