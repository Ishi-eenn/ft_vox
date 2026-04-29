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
#include <cmath>

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
Renderer::Renderer() = default;

Renderer::~Renderer() {
    chunk_shader_.destroy();
    sky_shader_.destroy();
    hud_shader_.destroy();
    if (hud_vao_)     { glDeleteVertexArrays(1, &hud_vao_);     hud_vao_     = 0; }
    if (hud_vbo_)     { glDeleteBuffers(1, &hud_vbo_);          hud_vbo_     = 0; }
    if (overlay_vao_) { glDeleteVertexArrays(1, &overlay_vao_); overlay_vao_ = 0; }
    if (overlay_vbo_) { glDeleteBuffers(1, &overlay_vbo_);      overlay_vbo_ = 0; }
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

    // HUD (crosshair + FPS digits) — dynamic line geometry
    glGenVertexArrays(1, &hud_vao_);
    glGenBuffers(1, &hud_vbo_);
    glBindVertexArray(hud_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, hud_vbo_);
    glBufferData(GL_ARRAY_BUFFER, 256 * 2 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glBindVertexArray(0);

    // Underwater overlay — fullscreen quad (two triangles in NDC)
    static const float quad[] = {
        -1.f, -1.f,  1.f, -1.f,  1.f,  1.f,
        -1.f, -1.f,  1.f,  1.f, -1.f,  1.f,
    };
    glGenVertexArrays(1, &overlay_vao_);
    glGenBuffers(1, &overlay_vbo_);
    glBindVertexArray(overlay_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, overlay_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
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
    hud_shader_.setVec4("uColor", 1.0f, 1.0f, 1.0f, 0.9f);
    glBindVertexArray(hud_vao_);
    glDrawArrays(GL_LINES, 0, count / 2);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

// ---------------------------------------------------------------------------
// drawUnderwaterOverlay()
// ---------------------------------------------------------------------------
void Renderer::drawUnderwaterOverlay() {
    glDisable(GL_DEPTH_TEST);
    hud_shader_.use();
    hud_shader_.setVec4("uColor", 0.0f, 0.25f, 0.65f, 0.40f);
    glBindVertexArray(overlay_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
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

    // Pack opaque indices followed by water indices into one EBO.
    // drawChunk draws [0, idx_count), drawChunkWater draws the tail.
    const size_t n_opaque = chunk->indices.size();
    const size_t n_water  = chunk->indices_water.size();
    std::vector<uint32_t> all_indices;
    all_indices.reserve(n_opaque + n_water);
    all_indices.insert(all_indices.end(), chunk->indices.begin(),       chunk->indices.end());
    all_indices.insert(all_indices.end(), chunk->indices_water.begin(), chunk->indices_water.end());

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, chunk->gpu.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(all_indices.size() * sizeof(uint32_t)),
                 all_indices.data(),
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

    chunk->gpu.idx_count       = static_cast<int32_t>(n_opaque);
    chunk->gpu.idx_count_water = static_cast<int32_t>(n_water);
    chunk->gpu.uploaded        = true;

    // Free CPU-side mesh data — it has been transferred to the GPU
    chunk->vertices.clear();
    chunk->vertices.shrink_to_fit();
    chunk->indices.clear();
    chunk->indices.shrink_to_fit();
    chunk->indices_water.clear();
    chunk->indices_water.shrink_to_fit();

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

    chunk->gpu.vao             = 0;
    chunk->gpu.vbo             = 0;
    chunk->gpu.ebo             = 0;
    chunk->gpu.idx_count       = 0;
    chunk->gpu.idx_count_water = 0;
    chunk->gpu.uploaded        = false;
}

// ---------------------------------------------------------------------------
// beginFrame()
// ---------------------------------------------------------------------------
void Renderer::beginFrame() {
    // Match the clear colour to the sky horizon so there's no seam if chunks are
    // missing at the edges of the view.
    glClearColor(sky_horizon_[0], sky_horizon_[1], sky_horizon_[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

// ---------------------------------------------------------------------------
// buildMVP() — shared helper
// ---------------------------------------------------------------------------
static glm::mat4 buildMVP(const Chunk* chunk,
                           const float* view4x4, const float* proj4x4) {
    glm::mat4 model = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(
            static_cast<float>(chunk->pos.x) * static_cast<float>(CHUNK_SIZE_X),
            0.0f,
            static_cast<float>(chunk->pos.z) * static_cast<float>(CHUNK_SIZE_Z)
        )
    );
    glm::mat4 view = glm::make_mat4(view4x4);
    glm::mat4 proj = glm::make_mat4(proj4x4);
    return proj * view * model;
}

// ---------------------------------------------------------------------------
// drawChunk() — opaque pass only
// ---------------------------------------------------------------------------
// Set the lighting uniforms shared by both the opaque and water passes.
// Called once before the first drawChunk each frame.
static void setChunkLightingUniforms(Shader& shader,
                                     const float sun_dir[3],
                                     float ambient, float sun_strength) {
    shader.setVec3 ("uSunDir",      sun_dir[0],   sun_dir[1],   sun_dir[2]);
    shader.setFloat("uAmbient",     ambient);
    shader.setFloat("uSunStrength", sun_strength);
}

void Renderer::drawChunk(const Chunk* chunk, const float* view4x4, const float* proj4x4) {
    if (!chunk->gpu.uploaded || chunk->gpu.idx_count == 0) return;

    glm::mat4 mvp = buildMVP(chunk, view4x4, proj4x4);

    chunk_shader_.use();
    chunk_shader_.setMat4("uMVP", glm::value_ptr(mvp));
    setChunkLightingUniforms(chunk_shader_, sun_dir_, ambient_, sun_strength_);
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
// drawChunkWater() — transparent pass (no depth writes so terrain shows through)
// ---------------------------------------------------------------------------
void Renderer::drawChunkWater(const Chunk* chunk, const float* view4x4, const float* proj4x4) {
    if (!chunk->gpu.uploaded || chunk->gpu.idx_count_water == 0) return;

    glm::mat4 mvp = buildMVP(chunk, view4x4, proj4x4);

    chunk_shader_.use();
    chunk_shader_.setMat4("uMVP", glm::value_ptr(mvp));
    setChunkLightingUniforms(chunk_shader_, sun_dir_, ambient_, sun_strength_);
    atlas_.bind(0);
    chunk_shader_.setInt("uAtlas", 0);

    // Disable depth writes so transparent water doesn't occlude geometry behind it.
    // Depth test still reads, so water is occluded by closer solid surfaces.
    glDepthMask(GL_FALSE);

    glBindVertexArray(chunk->gpu.vao);
    const GLintptr water_offset =
        static_cast<GLintptr>(chunk->gpu.idx_count) * static_cast<GLintptr>(sizeof(uint32_t));
    glDrawElements(GL_TRIANGLES,
                   static_cast<GLsizei>(chunk->gpu.idx_count_water),
                   GL_UNSIGNED_INT,
                   reinterpret_cast<const void*>(water_offset));
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
}

// ---------------------------------------------------------------------------
// drawSkybox()
// ---------------------------------------------------------------------------
void Renderer::drawSkybox(const float* view3x3, const float* proj4x4) {
    // Set sky uniforms before the draw call (Skybox::draw will call use() again,
    // but that's the same program so the uniforms are already uploaded).
    sky_shader_.use();
    sky_shader_.setVec3("uSkyZenith",   sky_zenith_[0],  sky_zenith_[1],  sky_zenith_[2]);
    sky_shader_.setVec3("uSkyHorizon",  sky_horizon_[0], sky_horizon_[1], sky_horizon_[2]);
    sky_shader_.setVec3("uGroundColor", sky_ground_[0],  sky_ground_[1],  sky_ground_[2]);
    sky_shader_.setVec3("uSunDir",      sun_dir_[0],     sun_dir_[1],     sun_dir_[2]);
    sky_shader_.setVec3("uSunColor",    sun_color_[0],   sun_color_[1],   sun_color_[2]);
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

// ---------------------------------------------------------------------------
// setTimeOfDay()
// t ∈ [0, 1):  0.0 = midnight, 0.25 = sunrise, 0.5 = noon, 0.75 = sunset
// ---------------------------------------------------------------------------
void Renderer::setTimeOfDay(float t) {
    // ── Sun direction ─────────────────────────────────────────────────────────
    // angle = 0 at midnight; sun peaks overhead (y=1) at t=0.5 (noon).
    // X axis = east–west; slight Z tilt for visual interest (northern hemisphere).
    const float angle = 2.0f * static_cast<float>(M_PI) * t;
    const float raw_x = std::sinf(angle);
    const float raw_y = -std::cosf(angle);
    const float raw_z = 0.30f;
    const float len   = std::sqrtf(raw_x*raw_x + raw_y*raw_y + raw_z*raw_z);
    sun_dir_[0] = raw_x / len;
    sun_dir_[1] = raw_y / len;
    sun_dir_[2] = raw_z / len;

    const float elev = sun_dir_[1];  // -1 = below horizon, +1 = overhead

    // Helper: linear interpolation between two 3-component colours.
    auto lerpCol = [](float* dst,
                      const float a[3], const float b[3], float f) {
        for (int i = 0; i < 3; ++i)
            dst[i] = a[i] + (b[i] - a[i]) * f;
    };
    auto clamp01 = [](float x) { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); };

    // ── Reference sky colours ─────────────────────────────────────────────────
    static const float kNightZenith [3] = {0.01f, 0.02f, 0.08f};
    static const float kNightHorizon[3] = {0.02f, 0.04f, 0.12f};
    static const float kNightGround [3] = {0.01f, 0.01f, 0.03f};

    static const float kDawnZenith  [3] = {0.12f, 0.22f, 0.52f};
    static const float kDawnHorizon [3] = {0.90f, 0.40f, 0.10f};
    static const float kDawnGround  [3] = {0.20f, 0.15f, 0.10f};

    static const float kDayZenith   [3] = {0.08f, 0.25f, 0.65f};
    static const float kDayHorizon  [3] = {0.55f, 0.72f, 0.90f};
    static const float kDayGround   [3] = {0.35f, 0.30f, 0.25f};

    static const float kSunColorDawn[3] = {1.00f, 0.60f, 0.20f};
    static const float kSunColorDay [3] = {1.00f, 0.98f, 0.85f};
    static const float kSunColorDusk[3] = {1.00f, 0.50f, 0.10f};

    // ── Map elevation to blended sky colours ─────────────────────────────────
    // Use sun elevation as the blend driver so colours track actual sun position:
    //   elev ≤ -0.15  →  pure night
    //   elev  ∈ [-0.15, 0.15]  →  dawn/dusk blend
    //   elev ≥  0.15  →  day (deeper blue as elev → 1)
    if (elev <= -0.15f) {
        // Full night
        for (int i = 0; i < 3; ++i) {
            sky_zenith_ [i] = kNightZenith [i];
            sky_horizon_[i] = kNightHorizon[i];
            sky_ground_ [i] = kNightGround [i];
            sun_color_  [i] = kSunColorDay [i];
        }
        ambient_      = 0.04f;
        sun_strength_ = 0.0f;
    } else if (elev < 0.15f) {
        // Dawn / dusk transition
        const float f = clamp01((elev + 0.15f) / 0.30f);  // 0=night, 1=day
        lerpCol(sky_zenith_,  kNightZenith,  kDawnZenith,  f);
        lerpCol(sky_horizon_, kNightHorizon, kDawnHorizon, f);
        lerpCol(sky_ground_,  kNightGround,  kDawnGround,  f);
        // Sun colour depends on whether we're rising (t < 0.5) or setting (t > 0.5)
        const float* sunrise_col = (t < 0.5f) ? kSunColorDawn : kSunColorDusk;
        lerpCol(sun_color_, kSunColorDay, sunrise_col, 1.0f - f);
        ambient_      = 0.04f + 0.14f * f;
        sun_strength_ = 0.30f * f;
    } else {
        // Daytime — sky deepens towards zenith as sun rises higher
        const float day_f = clamp01((elev - 0.15f) / 0.85f);  // 0=low sun, 1=overhead
        lerpCol(sky_zenith_,  kDawnZenith,  kDayZenith,  day_f);
        lerpCol(sky_horizon_, kDawnHorizon, kDayHorizon, day_f);
        lerpCol(sky_ground_,  kDawnGround,  kDayGround,  day_f);
        lerpCol(sun_color_,   kSunColorDawn, kSunColorDay, day_f);
        ambient_      = 0.18f + 0.14f * day_f;
        sun_strength_ = 0.30f + 0.35f * day_f;
    }

    // Never let the sun contribute diffuse light when it is below the horizon
    if (elev < 0.0f) sun_strength_ = 0.0f;
}
