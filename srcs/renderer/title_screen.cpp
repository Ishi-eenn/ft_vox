#include "renderer/title_screen.hpp"
#include "types.hpp"
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <cmath>
#include <cstring>

// ── Glyph definitions (segment-based, unit cell [0,1]×[0,1]) ─────────────────
// Segments: {x0, y0, x1, y1}, (0,0)=bottom-left (1,1)=top-right
struct Seg    { float x0, y0, x1, y1; };
struct Glyph  { int n; Seg s[8]; };

static constexpr float kT  = 0.92f;  // top
static constexpr float kM  = 0.52f;  // mid
static constexpr float kB  = 0.06f;  // bottom
static constexpr float kL  = 0.06f;  // left
static constexpr float kR  = 0.94f;  // right
static constexpr float kMR = 0.82f;  // partial right (F/E mid bar)

static const struct { char c; Glyph g; } kGlyphs[] = {
    {'F', {3, {
        {kL,kT,kR, kT},            // top bar
        {kL,kB,kL, kT},            // left vert
        {kL,kM,kMR,kM},            // mid bar (partial)
    }}},
    {'T', {2, {
        {kL, kT, kR,  kT},         // top bar
        {0.5f,kB, 0.5f,kT},        // center vert
    }}},
    {'_', {1, {
        {kL,0.08f,kR,0.08f},       // underscore
    }}},
    {'V', {2, {
        {kL,   kT, 0.5f, kB},      // left diagonal
        {0.5f, kB, kR,   kT},      // right diagonal
    }}},
    {'O', {4, {
        {kL,kT,kR,kT},             // top
        {kR,kT,kR,kB},             // right
        {kR,kB,kL,kB},             // bottom
        {kL,kB,kL,kT},             // left
    }}},
    {'X', {2, {
        {kL,kT,kR,kB},             // diagonal [\]
        {kL,kB,kR,kT},             // diagonal [/]
    }}},
    {'P', {4, {
        {kL,kB, kL,  kT},          // left vert
        {kL,kT, kR,  kT},          // top
        {kR,kM, kR,  kT},          // right upper
        {kL,kM, kR,  kM},          // mid
    }}},
    {'R', {5, {
        {kL,kB,  kL,  kT},         // left vert
        {kL,kT,  kR,  kT},         // top
        {kR,kM,  kR,  kT},         // right upper
        {kL,kM,  kR,  kM},         // mid
        {0.5f,kM,kR,  kB},         // lower-right diagonal
    }}},
    {'E', {4, {
        {kL,kB,  kL,  kT},         // left vert
        {kL,kT,  kR,  kT},         // top
        {kL,kM,  kMR, kM},         // mid (partial)
        {kL,kB,  kR,  kB},         // bottom
    }}},
    {'S', {5, {
        {kL,kT,  kR,  kT},         // top
        {kL,kM,  kL,  kT},         // left upper
        {kL,kM,  kR,  kM},         // mid
        {kR,kB,  kR,  kM},         // right lower
        {kL,kB,  kR,  kB},         // bottom
    }}},
    {'A', {3, {
        {kL,   kB, 0.5f,kT},       // left diagonal
        {0.5f, kT, kR,  kB},       // right diagonal
        {0.22f,kM, 0.78f,kM},      // mid bar
    }}},
    {'C', {3, {
        {kL,kT,kR,kT},             // top
        {kL,kB,kL,kT},             // left vert
        {kL,kB,kR,kB},             // bottom
    }}},
};

static const Glyph* findGlyph(char c) {
    for (const auto& entry : kGlyphs)
        if (entry.c == c) return &entry.g;
    return nullptr;
}

// ── Helper: addFace for cube mesh ─────────────────────────────────────────────
static void addFace(std::vector<Vertex>& verts, std::vector<uint32_t>& idx,
                    glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3,
                    glm::vec3 n,
                    float u0, float v0, float u1, float v1)
{
    uint32_t base = static_cast<uint32_t>(verts.size());
    verts.push_back({p0.x, p0.y, p0.z, u0, v0, n.x, n.y, n.z});
    verts.push_back({p1.x, p1.y, p1.z, u0, v1, n.x, n.y, n.z});
    verts.push_back({p2.x, p2.y, p2.z, u1, v1, n.x, n.y, n.z});
    verts.push_back({p3.x, p3.y, p3.z, u1, v0, n.x, n.y, n.z});
    idx.push_back(base+0); idx.push_back(base+1); idx.push_back(base+2);
    idx.push_back(base+0); idx.push_back(base+2); idx.push_back(base+3);
}

// ─────────────────────────────────────────────────────────────────────────────
void TitleScreen::buildCube(const AtlasUV& uv) {
    std::vector<Vertex>   verts;
    std::vector<uint32_t> idx;
    verts.reserve(24);
    idx.reserve(36);

    const float u0 = uv.u0, u1 = uv.u1, v0 = uv.v0, v1 = uv.v1;
    using V = glm::vec3;

    // Each face: 4 verts in CCW order (as seen from outside), outward normal.
    // +Y (top)
    addFace(verts,idx, V(0.5f,0.5f,-0.5f), V(-0.5f,0.5f,-0.5f),
                       V(-0.5f,0.5f,0.5f),  V(0.5f,0.5f,0.5f),
                       V(0,1,0),  u0,v0,u1,v1);
    // -Y (bottom)
    addFace(verts,idx, V(-0.5f,-0.5f,-0.5f), V(0.5f,-0.5f,-0.5f),
                       V(0.5f,-0.5f,0.5f),   V(-0.5f,-0.5f,0.5f),
                       V(0,-1,0), u0,v0,u1,v1);
    // +X (right)
    addFace(verts,idx, V(0.5f,0.5f,0.5f),  V(0.5f,-0.5f,0.5f),
                       V(0.5f,-0.5f,-0.5f), V(0.5f,0.5f,-0.5f),
                       V(1,0,0),  u0,v0,u1,v1);
    // -X (left)
    addFace(verts,idx, V(-0.5f,0.5f,-0.5f), V(-0.5f,-0.5f,-0.5f),
                       V(-0.5f,-0.5f,0.5f),  V(-0.5f,0.5f,0.5f),
                       V(-1,0,0), u0,v0,u1,v1);
    // +Z (front)
    addFace(verts,idx, V(-0.5f,0.5f,0.5f), V(-0.5f,-0.5f,0.5f),
                       V(0.5f,-0.5f,0.5f),  V(0.5f,0.5f,0.5f),
                       V(0,0,1),  u0,v0,u1,v1);
    // -Z (back)
    addFace(verts,idx, V(0.5f,0.5f,-0.5f), V(0.5f,-0.5f,-0.5f),
                       V(-0.5f,-0.5f,-0.5f),V(-0.5f,0.5f,-0.5f),
                       V(0,0,-1), u0,v0,u1,v1);

    cube_idx_ = static_cast<int>(idx.size());

    glGenVertexArrays(1, &cube_vao_);
    glGenBuffers(1, &cube_vbo_);
    glGenBuffers(1, &cube_ebo_);
    glBindVertexArray(cube_vao_);

    glBindBuffer(GL_ARRAY_BUFFER, cube_vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verts.size() * sizeof(Vertex)),
                 verts.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cube_ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(idx.size() * sizeof(uint32_t)),
                 idx.data(), GL_STATIC_DRAW);

    const GLsizei stride = static_cast<GLsizei>(sizeof(Vertex));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(Vertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(Vertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(Vertex, nx)));

    glBindVertexArray(0);
}

// ─────────────────────────────────────────────────────────────────────────────
void TitleScreen::appendSeg(float* v, int& cnt,
                             float x0, float y0, float x1, float y1) const {
    v[cnt++] = x0; v[cnt++] = y0;
    v[cnt++] = x1; v[cnt++] = y1;
}

float TitleScreen::textWidth(const char* text, float cw, float gap) const {
    float w = 0.0f;
    bool first = true;
    for (const char* p = text; *p; ++p) {
        if (!first) w += gap;
        w += (*p == ' ') ? cw * 0.5f : cw;
        first = false;
    }
    return w;
}

void TitleScreen::appendText(float* v, int& cnt, const char* text,
                              float start_x, float y,
                              float cw, float ch, float gap) const {
    float cx = start_x;
    for (const char* p = text; *p; ++p) {
        if (*p == ' ') { cx += cw * 0.5f + gap; continue; }
        const Glyph* g = findGlyph(*p);
        if (!g) { cx += cw + gap; continue; }
        for (int i = 0; i < g->n; ++i) {
            const Seg& s = g->s[i];
            appendSeg(v, cnt,
                cx + s.x0 * cw, y + s.y0 * ch,
                cx + s.x1 * cw, y + s.y1 * ch);
        }
        cx += cw + gap;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void TitleScreen::buildTextBuffers() {
    static const int kBufSize = 1024;
    float title_v [kBufSize] = {};
    float prompt_v[kBufSize] = {};
    int   tc = 0, pc = 0;

    // ── Title "FT_VOX" ────────────────────────────────────────────────────────
    const char*  title  = "FT_VOX";
    const float  t_cw   = 0.115f;
    const float  t_ch   = 0.175f;
    const float  t_gap  = 0.028f;
    float t_w = textWidth(title, t_cw, t_gap);
    appendText(title_v, tc, title,
               -t_w * 0.5f, 0.58f, t_cw, t_ch, t_gap);

    // Thin separator line below title
    appendSeg(title_v, tc, -0.32f, 0.54f, 0.32f, 0.54f);

    title_cnt_ = tc / 2;

    // ── Prompt "PRESS SPACE" ──────────────────────────────────────────────────
    const char*  prompt = "PRESS SPACE";
    const float  p_cw   = 0.052f;
    const float  p_ch   = 0.076f;
    const float  p_gap  = 0.013f;
    float p_w = textWidth(prompt, p_cw, p_gap);
    appendText(prompt_v, pc, prompt,
               -p_w * 0.5f, -0.68f, p_cw, p_ch, p_gap);

    prompt_cnt_ = pc / 2;

    // Upload title buffer
    glGenVertexArrays(1, &title_vao_);
    glGenBuffers(1, &title_vbo_);
    glBindVertexArray(title_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, title_vbo_);
    glBufferData(GL_ARRAY_BUFFER, tc * sizeof(float), title_v, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);

    // Upload prompt buffer
    glGenVertexArrays(1, &prompt_vao_);
    glGenBuffers(1, &prompt_vbo_);
    glBindVertexArray(prompt_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, prompt_vbo_);
    glBufferData(GL_ARRAY_BUFFER, pc * sizeof(float), prompt_v, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);
}

// ─────────────────────────────────────────────────────────────────────────────
bool TitleScreen::init(TextureAtlas& atlas, Shader& chunk_shader, Shader& hud_shader) {
    chunk_shader_ = &chunk_shader;
    hud_shader_   = &hud_shader;
    atlas_tex_    = atlas.glId();

    buildCube(atlas.getUV(BlockType::Dirt));
    buildTextBuffers();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
bool TitleScreen::render(float dt, GLFWwindow* window, int w, int h) {
    elapsed_ += dt;
    angle_   += dt * 0.75f;  // gentle rotation speed

    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
        return true;

    // Dark starry-night background
    glClearColor(0.04f, 0.04f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // ── Rotating dirt block (3D) ─────────────────────────────────────────────
    const float aspect = (h > 0) ? static_cast<float>(w) / static_cast<float>(h) : 1.0f;

    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.4f, 3.6f),
                                 glm::vec3(0.0f, 0.0f, 0.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(50.0f), aspect, 0.1f, 100.0f);

    // Rotate around Y with a slight constant tilt on X for the classic 3D look
    glm::mat4 model(1.0f);
    model = glm::rotate(model, angle_,            glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, glm::radians(18.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::scale(model, glm::vec3(1.55f));

    glm::mat4 mvp = proj * view * model;

    chunk_shader_->use();
    chunk_shader_->setMat4 ("uMVP",         glm::value_ptr(mvp));
    chunk_shader_->setVec3 ("uSunDir",      0.55f, 0.85f, 0.25f);
    chunk_shader_->setFloat("uAmbient",     0.38f);
    chunk_shader_->setFloat("uSunStrength", 0.52f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas_tex_);
    chunk_shader_->setInt("uAtlas", 0);

    glBindVertexArray(cube_vao_);
    glDrawElements(GL_TRIANGLES, cube_idx_, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    // ── 2D Text overlay ──────────────────────────────────────────────────────
    glDisable(GL_DEPTH_TEST);
    hud_shader_->use();

    // Title: golden yellow
    hud_shader_->setVec4("uColor", 1.0f, 0.83f, 0.0f, 1.0f);
    glBindVertexArray(title_vao_);
    glDrawArrays(GL_LINES, 0, title_cnt_);

    // Prompt: pulsing white
    const float alpha = 0.45f + 0.55f * (0.5f + 0.5f * std::sin(elapsed_ * 2.8f));
    hud_shader_->setVec4("uColor", 1.0f, 1.0f, 1.0f, alpha);
    glBindVertexArray(prompt_vao_);
    glDrawArrays(GL_LINES, 0, prompt_cnt_);

    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);

    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
void TitleScreen::destroy() {
    if (cube_vao_)   { glDeleteVertexArrays(1, &cube_vao_);   cube_vao_   = 0; }
    if (cube_vbo_)   { glDeleteBuffers(1, &cube_vbo_);        cube_vbo_   = 0; }
    if (cube_ebo_)   { glDeleteBuffers(1, &cube_ebo_);        cube_ebo_   = 0; }
    if (title_vao_)  { glDeleteVertexArrays(1, &title_vao_);  title_vao_  = 0; }
    if (title_vbo_)  { glDeleteBuffers(1, &title_vbo_);       title_vbo_  = 0; }
    if (prompt_vao_) { glDeleteVertexArrays(1, &prompt_vao_); prompt_vao_ = 0; }
    if (prompt_vbo_) { glDeleteBuffers(1, &prompt_vbo_);      prompt_vbo_ = 0; }
}
