#pragma once
#include <cstdint>

// Wire format: PacketHeader (3 bytes) + payload (header.size bytes).
// All multi-byte integers are little-endian (native on x86/ARM).

enum class PacketType : uint8_t {
    Connect     = 1,   // C→S
    Welcome     = 2,   // S→C: assigns player_id + seed
    Disconnect  = 3,   // S→C: broadcast when a peer leaves
    PlayerPos   = 4,   // C→S own pos; S→C broadcast
    BlockChange = 5,   // both directions
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
};

struct PktBlockChange {
    int32_t x, y, z;
    uint8_t block_type;
};

#pragma pack(pop)

static constexpr uint8_t MAX_PLAYERS = 8;
