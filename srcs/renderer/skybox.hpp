#pragma once
#include <cstdint>

class Shader;

class Skybox {
public:
    ~Skybox();
    bool init();
    void draw(const float* view3x3, const float* proj4x4, Shader& shader);
    void destroy();

private:
    uint32_t vao_ = 0, vbo_ = 0;
};
