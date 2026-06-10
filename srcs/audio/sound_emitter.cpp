#include "audio/sound_emitter.hpp"
#include "audio/audio_manager.hpp"
#include <miniaudio.h>
#include <algorithm>
#include <cstdio>
#include <string>

struct SoundEmitter::Impl {
    ma_sound sound       = {};
    bool     loaded      = false;
    bool     playing     = false;
};

SoundEmitter::SoundEmitter() : impl_(std::make_unique<Impl>()) {}
SoundEmitter::~SoundEmitter() { unload(); }

bool SoundEmitter::load(AudioManager& mgr,
                        const std::string& path,
                        float min_distance,
                        float max_distance,
                        AttenuationModel model) {
    unload();

    auto* eng = static_cast<ma_engine*>(mgr.getEngineHandle());
    if (!eng) return false;

    // Construct full path the same way AudioManager does
    // (AudioManager doesn't expose assets_root, so the caller is expected to
    // pass the full path, or a path relative to the cwd which is the same
    // as assets_root + "/" + relative_path.)
    if (ma_sound_init_from_file(eng, path.c_str(), 0,
                                nullptr, nullptr,
                                &impl_->sound) != MA_SUCCESS) {
        fprintf(stderr, "[SoundEmitter] load failed: %s\n", path.c_str());
        return false;
    }
    impl_->loaded = true;

    ma_sound_set_spatialization_enabled(&impl_->sound, MA_TRUE);

    switch (model) {
        case AttenuationModel::Linear:
            ma_sound_set_attenuation_model(&impl_->sound,
                                           ma_attenuation_model_linear);
            break;
        case AttenuationModel::InverseSquare:
            ma_sound_set_attenuation_model(&impl_->sound,
                                           ma_attenuation_model_inverse);
            break;
    }
    ma_sound_set_rolloff(&impl_->sound, 1.0f);
    ma_sound_set_min_distance(&impl_->sound, min_distance);
    ma_sound_set_max_distance(&impl_->sound, max_distance);
    return true;
}

void SoundEmitter::unload() {
    if (!impl_->loaded) return;
    ma_sound_stop(&impl_->sound);
    ma_sound_uninit(&impl_->sound);
    impl_->loaded   = false;
    impl_->playing  = false;
}

void SoundEmitter::setPosition(float x, float y, float z) {
    if (impl_->loaded)
        ma_sound_set_position(&impl_->sound, x, y, z);
}

void SoundEmitter::setVolume(float v) {
    if (impl_->loaded)
        ma_sound_set_volume(&impl_->sound, std::clamp(v, 0.0f, 1.0f));
}

void SoundEmitter::play(bool loop) {
    if (!impl_->loaded) return;
    ma_sound_set_looping(&impl_->sound, loop ? MA_TRUE : MA_FALSE);
    ma_sound_start(&impl_->sound);
    impl_->playing = true;
}

void SoundEmitter::stop() {
    if (!impl_->loaded) return;
    ma_sound_stop(&impl_->sound);
    impl_->playing = false;
}

bool SoundEmitter::isPlaying() const {
    return impl_->loaded && ma_sound_is_playing(&impl_->sound);
}
