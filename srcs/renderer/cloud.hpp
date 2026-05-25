#pragma once
#include "renderer/shader.hpp"
#include <cstdint>

class Cloud {
public:
    bool init();
    void draw(const float* view4x4, const float* proj4x4,
              float cam_x, float cam_z,
              float elapsed_s, const float* sun_dir);
    void destroy();
    ~Cloud();

    static constexpr float CLOUD_Y      = 136.0f;  // 雲の底面 Y 高度
    static constexpr float CLOUD_H      = 4.0f;    // 雲の厚み（ブロック）
    static constexpr float CELL_SIZE    = 12.0f;   // 1 セルの XZ 幅（ブロック）
    static constexpr int   GRID_W       = 64;      // タイル 1 辺のセル数
    static constexpr float DRIFT_SPEED  = 6.0f;    // ドリフト速度（ブロック/秒, +X 方向）

private:
    void buildMesh();
    bool cellAt(int gx, int gz) const;  // 境界ラップあり

    Shader   shader_;
    uint32_t vao_       = 0;
    uint32_t vbo_       = 0;
    uint32_t ebo_       = 0;
    int32_t  idx_count_ = 0;

    bool grid_[GRID_W][GRID_W] = {};
};
