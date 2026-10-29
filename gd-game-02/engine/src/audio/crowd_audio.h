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
// [C data: world/<v>/gen/<v>.dta]. One stream plays at a time: a new one
// deletes the old (0x822DF860) [C]:
//   intro_start_msg ($fast $encore) -> encore_intro when $encore and not
//     $fast, else intro (0x822DFCA8), and the crowd_begin reaction;
//   music_start -> started (+0x3a), the last level (+0x40) = Okay, then
//     the game's excitement (Game +0x338) as below;
//   excitement (level) (0x822DF590), only once started and while enabled
//     (+0x38): the reaction for last -> level (even the same level), the
//     last level = level, and an `excitement_loop level` message
//     crowd_audio_delay (config/sound.dta: 1 s; +0x34) later; each such
//     message plays that level's loop (0x822DFD40);
//   encore_outro -> encore_outro, looping between its loop_ms (0x822E1688);
//   turn_off (ui lose.dta, endgame.dta) -> the stream deleted (0x822DF0E8);
//   set_enabled (0x822DF9D8): off deletes the stream, on runs excitement
//     with the last level; the mode's crowd_audio (modes.dta: FALSE in
//     practice and tutorial) at construction.
// Volume (0x822DF2F8) [C]: the entry's dB + set_volume's dB (+0x2c) +
// crowd_volume (config/sound.dta: -2 dB; +0x30) + the options' sound
// effects volume (0x822DF140: 0 dB in the attract mode), applied when a
// stream starts and on set_volume.
// The reaction sounds (+0x64 crowd_begin at the intro, +0x68 crowd_win at
// game_won_msg, +0x6c crowd_lose at game_lost, per excitement crowd_upto_*
// / crowd_dnto_* (+0x70): 0x822DFAB0 looks them up, 0x822DF350 plays one,
// stopping the last, when crowd_reactions (config/sound.dta: TRUE; +0x39))
// are the venue bank's Sfx: game/crowd.cpp plays them through the world
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
    // musicStart / excitement / setEnabled return the reaction sound to
    // play (empty: none).
    void introStart(bool fast, bool encore);
    std::string musicStart(int excitement, double now);
    std::string excitement(int level, double now);
    void encoreOutro();
    void turnOff();
    void setPaused(bool paused);
    std::string setEnabled(bool on, double now);
    bool enabled() const { return enabled_; }
    // set_volume (dB, +0x2c).
    void setVolumeDb(float db);
    // The options' sound effects volume (dB) for the next stream / set_volume.
    void setFxVolumeDb(float db) { fxDb_ = db; }
    // Runs the delayed excitement_loop messages that are due.
    void update(double now);
    int lastLevel() const { return level_; }

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
        float gain = 0;
        bool loop = false;
        double loopStart = 0, loopEnd = 0;  // ms (loop_ms)
    };
    static void SDLCALL callback(void* user, SDL_AudioStream* stream, int additional, int total);
    void mix(float* out, int frames);
    void play(Stream s, bool loop);
    float gainOf(Stream s) const;
    void decodeAll();

    std::string venue_, bank_;
    std::array<Entry, kStreams> entries_;
    std::array<Bytes, kStreams> files_;
    std::array<Pcm, kStreams> pcm_;
    std::thread worker_;
    std::atomic<bool> decoded_{false};

    SDL_AudioStream* stream_ = nullptr;
    std::mutex lock_;          // voice_ (audio thread and main thread)
    Voice voice_;              // the one stream
    std::atomic<int> playing_{kStreams};
    std::vector<float> scratch_;
    bool enabled_ = true, paused_ = false;
    int started_ = 0;
    int level_ = 2;            // +0x40: the last level
    float volumeDb_ = 0;       // +0x2c set_volume
    float fxDb_ = 0;           // 0x822DF140
    std::vector<std::pair<double, int>> pending_;  // excitement_loop messages: (time, level)
};
