#pragma once
#include "interfaces/IRenderer.hpp"
#include "renderer/shader.hpp"
#include "renderer/texture_atlas.hpp"
#include "renderer/frustum.hpp"
#include "renderer/skybox.hpp"
#include <vector>

struct GLFWwindow;

class Renderer : public IRenderer {
public:
    Renderer();
    ~Renderer() override;

    bool init(GLFWwindow* window) override;
    void uploadChunkMesh(Chunk* chunk) override;
    void destroyChunkMesh(Chunk* chunk) override;
    void beginFrame() override;
    void drawChunk(const Chunk* chunk, const float* view4x4, const float* proj4x4) override;
    void drawSkybox(const float* view3x3, const float* proj4x4) override;
    void drawCrosshair();
    void endFrame() override;
    void onResize(int w, int h) override;

    const Frustum& getFrustum() const { return frustum_; }

private:
    void initHud();

    GLFWwindow*  window_  = nullptr;
    Shader       chunk_shader_;
    Shader       sky_shader_;
    Shader       hud_shader_;
    TextureAtlas atlas_;
    Skybox       skybox_;
    Frustum      frustum_;
    int          width_ = 1280, height_ = 720;

    uint32_t     hud_vao_ = 0;
    uint32_t     hud_vbo_ = 0;
};
