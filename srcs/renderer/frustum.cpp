#include "frustum.hpp"
#include <glm/glm.hpp>

// ---------------------------------------------------------------------------
// extractFromVP — Gribb / Hartmann fast frustum extraction
//
// GLM uses column-major storage: m[col][row]
// For a combined view-projection matrix VP, the clip planes are:
//
//   Left  : row3 + row0   =>  VP[0][3]+VP[0][0], VP[1][3]+VP[1][0], ...
//   Right : row3 - row0
//   Bottom: row3 + row1
//   Top   : row3 - row1
//   Near  : row3 + row2
//   Far   : row3 - row2
//
// where VP[c][r] means column c, row r.
// ---------------------------------------------------------------------------
void Frustum::extractFromVP(const glm::mat4& m) {
    // Left
    planes_[0] = glm::vec4(m[0][3] + m[0][0],
                            m[1][3] + m[1][0],
                            m[2][3] + m[2][0],
                            m[3][3] + m[3][0]);
    // Right
    planes_[1] = glm::vec4(m[0][3] - m[0][0],
                            m[1][3] - m[1][0],
                            m[2][3] - m[2][0],
                            m[3][3] - m[3][0]);
    // Bottom
    planes_[2] = glm::vec4(m[0][3] + m[0][1],
                            m[1][3] + m[1][1],
                            m[2][3] + m[2][1],
                            m[3][3] + m[3][1]);
    // Top
    planes_[3] = glm::vec4(m[0][3] - m[0][1],
                            m[1][3] - m[1][1],
                            m[2][3] - m[2][1],
                            m[3][3] - m[3][1]);
    // Near
    planes_[4] = glm::vec4(m[0][3] + m[0][2],
                            m[1][3] + m[1][2],
                            m[2][3] + m[2][2],
                            m[3][3] + m[3][2]);
    // Far
    planes_[5] = glm::vec4(m[0][3] - m[0][2],
                            m[1][3] - m[1][2],
                            m[2][3] - m[2][2],
                            m[3][3] - m[3][2]);

    // Normalize each plane by the length of its xyz normal
    for (auto& p : planes_) {
        float len = glm::length(glm::vec3(p));
        if (len > 0.0f) {
            p /= len;
        }
    }
}

// ---------------------------------------------------------------------------
// isAABBVisible — for each plane, test the "positive vertex" of the AABB.
// The positive vertex is the corner of the box that lies farthest in the
// direction of the plane's outward normal.  If even that corner is on the
// negative side of the plane, the entire box is outside the frustum.
// ---------------------------------------------------------------------------
bool Frustum::isAABBVisible(const AABB& box) const {
    for (const auto& p : planes_) {
        // Choose the AABB corner that maximises dot(corner, normal)
        glm::vec3 positive(
            (p.x >= 0.0f) ? box.max.x : box.min.x,
            (p.y >= 0.0f) ? box.max.y : box.min.y,
            (p.z >= 0.0f) ? box.max.z : box.min.z
        );

        float dist = p.x * positive.x + p.y * positive.y + p.z * positive.z + p.w;
        if (dist < 0.0f) {
            return false;   // fully outside this plane
        }
    }
    return true;
}
