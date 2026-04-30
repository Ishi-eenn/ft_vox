#version 410 core
in vec3 vDir;
out vec4 FragColor;

uniform vec3 uSkyZenith;
uniform vec3 uSkyHorizon;
uniform vec3 uGroundColor;
uniform vec3 uSunDir;
uniform vec3 uSunColor;

void main() {
    vec3 dir = normalize(vDir);

    // ── Sky gradient ──────────────────────────────────────────────────────────
    float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    t = pow(t, 0.7);
    vec3 sky;
    if (t > 0.5)
        sky = mix(uSkyHorizon, uSkyZenith, (t - 0.5) * 2.0);
    else
        sky = mix(uGroundColor, uSkyHorizon, t * 2.0);

    // ── Sun disc + glow ───────────────────────────────────────────────────────
    if (uSunDir.y > -0.1) {
        float sun_dot = dot(dir, uSunDir);
        float disc    = smoothstep(0.9975, 0.9990, sun_dot);
        float glow    = pow(max(sun_dot, 0.0), 24.0) * 0.25;
        sky += uSunColor * (disc + glow);
    }

    // ── Moon disc + glow (opposite the sun) ───────────────────────────────────
    vec3 moon_dir = -uSunDir;
    if (moon_dir.y > -0.1) {
        float moon_dot  = dot(dir, moon_dir);
        float disc      = smoothstep(0.9968, 0.9983, moon_dot);  // slightly larger than sun
        float glow      = pow(max(moon_dot, 0.0), 20.0) * 0.08;  // subtle halo
        vec3  moon_col  = vec3(0.82, 0.86, 1.00);                 // cool blue-white
        sky += moon_col * (disc + glow);
    }

    FragColor = vec4(sky, 1.0);
}
