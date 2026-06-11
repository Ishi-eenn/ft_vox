#include "network/server.hpp"
#include "network/packet.hpp"

#include <sys/select.h>
#include <sys/time.h>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <vector>
#include <sys/stat.h>

static volatile bool g_running = true;

static void handle_sigint(int) { g_running = false; }

bool VoxServer::init(uint16_t port, uint32_t seed) {
    seed_ = seed;
    mkdir("saves", 0755);
    char path[256];
    snprintf(path, sizeof(path), "saves/server_%u_blocks.bin", seed_);
    block_mod_path_ = path;
    loadBlockMods();
    if (!listen_sock_.listen(port)) return false;
    signal(SIGINT, handle_sigint);
    fprintf(stderr, "[Server] seed=%u  mods=%zu  waiting for players...\n",
            seed_, block_mods_.size());
    return true;
}

void VoxServer::loadBlockMods() {
    block_mods_.clear();
    FILE* f = fopen(block_mod_path_.c_str(), "rb");
    if (!f) return;

    char magic[4];
    uint8_t version;
    uint32_t count;
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "VSRM", 4) != 0 ||
        fread(&version, 1, 1, f) != 1 || version != 1 ||
        fread(&count, sizeof(count), 1, f) != 1) {
        fclose(f);
        return;
    }

    for (uint32_t i = 0; i < count; ++i) {
        PktBlockChange pkt;
        if (fread(&pkt, sizeof(pkt), 1, f) != 1) break;
        block_mods_[{pkt.x, pkt.y, pkt.z}] = pkt.block_type;
    }
    fclose(f);
}

void VoxServer::saveBlockMods() const {
    if (block_mod_path_.empty()) return;
    FILE* f = fopen(block_mod_path_.c_str(), "wb");
    if (!f) return;

    const char magic[4] = {'V','S','R','M'};
    const uint8_t version = 1;
    const uint32_t count = (uint32_t)block_mods_.size();
    fwrite(magic, 1, 4, f);
    fwrite(&version, 1, 1, f);
    fwrite(&count, sizeof(count), 1, f);
    for (const auto& [key, type] : block_mods_) {
        PktBlockChange pkt{key.x, key.y, key.z, type};
        fwrite(&pkt, sizeof(pkt), 1, f);
    }
    fclose(f);
}

void VoxServer::sendBlockModsTo(Client& client) {
    for (const auto& [key, type] : block_mods_) {
        PktBlockChange pkt{key.x, key.y, key.z, type};
        PacketHeader hdr{PacketType::BlockChange, sizeof(pkt)};
        client.sock.sendRaw(&hdr, sizeof(hdr));
        client.sock.sendRaw(&pkt, sizeof(pkt));
    }
}

void VoxServer::run() {
    timeval prev_tv;
    gettimeofday(&prev_tv, nullptr);

    while (g_running) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(listen_sock_.fd(), &rfds);
        int max_fd = listen_sock_.fd();

        for (auto& [fd, c] : clients_) {
            FD_SET(fd, &rfds);
            if (fd > max_fd) max_fd = fd;
        }

        timeval tv{0, 10'000};   // 10 ms poll interval
        int ret = select(max_fd + 1, &rfds, nullptr, nullptr, &tv);
        if (ret < 0) break;

        // 経過時間を計測して昼夜サイクルを進める
        timeval now_tv;
        gettimeofday(&now_tv, nullptr);
        float dt = (float)(now_tv.tv_sec  - prev_tv.tv_sec) +
                   (float)(now_tv.tv_usec - prev_tv.tv_usec) * 1e-6f;
        prev_tv = now_tv;
        if (dt > 0.1f) dt = 0.1f;  // 負荷スパイク対策

        time_of_day_ += dt / DAY_DURATION;
        if (time_of_day_ >= 1.0f) time_of_day_ -= 1.0f;

        // 5秒ごとに全クライアントへ時刻をブロードキャスト
        time_sync_acc_ -= dt;
        if (time_sync_acc_ <= 0.0f && !clients_.empty()) {
            time_sync_acc_ = TIME_SYNC_INTERVAL;
            PktTimeSync pkt{time_of_day_};
            broadcast(PacketType::TimeSync, &pkt, sizeof(pkt));
        }

        if (ret == 0) continue;

        if (FD_ISSET(listen_sock_.fd(), &rfds)) acceptNew();

        std::vector<int> ready;
        for (auto& [fd, _] : clients_)
            if (FD_ISSET(fd, &rfds)) ready.push_back(fd);
        for (int fd : ready) readClient(fd);
    }
    fprintf(stderr, "[Server] shutting down.\n");
}

void VoxServer::acceptNew() {
    TcpSocket s = listen_sock_.accept();
    if (!s.isValid()) return;
    if (clients_.size() >= MAX_PLAYERS) {
        fprintf(stderr, "[Server] full, rejecting connection\n");
        return;
    }
    uint8_t id = next_id_++;
    int fd = s.fd();
    Client& c = clients_[fd];
    c.sock = std::move(s);
    c.id   = id;
    fprintf(stderr, "[Server] player %u connected (fd=%d)\n", id, fd);
}

void VoxServer::readClient(int fd) {
    Client& c = clients_.at(fd);
    std::vector<std::vector<uint8_t>> pkts;
    if (!c.sock.poll(pkts)) {
        dropClient(fd);
        return;
    }
    for (auto& raw : pkts) {
        PacketHeader hdr;
        std::memcpy(&hdr, raw.data(), sizeof(hdr));
        const uint8_t* payload = raw.data() + sizeof(hdr);
        handlePacket(fd, hdr.type, payload, hdr.size);
    }
}

void VoxServer::handlePacket(int from_fd, PacketType type,
                              const uint8_t* payload, uint16_t size) {
    Client& from = clients_.at(from_fd);

    switch (type) {
    case PacketType::Connect: {
        if (size < sizeof(PktConnect)) break;
        PktConnect pkt;
        std::memcpy(&pkt, payload, sizeof(pkt));
        std::strncpy(from.name, pkt.name, 15);
        from.name[15] = '\0';
        fprintf(stderr, "[Server] player %u name='%s'\n", from.id, from.name);

        // Send WELCOME to the new client.
        PktWelcome wlc{from.id, seed_};
        PacketHeader hdr{PacketType::Welcome, sizeof(wlc)};
        from.sock.sendRaw(&hdr, sizeof(hdr));
        from.sock.sendRaw(&wlc, sizeof(wlc));

        // Send existing players' positions to the newcomer.
        for (auto& [fd, c] : clients_) {
            if (fd == from_fd) continue;
            PktPlayerPos pp{c.id, c.x, c.y, c.z, c.yaw, c.pitch,
                            c.health, c.state_flags};
            PacketHeader h2{PacketType::PlayerPos, sizeof(pp)};
            from.sock.sendRaw(&h2, sizeof(h2));
            from.sock.sendRaw(&pp, sizeof(pp));
        }

        sendBlockModsTo(from);
        break;
    }
    case PacketType::PlayerPos: {
        if (size < sizeof(PktPlayerPos)) break;
        PktPlayerPos pkt;
        std::memcpy(&pkt, payload, sizeof(pkt));
        // Override client-supplied id with the server-assigned one.
        pkt.player_id = from.id;
        from.x = pkt.x;  from.y = pkt.y;  from.z = pkt.z;
        from.yaw = pkt.yaw;  from.pitch = pkt.pitch;
        from.health = pkt.health;
        from.state_flags = pkt.state_flags;
        broadcast(PacketType::PlayerPos, &pkt, sizeof(pkt), from_fd);
        break;
    }
    case PacketType::BlockChange: {
        if (size < sizeof(PktBlockChange)) break;
        PktBlockChange pkt;
        std::memcpy(&pkt, payload, sizeof(pkt));
        block_mods_[{pkt.x, pkt.y, pkt.z}] = pkt.block_type;
        saveBlockMods();
        // 送信者を除いてブロードキャスト（送信者は既にローカルで適用済み）
        broadcast(PacketType::BlockChange, &pkt, sizeof(pkt), from_fd);
        break;
    }
    case PacketType::MobUpdate: {
        // ホスト（player_id==1）からのモブ状態を他全員に中継
        broadcast(PacketType::MobUpdate, payload, size, from_fd);
        break;
    }
    case PacketType::DragonSpawn: {
        // 任意プレイヤーが召喚 → 全員に中継 (送信者を除く)
        broadcast(PacketType::DragonSpawn, payload, size, from_fd);
        break;
    }
    case PacketType::DragonUpdate: {
        // ホストからのドラゴン状態を他全員に中継
        broadcast(PacketType::DragonUpdate, payload, size, from_fd);
        break;
    }
    case PacketType::DragonFireball: {
        // ホストのドラゴンが発射したファイアボールを他全員に中継
        broadcast(PacketType::DragonFireball, payload, size, from_fd);
        break;
    }
    case PacketType::PlayerDamage: {
        // ホストが計算したモブダメージを対象プレイヤーにのみ転送
        if (size < sizeof(PktPlayerDamage)) break;
        PktPlayerDamage pkt;
        std::memcpy(&pkt, payload, sizeof(pkt));
        for (auto& [fd, c] : clients_) {
            if (c.id != pkt.target_id) continue;
            PacketHeader hdr{PacketType::PlayerDamage, sizeof(pkt)};
            c.sock.sendRaw(&hdr, sizeof(hdr));
            c.sock.sendRaw(&pkt, sizeof(pkt));
            break;
        }
        break;
    }
    default:
        break;
    }
}

void VoxServer::broadcast(PacketType type, const void* payload, uint16_t size,
                           int exclude_fd) {
    PacketHeader hdr{type, size};
    for (auto& [fd, c] : clients_) {
        if (fd == exclude_fd) continue;
        c.sock.sendRaw(&hdr, sizeof(hdr));
        c.sock.sendRaw(payload, size);
    }
}

void VoxServer::dropClient(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) return;
    uint8_t id = it->second.id;
    clients_.erase(it);

    PktDisconnect pkt{id};
    broadcast(PacketType::Disconnect, &pkt, sizeof(pkt));
    fprintf(stderr, "[Server] player %u disconnected\n", id);
}
