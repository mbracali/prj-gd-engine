// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

// GH3's sound busses (FMOD channel groups): the tree CreateBussSystem builds
// from BussTree (sound_buss.qb: master > User_Sfx > ui > Front_End ...),
// each buss's vol (dB) and pitch (semitones) set by SetSoundBussParams
// {<BussSet> | Buss = {vol pitch} [time = s]}, ignored while the buss is
// locked (SoundBussLock / SoundBussUnLock), and Push / PopSoundBussParams
// [C scripts Master_SFX_Adding_Sound_Busses, menu_audio_settings_*,
// preload_song, create_attract_mode, GH3_AdjustCrowd*Surge, end_song].
// A buss's level is its own and its ancestors' together: each vol v is
// 100 * 2^(v / 6) percent (at most 400, nothing at -100 or less) and each
// pitch p 100 * 2^(p / 12) percent (nothing at -48 or less, at most 1600)
// [C 0x8217AFB0, 0x8217B0B8]. A change with a time ramps linearly in dB /
// semitones [I: FMOD's fade]. Voices and song stems name a leaf; a sound
// with no buss plays on `default` [I].
// The main thread sets and steps the busses; the levels are atomics the
// audio callback reads by index (index() is resolved on the main thread).
class SoundBusses {
public:
    static constexpr int kMax = 128;
    // CreateBussSystem: the tree (a struct of busses, each a struct of its
    // children and LeafNodes = [...]). Busses start at 0 dB, unlocked.
    template <class Visit>
    void create(Visit&& walk) {
        std::lock_guard<std::mutex> g(m_);
        busses_.clear(), index_.clear();
        walk([this](uint32_t name, uint32_t parent) { add(name, parent); });
        recompute();
    }
    bool created() const { return created_; }
    int count() const {
        std::lock_guard<std::mutex> g(m_);
        return int(busses_.size());
    }
    // SetSoundBussParams for one buss (null: that field unchanged).
    void set(uint32_t buss, const float* volDb, const float* pitchSemis, float timeS);
    void lock(uint32_t buss, bool locked);
    void push();
    void pop();
    // Advances the fades (main thread, once a frame).
    void update(double dtMs);
    // The buss's index for gainAt / pitchAt (-1: unknown; then unity).
    int index(uint32_t buss) const;
    // A leaf's level with its ancestors' (linear gain, pitch ratio).
    float gainAt(int i) const { return i >= 0 && i < kMax ? gain_[size_t(i)].load(std::memory_order_relaxed) : 1.0f; }
    float pitchAt(int i) const { return i >= 0 && i < kMax ? pitch_[size_t(i)].load(std::memory_order_relaxed) : 1.0f; }
    float gain(uint32_t buss) const { return gainAt(index(buss)); }
    float pitch(uint32_t buss) const { return pitchAt(index(buss)); }
    // `buss` is `ancestor` or under it (StopSoundsByBuss / PauseSoundsByBuss
    // reach the busses below: PauseGh3Sounds pauses master) [I: FMOD channel groups].
    bool under(uint32_t buss, uint32_t ancestor) const;
    // A buss's own vol (dB) as set (tests, logs); 0 when unknown.
    float volDb(uint32_t buss) const;

private:
    struct Buss {
        uint32_t name = 0;
        int parent = -1;
        float vol = 0, pitch = 0;          // now
        float volTo = 0, pitchTo = 0;      // the fade's target
        float volStep = 0, pitchStep = 0;  // per ms
        double left = 0;                   // ms of fade left
        bool locked = false;
    };
    void add(uint32_t name, uint32_t parent);
    void recompute();
    mutable std::mutex m_;
    std::vector<Buss> busses_;
    std::unordered_map<uint32_t, int> index_;
    std::vector<std::vector<std::pair<float, float>>> pushed_;
    std::array<std::atomic<float>, kMax> gain_{}, pitch_{};
    bool created_ = false;
};

// The game's one set of busses (the sound effects and the song read it).
SoundBusses& soundBusses();
