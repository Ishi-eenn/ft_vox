#pragma once
#include <array>
#include <glm/glm.hpp>

struct AABB {
    glm::vec3 min, max;
};

class Frustum {
public:
    void extractFromVP(const glm::mat4& view_proj);
    bool isAABBVisible(const AABB& box) const;

private:
    std::array<glm::vec4, 6> planes_;  // ax+by+cz+d=0
};
