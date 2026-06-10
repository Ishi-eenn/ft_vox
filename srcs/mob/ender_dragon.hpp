#pragma once
#include <cstdint>

// エンダードラゴン (ボス相当のシングルトンモブ)
//
// ゾンビ等と違い飛行するため、yaw に加えて pitch を持つ。
// 位置は「ドラゴンの胴体中心」（足元ではない）。
struct EnderDragon {
    float x = 0, y = 0, z = 0;       // 胴体中心
    float vx = 0, vy = 0, vz = 0;    // 速度（ブロック/秒）

    float yaw   = 0;                 // 水平向き（度。Camera と同じ慣習）
    float pitch = 0;                 // 上下角（度。+で機首上げ）

    float spawn_x = 0, spawn_y = 0, spawn_z = 0;  // 旋回中心
    float orbit_phase = 0.0f;        // 旋回位相 (rad)

    float health = 100.0f;
    bool  alive  = true;

    enum class State : uint8_t { Patrol, Dying } state = State::Patrol;
    float wing_phase = 0.0f;         // 羽ばたき位相 (rad)
    float life_timer = 0.0f;         // 生存秒数
    float dying_timer = 0.0f;        // Dying 状態になってからの秒数 (演出 + 自動消滅)
    float hit_flash_timer = 0.0f;    // 被弾フラッシュの残り秒数 (赤くする)
};

// ドラゴンの判定値定数 (DragonManager / ArrowManager 双方で参照する)
//
// ドラゴンは「胴体中心 → 頭の先」「胴体中心 → 尾の先」が大きく非対称な細長い形。
// 球で判定すると頭・尾が外れるので、yaw 方向の軸に沿った "カプセル" で判定する。
//   胴体軸は world XZ 平面に水平。点 P と軸の最近接点の距離 d を計算し、
//   d <= 半径 なら命中とみなす。
constexpr float DRAGON_MAX_HEALTH       = 100.0f;

// 胴体軸の中心からの伸び (kModelScale=2.0 を前提)
constexpr float DRAGON_AXIS_FRONT       = 9.0f;   // 中心から頭先まで
constexpr float DRAGON_AXIS_BACK        = 12.0f;  // 中心から尾先まで

constexpr float DRAGON_CONTACT_RADIUS   = 3.5f;   // 接触ダメージ判定 (体に触れているか)
constexpr float DRAGON_HIT_RADIUS       = 5.0f;   // 矢の命中判定 (やや甘め)
constexpr float DRAGON_CONTACT_DAMAGE_PER_SEC = 4.0f;
constexpr float DRAGON_ARROW_DAMAGE     = 10.0f;
constexpr float DRAGON_MELEE_DAMAGE     = 5.0f;
constexpr float DRAGON_MELEE_RANGE      = 6.0f;   // プレイヤー側のリーチ
constexpr float DRAGON_DYING_DURATION   = 3.0f;   // Dying → 自動消滅
