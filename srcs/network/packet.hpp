#pragma once
#include <cstdint>

// Wire format: PacketHeader (3 bytes) + payload (header.size bytes).
// All multi-byte integers are little-endian (native on x86/ARM).

enum class PacketType : uint8_t {
    Connect     = 1,   // C→S
    Welcome     = 2,   // S→C: assigns player_id + seed
    Disconnect  = 3,   // S→C: broadcast when a peer leaves
    PlayerPos   = 4,   // C→S own pos; S→C broadcast
    BlockChange = 5,   // C→S; S→C broadcast (excluding sender)
    TimeSync    = 6,   // S→C: server time_of_day broadcast
    MobUpdate   = 7,   // C(host)→S; S→C relay to others
    DragonSpawn  = 8,  // 任意のC→S; S→C broadcast: ドラゴン召喚イベント
    DragonUpdate = 9,  // C(host)→S; S→C relay to others: ドラゴンの状態
    PlayerDamage = 10, // C(host)→S; S→該当クライアントへ転送: モブによるダメージ
    DragonFireball = 11, // C(host)→S; S→C relay: ファイアボール発射イベント
};

#pragma pack(push, 1)

struct PacketHeader {
    PacketType type;
    uint16_t   size;   // payload bytes that follow
};

struct PktConnect {
    char name[16];     // null-terminated player name
};

struct PktWelcome {
    uint8_t  player_id;
    uint32_t seed;
};

struct PktDisconnect {
    uint8_t player_id;
};

struct PktPlayerPos {
    uint8_t player_id;
    float   x, y, z;
    float   yaw, pitch;
    float   health;
    uint8_t state_flags;  // bit0=walking, bit1=attacking, bit2=dead
};

struct PktBlockChange {
    int32_t x, y, z;
    uint8_t block_type;
};

struct PktTimeSync {
    float time_of_day;
};

// MobUpdate payload: uint8_t count, then count × PktMobEntry
struct PktMobEntry {
    float   x, y, z, yaw, health, fuse_timer;
    uint8_t type;    // MobType cast to uint8_t
    uint8_t state;   // Zombie::State cast to uint8_t
};

// DragonSpawn: 召喚地点 (= 召喚プレイヤーの足元)。受信側でローカル DragonManager に spawn する。
struct PktDragonSpawn {
    float x, y, z;
};

// DragonUpdate: ホストからの権威的なドラゴン状態。
//   exists=0 のときドラゴン不在 (受信側は despawn)。
//   exists=1 のとき他フィールドを適用し、必要なら spawn してから上書きする。
struct PktDragonState {
    uint8_t exists;       // 0 or 1
    uint8_t state;        // EnderDragon::State
    float   x, y, z;
    float   yaw, pitch;
    float   wing_phase;
    float   health;
};

// DragonFireball: ホストのドラゴンが発射したファイアボール。
// 飛翔は直線等速で決定的なので、発射イベントのみ配信し
// 受信側がローカルにシミュレートする (着弾のブレス雲も同様)。
struct PktDragonFireball {
    float x, y, z;       // 発射位置 (口)
    float vx, vy, vz;    // 速度 (ブロック/秒)
};

// PlayerDamage: ホストが計算したモブ→プレイヤーのダメージ。
// サーバーは target_id のクライアントにのみ転送し、受信側が自分の HP に適用する。
struct PktPlayerDamage {
    uint8_t target_id;
    float   damage;
};

#pragma pack(pop)

static constexpr uint8_t MAX_PLAYERS = 8;
