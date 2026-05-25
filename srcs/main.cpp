// ─────────────────────────────────────────────────────────────────────────────
// main.cpp — プログラムのエントリポイント
//
// ゲームの起動から終了までの最上位の流れを担う。
// やっていることは単純で
//   1. コマンドライン引数からシード値を受け取る
//   2. Engineを初期化する
//   3. ゲームループを開始する（run() は終了するまで戻ってこない）
// ─────────────────────────────────────────────────────────────────────────────
#include "core/engine.hpp"
#include "network/server.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Usage:
//   ./ft_vox [seed]                        — single-player
//   ./ft_vox --server [port] [seed]        — headless server
//   ./ft_vox --connect <ip> [port] [seed]  — multiplayer client

int main(int argc, char** argv) {
    // ── ヘッドレスサーバーモード ─────────────────────────────────────────────
    if (argc >= 2 && std::strcmp(argv[1], "--server") == 0) {
        uint16_t port = 25565;
        uint32_t seed = 42;
        if (argc >= 3) port = (uint16_t)std::atoi(argv[2]);
        if (argc >= 4) seed = (uint32_t)std::atol(argv[3]);

        VoxServer server;
        if (!server.init(port, seed)) return 1;
        server.run();
        return 0;
    }

    // ── クライアントモード ───────────────────────────────────────────────────
    const char* connect_host = nullptr;
    uint16_t    connect_port = 25565;
    uint32_t    seed         = 42;

    if (argc >= 2 && std::strcmp(argv[1], "--connect") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: ft_vox --connect <ip> [port] [seed]\n"); return 1; }
        connect_host = argv[2];
        if (argc >= 4) connect_port = (uint16_t)std::atoi(argv[3]);
        if (argc >= 5) seed         = (uint32_t)std::atol(argv[4]);
    } else if (argc >= 2) {
        seed = (uint32_t)std::atol(argv[1]);
    }

    Engine engine;
    if (!engine.init(seed)) {
        fprintf(stderr, "Engine init failed\n");
        return 1;
    }

    if (connect_host) {
        if (!engine.connectToServer(connect_host, connect_port)) {
            fprintf(stderr, "Failed to connect to %s:%u\n", connect_host, connect_port);
            return 1;
        }
    }

    engine.run();
    return 0;
}
