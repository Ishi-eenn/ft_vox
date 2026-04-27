#pragma once
#include "interfaces/ICamera.hpp"
#include <glm/glm.hpp>

class Camera : public ICamera {
public:
    Camera();

    void setPosition(float x, float y, float z);
    void setYaw(float yaw);
    void setPitch(float pitch);  // auto-clamps to [-89, 89]
    void setAspect(float aspect);

    glm::vec3 position() const { return pos_; }
    glm::vec3 front()    const { return front_; }
    glm::vec3 right()    const { return right_; }
    glm::vec3 up()       const { return up_; }

    glm::mat4 viewMatrix() const;
    glm::mat4 projMatrix() const;

    // ICamera
    void  getViewMatrix(float out4x4[16]) const override;
    void  getProjMatrix(float out4x4[16], float aspect) const override;
    void  getPosition(float out3[3])      const override;
    float getYaw()   const override { return yaw_; }
    float getPitch() const override { return pitch_; }

private:
    void updateVectors();

    glm::vec3 pos_    = {0.0f, 70.0f, 0.0f};  // spawn above terrain
    float     yaw_    = -90.0f;  // facing -Z initially
    float     pitch_  = 0.0f;
    float     aspect_ = 16.0f / 9.0f;

    // Derived
    glm::vec3 front_ = {0.0f, 0.0f, -1.0f};
    glm::vec3 right_ = {1.0f, 0.0f,  0.0f};
    glm::vec3 up_    = {0.0f, 1.0f,  0.0f};
};
