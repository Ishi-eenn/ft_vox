#include "shader.hpp"
#include <glad/gl.h>
#include <fstream>
#include <sstream>
#include <iostream>

// ---------------------------------------------------------------------------
// Helper: read file to string
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Shader::compileStage
// ---------------------------------------------------------------------------
uint32_t Shader::compileStage(uint32_t type, const std::string& src) {
    uint32_t shader = glCreateShader(type);
    const char* src_ptr = src.c_str();
    glShaderSource(shader, 1, &src_ptr, nullptr);
    glCompileShader(shader);

    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
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

// ---------------------------------------------------------------------------
// Shader::load
// ---------------------------------------------------------------------------
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

    program_ = glCreateProgram();
    glAttachShader(program_, vert);
    glAttachShader(program_, frag);
    glLinkProgram(program_);

    GLint linked = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (!linked) {
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

    glDetachShader(program_, vert);
    glDetachShader(program_, frag);
    glDeleteShader(vert);
    glDeleteShader(frag);
    return true;
}

// ---------------------------------------------------------------------------
// Shader::use / destroy
// ---------------------------------------------------------------------------
void Shader::use() const {
    glUseProgram(program_);
}

void Shader::destroy() {
    if (program_ != 0) {
        glDeleteProgram(program_);
        program_ = 0;
    }
}

Shader::~Shader() {
    destroy();
}

// ---------------------------------------------------------------------------
// Uniform setters
// ---------------------------------------------------------------------------
void Shader::setMat4(const char* name, const float* mat4) const {
    GLint loc = glGetUniformLocation(program_, name);
    if (loc != -1)
        glUniformMatrix4fv(loc, 1, GL_FALSE, mat4);
}

void Shader::setInt(const char* name, int val) const {
    GLint loc = glGetUniformLocation(program_, name);
    if (loc != -1)
        glUniform1i(loc, val);
}

void Shader::setFloat(const char* name, float val) const {
    GLint loc = glGetUniformLocation(program_, name);
    if (loc != -1)
        glUniform1f(loc, val);
}

void Shader::setVec3(const char* name, float x, float y, float z) const {
    GLint loc = glGetUniformLocation(program_, name);
    if (loc != -1)
        glUniform3f(loc, x, y, z);
}

void Shader::setVec4(const char* name, float x, float y, float z, float w) const {
    GLint loc = glGetUniformLocation(program_, name);
    if (loc != -1)
        glUniform4f(loc, x, y, z, w);
}
