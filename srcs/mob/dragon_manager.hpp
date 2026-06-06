#pragma once
#include "mob/ender_dragon.hpp"

// エンダードラゴンを最大 1 体だけ所有・管理するクラス。
//
// Phase A の責務:
//   ・召喚 (spawn) と消滅 (despawn)
//   ・基本飛行 AI (召喚地点の周りを高所で旋回)
//   ・羽ばたきフェーズの更新
//
// 戦闘 (HP・矢ダメージ・接触ダメージ) は Phase B、
// ネットワーク同期は Phase C で追加する。
class DragonManager {
public:
    // ドラゴンを指定地点の上空 spawn_height_offset ブロックに召喚する。
    // 既に存在する場合は何もしない (シングルトン)。
    void spawn(float wx, float wy, float wz);

    // 任意呼び出しで強制消滅。
    void despawn();

    // 毎フレーム呼び出し。世界の衝突判定は不要 (空中飛行のため)。
    // player_x/y/z はカメラ位置。プレイヤーが接触範囲にいたら接触ダメージを返す
    // (return: このフレーム中にプレイヤーへ与えたダメージ)。
    float update(float dt, float player_x, float player_y, float player_z);

    // 矢・近接からダメージを与える。HP が 0 を切ったら Dying に遷移。
    void applyDamage(float amount);

    // プレイヤーが視線方向に近接攻撃したときの命中判定。
    // ヒットしたら DRAGON_MELEE_DAMAGE を与える。戻り値: ヒットしたかどうか。
    bool playerMeleeAttack(float px, float py, float pz,
                            float front_x, float front_y, float front_z);

    // 点 (px,py,pz) からドラゴン胴体軸の最近接点までの距離 (ブロック)。
    // ドラゴンが存在しないときは float の最大値相当を返す。
    // 軸は yaw 方向に水平に伸びるとみなす (pitch は近似のため無視)。
    float distanceToBody(float px, float py, float pz) const;

    // ── マルチプレイヤー同期用 ────────────────────────────────────────────
    // ホスト側がブロードキャストに使う「読み取り」: 現状の状態を packet 用に取り出す。
    // exists=false ならドラゴン不在。
    struct NetState {
        bool                 exists = false;
        EnderDragon::State   state  = EnderDragon::State::Patrol;
        float x = 0, y = 0, z = 0;
        float yaw = 0, pitch = 0;
        float wing_phase = 0;
        float health = 0;
    };
    NetState snapshot() const;

    // 非ホスト側が受信した state を流し込む。
    // exists=false なら despawn。true なら必要に応じて spawn してから上書き。
    void applyNetState(const NetState& st);

    bool exists() const { return dragon_.has_value(); }
    const EnderDragon* dragon() const {
        return dragon_.has_value() ? &(*dragon_) : nullptr;
    }
    EnderDragon* dragonMut() {
        return dragon_.has_value() ? &(*dragon_) : nullptr;
    }

private:
    // std::optional 相当の手書き実装 (C++17 で <optional> は使えるが
    //   既存コードと揃えるため最小限の包装)。
    struct Maybe {
        EnderDragon value;
        bool        present = false;
        bool has_value() const { return present; }
        EnderDragon&       operator*()       { return value; }
        const EnderDragon& operator*() const { return value; }
        void reset() { present = false; value = EnderDragon{}; }
        void emplace(const EnderDragon& v) { value = v; present = true; }
    };
    Maybe dragon_;

    static constexpr float kSpawnHeightOffset = 30.0f;  // 召喚高度差
    static constexpr float kOrbitRadius       = 25.0f;  // 旋回半径
    static constexpr float kOrbitSpeed        = 0.35f;  // 旋回角速度 (rad/s)
    static constexpr float kWingFlapSpeed     = 6.0f;   // 羽ばたき角速度 (rad/s)
};
