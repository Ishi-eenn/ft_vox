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
                                    float yaw, float pitch,
                                    float health, uint8_t state_flags) {
    px_ = x;  py_ = y;  pz_ = z;
    pyaw_ = yaw;  ppitch_ = pitch;
    phealth_ = health;
    pflags_ = state_flags;
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
        PktPlayerPos pp{player_id_, px_, py_, pz_, pyaw_, ppitch_, phealth_, pflags_};
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
        rp.health  = pkt.health;
        if ((pkt.state_flags & 0x02u) && !(rp.state_flags & 0x02u)) {
            rp.attack_timer = 0.28f;
            rp.attack_sound_pending = true;  // engine 側で 3D 攻撃音を再生
        }
        rp.state_flags = pkt.state_flags;
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
            z.fuse_timer  = entry.fuse_timer;
            z.type        = static_cast<MobType>(entry.type);
            z.state       = static_cast<Zombie::State>(entry.state);
        }
        out.push_back(std::move(ev));
        break;
    }
    case PacketType::DragonSpawn: {
        if (size < sizeof(PktDragonSpawn)) break;
        PktDragonSpawn pkt;
        std::memcpy(&pkt, payload, sizeof(pkt));
        NetworkEvent ev;
        ev.kind = NetworkEvent::Kind::DragonSpawn;
        ev.dragon_spawn_x = pkt.x;
        ev.dragon_spawn_y = pkt.y;
        ev.dragon_spawn_z = pkt.z;
        out.push_back(ev);
        break;
    }
    case PacketType::DragonUpdate: {
        if (size < sizeof(PktDragonState)) break;
        PktDragonState pkt;
        std::memcpy(&pkt, payload, sizeof(pkt));
        NetworkEvent ev;
        ev.kind = NetworkEvent::Kind::DragonUpdate;
        ev.dragon_exists     = pkt.exists;
        ev.dragon_state      = pkt.state;
        ev.dragon_x          = pkt.x;
        ev.dragon_y          = pkt.y;
        ev.dragon_z          = pkt.z;
        ev.dragon_yaw        = pkt.yaw;
        ev.dragon_pitch      = pkt.pitch;
        ev.dragon_wing_phase = pkt.wing_phase;
        ev.dragon_health     = pkt.health;
        out.push_back(ev);
        break;
    }
    case PacketType::DragonFireball: {
        if (size < sizeof(PktDragonFireball)) break;
        PktDragonFireball pkt;
        std::memcpy(&pkt, payload, sizeof(pkt));
        NetworkEvent ev;
        ev.kind  = NetworkEvent::Kind::DragonFireball;
        ev.fb_x  = pkt.x;
        ev.fb_y  = pkt.y;
        ev.fb_z  = pkt.z;
        ev.fb_vx = pkt.vx;
        ev.fb_vy = pkt.vy;
        ev.fb_vz = pkt.vz;
        out.push_back(ev);
        break;
    }
    case PacketType::PlayerDamage: {
        if (size < sizeof(PktPlayerDamage)) break;
        PktPlayerDamage pkt;
        std::memcpy(&pkt, payload, sizeof(pkt));
        NetworkEvent ev;
        ev.kind      = NetworkEvent::Kind::PlayerDamage;
        ev.player_id = pkt.target_id;
        ev.damage    = pkt.damage;
        out.push_back(ev);
        break;
    }
    default:
        break;
    }
}
