#pragma once
#include <cstdint>
#include <memory>
#include <string>

struct GLFWwindow;

class Engine {
public:
    Engine();
    ~Engine();

    bool init(uint32_t seed, int width = 1280, int height = 720);
    // Optionally call before run() to connect to a multiplayer server.
    // host: IP address string, port: TCP port.
    bool connectToServer(const char* host, uint16_t port);
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

    struct Impl;
    std::unique_ptr<Impl> impl_;
};
