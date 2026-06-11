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

    // 状態遷移: Patrol → (クールダウン明け) Charge → (体当たり/タイムアウト)
    //           Retreat → (旋回円に復帰) Patrol。HP 0 でどの状態からも Dying。
    // 注意: wire format は uint8_t キャストなので既存値 (Patrol=0, Dying=1) は
    //       変更せず末尾に追加する。
    enum class State : uint8_t { Patrol, Dying, Charge, Retreat } state = State::Patrol;
    float wing_phase = 0.0f;         // 羽ばたき位相 (rad)
    float life_timer = 0.0f;         // 生存秒数
    float dying_timer = 0.0f;        // Dying 状態になってからの秒数 (演出 + 自動消滅)
    float hit_flash_timer = 0.0f;    // 被弾フラッシュの残り秒数 (赤くする)

    // ── 攻撃 AI 用タイマー (ホスト側のみ意味を持つ。ネット同期しない) ──────
    float charge_cooldown   = 10.0f; // 次のチャージ突進までの秒数
    float fireball_cooldown = 4.0f;  // 次のファイアボールまでの秒数
    float charge_timer      = 0.0f;  // Charge 状態の経過秒数 (タイムアウト用)
};

// ドラゴンファイアボール (本家準拠: 弾自体に接触ダメージはなく、
// 着弾点に残留ダメージ雲 = ドラゴンブレスを生成する)。
// 飛翔は直線等速で決定的なので、発射イベントだけネット配信すれば
// 各クライアントがローカルにシミュレートできる。
struct DragonFireball {
    float x = 0, y = 0, z = 0;
    float vx = 0, vy = 0, vz = 0;   // ブロック/秒
    float life = 0;                  // 残り秒数 (0 で自然消滅 = その場で爆発)
};

// ブレス雲 (残留ダメージ領域)。着弾点に一定時間とどまる。
struct DragonBreathCloud {
    float x = 0, y = 0, z = 0;       // 中心 (地表付近)
    float timer = 0;                 // 残り秒数
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

// ── チャージ突進 (本家のswoop攻撃相当) ────────────────────────────────────
constexpr float DRAGON_CHARGE_RANGE     = 80.0f;  // この距離内のプレイヤーを狙う
constexpr float DRAGON_CHARGE_SPEED     = 16.0f;  // 突進速度 (ブロック/秒)
constexpr float DRAGON_CHARGE_TURN      = 2.5f;   // 旋回の利き (1/秒, 大きいほど機敏)
constexpr float DRAGON_CHARGE_DAMAGE    = 12.0f;  // 体当たりの一撃ダメージ
constexpr float DRAGON_CHARGE_TIMEOUT   = 6.0f;   // 当たらなければ諦めて復帰
constexpr float DRAGON_CHARGE_COOLDOWN  = 12.0f;  // 突進間隔
constexpr float DRAGON_RETREAT_SPEED    = 11.0f;  // 旋回円への復帰速度

// ── ファイアボール + ブレス雲 (本家のDragonFireball + AreaEffectCloud) ────
constexpr float DRAGON_FIREBALL_SPEED    = 14.0f; // ブロック/秒
constexpr float DRAGON_FIREBALL_LIFE     = 8.0f;  // 最大飛翔秒数
constexpr float DRAGON_FIREBALL_COOLDOWN = 5.5f;  // 発射間隔
constexpr float DRAGON_FIREBALL_MIN_DIST = 12.0f; // これより近いと撃たない (突進向き)
constexpr float DRAGON_FIREBALL_MAX_DIST = 64.0f; // 射程
constexpr float DRAGON_CLOUD_DURATION    = 5.0f;  // ブレス雲の持続秒数
constexpr float DRAGON_CLOUD_RADIUS      = 3.5f;  // 雲の水平半径
constexpr float DRAGON_CLOUD_HEIGHT      = 3.5f;  // 雲の縦の効果範囲 (中心から±)
constexpr float DRAGON_CLOUD_DPS         = 6.0f;  // 雲の中にいるときの毎秒ダメージ
