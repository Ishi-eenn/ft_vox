#pragma once
#include "renderer/shader.hpp"
#include "renderer/texture_atlas.hpp"
#include <cstdint>

struct GLFWwindow;

class TitleScreen {
public:
    bool init(TextureAtlas& atlas, Shader& chunk_shader, Shader& hud_shader);
    // Returns true when SPACE is pressed (signal to start the game)
    bool render(float dt, GLFWwindow* window, int w, int h);
    void destroy();

private:
    void buildCube(const AtlasUV& uv);
    void buildTextBuffers();

    float textWidth(const char* text, float cw, float gap) const;
    void  appendText(float* v, int& cnt, const char* text,
                     float start_x, float y, float cw, float ch, float gap) const;
    void  appendSeg(float* v, int& cnt,
                    float x0, float y0, float x1, float y1) const;

    Shader*       chunk_shader_ = nullptr;
    Shader*       hud_shader_   = nullptr;
    uint32_t      atlas_tex_    = 0;

    uint32_t cube_vao_   = 0, cube_vbo_   = 0, cube_ebo_ = 0;
    int      cube_idx_   = 0;

    uint32_t title_vao_  = 0, title_vbo_  = 0;
    int      title_cnt_  = 0;

    uint32_t prompt_vao_ = 0, prompt_vbo_ = 0;
    int      prompt_cnt_ = 0;

    float    angle_   = 0.0f;
    float    elapsed_ = 0.0f;
};
