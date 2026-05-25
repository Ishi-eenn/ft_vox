#include "network/client.hpp"
#include <cstdio>
#include <cstring>

bool NetworkClient::connect(const char* host, uint16_t port, const char* name) {
    if (!sock_.connect(host, port)) return false;

    PktConnect pkt{};
    std::strncpy(pkt.name, name, 15);
    pkt.name[15] = '\0';
    return sendPacket(PacketType::Connect, &pkt, sizeof(pkt));
}

bool NetworkClient::sendPacket(PacketType type, const void* payload, uint16_t size) {
    PacketHeader hdr{type, size};
    if (!sock_.sendRaw(&hdr, sizeof(hdr))) return false;
    if (size > 0 && !sock_.sendRaw(payload, size)) return false;
    return true;
}

void NetworkClient::updatePosition(float x, float y, float z,
                                    float yaw, float pitch) {
    px_ = x;  py_ = y;  pz_ = z;
    pyaw_ = yaw;  ppitch_ = pitch;
    pos_dirty_ = true;
}

void NetworkClient::sendBlockChange(int x, int y, int z, uint8_t block_type) {
    if (!sock_.isValid()) return;
    PktBlockChange pkt{x, y, z, block_type};
    sendPacket(PacketType::BlockChange, &pkt, sizeof(pkt));
}

void NetworkClient::sendRaw(PacketType type, const void* payload, uint16_t size) {
    if (!sock_.isValid()) return;
    sendPacket(type, payload, size);
}

void NetworkClient::poll(std::vector<NetworkEvent>& out_events) {
    if (!sock_.isValid()) return;

    // Position throttle — dt is not available here, so we track wall time via
    // a call-count approximation.  Caller drives the timer externally.
    if (pos_dirty_) {
        PktPlayerPos pp{player_id_, px_, py_, pz_, pyaw_, ppitch_};
        sendPacket(PacketType::PlayerPos, &pp, sizeof(pp));
        pos_dirty_ = false;
    }

    std::vector<std::vector<uint8_t>> pkts;
    if (!sock_.poll(pkts)) {
        fprintf(stderr, "[Client] disconnected from server\n");
        return;
    }
    for (auto& raw : pkts) {
        PacketHeader hdr;
        std::memcpy(&hdr, raw.data(), sizeof(hdr));
        const uint8_t* payload = raw.data() + sizeof(hdr);
        handlePacket(hdr.type, payload, hdr.size, out_events);
    }
}

void NetworkClient::handlePacket(PacketType type, const uint8_t* payload,
                                  uint16_t size,
                                  std::vector<NetworkEvent>& out) {
    switch (type) {
    case PacketType::Welcome: {
        if (size < sizeof(PktWelcome)) break;
        PktWelcome pkt;
        std::memcpy(&pkt, payload, sizeof(pkt));
        player_id_ = pkt.player_id;
        seed_      = pkt.seed;
        welcomed_  = true;
        fprintf(stderr, "[Client] welcomed as player %u  seed=%u\n",
                player_id_, seed_);
        break;
    }
    case PacketType::PlayerPos: {
        if (size < sizeof(PktPlayerPos)) break;
        PktPlayerPos pkt;
        std::memcpy(&pkt, payload, sizeof(pkt));
        if (pkt.player_id == player_id_) break;   // ignore echo of own pos
        auto& rp   = remote_players_[pkt.player_id];
        rp.x       = pkt.x;  rp.y     = pkt.y;  rp.z   = pkt.z;
        rp.yaw     = pkt.yaw;  rp.pitch = pkt.pitch;
        break;
    }
    case PacketType::BlockChange: {
        if (size < sizeof(PktBlockChange)) break;
        PktBlockChange pkt;
        std::memcpy(&pkt, payload, sizeof(pkt));
        NetworkEvent ev;
        ev.kind       = NetworkEvent::Kind::BlockChange;
        ev.bx         = pkt.x;  ev.by = pkt.y;  ev.bz = pkt.z;
        ev.block_type = pkt.block_type;
        out.push_back(ev);
        break;
    }
    case PacketType::Disconnect: {
        if (size < sizeof(PktDisconnect)) break;
        PktDisconnect pkt;
        std::memcpy(&pkt, payload, sizeof(pkt));
        remote_players_.erase(pkt.player_id);
        NetworkEvent ev;
        ev.kind      = NetworkEvent::Kind::PlayerDisconnect;
        ev.player_id = pkt.player_id;
        out.push_back(ev);
        fprintf(stderr, "[Client] player %u left\n", pkt.player_id);
        break;
    }
    case PacketType::TimeSync: {
        if (size < sizeof(PktTimeSync)) break;
        PktTimeSync pkt;
        std::memcpy(&pkt, payload, sizeof(pkt));
        NetworkEvent ev;
        ev.kind        = NetworkEvent::Kind::TimeSync;
        ev.time_of_day = pkt.time_of_day;
        out.push_back(ev);
        break;
    }
    case PacketType::MobUpdate: {
        if (size < 1) break;
        uint8_t count = payload[0];
        const uint8_t* p = payload + 1;
        if (size < 1 + (uint16_t)count * sizeof(PktMobEntry)) break;
        NetworkEvent ev;
        ev.kind = NetworkEvent::Kind::MobUpdate;
        ev.mobs.resize(count);
        for (int i = 0; i < count; ++i) {
            PktMobEntry entry;
            std::memcpy(&entry, p, sizeof(entry));
            p += sizeof(entry);
            Zombie& z     = ev.mobs[i];
            z.x           = entry.x;
            z.y           = entry.y;
            z.z           = entry.z;
            z.yaw         = entry.yaw;
            z.health      = entry.health;
            z.state       = static_cast<Zombie::State>(entry.state);
        }
        out.push_back(std::move(ev));
        break;
    }
    default:
        break;
    }
}
