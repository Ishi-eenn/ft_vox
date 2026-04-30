// ─────────────────────────────────────────────────────────────────────────────
// shader.cpp — GLSL シェーダーの読み込み・コンパイル・リンク
//
// 【シェーダーとは？】
//   GPU 上で動くプログラム。C 言語に似た GLSL という言語で書かれている。
//   主に2種類のシェーダーを使う:
//
//   頂点シェーダー（.vert）:
//     3D ワールド座標の頂点を、画面上の2D 座標に変換する。
//     MVP 行列を掛けてクリップ座標系へ変換するのが主な役割。
//
//   フラグメントシェーダー（.frag）:
//     三角形を構成する各ピクセルの色を計算する。
//     テクスチャの色にライティングを掛けて最終的な色を出力する。
//
// 【シェーダープログラムの作り方】
//   1. 頂点シェーダーをコンパイル（GPU ネイティブコードへ）
//   2. フラグメントシェーダーをコンパイル
//   3. 2つをリンクして「プログラム」を作成
//   4. 描画前に glUseProgram() で有効化する
//
// 【ユニフォーム変数とは？】
//   シェーダーに渡す定数（毎頂点変わらない値）。
//   MVP 行列・テクスチャスロット・ライト方向などを渡す。
//   名前文字列で場所を検索（glGetUniformLocation）してから値をセットする。
// ─────────────────────────────────────────────────────────────────────────────
#include "shader.hpp"
#include <glad/gl.h>
#include <fstream>
#include <sstream>
#include <iostream>

// ─────────────────────────────────────────────────────────────────────────────
// readFile() — ファイルを文字列として読み込む
// ─────────────────────────────────────────────────────────────────────────────
static bool readFile(const char* path, std::string& out) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[Shader] Cannot open file: " << path << "\n";
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// compileStage() — 1つのシェーダーステージをコンパイルする
//
// type: GL_VERTEX_SHADER または GL_FRAGMENT_SHADER
// src:  GLSL ソースコード文字列
// 戻り値: シェーダーオブジェクトID（失敗時は 0）
//
// コンパイルエラーがあると、GPU ドライバがエラーログを生成するので
// glGetShaderInfoLog() で取り出してコンソールに出力する。
// ─────────────────────────────────────────────────────────────────────────────
uint32_t Shader::compileStage(uint32_t type, const std::string& src) {
    uint32_t shader = glCreateShader(type);
    const char* src_ptr = src.c_str();
    glShaderSource(shader, 1, &src_ptr, nullptr);
    glCompileShader(shader);

    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        // コンパイル失敗: エラーログを取得して表示
        GLint log_len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
        std::string log(static_cast<size_t>(log_len), '\0');
        glGetShaderInfoLog(shader, log_len, nullptr, log.data());
        const char* type_str = (type == GL_VERTEX_SHADER) ? "vertex" : "fragment";
        std::cerr << "[Shader] " << type_str << " compile error:\n" << log << "\n";
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// ─────────────────────────────────────────────────────────────────────────────
// load() — 頂点・フラグメントシェーダーファイルを読み込んでプログラムを作成する
//
// 処理の流れ:
//   ファイル読み込み → コンパイル → リンク → デタッチ（中間オブジェクト削除）
// ─────────────────────────────────────────────────────────────────────────────
bool Shader::load(const char* vert_path, const char* frag_path) {
    std::string vert_src, frag_src;

    if (!readFile(vert_path, vert_src)) {
        std::cerr << "[Shader] Failed to read vertex shader: " << vert_path << "\n";
        return false;
    }
    if (!readFile(frag_path, frag_src)) {
        std::cerr << "[Shader] Failed to read fragment shader: " << frag_path << "\n";
        return false;
    }

    uint32_t vert = compileStage(GL_VERTEX_SHADER,   vert_src);
    uint32_t frag = compileStage(GL_FRAGMENT_SHADER, frag_src);

    if (!vert || !frag) {
        if (vert) glDeleteShader(vert);
        if (frag) glDeleteShader(frag);
        return false;
    }

    // プログラムを作成して2つのシェーダーをアタッチし、リンクする
    program_ = glCreateProgram();
    glAttachShader(program_, vert);
    glAttachShader(program_, frag);
    glLinkProgram(program_);

    GLint linked = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (!linked) {
        // リンク失敗: エラーログを出力
        GLint log_len = 0;
        glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &log_len);
        std::string log(static_cast<size_t>(log_len), '\0');
        glGetProgramInfoLog(program_, log_len, nullptr, log.data());
        std::cerr << "[Shader] Link error (" << vert_path << " + " << frag_path << "):\n" << log << "\n";
        glDeleteShader(vert);
        glDeleteShader(frag);
        glDeleteProgram(program_);
        program_ = 0;
        return false;
    }

    // リンク成功後は中間のシェーダーオブジェクトは不要
    glDetachShader(program_, vert);
    glDetachShader(program_, frag);
    glDeleteShader(vert);
    glDeleteShader(frag);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// use() / destroy()
// ─────────────────────────────────────────────────────────────────────────────

// このシェーダープログラムを描画に使用する（GPU に「これを使え」と命令）
void Shader::use() const {
    glUseProgram(program_);
}

// GPU リソースを解放する
void Shader::destroy() {
    if (program_ != 0) {
        glDeleteProgram(program_);
        program_ = 0;
    }
}

Shader::~Shader() {
    destroy();
}

// ─────────────────────────────────────────────────────────────────────────────
// ユニフォーム変数セッター
//
// glGetUniformLocation: シェーダー内の変数名から場所（番号）を検索する。
// loc == -1 は「その名前のユニフォームは存在しない」（シェーダーが最適化で削除済みなど）
// その場合は何もしない（エラーにしない）。
// ─────────────────────────────────────────────────────────────────────────────

// 4×4 浮動小数点行列（例: MVP 行列）を送る
void Shader::setMat4(const char* name, const float* mat4) const {
    GLint loc = glGetUniformLocation(program_, name);
    if (loc != -1)
        glUniformMatrix4fv(loc, 1, GL_FALSE, mat4);  // GL_FALSE: 転置しない
}

// 整数値を送る（例: テクスチャスロット番号）
void Shader::setInt(const char* name, int val) const {
    GLint loc = glGetUniformLocation(program_, name);
    if (loc != -1)
        glUniform1i(loc, val);
}

// 浮動小数点スカラーを送る（例: 環境光強度）
void Shader::setFloat(const char* name, float val) const {
    GLint loc = glGetUniformLocation(program_, name);
    if (loc != -1)
        glUniform1f(loc, val);
}

// 3次元ベクトルを送る（例: 太陽の方向・空の色）
void Shader::setVec3(const char* name, float x, float y, float z) const {
    GLint loc = glGetUniformLocation(program_, name);
    if (loc != -1)
        glUniform3f(loc, x, y, z);
}

// 4次元ベクトルを送る（例: RGBA 色）
void Shader::setVec4(const char* name, float x, float y, float z, float w) const {
    GLint loc = glGetUniformLocation(program_, name);
    if (loc != -1)
        glUniform4f(loc, x, y, z, w);
}
