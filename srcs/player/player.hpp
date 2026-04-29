#pragma once
#include "player/camera.hpp"
#include "player/input_handler.hpp"
#include "types.hpp"
#include <functional>

class Player {
public:
    Player();

    void init(GLFWwindow* window);

    // isSolid: true if block is solid (not air/water) — used for AABB collision.
    // isWater: true if block is water — used for underwater physics/visuals.
    void update(float dt,
                const std::function<bool(int,int,int)>& isSolid,
                const std::function<bool(int,int,int)>& isWater);

    Camera&       camera()       { return camera_; }
    const Camera& camera() const { return camera_; }
    InputHandler& input()        { return input_; }

    ChunkPos chunkPos() const;
    bool isFlyMode()  const { return fly_mode_; }
    bool isInWater()  const { return in_water_; }

    bool shouldClose() const { return input_.shouldClose(); }
    bool wasResized()  const { return input_.wasResized(); }
    int  resizeW()     const { return input_.resizeW(); }
    int  resizeH()     const { return input_.resizeH(); }
    void clearResize()       { input_.clearResize(); }

private:
    static bool overlapsAny(float px, float py, float pz,
                             const std::function<bool(int,int,int)>& isSolid);

    Camera       camera_;
    InputHandler input_;

    bool  fast_mode_        = false;
    bool  shift_was_down_   = false;

    bool  fly_mode_         = true;
    bool  space_was_down_   = false;
    int   space_tap_count_  = 0;
    float space_tap_timer_  = 0.0f;

    float velocity_y_       = 0.0f;
    bool  on_ground_        = false;
    bool  in_water_         = false;
};
