#include "core/engine.hpp"

// glad MUST be included before GLFW
#define GLAD_GL_IMPLEMENTATION
#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "renderer/renderer.hpp"
#include "renderer/frustum.hpp"
#include "world/world.hpp"
#include "player/player.hpp"
#include "streaming/chunk_manager.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// Engine::Impl
// ─────────────────────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
// DDA ray–voxel intersection
// ─────────────────────────────────────────────────────────────────────────────
struct RayHit {
    bool hit = false;
    int  bx, by, bz;  // world pos of the block that was hit
    int  nx, ny, nz;  // world pos of the air block just before (for placement)
};

static RayHit castRay(const glm::vec3& origin, const glm::vec3& dir,
                      float max_dist, World& world)
{
    RayHit result;

    int ix = (int)std::floor(origin.x);
    int iy = (int)std::floor(origin.y);
    int iz = (int)std::floor(origin.z);

    int sx = (dir.x >= 0.f) ? 1 : -1;
    int sy = (dir.y >= 0.f) ? 1 : -1;
    int sz = (dir.z >= 0.f) ? 1 : -1;

    float tdx = (std::abs(dir.x) > 1e-6f) ? std::abs(1.f / dir.x) : 1e30f;
    float tdy = (std::abs(dir.y) > 1e-6f) ? std::abs(1.f / dir.y) : 1e30f;
    float tdz = (std::abs(dir.z) > 1e-6f) ? std::abs(1.f / dir.z) : 1e30f;

    float tx = (sx > 0) ? ((ix + 1) - origin.x) * tdx : (origin.x - ix) * tdx;
    float ty = (sy > 0) ? ((iy + 1) - origin.y) * tdy : (origin.y - iy) * tdy;
    float tz = (sz > 0) ? ((iz + 1) - origin.z) * tdz : (origin.z - iz) * tdz;

    int px = ix, py = iy, pz = iz;

    for (;;) {
        BlockType bt = world.getWorldBlock(ix, iy, iz);
        if (bt != BlockType::Air && bt != BlockType::Water) {
            result.hit = true;
            result.bx = ix;  result.by = iy;  result.bz = iz;
            result.nx = px;  result.ny = py;  result.nz = pz;
            return result;
        }

        px = ix;  py = iy;  pz = iz;

        float tmin = std::min({tx, ty, tz});
        if (tmin > max_dist) break;

        if (tx <= ty && tx <= tz) { ix += sx;  tx += tdx; }
        else if (ty <= tz)        { iy += sy;  ty += tdy; }
        else                      { iz += sz;  tz += tdz; }
    }
    return result;
}

// Rebuild the chunk that owns (wx,*,wz) and any chunk-boundary neighbors.
static void rebuildModified(int wx, int wz, ChunkManager& mgr) {
    int cx = (int)std::floor((float)wx / CHUNK_SIZE_X);
    int cz = (int)std::floor((float)wz / CHUNK_SIZE_Z);
    int lx = wx - cx * CHUNK_SIZE_X;
    int lz = wz - cz * CHUNK_SIZE_Z;

    mgr.rebuildChunkAt({cx, cz});
    if (lx == 0)               mgr.rebuildChunkAt({cx - 1, cz});
    if (lx == CHUNK_SIZE_X-1)  mgr.rebuildChunkAt({cx + 1, cz});
    if (lz == 0)               mgr.rebuildChunkAt({cx, cz - 1});
    if (lz == CHUNK_SIZE_Z-1)  mgr.rebuildChunkAt({cx, cz + 1});
}

// ─────────────────────────────────────────────────────────────────────────────
// Engine::Impl
// ─────────────────────────────────────────────────────────────────────────────
struct Engine::Impl {
    Renderer      renderer;
    World         world;
    Player        player;
    ChunkManager* chunk_mgr = nullptr;  // constructed after world + renderer

    BlockType selected_block = BlockType::Stone;

    ~Impl() { delete chunk_mgr; }
};

// ─────────────────────────────────────────────────────────────────────────────
Engine::Engine()  : impl_(std::make_unique<Impl>()) {}
Engine::~Engine() { shutdown(); }

// ─────────────────────────────────────────────────────────────────────────────
bool Engine::init(uint32_t seed, int width, int height) {
    width_  = width;
    height_ = height;

    // ── GLFW init ────────────────────────────────────────────────────────────
    if (!glfwInit()) {
        fprintf(stderr, "[Engine] glfwInit failed\n");
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    glfwWindowHint(GLFW_RESIZABLE, GL_TRUE);

    GLFWmonitor*       monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode    = glfwGetVideoMode(monitor);
    width_  = mode->width;
    height_ = mode->height;

    window_ = glfwCreateWindow(width_, height_, "ft_vox", monitor, nullptr);
    if (!window_) {
        fprintf(stderr, "[Engine] glfwCreateWindow failed\n");
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);  // vsync

    // On Retina/HiDPI displays the framebuffer is larger than the window size
    // reported by glfwGetVideoMode (logical pixels).  Use the actual framebuffer
    // dimensions for the OpenGL viewport and aspect ratio.
    int fb_w, fb_h;
    glfwGetFramebufferSize(window_, &fb_w, &fb_h);
    width_  = fb_w;
    height_ = fb_h;

    // ── Renderer (loads GLAD + sets up OpenGL state) ─────────────────────────
    if (!impl_->renderer.init(window_)) {
        fprintf(stderr, "[Engine] Renderer::init failed\n");
        return false;
    }
    impl_->renderer.onResize(width_, height_);

    // ── World ─────────────────────────────────────────────────────────────────
    impl_->world.setSeed(seed);

    // ── Player / camera ───────────────────────────────────────────────────────
    impl_->player.init(window_);
    impl_->player.camera().setAspect((float)width_ / (float)height_);

    // ── Chunk manager ─────────────────────────────────────────────────────────
    impl_->chunk_mgr = new ChunkManager(impl_->world, impl_->renderer);

    running_ = true;
    fprintf(stderr, "[Engine] init OK  seed=%u  %dx%d\n", seed, width_, height_);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
void Engine::run() {
    using Clock = std::chrono::steady_clock;
    auto prev   = Clock::now();
    float fps_timer       = 0.0f;
    int   fps_frames      = 0;
    int   fps_display     = 0;
    float rd_adjust_timer = 0.0f;  // throttle: adjust render distance at most once/sec

    while (running_) {
        // ── Delta time ────────────────────────────────────────────────────────
        auto  now = Clock::now();
        float dt  = std::chrono::duration<float>(now - prev).count();
        prev      = now;
        if (dt > 0.1f) dt = 0.1f;  // cap to prevent spiral-of-death
        fps_timer       += dt;
        rd_adjust_timer += dt;
        ++fps_frames;
        if (fps_timer >= 0.25f) {
            fps_display = (int)std::round((float)fps_frames / fps_timer);
            fps_timer  = 0.0f;
            fps_frames = 0;
        }

        // ── Dynamic render distance ───────────────────────────────────────────
        // Adjust at most once per second with hysteresis to avoid oscillation:
        //   < 30 FPS → shrink  (free up GPU work)
        //   > 55 FPS → grow    (use available headroom)
        if (rd_adjust_timer >= 1.0f) {
            rd_adjust_timer = 0.0f;
            int cur = impl_->chunk_mgr->renderDistance();
            if (fps_display > 0 && fps_display < 30 && cur > RENDER_DISTANCE_MIN) {
                impl_->chunk_mgr->setRenderDistance(cur - 1);
            } else if (fps_display > 55 && cur < RENDER_DISTANCE_MAX) {
                impl_->chunk_mgr->setRenderDistance(cur + 1);
            }
        }

        // ── Input + movement (Player calls glfwPollEvents internally) ─────────
        impl_->player.update(dt);
        if (impl_->player.shouldClose()) break;

        // ── Window resize ─────────────────────────────────────────────────────
        if (impl_->player.wasResized()) {
            width_  = impl_->player.resizeW();
            height_ = impl_->player.resizeH();
            if (width_ > 0 && height_ > 0) {
                impl_->renderer.onResize(width_, height_);
                impl_->player.camera().setAspect((float)width_ / (float)height_);
            }
            impl_->player.clearResize();
        }

        // ── Block type selection (keys 1-6) ───────────────────────────────────
        {
            InputHandler& inp = impl_->player.input();
            for (int k = GLFW_KEY_1; k <= GLFW_KEY_6; ++k) {
                if (inp.isHeld(k)) {
                    impl_->selected_block =
                        static_cast<BlockType>(k - GLFW_KEY_1 + 1);
                    break;
                }
            }

            // ── Block interaction (ray cast, max reach 6 blocks) ──────────────
            if (inp.isCursorCaptured()) {
                glm::vec3 pos   = impl_->player.camera().position();
                glm::vec3 front = impl_->player.camera().front();
                RayHit hit = castRay(pos, front, 6.0f, impl_->world);

                if (hit.hit) {
                    if (inp.wasLeftClicked()) {
                        impl_->world.setWorldBlock(hit.bx, hit.by, hit.bz,
                                                   BlockType::Air);
                        rebuildModified(hit.bx, hit.bz, *impl_->chunk_mgr);
                    }
                    if (inp.wasRightClicked()) {
                        impl_->world.setWorldBlock(hit.nx, hit.ny, hit.nz,
                                                   impl_->selected_block);
                        rebuildModified(hit.nx, hit.nz, *impl_->chunk_mgr);
                    }
                }
            }
        }

        // ── Chunk streaming ───────────────────────────────────────────────────
        glm::vec3 ppos = impl_->player.camera().position();
        impl_->chunk_mgr->update(ppos.x, ppos.z, frame_);

        // ── Build matrices ────────────────────────────────────────────────────
        float view4x4[16], proj4x4[16];
        float aspect = (height_ > 0) ? (float)width_ / (float)height_ : 1.0f;
        impl_->player.camera().getViewMatrix(view4x4);
        impl_->player.camera().getProjMatrix(proj4x4, aspect);

        // Sky view = rotation only (strip translation to keep sky fixed around camera)
        glm::mat4 view, proj;
        std::memcpy(glm::value_ptr(view), view4x4, 64);
        std::memcpy(glm::value_ptr(proj), proj4x4, 64);
        glm::mat4 sky_view = glm::mat4(glm::mat3(view));
        float sky_view4x4[16];
        std::memcpy(sky_view4x4, glm::value_ptr(sky_view), 64);

        // ── Frustum culling ───────────────────────────────────────────────────
        Frustum frustum;
        frustum.extractFromVP(proj * view);

        // ── Draw ─────────────────────────────────────────────────────────────
        impl_->renderer.beginFrame();
        impl_->renderer.drawSkybox(sky_view4x4, proj4x4);

        auto visible = impl_->chunk_mgr->getVisibleChunks(frustum);

        // Pass 1: opaque geometry (writes to depth buffer)
        for (Chunk* c : visible) {
            impl_->renderer.drawChunk(c, view4x4, proj4x4);
        }

        // Pass 2: transparent geometry (reads depth, does NOT write depth)
        // This ensures terrain is always visible through water from any angle.
        for (Chunk* c : visible) {
            impl_->renderer.drawChunkWater(c, view4x4, proj4x4);
        }

        impl_->renderer.drawHud(fps_display);
        impl_->renderer.endFrame();
        ++frame_;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void Engine::shutdown() {
    if (!window_) return;  // guard against double-call

    // Destroy all GL resources while context is still alive.
    // ~Impl() will delete chunk_mgr (which calls ChunkManager::destroyAll),
    // then ~Renderer destroys shaders/skybox/atlas.
    impl_.reset();

    glfwDestroyWindow(window_);
    window_ = nullptr;
    glfwTerminate();

    fprintf(stderr, "[Engine] shutdown complete  frames=%llu\n",
            (unsigned long long)frame_);
}
