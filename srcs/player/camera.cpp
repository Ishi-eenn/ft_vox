#include "player/camera.hpp"
#include "types.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>

Camera::Camera() { updateVectors(); }

void Camera::setPosition(float x, float y, float z) { pos_ = {x, y, z}; }
void Camera::setAspect(float aspect) { aspect_ = aspect; }

void Camera::setYaw(float yaw) {
    yaw_ = yaw;
    // Normalize to [-180, 180]
    while (yaw_ >  180.0f) yaw_ -= 360.0f;
    while (yaw_ < -180.0f) yaw_ += 360.0f;
    updateVectors();
}

void Camera::setPitch(float pitch) {
    pitch_ = glm::clamp(pitch, -89.0f, 89.0f);
    updateVectors();
}

void Camera::updateVectors() {
    float yr = glm::radians(yaw_);
    float pr = glm::radians(pitch_);
    front_.x = glm::cos(yr) * glm::cos(pr);
    front_.y = glm::sin(pr);
    front_.z = glm::sin(yr) * glm::cos(pr);
    front_   = glm::normalize(front_);
    right_   = glm::normalize(glm::cross(front_, glm::vec3(0,1,0)));
    up_      = glm::normalize(glm::cross(right_, front_));
}

glm::mat4 Camera::viewMatrix() const {
    return glm::lookAt(pos_, pos_ + front_, glm::vec3(0,1,0));
}

glm::mat4 Camera::projMatrix() const {
    return glm::perspective(glm::radians(FOV_DEGREES), aspect_, NEAR_PLANE, FAR_PLANE);
}

void Camera::getViewMatrix(float out4x4[16]) const {
    glm::mat4 m = viewMatrix();
    std::copy(glm::value_ptr(m), glm::value_ptr(m) + 16, out4x4);
}

void Camera::getProjMatrix(float out4x4[16], float aspect) const {
    glm::mat4 m = glm::perspective(glm::radians(FOV_DEGREES), aspect, NEAR_PLANE, FAR_PLANE);
    std::copy(glm::value_ptr(m), glm::value_ptr(m) + 16, out4x4);
}

void Camera::getPosition(float out3[3]) const {
    out3[0] = pos_.x; out3[1] = pos_.y; out3[2] = pos_.z;
}
