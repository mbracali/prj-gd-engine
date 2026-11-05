// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "audio/audio_source.h"
#include "audio/fast_pitch.h"
#include "audio/guitar_fx.h"

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
        bool guitar2 = false;  // the second player's part (two players on different parts)
        // Its sound buss (audio/sound_buss.h: SoundBusses::index; -1 none):
        // the buss's level and its parents' scale it.
        int buss = -1;
    };
    // Starts playback after `leadInMs` of silence; song time 0 is the start of the audio.
    // `stream`: a song (VorbisStream) or any AudioSource (a video's sound).
    void play(std::unique_ptr<AudioSource> stream, std::vector<Track> tracks, double leadInMs);
    void stop();
    void setPaused(bool paused);
    // The song's streams stopped (GH3's StopStream at the song's end) while
    // the song clock runs on: the rest plays silent [C script end_song].
    void silence() { silenced_ = true; }
    // GH3's Band_Balance / Guitar_Balance busses at the song's end
    // (SetSoundBussParams {Band_Balance = {vol pitch}} time = s): `db` the
    // band's (guitar = false) or the guitars' volume, `semitones` the pitch,
    // reached linearly over `seconds` from where they are [C script
    // Failed_Song_Pitch_Down: vol -20, pitch -8 in 3 s; 0x8217B0B8: pitch in
    // semitones, 2^(st / 12); I: linear in dB and semitones]. The streams
    // are resampled (both busses pitch alike in GH3's data: the band's sets
    // the ratio); the song clock then runs on with the wall clock.
    void setBuss(bool guitar, float db, float semitones, float seconds);
    // The player's guitar channels heard (true) or muted after a miss (false).
    void setGuitarAudible(bool audible) { guitarTarget_.store(audible ? 1.0f : 0.0f); }
    // The second player's part (Track::guitar2), the same way.
    void setGuitar2Audible(bool audible) { guitar2Target_.store(audible ? 1.0f : 0.0f); }
    // Practice's slowed song (GH3's setslomo current_speedfactor, the streams
    // started at pitch = current_speedfactor * 100) [C scripts
    // start_gem_scroller, startpreloadpaused_song]: every stream is resampled
    // to `speed` (1 normal) and the song clock runs at `speed` with it (it
    // counts the stream's frames). `guitarPitch` (PitchShiftSlow1's pitch,
    // Player1PracticeEffects: 1 none) shifts player 1's guitar tracks back
    // up, ahead of the whammy's FastPitch [C data]. play() resets both to 1.
    void setSlomo(float speed, float guitarPitch);
    float slomo() const { return speed_.load(); }
    // The guitar buss's echo / reverb / volume (GH3's setsoundbusseffects,
    // SetSoundBussParams; ui/song_hooks.cpp): the guitar tracks pass through it.
    GuitarFx& guitarFx() { return guitarFx_; }
    // The FastPitchShift on a player's guitar tracks (the whammy, the boss's
    // pitch down); 1 is off, 0 counts as 1 [C 0x826E67E0].
    void setGuitarPitch(int player, float pitch) { guitarPitch_[size_t(player & 1)].store(pitch == 0 ? 1.0f : pitch); }
    float guitarPitch(int player) const { return guitarPitch_[size_t(player & 1)].load(); }
    // Frames that guitar lags behind the band.
    float guitarPitchLag(int player) const { return guitarLag_[size_t(player & 1)].load(); }

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

    std::unique_ptr<AudioSource> source_;
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
    std::atomic<float> guitar2Target_{1};
    float guitar2Gain_ = 1;
    std::vector<float> bussGain_;  // per track, eased to its buss's level (audio thread)
    GuitarFx guitarFx_;
    std::array<FastPitch, 2> pitch_;  // audio thread only
    std::array<std::atomic<float>, 2> guitarPitch_{1.0f, 1.0f}, guitarLag_{0.0f, 0.0f};

    // Clock anchor: `anchorFrames_` frames had been played at `anchorNs_`.
    std::atomic<int64_t> anchorFrames_{0};
    std::atomic<uint64_t> anchorNs_{0};
    std::atomic<int64_t> leadIn_{0};
    std::atomic<bool> paused_{false};
    std::atomic<bool> silenced_{false};
    // The busses' ramps (setBuss; under the stream's lock): band 0, guitar 1.
    struct Buss {
        float fromDb = 0, toDb = 0, fromSt = 0, toSt = 0, seconds = 0;
        uint64_t startNs = 0;
        float db(uint64_t now) const { return fromDb + (toDb - fromDb) * t(now); }
        float st(uint64_t now) const { return fromSt + (toSt - fromSt) * t(now); }
        float t(uint64_t now) const {
            return seconds <= 0 || now <= startNs ? (seconds <= 0 ? 1.0f : 0.0f) : std::min(1.0f, float(double(now - startNs) / 1e9 / seconds));
        }
    };
    std::array<Buss, 2> buss_{};
    std::atomic<bool> pitched_{false};  // a buss pitch was set: the clock follows the wall clock
    float ratio_ = 1;  // the stream's frequency ratio now (main thread)
    std::atomic<float> speed_{1.0f};      // setSlomo's speed: the stream's ratio and the clock's rate
    std::atomic<float> slowPitch_{1.0f};  // setSlomo's guitar pitch (PitchShiftSlow1)
    // PitchShiftSlow1 [C data: effect PitchShift, fftsize 4096] as a two-tap
    // delay-line shifter (crossfaded taps W / 2 apart, sin^2 gains) reading
    // its input W / 2 frames ahead so it adds no delay on average [I: GH3's
    // is FMOD's FFT pitch shifter]. Audio thread only.
    struct SlowShift {
        static constexpr int kW = 2048, kRing = 4096;
        std::array<std::array<float, kRing>, 2> ring{};
        uint32_t write = 0;
        float d = 0;
        void process(float& l, float& r, float pitch);
    };
    SlowShift slowShift_;
    void updatePitch();
    double pausedTimeMs_ = 0;
    double frameMs_ = 0;
    uint64_t frameNs_ = 0;
};
