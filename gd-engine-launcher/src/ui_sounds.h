// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The launcher's menu sounds: a game's own (Guitar Hero's, from the player's
// disc, given by its module's gd_game_ui_sounds), else the launcher's own
// built-in ones (synthesized at start: plucked strings and a power chord,
// nothing taken from a game). Kept in memory and played on SDL's default
// audio device. The device is released while a game runs (the game opens its own).
#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <random>
#include <string>
#include <vector>

enum class UiSound { Move, Select, Play, Back, Error, Count };

class UiSounds {
public:
    UiSounds() = default;
    ~UiSounds() { close(); }
    UiSounds(const UiSounds&) = delete;
    UiSounds& operator=(const UiSounds&) = delete;

    // Adds a variation of `role` ("move", "select", "play", "back", "error"):
    // interleaved stereo float at `rate` Hz. Unknown roles are ignored.
    void add(const std::string& role, const float* stereo, int frames, int rate);
    void clear();
    // Replaces whatever is loaded with the launcher's own sounds.
    void addBuiltIn();
    bool builtIn() const { return builtIn_; }
    int count() const;
    // Opens / releases the audio device (nothing to do while none are known).
    bool open();
    void close();
    void play(UiSound s);
    // Logs each sound played (SDL_Log), for scripted runs.
    void setLogging(bool on) { logging_ = on; }

private:
    struct Clip {
        std::vector<float> pcm;
        int rate = 48000;
    };
    std::array<std::vector<Clip>, size_t(UiSound::Count)> clips_;
    SDL_AudioDeviceID device_ = 0;
    std::array<SDL_AudioStream*, 4> streams_{};
    size_t next_ = 0;
    bool audioInit_ = false, logging_ = false, builtIn_ = false;
    std::mt19937 rng_{std::random_device{}()};
};
