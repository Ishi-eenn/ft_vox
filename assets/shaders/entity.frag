#version 410 core
in  vec3 vNormal;
out vec4 fragColor;

uniform vec3  uColor;
uniform vec3  uSunDir;
uniform float uAmbient;
uniform float uSunStrength;

void main() {
    float diff = max(dot(normalize(vNormal), normalize(uSunDir)), 0.0) * uSunStrength;
    fragColor  = vec4(uColor * (uAmbient + diff), 1.0);
}
