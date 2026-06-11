#include "audio/audio_manager.hpp"
#include <miniaudio.h>
#include <array>
#include <algorithm>
#include <cstdio>
#include <string>

// ── 拡張子なしの SE パス（stems） ────────────────────────────────────────────
static const char* kSeStems[] = {
    "sounds/footstep_grass",  // FootstepGrass
    "sounds/footstep_stone",  // FootstepStone
    "sounds/footstep_sand",   // FootstepSand
    "sounds/footstep_snow",   // FootstepSnow
    "sounds/footstep_wood",   // FootstepWood
    "sounds/attack",          // Attack
    "sounds/hurt",            // Hurt
    "sounds/swim",            // Swim
    "sounds/block_break",     // BlockBreak
    "sounds/block_place",     // BlockPlace
    "sounds/mob_groan",       // MobGroan
    "sounds/mob_hurt",        // MobHurt
    "sounds/mob_explode",     // MobExplode
};
static_assert(sizeof(kSeStems) / sizeof(kSeStems[0]) ==
              static_cast<std::size_t>(SoundEvent::COUNT),
              "kSeStems length mismatch");

// ogg / mp3 / wav / flac のどれかが存在するパスを返す。
// 見つからなければ stem + ".ogg" を返す（AudioManager が警告を出す）。
static std::string resolveAudioPath(const std::string& stem) {
    static const char* kExts[] = {".ogg", ".mp3", ".wav", ".flac"};
    for (const char* ext : kExts) {
        std::string candidate = stem + ext;
        if (FILE* f = std::fopen(candidate.c_str(), "rb")) {
            std::fclose(f);
            return candidate;
        }
    }
    return stem + ".ogg";
}

// ── BGM slot (one of two ping-pong slots) ─────────────────────────────────────
struct BgmSlot {
    ma_sound    sound       = {};
    bool        initialized = false;
    bool        playing     = false;
    std::string path;
};

// ── SE pool slot (3D one-shot sounds) ────────────────────────────────────────
struct SeSlot {
    ma_sound sound       = {};
    bool     initialized = false;
};

static constexpr int kSePoolSize = 16;

// ── pImpl ─────────────────────────────────────────────────────────────────────
struct AudioManager::Impl {
    ma_engine      engine        = {};
    ma_sound_group bgm_group     = {};
    ma_sound_group ambient_group = {};
    ma_sound_group se_group      = {};
    bool engine_ok   = false;
    bool bgm_grp_ok  = false;
    bool amb_grp_ok  = false;
    bool se_grp_ok   = false;

    // Two BGM slots for crossfade (ping-pong)
    BgmSlot bgm[2];
    int     active_slot  = -1;   // slot currently playing (fading in or at full vol)
    int     fading_slot  = -1;   // slot currently fading out (-1 = none)
    float   fade_timer   = 0.0f; // seconds until fading_slot is stopped

    // Ambient
    ma_sound    ambient_snd        = {};
    bool        ambient_ok         = false;
    bool        ambient_fading_out = false;
    float       ambient_fade_timer = 0.0f;
    std::string ambient_path;

    // 3D SE pool
    std::array<SeSlot, kSePoolSize> se_pool = {};

    // Cached listener position (for distance culling in playSe3D)
    float listener_x = 0.0f, listener_y = 0.0f, listener_z = 0.0f;

    std::string assets_root;
    float master_vol  = 1.0f;
    float bgm_vol     = 0.8f;
    float se_vol      = 1.0f;
    float ambient_vol = 0.6f;
};

// ─────────────────────────────────────────────────────────────────────────────

AudioManager::AudioManager() : impl_(std::make_unique<Impl>()) {}
AudioManager::~AudioManager() { shutdown(); }

bool AudioManager::init(const std::string& assets_root) {
    Impl& d = *impl_;
    d.assets_root = assets_root;

    ma_engine_config cfg = ma_engine_config_init();
    cfg.listenerCount = 1;
    if (ma_engine_init(&cfg, &d.engine) != MA_SUCCESS) {
        fprintf(stderr, "[Audio] ma_engine_init failed\n");
        return false;
    }
    d.engine_ok = true;

    auto init_group = [&](ma_sound_group& g, bool& ok, const char* name) -> bool {
        if (ma_sound_group_init(&d.engine, 0, nullptr, &g) != MA_SUCCESS) {
            fprintf(stderr, "[Audio] %s group init failed\n", name);
            return false;
        }
        ok = true;
        return true;
    };
    if (!init_group(d.bgm_group,     d.bgm_grp_ok, "BGM"))     return false;
    if (!init_group(d.ambient_group, d.amb_grp_ok, "Ambient")) return false;
    if (!init_group(d.se_group,      d.se_grp_ok,  "SE"))      return false;

    ma_engine_set_volume(&d.engine, d.master_vol);
    ma_sound_group_set_volume(&d.bgm_group,     d.bgm_vol);
    ma_sound_group_set_volume(&d.ambient_group, d.ambient_vol);
    ma_sound_group_set_volume(&d.se_group,      d.se_vol);

    // Default listener orientation: looking -Z (OpenGL convention)
    ma_engine_listener_set_direction(&d.engine, 0, 0.0f, 0.0f, -1.0f);
    ma_engine_listener_set_world_up(&d.engine,  0, 0.0f, 1.0f,  0.0f);

    fprintf(stderr, "[Audio] init OK  root='%s'\n", assets_root.c_str());
    return true;
}

void AudioManager::shutdown() {
    Impl& d = *impl_;
    if (!d.engine_ok) return;

    for (auto& se : d.se_pool)
        if (se.initialized) { ma_sound_uninit(&se.sound); se.initialized = false; }

    if (d.ambient_ok) {
        ma_sound_stop(&d.ambient_snd);
        ma_sound_uninit(&d.ambient_snd);
        d.ambient_ok = false;
    }
    for (auto& slot : d.bgm)
        if (slot.initialized) {
            ma_sound_stop(&slot.sound);
            ma_sound_uninit(&slot.sound);
            slot.initialized = false;
        }

    if (d.se_grp_ok)  { ma_sound_group_uninit(&d.se_group);      d.se_grp_ok  = false; }
    if (d.amb_grp_ok) { ma_sound_group_uninit(&d.ambient_group); d.amb_grp_ok = false; }
    if (d.bgm_grp_ok) { ma_sound_group_uninit(&d.bgm_group);     d.bgm_grp_ok = false; }

    ma_engine_uninit(&d.engine);
    d.engine_ok = false;
    fprintf(stderr, "[Audio] shutdown OK\n");
}

void AudioManager::update(float dt) {
    Impl& d = *impl_;
    if (!d.engine_ok) return;

    // Stop the fading-out BGM slot after crossfade completes
    if (d.fading_slot >= 0 && d.fade_timer > 0.0f) {
        d.fade_timer -= dt;
        if (d.fade_timer <= 0.0f) {
            d.fade_timer = 0.0f;
            BgmSlot& s = d.bgm[d.fading_slot];
            if (s.initialized) {
                ma_sound_stop(&s.sound);
                ma_sound_uninit(&s.sound);
                s.initialized = false;
                s.playing     = false;
            }
            d.fading_slot = -1;
        }
    }

    // Stop the fading-out ambient after fade completes
    if (d.ambient_fading_out && d.ambient_fade_timer > 0.0f) {
        d.ambient_fade_timer -= dt;
        if (d.ambient_fade_timer <= 0.0f) {
            d.ambient_fade_timer = 0.0f;
            d.ambient_fading_out = false;
            if (d.ambient_ok) {
                ma_sound_stop(&d.ambient_snd);
                ma_sound_uninit(&d.ambient_snd);
                d.ambient_ok = false;
            }
        }
    }

    // Reclaim SE pool slots that have finished playing.
    // 停止予約 (ma_sound_set_stop_time_*) で止まった音は at_end にならないため
    // is_playing もあわせて確認する (start 直後の音は必ず is_playing == true)。
    for (auto& se : d.se_pool)
        if (se.initialized &&
            (ma_sound_at_end(&se.sound) || !ma_sound_is_playing(&se.sound))) {
            ma_sound_uninit(&se.sound);
            se.initialized = false;
        }
}

// ── Volume ────────────────────────────────────────────────────────────────────

void AudioManager::setMasterVolume(float v) {
    impl_->master_vol = std::clamp(v, 0.0f, 1.0f);
    if (impl_->engine_ok) ma_engine_set_volume(&impl_->engine, impl_->master_vol);
}
void AudioManager::setBgmVolume(float v) {
    impl_->bgm_vol = std::clamp(v, 0.0f, 1.0f);
    if (impl_->bgm_grp_ok)
        ma_sound_group_set_volume(&impl_->bgm_group, impl_->bgm_vol);
}
void AudioManager::setSeVolume(float v) {
    impl_->se_vol = std::clamp(v, 0.0f, 1.0f);
    if (impl_->se_grp_ok)
        ma_sound_group_set_volume(&impl_->se_group, impl_->se_vol);
}
void AudioManager::setAmbientVolume(float v) {
    impl_->ambient_vol = std::clamp(v, 0.0f, 1.0f);
    if (impl_->amb_grp_ok)
        ma_sound_group_set_volume(&impl_->ambient_group, impl_->ambient_vol);
}

float AudioManager::getMasterVolume()  const { return impl_->master_vol;  }
float AudioManager::getBgmVolume()     const { return impl_->bgm_vol;     }
float AudioManager::getSeVolume()      const { return impl_->se_vol;      }
float AudioManager::getAmbientVolume() const { return impl_->ambient_vol; }

// ── Listener ──────────────────────────────────────────────────────────────────

void AudioManager::setListenerPosition(float x, float y, float z) {
    impl_->listener_x = x; impl_->listener_y = y; impl_->listener_z = z;
    if (impl_->engine_ok)
        ma_engine_listener_set_position(&impl_->engine, 0, x, y, z);
}
void AudioManager::setListenerDirection(float fx, float fy, float fz) {
    if (impl_->engine_ok)
        ma_engine_listener_set_direction(&impl_->engine, 0, fx, fy, fz);
}
void AudioManager::setListenerUp(float ux, float uy, float uz) {
    if (impl_->engine_ok)
        ma_engine_listener_set_world_up(&impl_->engine, 0, ux, uy, uz);
}

// ── BGM crossfade ─────────────────────────────────────────────────────────────

void AudioManager::playBgm(const std::string& path, float fade_duration) {
    Impl& d = *impl_;
    if (!d.engine_ok) return;

    // Skip if already playing this exact track
    if (d.active_slot >= 0 &&
        d.bgm[d.active_slot].playing &&
        d.bgm[d.active_slot].path == path)
        return;

    // Choose the slot that is NOT currently active
    int next = (d.active_slot == 0) ? 1 : 0;

    // If next slot is still alive from a previous transition, evict it
    if (d.bgm[next].initialized) {
        if (next == d.fading_slot) d.fading_slot = -1;
        ma_sound_stop(&d.bgm[next].sound);
        ma_sound_uninit(&d.bgm[next].sound);
        d.bgm[next].initialized = false;
        d.bgm[next].playing     = false;
    }

    std::string full = resolveAudioPath(d.assets_root + "/" + path);
    ma_uint32 flags  = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION;
    if (ma_sound_init_from_file(&d.engine, full.c_str(), flags,
                                &d.bgm_group, nullptr,
                                &d.bgm[next].sound) != MA_SUCCESS) {
        fprintf(stderr, "[Audio] BGM load failed: %s\n", full.c_str());
        return;
    }
    d.bgm[next].initialized = true;
    d.bgm[next].playing     = true;
    d.bgm[next].path        = path;
    ma_sound_set_looping(&d.bgm[next].sound, MA_TRUE);

    // Fade new track in from silence
    auto fade_ms = static_cast<ma_uint64>(fade_duration * 1000.0f);
    ma_sound_set_fade_in_milliseconds(&d.bgm[next].sound, 0.0f, 1.0f, fade_ms);
    ma_sound_start(&d.bgm[next].sound);

    // Fade old track out; retire any pre-existing fading slot immediately
    if (d.active_slot >= 0 && d.bgm[d.active_slot].initialized) {
        if (d.fading_slot >= 0 && d.fading_slot != d.active_slot &&
            d.bgm[d.fading_slot].initialized) {
            ma_sound_stop(&d.bgm[d.fading_slot].sound);
            ma_sound_uninit(&d.bgm[d.fading_slot].sound);
            d.bgm[d.fading_slot].initialized = false;
            d.bgm[d.fading_slot].playing     = false;
        }
        ma_sound_set_fade_in_milliseconds(
            &d.bgm[d.active_slot].sound, 1.0f, 0.0f, fade_ms);
        d.fading_slot = d.active_slot;
        d.fade_timer  = fade_duration;
    }

    d.active_slot = next;
}

void AudioManager::stopBgm(float fade_out) {
    Impl& d = *impl_;
    if (!d.engine_ok || d.active_slot < 0) return;

    BgmSlot& s = d.bgm[d.active_slot];
    if (s.initialized && s.playing) {
        auto fade_ms = static_cast<ma_uint64>(fade_out * 1000.0f);
        ma_sound_set_fade_in_milliseconds(&s.sound, 1.0f, 0.0f, fade_ms);
        d.fading_slot = d.active_slot;
        d.fade_timer  = fade_out;
    }
    d.active_slot = -1;
}

// ── Ambient ───────────────────────────────────────────────────────────────────

void AudioManager::playAmbient(const std::string& path, float fade_in) {
    Impl& d = *impl_;
    if (!d.engine_ok) return;
    if (d.ambient_ok && d.ambient_path == path) return;  // already playing

    // Stop current ambient immediately (crossfade handled by BiomeAudioSystem timing)
    if (d.ambient_ok) {
        ma_sound_stop(&d.ambient_snd);
        ma_sound_uninit(&d.ambient_snd);
        d.ambient_ok         = false;
        d.ambient_fading_out = false;
        d.ambient_fade_timer = 0.0f;
    }

    std::string full = resolveAudioPath(d.assets_root + "/" + path);
    ma_uint32 flags  = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION;
    if (ma_sound_init_from_file(&d.engine, full.c_str(), flags,
                                &d.ambient_group, nullptr,
                                &d.ambient_snd) != MA_SUCCESS) {
        fprintf(stderr, "[Audio] Ambient load failed: %s\n", full.c_str());
        return;
    }
    d.ambient_ok   = true;
    d.ambient_path = path;
    ma_sound_set_looping(&d.ambient_snd, MA_TRUE);
    auto fade_ms = static_cast<ma_uint64>(fade_in * 1000.0f);
    ma_sound_set_fade_in_milliseconds(&d.ambient_snd, 0.0f, 1.0f, fade_ms);
    ma_sound_start(&d.ambient_snd);
}

void AudioManager::stopAmbient(float fade_out) {
    Impl& d = *impl_;
    if (!d.engine_ok || !d.ambient_ok || d.ambient_fading_out) return;

    auto fade_ms = static_cast<ma_uint64>(fade_out * 1000.0f);
    ma_sound_set_fade_in_milliseconds(&d.ambient_snd, 1.0f, 0.0f, fade_ms);
    d.ambient_fading_out = true;
    d.ambient_fade_timer = fade_out;
    d.ambient_path.clear();
}

// ── SE ────────────────────────────────────────────────────────────────────────

// 再生長制限: max_duration_sec かけて 0 へフェードしつつ、終端で停止を予約する。
// ハードカットだとクリックノイズが出るため、全区間フェードで自然に減衰させる。
static void applyDurationLimit(ma_engine* engine, ma_sound* sound,
                                float volume, float max_duration_sec) {
    if (max_duration_sec <= 0.0f) return;
    const ma_uint64 sr  = ma_engine_get_sample_rate(engine);
    const ma_uint64 now = ma_engine_get_time_in_pcm_frames(engine);
    const ma_uint64 dur = (ma_uint64)((double)max_duration_sec * (double)sr);
    ma_sound_set_fade_in_milliseconds(
        sound, volume, 0.0f, (ma_uint64)(max_duration_sec * 1000.0f));
    ma_sound_set_stop_time_in_pcm_frames(sound, now + dur);
}

void AudioManager::playSe(SoundEvent event, float volume,
                          float max_duration_sec) {
    Impl& d = *impl_;
    if (!d.engine_ok) return;

    int idx = static_cast<int>(event);
    if (idx < 0 || idx >= static_cast<int>(SoundEvent::COUNT)) return;

    std::string full = resolveAudioPath(d.assets_root + "/" + kSeStems[idx]);

    // 音量・再生長の指定がなければ従来どおり fire-and-forget
    if (volume >= 0.999f && max_duration_sec <= 0.0f) {
        if (ma_engine_play_sound(&d.engine, full.c_str(), &d.se_group) != MA_SUCCESS)
            fprintf(stderr, "[Audio] playSe failed: %s\n", full.c_str());
        return;
    }

    // 音量/再生長を制御するため SE プールの ma_sound を使う (非3D)
    SeSlot* slot = nullptr;
    for (auto& s : d.se_pool) if (!s.initialized) { slot = &s; break; }
    if (!slot) return;  // pool full; drop the sound

    if (ma_sound_init_from_file(&d.engine, full.c_str(), 0,
                                &d.se_group, nullptr,
                                &slot->sound) != MA_SUCCESS) {
        fprintf(stderr, "[Audio] playSe failed: %s\n", full.c_str());
        return;
    }
    slot->initialized = true;
    ma_sound_set_spatialization_enabled(&slot->sound, MA_FALSE);
    ma_sound_set_volume(&slot->sound, volume);
    ma_sound_set_looping(&slot->sound, MA_FALSE);
    applyDurationLimit(&d.engine, &slot->sound, volume, max_duration_sec);
    ma_sound_start(&slot->sound);
}

void AudioManager::playSe3D(SoundEvent event, float x, float y, float z,
                            float max_distance, float max_duration_sec) {
    Impl& d = *impl_;
    if (!d.engine_ok) return;

    // Distance cull
    float dx = x - d.listener_x, dy = y - d.listener_y, dz = z - d.listener_z;
    if (dx*dx + dy*dy + dz*dz > max_distance * max_distance) return;

    // Find a free SE pool slot
    SeSlot* slot = nullptr;
    for (auto& s : d.se_pool) if (!s.initialized) { slot = &s; break; }
    if (!slot) return;  // pool full; drop the sound

    int idx = static_cast<int>(event);
    if (idx < 0 || idx >= static_cast<int>(SoundEvent::COUNT)) return;

    std::string full = resolveAudioPath(d.assets_root + "/" + kSeStems[idx]);
    // Load without streaming (SE are short) and with spatialization enabled
    if (ma_sound_init_from_file(&d.engine, full.c_str(), 0,
                                &d.se_group, nullptr,
                                &slot->sound) != MA_SUCCESS)
        return;

    slot->initialized = true;
    ma_sound_set_spatialization_enabled(&slot->sound, MA_TRUE);
    ma_sound_set_position(&slot->sound, x, y, z);
    ma_sound_set_attenuation_model(&slot->sound, ma_attenuation_model_inverse);
    ma_sound_set_rolloff(&slot->sound, 1.0f);
    ma_sound_set_min_distance(&slot->sound, 1.0f);
    ma_sound_set_max_distance(&slot->sound, max_distance);
    ma_sound_set_looping(&slot->sound, MA_FALSE);
    applyDurationLimit(&d.engine, &slot->sound, 1.0f, max_duration_sec);
    ma_sound_start(&slot->sound);
}

void* AudioManager::getEngineHandle() const {
    return impl_->engine_ok ? &impl_->engine : nullptr;
}
