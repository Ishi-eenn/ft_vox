#include "player/input_handler.hpp"
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
void InputHandler::init(GLFWwindow* window) {
    window_ = window;
    glfwSetWindowUserPointer(window, this);

    glfwSetKeyCallback(window,              keyCallback);
    glfwSetCursorPosCallback(window,        cursorPosCallback);
    glfwSetMouseButtonCallback(window,      mouseButtonCallback);
    glfwSetWindowFocusCallback(window,      focusCallback);
    glfwSetFramebufferSizeCallback(window,  resizeCallback);

    // Capture cursor on startup
    captureCursor();
}

// ─────────────────────────────────────────────────────────────────────────────
void InputHandler::captureCursor() {
    glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    cursor_captured_ = true;
    first_ = true;  // discard stale delta after re-capture
}

void InputHandler::releaseCursor() {
    glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    cursor_captured_ = false;
    first_ = true;
}

// ─────────────────────────────────────────────────────────────────────────────
void InputHandler::newFrame() {
    dx_ = 0.0f;
    dy_ = 0.0f;
    left_clicked_  = false;
    right_clicked_ = false;
}

bool InputHandler::isHeld(int key) const {
    if (key < 0 || key >= KEY_COUNT) return false;
    return keys_[key];
}

// ─────────────────────────────────────────────────────────────────────────────
// Callbacks
// ─────────────────────────────────────────────────────────────────────────────

void InputHandler::keyCallback(GLFWwindow* w, int key, int /*sc*/, int action, int /*mods*/) {
    auto* self = (InputHandler*)glfwGetWindowUserPointer(w);
    if (!self) return;

    // ESC: first press = release cursor; second press (cursor free) = close
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        if (self->cursor_captured_) {
            self->releaseCursor();
            fprintf(stderr, "[Input] Cursor released. ESC again to quit.\n");
        } else {
            glfwSetWindowShouldClose(w, GLFW_TRUE);
        }
        return;
    }

    if (key < 0 || key >= KEY_COUNT) return;
    if (action == GLFW_PRESS)   self->keys_[key] = true;
    if (action == GLFW_RELEASE) self->keys_[key] = false;
}

void InputHandler::cursorPosCallback(GLFWwindow* w, double xpos, double ypos) {
    auto* self = (InputHandler*)glfwGetWindowUserPointer(w);
    if (!self || !self->cursor_captured_) return;

    if (self->first_) {
        self->last_x_ = xpos;
        self->last_y_ = ypos;
        self->first_  = false;
        return;
    }
    self->dx_ += (float)(xpos - self->last_x_);
    self->dy_ += (float)(ypos - self->last_y_);
    self->last_x_ = xpos;
    self->last_y_ = ypos;
}

void InputHandler::mouseButtonCallback(GLFWwindow* w, int button, int action, int /*mods*/) {
    auto* self = (InputHandler*)glfwGetWindowUserPointer(w);
    if (!self) return;

    if (action == GLFW_PRESS) {
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            if (!self->cursor_captured_)
                self->captureCursor();
            else
                self->left_clicked_ = true;
        } else if (button == GLFW_MOUSE_BUTTON_RIGHT && self->cursor_captured_) {
            self->right_clicked_ = true;
        }
    }
}

void InputHandler::focusCallback(GLFWwindow* w, int focused) {
    auto* self = (InputHandler*)glfwGetWindowUserPointer(w);
    if (!self) return;
    // Release cursor when window loses focus so the OS can regain control
    if (!focused && self->cursor_captured_) {
        self->releaseCursor();
    }
}

void InputHandler::resizeCallback(GLFWwindow* w, int width, int height) {
    auto* self = (InputHandler*)glfwGetWindowUserPointer(w);
    if (!self) return;
    self->resize_w_ = width;
    self->resize_h_ = height;
    self->resized_  = true;
}
