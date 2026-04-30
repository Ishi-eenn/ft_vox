#pragma once
#include "renderer/shader.hpp"
#include <cstdint>

class World;  // forward declaration — full definition only needed in .cpp

class Minimap {
public:
    static constexpr int kSize = 128;  // texture pixels = world-block coverage radius

    bool init();
    // Update the minimap texture. dt = frame delta time (seconds).
    void update(World& world, float px, float pz, float yaw_deg, float dt);
    // Render the minimap in the top-left corner.
    // hud_shader must be the solid-color 2D shader already in use.
    void draw(Shader& hud_shader, int sw, int sh);
    void destroy();

private:
    static constexpr int kHalf = kSize / 2;

    Shader   shader_;                          // textured quad shader
    uint32_t tex_     = 0;                     // GL texture handle
    uint32_t tex_vao_ = 0, tex_vbo_ = 0;      // quad with pos+UV (dynamic)
    uint32_t dyn_vao_ = 0, dyn_vbo_ = 0;      // bg + border lines (pos only, dynamic)

    uint8_t  pixels_[kSize * kSize * 4] = {}; // CPU pixel buffer (RGBA)

    float update_timer_ = 0.f;
    float last_px_      = -99999.f;
    float last_pz_      = -99999.f;
};
