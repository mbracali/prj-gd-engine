// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "audio/spu.h"
#include "data/vgs.h"

// Plays a song's multitrack audio and provides the song clock.
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
        float volume = 1;
        float pan = 0;       // -1 left .. 1 right
        bool guitar = false; // muted while the player is missing
    };
    // Starts playback after `leadInMs` of silence; song time 0 is the start of the audio.
    void play(std::unique_ptr<VgsStream> stream, std::vector<Track> tracks, double leadInMs);
    void stop();
    void setPaused(bool paused);
    // Each player's copy of the guitar channels (UnmuteBeatMatchAudio, one per
    // PlayerMatcher; docs/guitar_audio.md). Single player: one copy, the
    // song's own guitar channels. Face-off: two copies, each panned toward its
    // player's side (pan * multiplayer_scale -/+ (1 - scale)) and attenuated by
    // multiplayer_attenuation; player 2's copy is a second stream instance
    // holding only the guitar channels. `atten` is a gain (10^(-dB/20)).
    void setGuitarCopies(int count, float panScale = 1, float atten = 1);
    // StopSlip (false) / SlipTo (true): a copy's slip voice, the guitar heard or not.
    void setGuitarAudible(bool audible, int copy = 0) { copies_[size_t(copy)].target.store(audible ? 1.0f : 0.0f); }
    // A copy's channel volume factor (player_matcher set_volume; the common
    // attenuation latched with it).
    void setGuitarVolume(float v, int copy = 0) { copies_[size_t(copy)].volume.store(v); }
    // The whammy (docs/whammy_audio_plan.md): the guitar channels play through a
    // delay line at `speed` (<= 1), so they drop in pitch and fall behind the
    // song (the PS2's slip voice); resyncGuitar() jumps them back in sync.
    void setGuitarSlipSpeed(float speed, int copy = 0) { copies_[size_t(copy)].slipSpeed.store(speed); }
    void resyncGuitar(int copy = 0) { copies_[size_t(copy)].resync.fetch_add(1); }
    double guitarSlipMs(int copy = 0) const { return copies_[size_t(copy)].lagMs.load(); }

    // Looping stream voices besides the song (VGS, e.g. menu music and the
    // crowd): slot `slot` plays `stream` from `startMs`, looping back there at
    // `endMs` when `loop` (endMs 0 = the end of the stream), fading in over
    // `fadeMs`. stopStream fades it out.
    enum StreamSlot { kMusic = 0, kCrowd = 1, kVoice = 2, kStreamSlots = 3 };
    void playStream(int slot, std::unique_ptr<VgsStream> stream, std::vector<Track> tracks, double startMs, double endMs,
                    bool loop, double fadeMs, float volume);
    void stopStream(int slot, double fadeMs);
    void setStreamVolume(int slot, float volume);
    void setStreamPaused(int slot, bool paused);
    bool streamPlaying(int slot) const { return streams_[slot].playing.load(); }
    void playMusic(std::unique_ptr<VgsStream> stream, std::vector<Track> tracks, double startMs, double endMs,
                   bool loop, double fadeMs, float volume) {
        playStream(kMusic, std::move(stream), std::move(tracks), startMs, endMs, loop, fadeMs, volume);
    }
    void stopMusic(double fadeMs) { stopStream(kMusic, fadeMs); }
    bool musicPlaying() const { return streamPlaying(kMusic); }

    // The sound-effect voices.
    Spu& spu() { return spu_; }
    // Song playback speed (game_lost slows the music to 0 over 2 s).
    void setSongSpeed(float speed) { songSpeed_.store(speed); }

    // Song time in ms at `ticksNs` (SDL_GetTicksNS clock), or now if 0.
    double songTimeMs(uint64_t ticksNs = 0) const;
    bool paused() const { return paused_; }
    // The clock for drawing a frame: songTimeMs advanced by the wall clock and
    // eased toward the audio position, so the per-callback re-anchoring of the
    // audio clock never shows up as uneven scrolling. Main thread only.
    double frameTimeMs();
    bool playing() const { return stream_ != nullptr && vgs_ != nullptr; }

private:
    static void SDLCALL callback(void* user, SDL_AudioStream* stream, int additional, int total);
    void fill(int frames);

    SDL_AudioStream* stream_ = nullptr;
    int rate_ = 44100;

    std::mutex mutex_;  // guards the fields below against the audio thread
    std::unique_ptr<VgsStream> vgs_;
    std::vector<Track> tracks_;
    std::vector<std::vector<float>> scratch_;
    std::vector<float> out_;
    int64_t leadInFrames_ = 0;
    int64_t framesPushed_ = 0;
    int64_t songFrames_ = 0;  // frames the song has played (not while paused)
    struct GuitarCopy {
        std::atomic<float> target{1}, volume{1}, slipSpeed{1};
        std::atomic<int> resync{0};
        std::atomic<double> lagMs{0};
        // Audio thread only:
        float gain = 1;
        int resyncSeen = 0;
        double lag = 0;  // the slip voice's lag, in source frames
    };
    std::array<GuitarCopy, 2> copies_;
    std::atomic<int> copyCount_{1};
    std::atomic<float> copyPanScale_{1}, copyAtten_{1};
    static constexpr uint32_t kSlipRing = 16384;
    std::vector<std::vector<float>> slipHist_;  // per channel (guitar channels only used)
    uint32_t slipWrite_ = 0;

    // Clock anchor: `anchorFrames_` frames had been played at `anchorNs_`.
    std::atomic<int64_t> anchorFrames_{0};
    std::atomic<uint64_t> anchorNs_{0};
    std::atomic<int64_t> leadIn_{0};
    std::atomic<bool> paused_{false};
    double pausedTimeMs_ = 0;
    double frameMs_ = 0;
    uint64_t frameNs_ = 0;

    struct Stream {
        std::unique_ptr<VgsStream> vgs;
        std::vector<Track> tracks;
        int64_t pos = 0, start = 0, end = 0;
        bool loop = false, paused = false;
        float gain = 0, target = 0, step = 0, volume = 1;
        std::atomic<bool> playing{false};
        // Resampling to the device rate (the crowd loops are 22050 Hz):
        // source frames per output frame, the position between src[0] and
        // src[1], and per channel the previous source frame then the new ones.
        double ratio = 1, frac = 1;
        std::vector<std::vector<float>> src;
    };
    std::array<Stream, kStreamSlots> streams_;
    void fillStream(Stream& s, int frames);
    Spu spu_;
    std::atomic<float> songSpeed_{1};
    double songFrac_ = 0;
};
