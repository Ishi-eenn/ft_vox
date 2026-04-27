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
    void drawHud(int fps);
    void endFrame() override;
    void onResize(int w, int h) override;

    const Frustum& getFrustum() const { return frustum_; }

private:
    void initHud();
    void appendLine(float* verts, int& count, float x0, float y0, float x1, float y1) const;
    void appendDigit(float* verts, int& count, int digit, float left, float top, float w, float h) const;
    void appendNumber(float* verts, int& count, int value, float right, float top, float w, float h, float gap) const;

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
