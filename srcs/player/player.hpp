#pragma once
#include "player/camera.hpp"
#include "player/input_handler.hpp"
#include "types.hpp"
#include <functional>

class Player {
public:
    Player();

    void init(GLFWwindow* window);

    // isSolid: returns true if the world block at (x,y,z) is solid (not air/water).
    // Used for AABB collision detection in normal (non-flight) mode.
    void update(float dt, const std::function<bool(int,int,int)>& isSolid);

    Camera&       camera()       { return camera_; }
    const Camera& camera() const { return camera_; }
    InputHandler& input()        { return input_; }

    ChunkPos chunkPos() const;
    bool isFlyMode() const { return fly_mode_; }

    bool shouldClose() const { return input_.shouldClose(); }
    bool wasResized()  const { return input_.wasResized(); }
    int  resizeW()     const { return input_.resizeW(); }
    int  resizeH()     const { return input_.resizeH(); }
    void clearResize()       { input_.clearResize(); }

private:
    // Returns true if the player AABB at (px, py, pz) overlaps any solid block.
    static bool overlapsAny(float px, float py, float pz,
                             const std::function<bool(int,int,int)>& isSolid);

    Camera       camera_;
    InputHandler input_;

    // Speed modes
    bool  fast_mode_        = false;
    bool  shift_was_down_   = false;

    // Flight / normal toggle (double-tap Space, like Minecraft)
    bool  fly_mode_         = true;   // start in creative flight
    bool  space_was_down_   = false;
    int   space_tap_count_  = 0;
    float space_tap_timer_  = 0.0f;

    // Physics (normal mode)
    float velocity_y_       = 0.0f;
    bool  on_ground_        = false;
};
