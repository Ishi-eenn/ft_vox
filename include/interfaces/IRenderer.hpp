#pragma once
#include "types.hpp"
#include <cstdint>

struct GLFWwindow;

class IRenderer {
public:
    virtual ~IRenderer() = default;
    virtual bool init(GLFWwindow* window)           = 0;
    virtual void uploadChunkMesh(Chunk* chunk)       = 0;
    virtual void destroyChunkMesh(Chunk* chunk)      = 0;
    virtual void beginFrame()                        = 0;
    virtual void drawChunk(const Chunk* chunk,
                           const float* view4x4,
                           const float* proj4x4)    = 0;
    virtual void drawSkybox(const float* view3x3,
                            const float* proj4x4)   = 0;
    virtual void endFrame()                          = 0;
    virtual void onResize(int w, int h)              = 0;
};
