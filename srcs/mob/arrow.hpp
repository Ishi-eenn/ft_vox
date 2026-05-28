#pragma once

// 弓矢（プロジェクタイル）
//
// 位置は矢の先端（チップ）のワールド座標。
// 速度ベクトルから自動的に向き（yaw / pitch）が決まる。
struct Arrow {
    float x = 0, y = 0, z = 0;
    float vx = 0, vy = 0, vz = 0;
    float lifetime = 0.0f;     // 経過秒数
    bool  stuck    = false;    // ブロックに刺さって停止
    bool  alive    = true;
};
