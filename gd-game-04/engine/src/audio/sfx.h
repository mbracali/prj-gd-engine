// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <SDL3/SDL_audio.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "data/bytes.h"

class Disc;

// GH3's sound effects: the .wav entries of the sound paks (RIFF/WAVE with
// XMA, FMOD's XMA codec; docs/formats.md), decoded on first use and played
// as voices on one logical playback device (each voice an SDL stream bound
// to it, resampled by SDL). The sound events that pick them (the scripts'
// <event>_container structs) are resolved by the natives (natives_sound.cpp).
class Sfx {
public:
    ~Sfx();
    bool open(std::string& error);
    void close();
    // Indexes a pak's .wav entries (by full and short name).
    void addPak(const Disc& disc, const std::string& pakPath);
    // Indexes an FMOD bank's samples by its .dat names (streamall: the menu
    // music, guitar licks, crowd) [C data].
    void addFsb(const Disc& disc, const std::string& fsbPath, const std::string& datPath);
    // Plays a sample; volume 1 = as recorded, pitch 1 = normal. Returns a
    // voice id (0: unknown sample).
    uint32_t play(uint32_t sample, float volume, float pitch);
    bool playing(uint32_t voice) const;
    void stop(uint32_t voice);
    void stopAll();
    // Frees finished voices (call once a frame).
    void update();
    bool has(uint32_t sample) const { return entries_.count(sample) != 0; }
    // A sample's decoded audio (interleaved float), or false.
    bool pcm(uint32_t sample, std::vector<float>& out, int& rate, int& channels);
    struct Stats {
        uint64_t plays = 0, decoded = 0, failures = 0, unknown = 0;
    };
    const Stats& stats() const { return stats_; }

private:
    struct Entry {
        Bytes riff;
        std::vector<float> pcm;  // interleaved, decoded on first play
        int rate = 44100, channels = 1;
        bool decoded = false, bad = false;
        // An FSB sample instead of a RIFF: decoded from the bank.
        const Disc* disc = nullptr;
        std::string bank;
        uint64_t offset = 0;
        uint32_t stored = 0, length = 0;
        bool encrypted = false;
    };
    bool decode(Entry& e);
    SDL_AudioDeviceID device_ = 0;
    std::unordered_map<uint32_t, std::shared_ptr<Entry>> entries_;
    struct Voice {
        uint32_t id;
        SDL_AudioStream* stream;
    };
    std::vector<Voice> voices_;
    uint32_t nextVoice_ = 1;
    Stats stats_;
};
