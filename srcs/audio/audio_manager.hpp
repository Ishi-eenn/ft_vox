#pragma once
#include <cstdint>
#include <memory>
#include <string>

// One-shot sound effect categories.
// Paths mapped in audio_manager.cpp :: kSePaths[].
enum class SoundEvent : uint8_t {
    FootstepGrass = 0,
    FootstepStone,
    FootstepSand,
    FootstepSnow,
    FootstepWood,
    Attack,
    Hurt,
    Swim,
    BlockBreak,
    BlockPlace,
    MobGroan,
    MobHurt,
    MobExplode,
    COUNT
};

// Central audio manager.
//
// Threading model:
//   All public methods must be called from the main thread.
//   miniaudio's audio callback runs on a dedicated audio thread internally;
//   miniaudio's own API is thread-safe, so we do not need extra locking here.
//
// Usage:
//   AudioManager audio;
//   audio.init("assets");
//   // each frame:
//   audio.setListenerPosition(px, py, pz);
//   audio.setListenerDirection(fx, fy, fz);
//   audio.update(dt);
class AudioManager {
public:
    AudioManager();
    ~AudioManager();

    // assets_root: directory that contains "music/" and "sounds/" subdirs.
    bool init(const std::string& assets_root = "assets");
    void shutdown();

    // Call once per frame from the main thread.
    void update(float dt);

    // ── Volume [0.0, 1.0] ───────────────────────────────────────────────────
    void  setMasterVolume(float v);
    void  setBgmVolume(float v);
    void  setSeVolume(float v);
    void  setAmbientVolume(float v);

    float getMasterVolume()   const;
    float getBgmVolume()      const;
    float getSeVolume()       const;
    float getAmbientVolume()  const;

    // ── Listener (player camera) ─────────────────────────────────────────────
    void setListenerPosition(float x, float y, float z);
    void setListenerDirection(float fx, float fy, float fz);
    void setListenerUp(float ux, float uy, float uz);

    // ── BGM: looping track with crossfade on switch ──────────────────────────
    // path: relative to assets_root, e.g. "music/forest_bgm.ogg"
    // fade_duration: seconds for both fade-in of new and fade-out of old track.
    void playBgm(const std::string& path, float fade_duration = 4.0f);
    void stopBgm(float fade_out = 2.0f);

    // ── Ambient: independent looping sound layer ─────────────────────────────
    void playAmbient(const std::string& path, float fade_in = 2.0f);
    void stopAmbient(float fade_out = 2.0f);

    // ── SE: 2D one-shot ──────────────────────────────────────────────────────
    void playSe(SoundEvent event);

    // ── SE: 3D positional one-shot ───────────────────────────────────────────
    // Skipped automatically when the source is farther than max_distance.
    // Uses inverse-distance attenuation (OpenAL-style).
    void playSe3D(SoundEvent event, float x, float y, float z,
                  float max_distance = 32.0f);

    // ── Internal access for SoundEmitter ────────────────────────────────────
    // Returns the raw ma_engine* as void*. Used only by SoundEmitter.
    void* getEngineHandle() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
