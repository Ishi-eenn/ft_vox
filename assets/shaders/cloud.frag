#version 410 core
in  float vBright;
out vec4  FragColor;

uniform vec3 uSunDir;

void main() {
    // 昼（sunDir.y > 0）は白、夜は暗い青灰色
    float sunH   = clamp(uSunDir.y, 0.0, 1.0);
    vec3  dayCol = vec3(1.0, 1.0, 1.0);
    vec3  ngtCol = vec3(0.13, 0.15, 0.23);
    vec3  col    = mix(ngtCol, dayCol, sunH) * vBright;

    FragColor = vec4(col, 0.88);
}
