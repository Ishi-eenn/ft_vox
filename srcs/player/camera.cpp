// ─────────────────────────────────────────────────────────────────────────────
// camera.cpp — カメラ（視点）の管理
//
// 【カメラの仕組み】
//   3Dゲームのカメラは「どこにいて、どこを向いているか」を管理する。
//   この情報を2つの行列（View行列・Proj行列）に変換してGPUに渡すことで
//   3Dの世界が2D画面に正しく映し出される。
//
//   ・View行列  : カメラの位置・向きによる変換（視点変換）
//   ・Proj行列  : 遠近感（透視投影）を付ける変換
//
// 【ヨーとピッチ】
//   FPSゲームではマウスで視点を動かす:
//   ・Yaw（ヨー）  : 水平方向の回転（左右に首を振る）
//   ・Pitch（ピッチ）: 垂直方向の回転（上下に首を振る）
//   ・Roll（ロール）: ここでは使わない（体を傾ける）
// ─────────────────────────────────────────────────────────────────────────────
#include "player/camera.hpp"
#include "types.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>

Camera::Camera() { updateVectors(); }

void Camera::setPosition(float x, float y, float z) { pos_ = {x, y, z}; }
void Camera::setAspect(float aspect) { aspect_ = aspect; }

// ヨー（水平回転）を設定する。-180〜180度の範囲に正規化する。
void Camera::setYaw(float yaw) {
    yaw_ = yaw;
    // -180〜180度の範囲に収める（360度を超えたら折り返す）
    while (yaw_ >  180.0f) yaw_ -= 360.0f;
    while (yaw_ < -180.0f) yaw_ += 360.0f;
    updateVectors();
}

// ピッチ（垂直回転）を設定する。-89〜89度にクランプして真上・真下を向かないようにする。
// 90度ちょうどにするとジンバルロック（計算の破綻）が起きるため±89度で止める。
void Camera::setPitch(float pitch) {
    pitch_ = glm::clamp(pitch, -89.0f, 89.0f);
    updateVectors();
}

// ─────────────────────────────────────────────────────────────────────────────
// updateVectors() — ヨー・ピッチから前方・右・上ベクトルを再計算する
//
// 【前方ベクトルとは？】
//   カメラが向いている方向を長さ1のベクトルで表したもの。
//   ヨー・ピッチから三角関数で計算できる（球面座標→直交座標の変換）。
//
//   front.x = cos(yaw) * cos(pitch)
//   front.y = sin(pitch)              ← ピッチが上がれば y 成分が大きくなる
//   front.z = sin(yaw) * cos(pitch)
//
// 【右ベクトル・上ベクトル】
//   外積（cross product）を使って求める。
//   外積: 2つのベクトルに直交するベクトルを返す演算。
//   right = normalize(front × world_up)  （world_up = (0,1,0) = 世界の上方向）
//   up    = normalize(right × front)
// ─────────────────────────────────────────────────────────────────────────────
void Camera::updateVectors() {
    float yr = glm::radians(yaw_);    // 度 → ラジアン変換（三角関数はラジアンで計算する）
    float pr = glm::radians(pitch_);
    front_.x = glm::cos(yr) * glm::cos(pr);
    front_.y = glm::sin(pr);
    front_.z = glm::sin(yr) * glm::cos(pr);
    front_   = glm::normalize(front_);                            // 長さを1に正規化
    right_   = glm::normalize(glm::cross(front_, glm::vec3(0,1,0)));
    up_      = glm::normalize(glm::cross(right_, front_));
}

// ─────────────────────────────────────────────────────────────────────────────
// View行列を生成する。
// glm::lookAt(eye, center, up):
//   eye    = カメラの位置
//   center = 注視点（カメラ位置 + 前方ベクトル）
//   up     = ワールドの上方向
// ─────────────────────────────────────────────────────────────────────────────
glm::mat4 Camera::viewMatrix() const {
    return glm::lookAt(pos_, pos_ + front_, glm::vec3(0,1,0));
}

// ─────────────────────────────────────────────────────────────────────────────
// Projection（射影）行列を生成する。
// glm::perspective(fov, aspect, near, far):
//   fov    = 垂直方向の画角（ラジアン）。広いほど魚眼レンズ的になる。
//   aspect = 画面の縦横比（幅÷高さ）
//   near   = 近クリップ距離（これより近いものは描画しない）
//   far    = 遠クリップ距離（これより遠いものは描画しない）
// ─────────────────────────────────────────────────────────────────────────────
glm::mat4 Camera::projMatrix() const {
    return glm::perspective(glm::radians(FOV_DEGREES), aspect_, NEAR_PLANE, FAR_PLANE);
}

// glm::mat4 → float[16] へコピー（OpenGLに渡すインターフェース用）
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
