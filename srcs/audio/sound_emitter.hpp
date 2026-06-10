#pragma once
#include <memory>
#include <string>

class AudioManager;

// A persistent 3D sound source attached to a world object or mob.
//
// Example:
//   SoundEmitter emitter;
//   emitter.load(audio_mgr, "sounds/ambient/fire_crackle.ogg",
//                /*min_dist=*/1.0f, /*max_dist=*/16.0f);
//   emitter.setPosition(x, y, z);
//   emitter.play(/*loop=*/true);
//   // each frame:
//   emitter.setPosition(mob.x, mob.y, mob.z);
//   // on death:
//   emitter.stop();
class SoundEmitter {
public:
    enum class AttenuationModel { Linear, InverseSquare };

    SoundEmitter();
    ~SoundEmitter();

    // Load a sound file from AudioManager's assets_root.
    // Returns false if the file could not be loaded (non-fatal; other methods
    // become no-ops).
    bool load(AudioManager& mgr,
              const std::string& path,
              float min_distance   = 1.0f,
              float max_distance   = 32.0f,
              AttenuationModel model = AttenuationModel::InverseSquare);

    void unload();

    void setPosition(float x, float y, float z);
    void setVolume(float v);          // [0, 1], independent of SE group volume
    void play(bool loop = true);
    void stop();
    bool isPlaying() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
