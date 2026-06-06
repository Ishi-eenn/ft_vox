#include "mob/arrow_manager.hpp"
#include "mob/dragon_manager.hpp"
#include <algorithm>
#include <cmath>

static constexpr float ARROW_GRAVITY = 9.8f;
static constexpr float DRAG = 0.99f;
static constexpr int   SUBSTEPS = 4;            // 高速時の貫通防止

void ArrowManager::spawn(float x, float y, float z,
                          float vx, float vy, float vz) {
    Arrow a;
    a.x  = x;  a.y  = y;  a.z  = z;
    a.vx = vx; a.vy = vy; a.vz = vz;
    arrows_.push_back(a);
}

void ArrowManager::update(float dt,
                           const std::function<bool(int,int,int)>& isSolid,
                           std::vector<Zombie>& zombies,
                           DragonManager* dragon_mgr) {
    if (arrows_.empty()) return;

    const float sub_dt = dt / static_cast<float>(SUBSTEPS);

    for (Arrow& a : arrows_) {
        if (!a.alive) continue;

        a.lifetime += dt;
        // 刺さった矢は STUCK_LIFETIME 後に消える
        if (a.stuck) {
            if (a.lifetime > STUCK_LIFETIME) a.alive = false;
            continue;
        }
        if (a.lifetime > MAX_LIFETIME) {
            a.alive = false;
            continue;
        }

        for (int s = 0; s < SUBSTEPS && a.alive && !a.stuck; ++s) {
            // 重力 + 空気抵抗
            a.vy -= ARROW_GRAVITY * sub_dt;
            a.vx *= std::pow(DRAG, sub_dt * 60.0f);
            a.vy *= std::pow(DRAG, sub_dt * 60.0f);
            a.vz *= std::pow(DRAG, sub_dt * 60.0f);

            const float nx = a.x + a.vx * sub_dt;
            const float ny = a.y + a.vy * sub_dt;
            const float nz = a.z + a.vz * sub_dt;

            // ブロック衝突: 矢の先端が固体ブロックに入ったら刺さる
            const int bx = static_cast<int>(std::floor(nx));
            const int by = static_cast<int>(std::floor(ny));
            const int bz = static_cast<int>(std::floor(nz));
            if (isSolid(bx, by, bz)) {
                a.stuck = true;
                a.lifetime = 0.0f;
                // 速度は残しておく（レンダリング時の向きに使う）
                // 位置はブロック面ギリギリに留める
                a.x = nx; a.y = ny; a.z = nz;
                break;
            }

            // ドラゴン命中判定（胴体軸カプセル、ゾンビより優先）
            if (dragon_mgr && dragon_mgr->exists()) {
                const EnderDragon* dr = dragon_mgr->dragon();
                if (dr && dr->state != EnderDragon::State::Dying) {
                    if (dragon_mgr->distanceToBody(nx, ny, nz) < DRAGON_HIT_RADIUS) {
                        dragon_mgr->applyDamage(DRAGON_ARROW_DAMAGE);
                        a.alive = false;
                        break;
                    }
                }
            }

            // ゾンビ命中判定（球状）
            int hit_idx = -1;
            float best_d2 = HIT_RADIUS * HIT_RADIUS;
            for (int i = 0; i < static_cast<int>(zombies.size()); ++i) {
                Zombie& z = zombies[i];
                if (!z.alive()) continue;
                // ゾンビの中心 = (x, y + 0.9, z)（高さ1.8の中央）
                const float dx = nx - z.x;
                const float dy = ny - (z.y + 0.9f);
                const float dz = nz - z.z;
                const float d2 = dx*dx + dy*dy + dz*dz;
                if (d2 < best_d2) {
                    best_d2 = d2;
                    hit_idx = i;
                }
            }
            if (hit_idx >= 0) {
                zombies[hit_idx].health -= ARROW_DAMAGE;
                a.alive = false;
                break;
            }

            a.x = nx; a.y = ny; a.z = nz;
        }
    }

    arrows_.erase(
        std::remove_if(arrows_.begin(), arrows_.end(),
                       [](const Arrow& a) { return !a.alive; }),
        arrows_.end());
}
