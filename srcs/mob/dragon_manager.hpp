#pragma once
#include "mob/ender_dragon.hpp"
#include <functional>
#include <vector>

// エンダードラゴンを最大 1 体だけ所有・管理するクラス。
//
// 責務:
//   ・召喚 (spawn) と消滅 (despawn)
//   ・飛行 AI: 旋回 (Patrol) / チャージ突進 (Charge) / 復帰 (Retreat)
//   ・攻撃: 体当たり・接触ダメージ・ファイアボール + ブレス雲
//   ・羽ばたきフェーズの更新
//
// AI (update) はホストのみが実行し 10Hz で状態配信する。
// ファイアボール/ブレス雲 (updateProjectiles) は直線等速で決定的なため、
// 発射イベントの受信後は全クライアントがローカルにシミュレートする。
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

    // ── ファイアボール / ブレス雲 ────────────────────────────────────────
    // 毎フレーム・全クライアントで呼ぶ (ホスト/非ホスト共通)。
    // 弾の飛翔 → 着弾 (isSolid) でブレス雲生成 → 雲のダメージ判定。
    // 戻り値: ローカルプレイヤー (px,py,pz=カメラ位置) へのこのフレームのダメージ。
    float updateProjectiles(float dt,
                            const std::function<bool(int,int,int)>& isSolid,
                            float px, float py, float pz);

    // ネット受信した発射イベントを反映する (非ホスト用)。
    void spawnFireballNet(float x, float y, float z,
                          float vx, float vy, float vz);

    // ホストが今フレーム発射した弾を取り出す (ブロードキャスト用 outbox)。
    // 戻り値 true の間繰り返し呼ぶ。
    bool pollFireballSpawn(DragonFireball& out);

    // このフレームに起きた着弾爆発の座標を取り出す (効果音用 outbox)。
    bool pollExplosion(float& x, float& y, float& z);

    const std::vector<DragonFireball>&    fireballs() const { return fireballs_; }
    const std::vector<DragonBreathCloud>& clouds()    const { return clouds_; }

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

    // 弾・雲はドラゴン本体と独立に生存する (死亡後も雲は残る)。
    std::vector<DragonFireball>    fireballs_;
    std::vector<DragonBreathCloud> clouds_;
    std::vector<DragonFireball>    pending_spawns_;   // ホスト→ネット送信待ち
    std::vector<DragonBreathCloud> pending_booms_;    // 着弾爆発の効果音待ち

    // Patrol 中の攻撃判断 (ホストのみで実行される)
    void tryAttacks(EnderDragon& d, float px, float py, float pz);

    static constexpr float kSpawnHeightOffset = 30.0f;  // 召喚高度差
    static constexpr float kOrbitRadius       = 25.0f;  // 旋回半径
    static constexpr float kOrbitSpeed        = 0.35f;  // 旋回角速度 (rad/s)
    static constexpr float kWingFlapSpeed     = 2.4f;   // 羽ばたき角速度 (rad/s)。本家のゆったりした周期 (約2.6秒/回)
};
