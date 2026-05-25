#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec3 aNormal;

uniform mat4 uMVP;
uniform mat4 uModelView;  // view * model

out vec3 vViewPos;
out vec3 vViewNormal;

void main() {
    gl_Position  = uMVP * vec4(aPos, 1.0);
    vViewPos     = (uModelView * vec4(aPos, 1.0)).xyz;
    // チャンクのmodel行列は平行移動のみなので mat3(uModelView) で正しい
    vViewNormal  = mat3(uModelView) * aNormal;
}
