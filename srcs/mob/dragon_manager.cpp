// ─────────────────────────────────────────────────────────────────────────────
// dragon_manager.cpp — Phase A (旋回飛行) + Phase B (戦闘ロジック)
// ─────────────────────────────────────────────────────────────────────────────
#include "mob/dragon_manager.hpp"
#include <cmath>
#include <algorithm>
#include <limits>

void DragonManager::spawn(float wx, float wy, float wz) {
    if (dragon_.has_value()) return;

    EnderDragon d{};
    d.spawn_x = wx;
    d.spawn_y = wy + kSpawnHeightOffset;
    d.spawn_z = wz;
    // 初期位置: 旋回円周上の角度 0
    d.x = d.spawn_x + kOrbitRadius;
    d.y = d.spawn_y;
    d.z = d.spawn_z;
    d.yaw   = 90.0f;  // 旋回の接線方向 (+Z) を向く
    d.pitch = 0.0f;
    d.orbit_phase = 0.0f;
    d.health = DRAGON_MAX_HEALTH;
    d.alive  = true;
    d.state  = EnderDragon::State::Patrol;
    dragon_.emplace(d);
}

void DragonManager::despawn() {
    dragon_.reset();
}

float DragonManager::update(float dt, float px, float py, float pz) {
    if (!dragon_.has_value()) return 0.0f;
    EnderDragon& d = *dragon_;

    d.life_timer += dt;
    d.wing_phase += kWingFlapSpeed * dt;
    if (d.hit_flash_timer > 0.0f) d.hit_flash_timer -= dt;

    // ── 死亡演出: ゆっくり上昇 → DRAGON_DYING_DURATION 後に消える ────────────
    if (d.state == EnderDragon::State::Dying) {
        d.dying_timer += dt;
        d.y += 2.0f * dt;          // 2 ブロック/秒で上昇
        d.pitch = 45.0f;            // 機首を上げて昇天感
        if (d.dying_timer >= DRAGON_DYING_DURATION) {
            dragon_.reset();
        }
        return 0.0f;                // 死んでいる間は接触ダメージなし
    }

    // ── 旋回飛行 ────────────────────────────────────────────────────────────
    d.orbit_phase += kOrbitSpeed * dt;
    if (d.orbit_phase > 6.2831853f) d.orbit_phase -= 6.2831853f;

    const float new_x = d.spawn_x + std::cos(d.orbit_phase) * kOrbitRadius;
    const float new_z = d.spawn_z + std::sin(d.orbit_phase) * kOrbitRadius;
    const float new_y = d.spawn_y + std::sin(d.orbit_phase * 0.5f) * 1.5f;

    d.vx = (new_x - d.x) / dt;
    d.vy = (new_y - d.y) / dt;
    d.vz = (new_z - d.z) / dt;

    d.x = new_x;
    d.y = new_y;
    d.z = new_z;

    d.yaw = std::atan2(d.vz, d.vx) * (180.0f / 3.1415926535f);
    const float horiz = std::sqrt(d.vx * d.vx + d.vz * d.vz);
    d.pitch = std::atan2(d.vy, horiz) * (180.0f / 3.1415926535f);

    // ── 接触ダメージ判定 ───────────────────────────────────────────────────
    // 胴体軸 (頭〜尾) に沿った最近接点までの距離で判定する。
    if (distanceToBody(px, py, pz) < DRAGON_CONTACT_RADIUS) {
        return DRAGON_CONTACT_DAMAGE_PER_SEC * dt;
    }
    return 0.0f;
}

float DragonManager::distanceToBody(float px, float py, float pz) const {
    if (!dragon_.has_value()) return std::numeric_limits<float>::max();
    const EnderDragon& d = *dragon_;

    // 胴体軸 = world XZ 平面で yaw 方向に伸びる線分。
    //   軸範囲: along ∈ [-DRAGON_AXIS_BACK, +DRAGON_AXIS_FRONT]
    const float yaw_rad = d.yaw * (3.1415926535f / 180.0f);
    const float fx = std::cos(yaw_rad);
    const float fz = std::sin(yaw_rad);

    const float ex = px - d.x;
    const float ez = pz - d.z;
    float along = ex * fx + ez * fz;
    along = std::clamp(along, -DRAGON_AXIS_BACK, DRAGON_AXIS_FRONT);

    const float cx = d.x + fx * along;   // 軸上の最近接点
    const float cz = d.z + fz * along;
    const float dx = px - cx;
    const float dy = py - d.y;             // 軸は水平とみなす
    const float dz = pz - cz;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void DragonManager::applyDamage(float amount) {
    if (!dragon_.has_value()) return;
    EnderDragon& d = *dragon_;
    if (d.state == EnderDragon::State::Dying) return;
    d.health -= amount;
    d.hit_flash_timer = 0.15f;
    if (d.health <= 0.0f) {
        d.health = 0.0f;
        d.state = EnderDragon::State::Dying;
        d.dying_timer = 0.0f;
        // 上昇開始のため水平速度をゼロ化
        d.vx = 0.0f;
        d.vz = 0.0f;
    }
}

DragonManager::NetState DragonManager::snapshot() const {
    NetState s;
    if (!dragon_.has_value()) return s;
    const EnderDragon& d = *dragon_;
    s.exists     = true;
    s.state      = d.state;
    s.x          = d.x;
    s.y          = d.y;
    s.z          = d.z;
    s.yaw        = d.yaw;
    s.pitch      = d.pitch;
    s.wing_phase = d.wing_phase;
    s.health     = d.health;
    return s;
}

void DragonManager::applyNetState(const NetState& s) {
    if (!s.exists) {
        dragon_.reset();
        return;
    }
    // 必要なら "空の" ドラゴンを生成 (spawn_x/y/z は推測値を入れておく)。
    if (!dragon_.has_value()) {
        EnderDragon d{};
        d.spawn_x = s.x;
        d.spawn_y = s.y;
        d.spawn_z = s.z;
        d.alive = true;
        dragon_.emplace(d);
    }
    EnderDragon& d = *dragon_;
    d.state      = s.state;
    d.x          = s.x;
    d.y          = s.y;
    d.z          = s.z;
    d.yaw        = s.yaw;
    d.pitch      = s.pitch;
    d.wing_phase = s.wing_phase;
    d.health     = s.health;
}

bool DragonManager::playerMeleeAttack(float px, float py, float pz,
                                        float front_x, float front_y, float front_z) {
    if (!dragon_.has_value()) return false;
    EnderDragon& d = *dragon_;
    if (d.state == EnderDragon::State::Dying) return false;

    // 胴体軸上の最近接点を再計算してそこへの方向で判定する。
    // (distanceToBody は距離だけ返すので、ここでは座標計算をローカルで実施)
    const float yaw_rad = d.yaw * (3.1415926535f / 180.0f);
    const float fx = std::cos(yaw_rad);
    const float fz = std::sin(yaw_rad);
    const float ex = px - d.x;
    const float ez = pz - d.z;
    float along = ex * fx + ez * fz;
    along = std::clamp(along, -DRAGON_AXIS_BACK, DRAGON_AXIS_FRONT);
    const float cx = d.x + fx * along;
    const float cz = d.z + fz * along;

    // 軸上の最近接点へのベクトル
    const float dx = cx - px;
    const float dy = d.y - py;
    const float dz = cz - pz;
    const float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
    // リーチ = プレイヤーの腕の長さ + ドラゴンの胴体厚み (HIT_RADIUS)
    if (dist > DRAGON_MELEE_RANGE + DRAGON_HIT_RADIUS) return false;

    if (dist < 1e-3f) {
        // 体に埋まっている: 前方判定不要で強制ヒット
        applyDamage(DRAGON_MELEE_DAMAGE);
        return true;
    }
    // 視線方向との内積で「前方半球」判定 (dot > 0.5 ≒ 60°円錐)
    const float inv = 1.0f / dist;
    const float dot = (dx * front_x + dy * front_y + dz * front_z) * inv;
    if (dot < 0.5f) return false;

    applyDamage(DRAGON_MELEE_DAMAGE);
    return true;
}
