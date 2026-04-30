// ─────────────────────────────────────────────────────────────────────────────
// input_handler.cpp — キーボード・マウス入力の受け取りと管理
//
// 【入力処理の仕組み】
//   GLFW はウィンドウシステムと OpenGL をつなぐライブラリで、
//   キー・マウスのイベントを「コールバック関数」で通知してくれる。
//
//   コールバックとは？
//     「何かが起きたら呼んでください」と登録しておく関数のこと。
//     例: キーが押されたら keyCallback() を呼ぶ、という契約をする。
//
// 【カーソルキャプチャとは？】
//   ゲームでは画面の外にマウスが出ないようにカーソルを「捕まえる」必要がある。
//   GLFW_CURSOR_DISABLED モードにすると:
//     - マウスカーソルが非表示になる
//     - カーソルは画面中央に固定され、移動量だけが通知される
//     - これにより360度自由にカメラを回せる
//
// 【フレームの入力リセット】
//   マウスの移動量やクリック状態は「1フレームで何が起きたか」の情報なので、
//   newFrame() を呼ぶたびにリセットする。
//   一方、キーの押しっぱなし状態（keys_[]）はリセットしない。
// ─────────────────────────────────────────────────────────────────────────────
#include "player/input_handler.hpp"
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
// init() — GLFW コールバックを登録してカーソルをキャプチャする
//
// glfwSetWindowUserPointer: ウィンドウに this ポインタを関連付ける。
// コールバック内では C 関数しか使えないため、this を外から渡す手法（ユーザーポインタ）。
// ─────────────────────────────────────────────────────────────────────────────
void InputHandler::init(GLFWwindow* window) {
    window_ = window;
    glfwSetWindowUserPointer(window, this);  // コールバックから this を取得できるようにする

    glfwSetKeyCallback(window,              keyCallback);          // キー押下・解放
    glfwSetCursorPosCallback(window,        cursorPosCallback);    // マウス移動
    glfwSetMouseButtonCallback(window,      mouseButtonCallback);  // マウスクリック
    glfwSetWindowFocusCallback(window,      focusCallback);        // ウィンドウフォーカス
    glfwSetFramebufferSizeCallback(window,  resizeCallback);       // ウィンドウリサイズ

    captureCursor();  // 起動時からカーソルをキャプチャ
}

// ─────────────────────────────────────────────────────────────────────────────
// captureCursor() / releaseCursor() — カーソルキャプチャの切り替え
//
// first_ = true にするのは、カーソル再キャプチャ時に
// 「前の位置からの差分」が大きな値になってカメラが飛ぶ問題を防ぐため。
// ─────────────────────────────────────────────────────────────────────────────
void InputHandler::captureCursor() {
    glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    cursor_captured_ = true;
    first_ = true;  // 次の移動イベントは「基準位置の記録」として使う
}

void InputHandler::releaseCursor() {
    glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    cursor_captured_ = false;
    first_ = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// newFrame() — フレームの開始時にリセットする
//
// マウス移動量（dx_, dy_）とクリック（left/right_clicked_）は
// 1フレームの出来事なので毎フレームリセットする。
// keys_[] の「押しっぱなし」状態はリセットしない。
// ─────────────────────────────────────────────────────────────────────────────
void InputHandler::newFrame() {
    dx_ = 0.0f;
    dy_ = 0.0f;
    left_clicked_  = false;
    right_clicked_ = false;
}

// キーが現在押されているかを返す（キーコードは GLFW_KEY_W などの定数）
bool InputHandler::isHeld(int key) const {
    if (key < 0 || key >= KEY_COUNT) return false;
    return keys_[key];
}

// ─────────────────────────────────────────────────────────────────────────────
// コールバック関数群
//
// GLFW は C スタイルの関数ポインタを要求するため static 関数として定義する。
// glfwGetWindowUserPointer() で登録した this を取り出して
// メンバ変数にアクセスする。
// ─────────────────────────────────────────────────────────────────────────────

// キーが押された・離されたときに呼ばれる
void InputHandler::keyCallback(GLFWwindow* w, int key, int /*sc*/, int action, int /*mods*/) {
    auto* self = (InputHandler*)glfwGetWindowUserPointer(w);
    if (!self) return;

    // ESC キーの特殊処理:
    //   カーソルキャプチャ中 → カーソルを解放（ゲームを一時停止）
    //   カーソルフリー中    → ウィンドウを閉じる
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
    if (action == GLFW_PRESS)   self->keys_[key] = true;   // キーが押された
    if (action == GLFW_RELEASE) self->keys_[key] = false;  // キーが離された
}

// マウスカーソルが動いたときに呼ばれる
// キャプチャ中でない場合は無視する
void InputHandler::cursorPosCallback(GLFWwindow* w, double xpos, double ypos) {
    auto* self = (InputHandler*)glfwGetWindowUserPointer(w);
    if (!self || !self->cursor_captured_) return;

    if (self->first_) {
        // 最初のイベントでは差分を計算せず「基準位置」として記録するだけ
        // （カーソル再キャプチャ直後の大ジャンプを防ぐ）
        self->last_x_ = xpos;
        self->last_y_ = ypos;
        self->first_  = false;
        return;
    }
    // 前フレームからの移動量を累積する（同フレーム内に複数回呼ばれることもある）
    self->dx_ += (float)(xpos - self->last_x_);
    self->dy_ += (float)(ypos - self->last_y_);
    self->last_x_ = xpos;
    self->last_y_ = ypos;
}

// マウスボタンがクリックされたときに呼ばれる
void InputHandler::mouseButtonCallback(GLFWwindow* w, int button, int action, int /*mods*/) {
    auto* self = (InputHandler*)glfwGetWindowUserPointer(w);
    if (!self) return;

    if (action == GLFW_PRESS) {
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            if (!self->cursor_captured_)
                self->captureCursor();   // カーソルが解放中なら左クリックで再キャプチャ
            else
                self->left_clicked_ = true;  // キャプチャ中なら「ブロックを壊す」
        } else if (button == GLFW_MOUSE_BUTTON_RIGHT && self->cursor_captured_) {
            self->right_clicked_ = true;  // 右クリックで「ブロックを置く」
        }
    }
}

// ウィンドウがフォーカスを失ったときに呼ばれる（Alt+Tab など）
void InputHandler::focusCallback(GLFWwindow* w, int focused) {
    auto* self = (InputHandler*)glfwGetWindowUserPointer(w);
    if (!self) return;
    // フォーカスが外れたらカーソルを解放してOSに制御を返す
    if (!focused && self->cursor_captured_) {
        self->releaseCursor();
    }
}

// ウィンドウサイズが変わったときに呼ばれる
void InputHandler::resizeCallback(GLFWwindow* w, int width, int height) {
    auto* self = (InputHandler*)glfwGetWindowUserPointer(w);
    if (!self) return;
    self->resize_w_ = width;
    self->resize_h_ = height;
    self->resized_  = true;  // Engine がフレーム終わりに確認してビューポートを更新する
}
