// ─────────────────────────────────────────────────────────────────────────────
// dragon_manager.cpp — 飛行 AI (旋回/チャージ/復帰) + 戦闘 + ファイアボール
// ─────────────────────────────────────────────────────────────────────────────
#include "mob/dragon_manager.hpp"
#include <cmath>
#include <algorithm>
#include <limits>

namespace {
constexpr float kPi = 3.1415926535f;
}

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
    if (d.hit_flash_timer > 0.0f)   d.hit_flash_timer   -= dt;
    if (d.charge_cooldown > 0.0f)   d.charge_cooldown   -= dt;
    if (d.fireball_cooldown > 0.0f) d.fireball_cooldown -= dt;

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

    // 速度を目標方向へ滑らかに寄せるステアリング (Charge / Retreat 共用)
    auto steerTowards = [&](float tx, float ty, float tz, float speed) {
        const float ex = tx - d.x, ey = ty - d.y, ez = tz - d.z;
        const float len = std::sqrt(ex*ex + ey*ey + ez*ez);
        if (len > 1e-4f) {
            const float k = std::min(1.0f, DRAGON_CHARGE_TURN * dt);
            d.vx += (ex / len * speed - d.vx) * k;
            d.vy += (ey / len * speed - d.vy) * k;
            d.vz += (ez / len * speed - d.vz) * k;
        }
        d.x += d.vx * dt;
        d.y += d.vy * dt;
        d.z += d.vz * dt;
        d.yaw = std::atan2(d.vz, d.vx) * (180.0f / kPi);
        const float horiz = std::sqrt(d.vx * d.vx + d.vz * d.vz);
        d.pitch = std::atan2(d.vy, horiz) * (180.0f / kPi);
        return len;
    };

    // ── チャージ突進: プレイヤーへ急降下し、体当たりの一撃を狙う ──────────
    if (d.state == EnderDragon::State::Charge) {
        d.charge_timer += dt;
        const float dist = steerTowards(px, py, pz, DRAGON_CHARGE_SPEED);

        if (distanceToBody(px, py, pz) < DRAGON_CONTACT_RADIUS) {
            // 命中: 一撃ダメージを与えて離脱
            d.state = EnderDragon::State::Retreat;
            return DRAGON_CHARGE_DAMAGE;
        }
        if (dist < 2.0f || d.charge_timer > DRAGON_CHARGE_TIMEOUT) {
            d.state = EnderDragon::State::Retreat;  // すれ違った / 諦め
        }
        return 0.0f;
    }

    // ── 復帰: 旋回円上の最寄り点へ戻り、Patrol を再開する ─────────────────
    if (d.state == EnderDragon::State::Retreat) {
        const float ang = std::atan2(d.z - d.spawn_z, d.x - d.spawn_x);
        const float tx  = d.spawn_x + std::cos(ang) * kOrbitRadius;
        const float tz  = d.spawn_z + std::sin(ang) * kOrbitRadius;
        const float dist = steerTowards(tx, d.spawn_y, tz, DRAGON_RETREAT_SPEED);

        if (dist < 2.0f) {
            // 現在角度から旋回を再開 (位置が連続するように位相を合わせる)
            d.orbit_phase = ang;
            d.state = EnderDragon::State::Patrol;
            d.charge_cooldown = DRAGON_CHARGE_COOLDOWN;
        }
        // 復帰中も接触していればダメージ
        if (distanceToBody(px, py, pz) < DRAGON_CONTACT_RADIUS)
            return DRAGON_CONTACT_DAMAGE_PER_SEC * dt;
        return 0.0f;
    }

    // ── 旋回飛行 (Patrol) ───────────────────────────────────────────────────
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

    d.yaw = std::atan2(d.vz, d.vx) * (180.0f / kPi);
    const float horiz = std::sqrt(d.vx * d.vx + d.vz * d.vz);
    d.pitch = std::atan2(d.vy, horiz) * (180.0f / kPi);

    // 攻撃判断 (チャージ開始 / ファイアボール発射)
    tryAttacks(d, px, py, pz);

    // ── 接触ダメージ判定 ───────────────────────────────────────────────────
    // 胴体軸 (頭〜尾) に沿った最近接点までの距離で判定する。
    if (distanceToBody(px, py, pz) < DRAGON_CONTACT_RADIUS) {
        return DRAGON_CONTACT_DAMAGE_PER_SEC * dt;
    }
    return 0.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// tryAttacks() — Patrol 中の攻撃判断 (ホストのみ)
//   ・チャージ: クールダウン明けに射程内のプレイヤーへ突進開始
//   ・ファイアボール: 口の位置からプレイヤーを狙って発射。
//     頭がプレイヤー側を向いているときのみ (背後には撃てない)。
// ─────────────────────────────────────────────────────────────────────────────
void DragonManager::tryAttacks(EnderDragon& d, float px, float py, float pz) {
    const float ex = px - d.x, ey = py - d.y, ez = pz - d.z;
    const float dist = std::sqrt(ex*ex + ey*ey + ez*ez);

    if (d.charge_cooldown <= 0.0f && dist < DRAGON_CHARGE_RANGE) {
        d.state = EnderDragon::State::Charge;
        d.charge_timer = 0.0f;
        return;
    }

    if (d.fireball_cooldown <= 0.0f &&
        dist > DRAGON_FIREBALL_MIN_DIST && dist < DRAGON_FIREBALL_MAX_DIST) {
        // 頭の向きチェック: 水平前方とプレイヤー方向の内積
        const float yaw_rad = d.yaw * (kPi / 180.0f);
        const float fx = std::cos(yaw_rad);
        const float fz = std::sin(yaw_rad);
        const float horiz = std::sqrt(ex*ex + ez*ez);
        const float facing = (horiz > 1e-4f)
                               ? (ex * fx + ez * fz) / horiz : 1.0f;
        if (facing < 0.2f) {
            d.fireball_cooldown = 0.6f;  // 向きが合うまで小刻みにリトライ
            return;
        }

        // 口の位置 (頭の先端付近) から発射
        DragonFireball fb;
        fb.x = d.x + fx * 8.5f;
        fb.y = d.y + 1.8f;
        fb.z = d.z + fz * 8.5f;
        const float ax = px - fb.x, ay = py - fb.y, az = pz - fb.z;
        const float alen = std::sqrt(ax*ax + ay*ay + az*az);
        if (alen > 1e-4f) {
            fb.vx = ax / alen * DRAGON_FIREBALL_SPEED;
            fb.vy = ay / alen * DRAGON_FIREBALL_SPEED;
            fb.vz = az / alen * DRAGON_FIREBALL_SPEED;
            fb.life = DRAGON_FIREBALL_LIFE;
            fireballs_.push_back(fb);
            pending_spawns_.push_back(fb);   // engine がネット配信する
        }
        d.fireball_cooldown = DRAGON_FIREBALL_COOLDOWN;
    }
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

// ─────────────────────────────────────────────────────────────────────────────
// updateProjectiles() — ファイアボールの飛翔・着弾とブレス雲の維持・ダメージ
//
// 全クライアントで毎フレーム実行する。飛翔は直線等速 + 着弾判定のみで
// 乱数を使わないため、同一ワールドなら全クライアントで決定的に一致する。
// ダメージは「自分のプレイヤー」の分だけ各クライアントがローカルに計算する。
// ─────────────────────────────────────────────────────────────────────────────
float DragonManager::updateProjectiles(
        float dt, const std::function<bool(int,int,int)>& isSolid,
        float px, float py, float pz) {
    // ── 弾の飛翔と着弾 ──────────────────────────────────────────────────────
    for (size_t i = 0; i < fireballs_.size(); ) {
        DragonFireball& fb = fireballs_[i];
        fb.x += fb.vx * dt;
        fb.y += fb.vy * dt;
        fb.z += fb.vz * dt;
        fb.life -= dt;

        const bool hit = isSolid((int)std::floor(fb.x),
                                 (int)std::floor(fb.y),
                                 (int)std::floor(fb.z));
        if (hit || fb.life <= 0.0f) {
            DragonBreathCloud cl;
            cl.x = fb.x;
            cl.y = fb.y + 0.5f;   // 地面に埋まらないよう少し持ち上げる
            cl.z = fb.z;
            cl.timer = DRAGON_CLOUD_DURATION;
            clouds_.push_back(cl);
            pending_booms_.push_back(cl);  // engine が効果音を鳴らす
            fireballs_.erase(fireballs_.begin() + (long)i);
            continue;
        }
        ++i;
    }

    // ── 雲の維持とダメージ ──────────────────────────────────────────────────
    float dmg = 0.0f;
    for (size_t i = 0; i < clouds_.size(); ) {
        DragonBreathCloud& cl = clouds_[i];
        cl.timer -= dt;
        if (cl.timer <= 0.0f) {
            clouds_.erase(clouds_.begin() + (long)i);
            continue;
        }
        const float dx = px - cl.x;
        const float dy = py - cl.y;
        const float dz = pz - cl.z;
        if (dx * dx + dz * dz < DRAGON_CLOUD_RADIUS * DRAGON_CLOUD_RADIUS &&
            std::fabs(dy) < DRAGON_CLOUD_HEIGHT) {
            dmg += DRAGON_CLOUD_DPS * dt;
        }
        ++i;
    }
    return dmg;
}

void DragonManager::spawnFireballNet(float x, float y, float z,
                                      float vx, float vy, float vz) {
    DragonFireball fb;
    fb.x = x;  fb.y = y;  fb.z = z;
    fb.vx = vx; fb.vy = vy; fb.vz = vz;
    fb.life = DRAGON_FIREBALL_LIFE;
    fireballs_.push_back(fb);
}

bool DragonManager::pollFireballSpawn(DragonFireball& out) {
    if (pending_spawns_.empty()) return false;
    out = pending_spawns_.back();
    pending_spawns_.pop_back();
    return true;
}

bool DragonManager::pollExplosion(float& x, float& y, float& z) {
    if (pending_booms_.empty()) return false;
    x = pending_booms_.back().x;
    y = pending_booms_.back().y;
    z = pending_booms_.back().z;
    pending_booms_.pop_back();
    return true;
}
