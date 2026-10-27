// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "data/bytes.h"

class GameData;
struct DataNode;

// GH2's `crowd_audio` (CrowdAudio, ctor 0x822E0928, Handle 0x822DFD98;
// docs/venues.md "Crowd audio"): the venue's crowd streams,
// world/<v>/streams/<file>.mogg (plain Vorbis moggs, stereo), named by the
// venue type's (sound (crowd (intro dB file) (levels (kExcitementBoot dB
// file) ...) (encore_outro dB file (loop_ms a b)) (encore_intro dB file)))
// [C data: world/<v>/gen/<v>.dta]. One stream plays at a time
// (0x822DFC10 replaces it) [I: a short fade instead of a cut]:
//   intro_start_msg ($fast $encore) -> encore_intro when $encore and not
//     $fast, else intro (0x822DFCA8); played once;
//   music_start -> started, level Okay (+0x40 = 2) then the game's
//     excitement (0x822DF590);
//   excitement (level) -> after crowd_audio_delay (config/sound.dta: 1 s;
//     +0x34) the level's loop (excitement_loop), every time;
//   encore_outro -> encore_outro, looping between its loop_ms (0x822E1688);
//   turn_off (ui lose.dta; here at game_lost [I]) -> silence.
// The reaction sounds (+0x64 crowd_begin at the intro, +0x68 crowd_win at
// game_won_msg, +0x6c crowd_lose at game_lost, per level change
// crowd_upto_* / crowd_dnto_* (+0x70): 0x822DFAB0 looks them up, 0x822DF350
// plays one, stopping the last, when the mode's crowd_reactions) are the
// venue bank's Sfx: game/crowd.cpp plays them through the world
// (audio/synth.h), reaction() names them.
//
// Playback: its own SDL audio stream (SDL mixes it with the song's), 44100
// Hz stereo float; the moggs are decoded once, on a worker thread, into
// memory (a venue's seven files: ~1.5 MB of Vorbis).
class CrowdAudio {
public:
    // `sound`: the venue type's (sound ...) array (may be null).
    CrowdAudio(const GameData& data, const std::string& venue, const DataNode* sound);
    ~CrowdAudio();
    CrowdAudio(const CrowdAudio&) = delete;
    CrowdAudio& operator=(const CrowdAudio&) = delete;

    enum Stream { kIntro, kBoot, kBad, kOkay, kGreat, kPeak, kEncoreIntro, kEncoreOutro, kStreams };
    struct Entry {
        std::string file;
        float db = 0;
        int loopStartMs = -1, loopEndMs = -1;
    };
    const Entry& entry(Stream s) const { return entries_[size_t(s)]; }
    const std::string& bank() const { return bank_; }

    // Opens the audio device (no sound until then; the tests leave it shut).
    bool open();
    // The messages (see above). `now`: seconds on the world's clock.
    void introStart(bool fast, bool encore);
    void musicStart(int excitement, double now);
    void excitement(int level, double now);
    void encoreOutro();
    void turnOff();
    void setPaused(bool paused);
    void setEnabled(bool on);
    bool enabled() const { return enabled_; }
    void setVolume(float v) { volume_.store(v); }
    // Runs the delayed excitement_loop.
    void update(double now);

    // What plays (for the tests and logs): the stream, or kStreams.
    Stream playing() const { return Stream(playing_.load()); }
    int started() const { return started_; }
    // The reaction sound the game plays for a change prev -> level:
    // crowd_<upto | dnto>_<danger poor norm good peak> (0x822DFAB0, slot
    // (level + 14) x 2 + (prev <= level), 0x822DF590) [C]; the banks have
    // only upto_poor / norm / good and dnto_danger / poor / norm.
    static std::string reaction(int prev, int level);
    // Decoded streams (waits for the worker).
    int decodedStreams();

private:
    struct Pcm {
        std::vector<float> frames;  // stereo interleaved
        int rate = 44100;
        bool ok = false;
    };
    struct Voice {
        int stream = -1;
        double pos = 0;  // frames of the source
        float gain = 0, target = 0;
        bool loop = false;
        double loopStart = 0, loopEnd = 0;  // ms (loop_ms)
    };
    static void SDLCALL callback(void* user, SDL_AudioStream* stream, int additional, int total);
    void mix(float* out, int frames);
    void play(Stream s, bool loop);
    void decodeAll();

    std::string venue_, bank_;
    std::array<Entry, kStreams> entries_;
    std::array<Bytes, kStreams> files_;
    std::array<Pcm, kStreams> pcm_;
    std::thread worker_;
    std::atomic<bool> decoded_{false};

    SDL_AudioStream* stream_ = nullptr;
    std::mutex lock_;          // voices_ (audio thread and main thread)
    std::array<Voice, 2> voices_;  // the current stream and the one fading out
    std::atomic<int> playing_{kStreams};
    std::atomic<float> volume_{1};
    std::vector<float> scratch_;
    bool enabled_ = true, paused_ = false;
    int started_ = 0;
    int level_ = 2;
    double pendingAt_ = -1;  // excitement_loop time
};
