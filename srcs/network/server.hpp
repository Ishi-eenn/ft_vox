#pragma once
#include "network/tcp_socket.hpp"
#include "network/packet.hpp"
#include <cstdint>
#include <map>

class VoxServer {
public:
    bool init(uint16_t port, uint32_t seed);
    void run();   // blocks until interrupted (Ctrl+C)

private:
    void acceptNew();
    void readClient(int fd);
    void handlePacket(int from_fd, PacketType type,
                      const uint8_t* payload, uint16_t size);
    void broadcast(PacketType type, const void* payload, uint16_t size,
                   int exclude_fd = -1);
    void dropClient(int fd);

    struct Client {
        TcpSocket sock;
        uint8_t   id    = 0;
        char      name[16] = {};
        float     x = 0, y = 80, z = 0;
        float     yaw = 0, pitch = 0;
    };

    TcpSocket         listen_sock_;
    std::map<int, Client> clients_;
    uint8_t  next_id_       = 1;
    uint32_t seed_          = 42;
    float    time_of_day_   = 0.35f;
    float    time_sync_acc_ = 0.0f;   // 次のTimeSync送信までの残り秒
    static constexpr float DAY_DURATION    = 600.0f;
    static constexpr float TIME_SYNC_INTERVAL = 5.0f;
};
