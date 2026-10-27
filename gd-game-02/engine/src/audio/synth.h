// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "audio/sfx_player.h"

class GameData;
namespace scene {
class Directory;
class Milo;
class Object;
class SynthSample;
}  // namespace scene

// GH2's Synth for sound effects (docs/audio_and_charts.md "Sound effects"):
// the banks' Sfx and sequences (scene/synth_objects.h) played on the
// SfxPlayer's voices.
//   Sfx::Play (0x822ED7F0): every map at once, a SampleInst each
//     (0x822ED458): volume = the caller's dB x the map's dB (x the master
//     fader), pan = caller's + map's, speed = 2^(caller's transpose / 12) x
//     2^(map's / 12) (0x822FFAB8), the map's ADSR (attack / release only on
//     the 360) and the Sfx's send / send_volume. fx_core does nothing on the
//     360 (SampleInst360's slot is empty) [C].
//   SfxSeq: its child; SerialGroupSeq: its children one after another;
//     RandomGroupSeq (0x822F4F18): min(num_simul, children) of them, one
//     uniformly at random when 1, else by selection sampling (each child in
//     order taken with probability needed / left), no repeat rule [C]; each
//     child gets the sequence's avg +- spread (uniform) added to volume,
//     transpose and pan (0x822F0398, 0x822F0560...) [C].
//   WaitSeq: avg_wait_seconds +- wait_spread (0x822EF118) [C].
// A sound has ended when its voices have (SampleInst::IsPlaying: the
// voice runs and its envelope has not finished, 0x826897A8) [C]. Samples
// are decoded once (XMA, audio/xma_decoder.h) and kept.
//
// The named banks are GH2's config/sound.dta (banks (game
// sfx/ingame_bank) (meta sfx/metagame_bank) ...): play(name) looks the name
// up in them in the order they were added. A venue's bank is played through
// its own objects (play(object)).
class Synth {
public:
    Synth();
    ~Synth();
    Synth(const Synth&) = delete;
    Synth& operator=(const Synth&) = delete;

    SfxPlayer& player() { return player_; }

    // Loads config/sound.dta's banks (game, meta, tutorial), decodes their
    // samples and adds them (in that file's order). Returns the problems.
    std::vector<std::string> loadBanks(const GameData& data);
    // config/sound.dta (options_screen (slider -96 ... 0)): the options'
    // volume index (0..11) in dB [C data].
    static float optionsVolumeDb(const GameData& data, int index);

    void addBank(const std::string& key, const scene::Directory* dir);
    void removeBank(const std::string& key);
    const std::vector<std::pair<std::string, const scene::Directory*>>& banks() const { return banks_; }
    // Decodes every SynthSample of `dir` (and its sub-directories) now;
    // returns how many decoded.
    int preload(const scene::Directory& dir);
    // Forgets what refers to `dir`'s objects (its sounds stop, its decoded
    // samples go): call before the directory is freed.
    void forget(const scene::Directory& dir);

    // Plays the bank object `name`; false when no bank has it.
    // `synth play_sequence <name> (volume dB) (pan p) (transpose semitones)`
    // (0x822E8A90) and an Sfx's play take the same three [C].
    bool play(const std::string& name, float volumeDb = 0, float pan = 0, float transpose = 0);
    // Plays a sound object (Sfx or a sequence); false for other classes.
    bool play(const scene::Object& o, float volumeDb = 0, float pan = 0, float transpose = 0);
    // Stops what `o` (or the bank object `name`) is playing.
    void stop(const scene::Object& o);
    bool stop(const std::string& name);
    void stopAll();
    void setPaused(bool paused);
    // Advances the sequences (waits, serial groups); every frame.
    void update();
    // `synth set_master_vol <dB>` / `master_vol` (the master Fader,
    // 0x822F7878; the mute cheat sets kDbSilence) [C].
    void setMasterVolumeDb(float db) { player_.setMasterDb(db); }
    float masterVolumeDb() const { return player_.masterDb(); }
    // The options' sound effects volume in dB ({options get_fx_volume}):
    // GH2 passes it as the volume of the game's own sounds (play_game_sfx,
    // the crowd's reactions 0x822DF140), not to every sound [C].
    void setFxVolumeDb(float db) { fxVolumeDb_ = db; }
    float fxVolumeDb() const { return fxVolumeDb_; }
    // The bank object `name`, or nullptr.
    const scene::Object* find(const std::string& name) const;

    // A SynthSample decoded (cached), or nullptr with `error`.
    std::shared_ptr<const SfxPcm> pcm(const scene::SynthSample& s, std::string* error = nullptr);
    // The submix voices of an FxSendReverb (made once per object).
    std::shared_ptr<SfxPlayer::Bus> bus(const scene::Object& send);

    // RandomGroupSeq's choice (0x822F4F18): the children (indices) it
    // plays, in order. One: randomInt(0, count); more: selection sampling,
    // each child in turn taken with probability needed / left [C].
    static std::vector<int> randomGroupPick(int count, uint32_t numSimul, const std::function<float()>& random01,
                                            const std::function<int(int, int)>& randomInt);

    struct Stats {
        int plays = 0;          // sounds started (play calls that found a sound)
        int voices = 0;         // sample voices started
        int decoded = 0;        // samples decoded
        int decodeFailures = 0;
        int unresolved = 0;     // references to missing samples / children
        double decodeMs = 0;
    };
    const Stats& stats() const { return stats_; }

private:
    struct Inst;
    void start(Inst& in, double now);
    void startChild(Inst& in, const scene::Object* child, double now);
    bool step(Inst& in, double now);  // true when done
    void stopInst(Inst& in);
    double now() const;
    float spread(float avg, float spread);
    float random01();                 // RandomFloat's 16-bit draw (0x8226DD10) / 65536
    int randomInt(int lo, int hi);    // [lo, hi) (0x8226DCB8)
    static bool refersTo(const Inst& in, const scene::Directory& dir);

    SfxPlayer player_;
    std::vector<std::unique_ptr<scene::Milo>> milos_;  // the loaded banks
    std::vector<std::pair<std::string, const scene::Directory*>> banks_;
    std::map<const scene::SynthSample*, std::shared_ptr<const SfxPcm>> cache_;
    std::map<const scene::Object*, std::shared_ptr<SfxPlayer::Bus>> buses_;
    std::vector<std::unique_ptr<Inst>> playing_;
    std::minstd_rand rng_;
    Stats stats_;
    float fxVolumeDb_ = 0;
    bool paused_ = false;
    double pausedAt_ = 0, pausedTotal_ = 0;
};
