#pragma once
#include "interfaces/IRenderer.hpp"
#include "renderer/shader.hpp"
#include "renderer/texture_atlas.hpp"
#include "renderer/frustum.hpp"
#include "renderer/skybox.hpp"
#include "renderer/title_screen.hpp"
#include "renderer/minimap.hpp"
#include "mob/zombie.hpp"
#include <map>
#include <cstdint>
#include <vector>

struct GLFWwindow;
class World;
struct RemotePlayer;

class Renderer : public IRenderer {
public:
    Renderer();
    ~Renderer() override;

    bool init(GLFWwindow* window) override;
    void uploadChunkMesh(Chunk* chunk) override;
    void destroyChunkMesh(Chunk* chunk) override;
    void beginFrame() override;
    void drawChunk(const Chunk* chunk, const float* view4x4, const float* proj4x4) override;
    void drawChunkWater(const Chunk* chunk, const float* view4x4, const float* proj4x4);
    void drawSkybox(const float* view3x3, const float* proj4x4) override;
    void drawHud(int fps, int px, int py, int pz);
    void drawUnderwaterOverlay();
    bool drawTitleScreen(float dt);
    void updateMinimap(World& world, float px, float pz, float yaw_deg, float dt);
    void drawMinimap();
    void drawRemotePlayers(const std::map<uint8_t, RemotePlayer>& players,
                           const float* view4x4, const float* proj4x4);
    void drawMobs(const std::vector<Zombie>& zombies,
                  const float* view4x4, const float* proj4x4);
    void endFrame() override;
    void onResize(int w, int h) override;
    void setTimeOfDay(float t);

    const Frustum& getFrustum() const { return frustum_; }

private:
    void initHud();
    void drawStevePart(const glm::mat4& mvp, const glm::mat4& model, const float* color);
    void appendLine(float* verts, int& count, float x0, float y0, float x1, float y1) const;
    void appendDigit(float* verts, int& count, int digit, float left, float top, float w, float h) const;
    void appendNumber(float* verts, int& count, int value, float right, float top, float w, float h, float gap) const;
    void appendLetter(float* verts, int& count, char letter, float left, float top, float w, float h) const;
    void appendSignedNumberLeft(float* verts, int& count, int value, float left, float top, float w, float h, float gap) const;

    GLFWwindow*  window_  = nullptr;
    Shader       chunk_shader_;
    Shader       sky_shader_;
    Shader       hud_shader_;
    Shader       entity_shader_;
    uint32_t     entity_vao_ = 0;
    uint32_t     entity_vbo_ = 0;
    uint32_t     entity_ebo_ = 0;
    TextureAtlas atlas_;
    Skybox       skybox_;
    Frustum      frustum_;
    int          width_ = 1280, height_ = 720;

    TitleScreen  title_screen_;
    Minimap      minimap_;

    uint32_t     hud_vao_     = 0;
    uint32_t     hud_vbo_     = 0;
    uint32_t     overlay_vao_ = 0;
    uint32_t     overlay_vbo_ = 0;

    float sun_dir_[3]      = { 0.0f,  1.0f, 0.0f};
    float ambient_         = 0.30f;
    float sun_strength_    = 0.65f;
    float sky_zenith_[3]   = {0.08f, 0.25f, 0.65f};
    float sky_horizon_[3]  = {0.55f, 0.72f, 0.90f};
    float sky_ground_[3]   = {0.35f, 0.30f, 0.25f};
    float sun_color_[3]    = {1.00f, 0.98f, 0.85f};
};
