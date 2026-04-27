#version 410 core
in vec3 vDir;
out vec4 FragColor;

void main() {
    float t = clamp(normalize(vDir).y * 0.5 + 0.5, 0.0, 1.0);
    t = pow(t, 0.7);  // softer transition
    vec3 zenith   = vec3(0.08, 0.25, 0.65);
    vec3 horizon  = vec3(0.55, 0.72, 0.90);
    vec3 ground   = vec3(0.35, 0.30, 0.25);
    vec3 sky;
    if (t > 0.5)
        sky = mix(horizon, zenith, (t - 0.5) * 2.0);
    else
        sky = mix(ground, horizon, t * 2.0);
    FragColor = vec4(sky, 1.0);
}
