// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <SDL3/SDL.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "audio/vorbis_stream.h"

// Plays a song's multitrack audio and owns the song clock. The clock and
// the per-channel volume / pan / guitar mute are gd01's mixer
// (gd-game-01/engine/src/audio/mixer.cpp); the source is new: Vorbis is far
// heavier than GH1's ADPCM, so a worker thread decodes ahead into a ring
// buffer and the audio callback only mixes (docs/reuse_and_lessons.md L8).
// SDL resamples the song's rate (32000 / 32250 / 44100 Hz) to the device.
//
// Locking: the audio callback runs with the SDL stream's lock held; play()
// and stop() take that lock (and nothing else) while they swap the state,
// with the worker stopped. The worker and the callback share only the ring,
// through atomics.
class Mixer {
public:
    Mixer() = default;
    ~Mixer();
    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;

    // Opens the default playback device; returns false and sets `error` on failure.
    bool open(std::string& error);
    // Releases the device; must run before SDL_Quit().
    void close();

    struct Track {
        float volume = 1;     // gain
        float pan = 0;        // -1 left .. 1 right
        bool guitar = false;  // muted while the player is missing
    };
    // Starts playback after `leadInMs` of silence; song time 0 is the start of the audio.
    void play(std::unique_ptr<VorbisStream> stream, std::vector<Track> tracks, double leadInMs);
    void stop();
    void setPaused(bool paused);
    // The player's guitar channels heard (true) or muted after a miss (false).
    void setGuitarAudible(bool audible) { guitarTarget_.store(audible ? 1.0f : 0.0f); }

    // Song time in ms at `ticksNs` (SDL_GetTicksNS clock), or now if 0.
    double songTimeMs(uint64_t ticksNs = 0) const;
    // The clock for drawing a frame: songTimeMs advanced by the wall clock and
    // eased toward the audio position, so the per-callback re-anchoring of the
    // audio clock never shows up as uneven scrolling (gd01 B54). Main thread only.
    double frameTimeMs();
    bool playing() const { return source_ != nullptr; }
    // Frames the callback found missing in the ring (decoder behind).
    int64_t underruns() const { return underruns_.load(); }

private:
    static void SDLCALL callback(void* user, SDL_AudioStream* stream, int additional, int total);
    void fill(int frames);
    void startWorker();
    void stopWorker();
    void workerLoop();

    SDL_AudioStream* stream_ = nullptr;
    int rate_ = 44100;

    std::unique_ptr<VorbisStream> source_;
    std::vector<Track> tracks_;
    std::vector<float> out_;
    int64_t leadInFrames_ = 0;
    int64_t songFrames_ = 0;  // frames the song has played, lead-in included (not while paused)

    // The decode ring: per channel kRing frames; the worker writes ahead of the callback.
    static constexpr int64_t kRing = 1 << 16;
    std::vector<std::vector<float>> ring_;
    std::atomic<int64_t> written_{0}, read_{0};
    std::atomic<bool> sourceEnded_{false}, workerRun_{false};
    std::thread worker_;
    std::atomic<int64_t> underruns_{0};

    std::atomic<float> guitarTarget_{1};
    float guitarGain_ = 1;  // audio thread only

    // Clock anchor: `anchorFrames_` frames had been played at `anchorNs_`.
    std::atomic<int64_t> anchorFrames_{0};
    std::atomic<uint64_t> anchorNs_{0};
    std::atomic<int64_t> leadIn_{0};
    std::atomic<bool> paused_{false};
    double pausedTimeMs_ = 0;
    double frameMs_ = 0;
    uint64_t frameNs_ = 0;
};
