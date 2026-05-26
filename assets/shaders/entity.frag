#version 410 core
in  vec3 vNormal;
in  float vFogFactor;
out vec4 fragColor;

uniform vec3  uColor;
uniform vec3  uSunDir;
uniform float uAmbient;
uniform float uSunStrength;
uniform vec3  uFogColor;

void main() {
    float diff = max(dot(normalize(vNormal), normalize(uSunDir)), 0.0) * uSunStrength;
    vec3 lit = uColor * (uAmbient + diff);
    fragColor  = vec4(mix(lit, uFogColor, clamp(vFogFactor, 0.0, 1.0)), 1.0);
}
