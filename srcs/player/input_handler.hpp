#pragma once
#include <GLFW/glfw3.h>
#include <array>

class InputHandler {
public:
    void init(GLFWwindow* window);

    // Call each frame before reading input
    void newFrame();

    // Key held state (excludes ESC — managed internally)
    bool isHeld(int glfw_key) const;

    // Mouse delta (pixels since last frame, reset in newFrame)
    float mouseDX() const { return dx_; }
    float mouseDY() const { return dy_; }

    // One-shot click events (true for exactly one frame after press, cursor must be captured)
    bool wasLeftClicked()  const { return left_clicked_;  }
    bool wasRightClicked() const { return right_clicked_; }

    // True when the main loop should exit
    bool shouldClose() const { return glfwWindowShouldClose(window_) != 0; }

    // Cursor state
    bool isCursorCaptured() const { return cursor_captured_; }
    void captureCursor();
    void releaseCursor();

    // Window resize
    int  resizeW()     const { return resize_w_; }
    int  resizeH()     const { return resize_h_; }
    bool wasResized()  const { return resized_; }
    void clearResize()       { resized_ = false; }

    // GLFW callbacks (registered via glfwSetWindowUserPointer)
    static void keyCallback        (GLFWwindow*, int key, int scan, int action, int mods);
    static void cursorPosCallback  (GLFWwindow*, double xpos, double ypos);
    static void mouseButtonCallback(GLFWwindow*, int button, int action, int mods);
    static void focusCallback      (GLFWwindow*, int focused);
    static void resizeCallback     (GLFWwindow*, int width, int height);

private:
    static constexpr int KEY_COUNT = GLFW_KEY_LAST + 1;

    GLFWwindow* window_          = nullptr;
    std::array<bool, KEY_COUNT> keys_ = {};

    double last_x_ = 0.0, last_y_ = 0.0;
    float  dx_     = 0.0f, dy_    = 0.0f;
    bool   first_  = true;

    bool cursor_captured_ = false;
    bool left_clicked_    = false;
    bool right_clicked_   = false;

    int  resize_w_ = 0, resize_h_ = 0;
    bool resized_  = false;
};
