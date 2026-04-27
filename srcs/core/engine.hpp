#pragma once
#include <cstdint>
#include <memory>

struct GLFWwindow;

class Engine {
public:
    Engine();
    ~Engine();

    bool init(uint32_t seed, int width = 1280, int height = 720);
    void run();
    void shutdown();

private:
    void processInput(float dt);
    void update(float dt);
    void render();

    GLFWwindow* window_   = nullptr;
    int         width_    = 1280;
    int         height_   = 720;
    uint64_t    frame_    = 0;
    bool        running_  = false;

    // Filled in by integration agent (engine.cpp)
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
