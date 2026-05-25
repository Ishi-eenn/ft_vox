#pragma once
#include "network/tcp_socket.hpp"
#include "network/packet.hpp"
#include "mob/zombie.hpp"
#include <cstdint>
#include <map>
#include <vector>

struct RemotePlayer {
    float x = 0, y = 0, z = 0;
    float yaw = 0, pitch = 0;

    // Animation state (updated by caller each frame)
    float walk_phase = 0.0f;
    float prev_x     = 0.0f;
    float prev_z     = 0.0f;
    bool  initialized = false;
};

struct NetworkEvent {
    enum class Kind { BlockChange, PlayerDisconnect, TimeSync, MobUpdate } kind;
    uint8_t  player_id  = 0;
    // BlockChange fields
    int      bx = 0, by = 0, bz = 0;
    uint8_t  block_type = 0;
    // TimeSync
    float    time_of_day = 0.0f;
    // MobUpdate
    std::vector<Zombie> mobs;
};

class NetworkClient {
public:
    // Connect to server and send CONNECT packet.  Returns false on failure.
    bool connect(const char* host, uint16_t port, const char* name = "player");

    // Call once per frame — sends queued data and receives pending packets.
    // Fires events into out_events.
    void poll(std::vector<NetworkEvent>& out_events);

    // Queue position update (sent at most every pos_interval_ms).
    void updatePosition(float x, float y, float z, float yaw, float pitch);

    // Send a block change immediately.
    void sendBlockChange(int x, int y, int z, uint8_t block_type);

    // Send an arbitrary packet (for host→server mob updates etc.)
    void sendRaw(PacketType type, const void* payload, uint16_t size);

    bool     isConnected() const { return sock_.isValid() && welcomed_; }
    uint8_t  playerId()    const { return player_id_; }
    uint32_t seed()        const { return seed_; }

    const std::map<uint8_t, RemotePlayer>& remotePlayers() const { return remote_players_; }
          std::map<uint8_t, RemotePlayer>& remotePlayers()       { return remote_players_; }

private:
    bool sendPacket(PacketType type, const void* payload, uint16_t size);
    void handlePacket(PacketType type, const uint8_t* payload, uint16_t size,
                      std::vector<NetworkEvent>& out);

    TcpSocket sock_;
    uint8_t   player_id_ = 0;
    uint32_t  seed_      = 0;
    bool      welcomed_  = false;

    // Position throttle (send ~20 Hz)
    static constexpr float POS_INTERVAL = 0.05f;
    float   pos_timer_  = 0.0f;
    float   px_ = 0, py_ = 0, pz_ = 0, pyaw_ = 0, ppitch_ = 0;
    bool    pos_dirty_  = false;

    std::map<uint8_t, RemotePlayer> remote_players_;
};
