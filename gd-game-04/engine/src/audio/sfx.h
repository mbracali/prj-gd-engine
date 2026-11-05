// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <SDL3/SDL_audio.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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
    // `buss`: the voice's FMOD buss key (0: none) for the buss calls below.
    uint32_t play(uint32_t sample, float volume, float pitch, uint32_t buss = 0);
    bool playing(uint32_t voice) const;
    void stop(uint32_t voice);
    // StopSound <sample>: every voice of that sample.
    void stopSample(uint32_t sample);
    bool samplePlaying(uint32_t sample) const;
    // StopSoundsByBuss / PauseSoundsByBuss / UnPauseSoundsByBuss.
    void stopBuss(uint32_t buss);
    void pauseBuss(uint32_t buss, bool paused);
    void stopAll();
    // Frees finished voices (call once a frame).
    void update();
    bool has(uint32_t sample) const { return entries_.count(sample) != 0; }
    // A sample's decoded audio (interleaved float), or false.
    bool pcm(uint32_t sample, std::vector<float>& out, int& rate, int& channels);
    struct Stats {
        uint64_t plays = 0, decoded = 0, failures = 0, unknown = 0;
        uint64_t lost = 0;  // played but not taken by the device (nothing heard)
    };
    const Stats& stats() const { return stats_; }

private:
    // A long sound (the menu music, a song preview, a zone's intro crowd:
    // over kStreamBytes of XMA) decodes on a worker thread, packet by
    // packet, while its voices play what is decoded so far (Sfx::update
    // feeds them); decoding a two-minute song on the main thread stopped the
    // game for a quarter of a second. The decoded sound stays (as a whole
    // decode's would).
    static constexpr size_t kStreamBytes = 64 * 1024;
    struct Job {
        std::mutex m;
        std::vector<float> pcm;  // interleaved, growing
        bool done = false, bad = false, counted = false;
        std::thread thread;
    };
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
        std::shared_ptr<Job> job;  // a streamed decode (then pcm stays empty)
    };
    bool decode(Entry& e);
    // Starts `e`'s decode on a worker when it is long; false: decode it at once.
    bool startStream(Entry& e);
    std::vector<std::shared_ptr<Job>> jobs_;  // started (joined at close)
    SDL_AudioDeviceID device_ = 0;
    std::unordered_map<uint32_t, std::shared_ptr<Entry>> entries_;
    struct Voice {
        uint32_t id = 0;
        SDL_AudioStream* stream = nullptr;
        uint32_t sample = 0, buss = 0;
        bool paused = false;
        int bussIndex = -1;           // audio/sound_buss.h (-1: none)
        float volume = 1, pitch = 1;  // the voice's own (vol / 100, pitch / 100)
        float gainSet = -1, ratioSet = -1;  // what the stream has
        std::shared_ptr<Job> job;  // still being fed from a streamed decode
        size_t fed = 0;            // floats of job->pcm put so far
    };
    void applyBuss(Voice& v);
    void feed(Voice& v);
    std::vector<Voice> voices_;
    uint32_t nextVoice_ = 1;
    Stats stats_;
};
