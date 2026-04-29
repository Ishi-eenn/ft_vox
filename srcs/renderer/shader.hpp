#pragma once
#include <string>
#include <cstdint>

class Shader {
public:
    Shader() = default;
    ~Shader();

    bool load(const char* vert_path, const char* frag_path);
    void use() const;
    void destroy();

    void setMat4(const char* name, const float* mat4) const;
    void setInt(const char* name, int val) const;
    void setFloat(const char* name, float val) const;
    void setVec3(const char* name, float x, float y, float z) const;
    void setVec4(const char* name, float x, float y, float z, float w) const;

    uint32_t id() const { return program_; }

private:
    uint32_t program_ = 0;
    static uint32_t compileStage(uint32_t type, const std::string& src);
};
