#pragma once
#include <string>
#include <unordered_map>

class AudioManager;

// BGM + ambient pair registered for one biome.
struct BiomeTrack {
    std::string bgm_path;      // e.g. "music/plains_bgm.ogg"
    std::string ambient_path;  // e.g. "sounds/ambient/plains.ogg"
};

// Maps biome names → BiomeTrack and drives BGM/ambient switching.
//
// Call init() once at startup, then update() every frame with the
// biome name returned by World::getBiomeNameAt().
//
// Crossfade duration is forwarded to AudioManager::playBgm / playAmbient.
// A 2-second debounce prevents rapid switching at biome borders.
class BiomeAudioSystem {
public:
    static constexpr float kDefaultFadeDuration = 4.0f;  // seconds
    static constexpr float kChangeCooldown      = 2.0f;  // seconds

    // Registers all built-in biome tracks and wires up the AudioManager.
    void init(AudioManager& mgr);

    // Register (or overwrite) a biome track by name.
    void registerBiome(const std::string& biome_name, BiomeTrack track);

    // Call every frame.  current_biome is the string from getBiomeNameAt().
    void update(float dt, const char* current_biome);

    void setFadeDuration(float s) { fade_duration_ = s; }
    float fadeDuration()    const { return fade_duration_; }

private:
    AudioManager* mgr_ = nullptr;
    std::unordered_map<std::string, BiomeTrack> tracks_;
    std::string current_biome_;
    float fade_duration_  = kDefaultFadeDuration;
    float change_cooldown_ = 0.0f;
};
