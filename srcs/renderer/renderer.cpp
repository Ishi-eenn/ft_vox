#include "renderer/renderer.hpp"
#include "types.hpp"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <array>
#include <cstdio>
#include <iostream>
#include <cstring>

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
Renderer::Renderer() = default;

Renderer::~Renderer() {
    chunk_shader_.destroy();
    sky_shader_.destroy();
    hud_shader_.destroy();
    if (hud_vao_) { glDeleteVertexArrays(1, &hud_vao_); hud_vao_ = 0; }
    if (hud_vbo_) { glDeleteBuffers(1, &hud_vbo_);      hud_vbo_ = 0; }
    atlas_.destroy();
    skybox_.destroy();
}

// ---------------------------------------------------------------------------
// init()
// ---------------------------------------------------------------------------
bool Renderer::init(GLFWwindow* window) {
    window_ = window;

    // Load OpenGL function pointers via GLAD (must be called after context is current)
    if (!gladLoadGL(glfwGetProcAddress)) {
        std::cerr << "[Renderer] gladLoadGL failed\n";
        return false;
    }

    std::cerr << "[Renderer] OpenGL " << glGetString(GL_VERSION)
              << "  GLSL " << glGetString(GL_SHADING_LANGUAGE_VERSION) << "\n";

    // ── Render state ────────────────────────────────────────────────────────
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // ── Shaders ─────────────────────────────────────────────────────────────
    if (!chunk_shader_.load("assets/shaders/chunk.vert", "assets/shaders/chunk.frag")) {
        std::cerr << "[Renderer] Failed to load chunk shaders\n";
        return false;
    }
    if (!sky_shader_.load("assets/shaders/skybox.vert", "assets/shaders/skybox.frag")) {
        std::cerr << "[Renderer] Failed to load skybox shaders\n";
        return false;
    }

    // ── Texture atlas ────────────────────────────────────────────────────────
    if (!atlas_.generate()) {
        std::cerr << "[Renderer] Failed to generate texture atlas\n";
        return false;
    }

    // ── Skybox geometry ──────────────────────────────────────────────────────
    if (!skybox_.init()) {
        std::cerr << "[Renderer] Failed to initialise skybox\n";
        return false;
    }

    // ── HUD (crosshair) ──────────────────────────────────────────────────────
    initHud();

    return true;
}

void Renderer::initHud() {
    hud_shader_.load("assets/shaders/hud.vert", "assets/shaders/hud.frag");

    glGenVertexArrays(1, &hud_vao_);
    glGenBuffers(1, &hud_vbo_);
    glBindVertexArray(hud_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, hud_vbo_);
    glBufferData(GL_ARRAY_BUFFER, 256 * 2 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glBindVertexArray(0);
}

void Renderer::appendLine(float* verts, int& count, float x0, float y0, float x1, float y1) const {
    verts[count++] = x0;
    verts[count++] = y0;
    verts[count++] = x1;
    verts[count++] = y1;
}

void Renderer::appendDigit(float* verts, int& count, int digit, float left, float top, float w, float h) const {
    static const uint8_t SEGMENTS[10] = {
        0b1111110, 0b0110000, 0b1101101, 0b1111001, 0b0110011,
        0b1011011, 0b1011111, 0b1110000, 0b1111111, 0b1111011
    };

    if (digit < 0 || digit > 9) return;

    const float right = left + w;
    const float mid   = top - h * 0.5f;
    const float bot   = top - h;
    const uint8_t mask = SEGMENTS[digit];

    if (mask & 0b1000000) appendLine(verts, count, left, top, right, top);
    if (mask & 0b0100000) appendLine(verts, count, right, top, right, mid);
    if (mask & 0b0010000) appendLine(verts, count, right, mid, right, bot);
    if (mask & 0b0001000) appendLine(verts, count, left, bot, right, bot);
    if (mask & 0b0000100) appendLine(verts, count, left, mid, left, bot);
    if (mask & 0b0000010) appendLine(verts, count, left, top, left, mid);
    if (mask & 0b0000001) appendLine(verts, count, left, mid, right, mid);
}

void Renderer::appendNumber(float* verts, int& count, int value, float right, float top, float w, float h, float gap) const {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", value);
    int len = 0;
    while (buf[len] != '\0') ++len;

    float total = len * w + (len > 0 ? (len - 1) * gap : 0.0f);
    float x = right - total;
    for (int i = 0; i < len; ++i) {
        appendDigit(verts, count, buf[i] - '0', x, top, w, h);
        x += w + gap;
    }
}

void Renderer::drawHud(int fps) {
    std::array<float, 256> verts{};
    int count = 0;

    float cx = 20.0f / (float)(width_  / 2);
    float cy = 20.0f / (float)(height_ / 2);
    appendLine(verts.data(), count, -cx, 0.f, cx, 0.f);
    appendLine(verts.data(), count, 0.f, -cy, 0.f, cy);

    float px = 14.0f / (float)(width_ / 2);
    float py = 18.0f / (float)(height_ / 2);
    float digit_w = 14.0f / (float)(width_ / 2);
    float digit_h = 24.0f / (float)(height_ / 2);
    float gap = 6.0f / (float)(width_ / 2);
    appendNumber(verts.data(), count, fps, 1.0f - px, 1.0f - py, digit_w, digit_h, gap);

    glBindBuffer(GL_ARRAY_BUFFER, hud_vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(float), verts.data());

    glDisable(GL_DEPTH_TEST);
    hud_shader_.use();
    glBindVertexArray(hud_vao_);
    glDrawArrays(GL_LINES, 0, count / 2);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

// ---------------------------------------------------------------------------
// uploadChunkMesh()
// ---------------------------------------------------------------------------
void Renderer::uploadChunkMesh(Chunk* chunk) {
    if (chunk->vertices.empty()) {
        // Nothing to upload — mark as clean so callers stop retrying
        chunk->is_dirty = false;
        return;
    }

    // Re-upload: destroy any existing GPU resources first
    if (chunk->gpu.uploaded) {
        destroyChunkMesh(chunk);
    }

    glGenVertexArrays(1, &chunk->gpu.vao);
    glGenBuffers(1, &chunk->gpu.vbo);
    glGenBuffers(1, &chunk->gpu.ebo);

    glBindVertexArray(chunk->gpu.vao);

    // Upload vertices
    glBindBuffer(GL_ARRAY_BUFFER, chunk->gpu.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(chunk->vertices.size() * sizeof(Vertex)),
                 chunk->vertices.data(),
                 GL_STATIC_DRAW);

    // Upload indices
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, chunk->gpu.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(chunk->indices.size() * sizeof(uint32_t)),
                 chunk->indices.data(),
                 GL_STATIC_DRAW);

    // Vertex attribute layout — must match struct Vertex:
    //   offset  0: position  (xyz)  — location 0
    //   offset 12: uv        (uv)   — location 1
    //   offset 20: normal    (xyz)  — location 2
    //   stride: sizeof(Vertex) = 32 bytes
    const GLsizei stride = static_cast<GLsizei>(sizeof(Vertex));

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                          (void*)offsetof(Vertex, x));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                          (void*)offsetof(Vertex, u));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride,
                          (void*)offsetof(Vertex, nx));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    // Note: do NOT unbind EBO while VAO is unbound — it was already recorded.

    chunk->gpu.idx_count = static_cast<int32_t>(chunk->indices.size());
    chunk->gpu.uploaded  = true;

    // Free CPU-side mesh data — it has been transferred to the GPU
    chunk->vertices.clear();
    chunk->vertices.shrink_to_fit();
    chunk->indices.clear();
    chunk->indices.shrink_to_fit();

    chunk->is_dirty = false;
}

// ---------------------------------------------------------------------------
// destroyChunkMesh()
// ---------------------------------------------------------------------------
void Renderer::destroyChunkMesh(Chunk* chunk) {
    if (!chunk->gpu.uploaded) return;

    glDeleteVertexArrays(1, &chunk->gpu.vao);
    glDeleteBuffers(1, &chunk->gpu.vbo);
    glDeleteBuffers(1, &chunk->gpu.ebo);

    chunk->gpu.vao       = 0;
    chunk->gpu.vbo       = 0;
    chunk->gpu.ebo       = 0;
    chunk->gpu.idx_count = 0;
    chunk->gpu.uploaded  = false;
}

// ---------------------------------------------------------------------------
// beginFrame()
// ---------------------------------------------------------------------------
void Renderer::beginFrame() {
    glClearColor(0.3f, 0.5f, 0.9f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

// ---------------------------------------------------------------------------
// drawChunk()
// ---------------------------------------------------------------------------
void Renderer::drawChunk(const Chunk* chunk, const float* view4x4, const float* proj4x4) {
    if (!chunk->gpu.uploaded || chunk->gpu.idx_count == 0) return;

    // Build model matrix: translate to chunk world position
    glm::mat4 model = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(
            static_cast<float>(chunk->pos.x) * static_cast<float>(CHUNK_SIZE_X),
            0.0f,
            static_cast<float>(chunk->pos.z) * static_cast<float>(CHUNK_SIZE_Z)
        )
    );

    // Reconstruct view and proj matrices from raw float pointers
    glm::mat4 view, proj;
    std::memcpy(glm::value_ptr(view), view4x4, 16 * sizeof(float));
    std::memcpy(glm::value_ptr(proj), proj4x4, 16 * sizeof(float));

    glm::mat4 mvp = proj * view * model;

    chunk_shader_.use();
    chunk_shader_.setMat4("uMVP", glm::value_ptr(mvp));

    atlas_.bind(0);
    chunk_shader_.setInt("uAtlas", 0);

    glBindVertexArray(chunk->gpu.vao);
    glDrawElements(GL_TRIANGLES,
                   static_cast<GLsizei>(chunk->gpu.idx_count),
                   GL_UNSIGNED_INT,
                   nullptr);
    glBindVertexArray(0);
}

// ---------------------------------------------------------------------------
// drawSkybox()
// ---------------------------------------------------------------------------
void Renderer::drawSkybox(const float* view3x3, const float* proj4x4) {
    skybox_.draw(view3x3, proj4x4, sky_shader_);
}

// ---------------------------------------------------------------------------
// endFrame()
// ---------------------------------------------------------------------------
void Renderer::endFrame() {
    glfwSwapBuffers(window_);
}

// ---------------------------------------------------------------------------
// onResize()
// ---------------------------------------------------------------------------
void Renderer::onResize(int w, int h) {
    glViewport(0, 0, w, h);
    width_  = w;
    height_ = h;
}
