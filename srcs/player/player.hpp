#pragma once
#include "player/camera.hpp"
#include "player/input_handler.hpp"
#include "types.hpp"

class Player {
public:
    Player();

    void init(GLFWwindow* window);
    void update(float dt);

    Camera&       camera()       { return camera_; }
    const Camera& camera() const { return camera_; }
    InputHandler& input()        { return input_; }

    // Returns the chunk position the player is currently in
    ChunkPos chunkPos() const;

    bool shouldClose() const { return input_.shouldClose(); }
    bool wasResized()  const { return input_.wasResized(); }
    int  resizeW()     const { return input_.resizeW(); }
    int  resizeH()     const { return input_.resizeH(); }
    void clearResize()       { input_.clearResize(); }

private:
    Camera       camera_;
    InputHandler input_;
};
