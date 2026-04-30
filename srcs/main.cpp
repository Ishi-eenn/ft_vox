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
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    // シード値 = 地形の「乱数の種」。同じ値なら毎回同じ地形が生成される。
    // 起動時に引数を渡すと変えられる: ./ft_vox 1234
    uint32_t seed = 42;
    if (argc >= 2) seed = (uint32_t)std::atol(argv[1]);

    Engine engine;
    if (!engine.init(seed)) {
        fprintf(stderr, "Engine init failed\n");
        return 1;
    }

    // ゲームループ開始。プレイヤーが終了するまでここで待機し続ける。
    engine.run();
    return 0;
}
