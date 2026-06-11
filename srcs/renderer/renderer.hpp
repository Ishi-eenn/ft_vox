#pragma once
#include "interfaces/IRenderer.hpp"
#include "renderer/shader.hpp"
#include "renderer/texture_atlas.hpp"
#include "renderer/frustum.hpp"
#include "renderer/skybox.hpp"
#include "renderer/cloud.hpp"
#include "renderer/title_screen.hpp"
#include "renderer/minimap.hpp"
#include "mob/zombie.hpp"
#include "mob/arrow.hpp"
#include "mob/ender_dragon.hpp"
#include <map>
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

struct GLFWwindow;
class World;
struct RemotePlayer;
struct Recipe;

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
    void drawClouds(const float* view4x4, const float* proj4x4,
                    float cam_x, float cam_z, float elapsed_s);
    void drawHud(int fps, int px, int py, int pz, float health, float max_health);
    void drawStats(int fps, int triangles, int cubes,
                   int visible_chunks, int loaded_chunks,
                   bool minimap_visible, const char* biome_name);
    void drawPlayerList(uint8_t local_id,
                        const std::map<uint8_t, RemotePlayer>& players,
                        bool multiplayer);
    void drawDeathScreen();
    void drawHotbar(const Inventory& inv);
    void drawCraftMenu(const Inventory& inv, const std::vector<Recipe>& recipes,
                       int selected_recipe);
    void drawUnderwaterOverlay();
    void drawFirstPersonHand(float walk_phase, float attack_timer_norm,
                              bool bow_equipped = false, float bow_charge = 0.0f);
    bool drawTitleScreen(float dt);
    void updateMinimap(World& world, float px, float pz, float yaw_deg, float dt);
    void drawMinimap();
    void drawRemotePlayers(const std::map<uint8_t, RemotePlayer>& players,
                           const float* view4x4, const float* proj4x4);
    void drawMobs(const std::vector<Zombie>& zombies,
                  const float* view4x4, const float* proj4x4);
    void drawArrows(const std::vector<Arrow>& arrows,
                    const float* view4x4, const float* proj4x4);
    void drawDragon(const EnderDragon& dragon,
                    const float* view4x4, const float* proj4x4);
    // ドラゴンのファイアボール (紫の火球) とブレス雲 (残留ダメージ領域) を描画。
    // 雲はドラゴン死亡後も残るため drawDragon とは独立に毎フレーム呼ぶ。
    void drawDragonEffects(const std::vector<DragonFireball>& fireballs,
                           const std::vector<DragonBreathCloud>& clouds,
                           const float* view4x4, const float* proj4x4);
    void drawBossBar(float health, float max_health);
    void endFrame() override;
    void onResize(int w, int h) override;
    void setTimeOfDay(float t);
    void setUnderwater(bool underwater) { underwater_ = underwater; }

    // 松明光源 (forward point lights) の位置リストを設定する。
    // 最大 MAX_TORCH_LIGHTS 件まで採用、それ以上は捨てる。
    // chunk.vert で各頂点ごとに距離フォールオフを計算するために使われる。
    void setTorchLights(const std::vector<glm::vec3>& positions);

    // Shadow mapping
    void updateShadowMatrix(float px, float py, float pz);
    // 光源空間行列 (lightProj × lightView)。シャドウパスのカリングに使う。
    const float* lightSpaceMatrix() const { return light_space_mat_; }
    void beginShadowPass();
    void endShadowPass();
    void drawChunkShadow(const Chunk* chunk);

    // SSAO
    void beginGBufferPass();
    void drawChunkGBuffer(const Chunk* chunk, const float* view4x4, const float* proj4x4);
    void endGBufferPass();
    void computeSSAO(const float* proj4x4);

    const Frustum& getFrustum() const { return frustum_; }

private:
    void initHud();
    void initSSAO();
    void resizeSSAOBuffers(int w, int h);
    void drawStevePart(const glm::mat4& mvp, const glm::mat4& model, const float* color);
    void appendLine(float* verts, int& count, float x0, float y0, float x1, float y1) const;
    void appendDigit(float* verts, int& count, int digit, float left, float top, float w, float h) const;
    void appendNumber(float* verts, int& count, int value, float right, float top, float w, float h, float gap) const;
    void appendLetter(float* verts, int& count, char letter, float left, float top, float w, float h) const;
    void appendHeart(float* verts, int& count, float left, float top, float w, float h) const;
    void appendSignedNumberLeft(float* verts, int& count, int value, float left, float top, float w, float h, float gap) const;

    static constexpr int SHADOW_MAP_SIZE   = 2048;
    static constexpr int SSAO_SAMPLES      = 64;
    static constexpr int MAX_TORCH_LIGHTS  = 16;
    static constexpr float TORCH_RANGE     = 10.0f;  // ブロック

    GLFWwindow*  window_  = nullptr;
    Shader       chunk_shader_;
    Shader       shadow_shader_;
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

    Cloud        cloud_;

    TitleScreen  title_screen_;
    Minimap      minimap_;

    uint32_t     hud_vao_     = 0;
    uint32_t     hud_vbo_     = 0;
    uint32_t     overlay_vao_ = 0;
    uint32_t     overlay_vbo_ = 0;
    uint32_t     hotbar_vao_  = 0;   // slot backgrounds (2D only)
    uint32_t     hotbar_vbo_  = 0;
    Shader       hotbar_shader_;    // textured block icon shader
    uint32_t     hotbar_tex_vao_ = 0;  // pos+UV for textured icons
    uint32_t     hotbar_tex_vbo_ = 0;

    // Shadow map FBO
    uint32_t     shadow_fbo_       = 0;
    uint32_t     shadow_depth_tex_ = 0;
    float        light_space_mat_[16] = {};  // lightProj * lightView

    // SSAO resources
    Shader   gbuffer_shader_;
    Shader   ssao_shader_;
    Shader   ssao_blur_shader_;

    uint32_t gbuffer_fbo_        = 0;
    uint32_t gbuffer_normal_tex_ = 0;
    uint32_t gbuffer_depth_tex_  = 0;

    uint32_t ssao_fbo_       = 0;
    uint32_t ssao_color_tex_ = 0;

    uint32_t ssao_blur_fbo_ = 0;
    uint32_t ssao_blur_tex_ = 0;

    uint32_t ssao_noise_tex_ = 0;
    uint32_t ssao_quad_vao_  = 0;
    uint32_t ssao_quad_vbo_  = 0;

    glm::vec3 ssao_kernel_[SSAO_SAMPLES];

    float sun_dir_[3]      = { 0.0f,  1.0f, 0.0f};
    float ambient_         = 0.30f;
    float sun_strength_    = 0.65f;
    float sky_zenith_[3]   = {0.08f, 0.25f, 0.65f};
    float sky_horizon_[3]  = {0.55f, 0.72f, 0.90f};
    float sky_ground_[3]   = {0.35f, 0.30f, 0.25f};
    float sun_color_[3]    = {1.00f, 0.98f, 0.85f};
    bool  underwater_      = false;

    // 松明光源 (近傍の最大 MAX_TORCH_LIGHTS 件を格納する xyz パックド配列)
    int   torch_count_           = 0;
    float torch_positions_[MAX_TORCH_LIGHTS * 3] = {};
};
