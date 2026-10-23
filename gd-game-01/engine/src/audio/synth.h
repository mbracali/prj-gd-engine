// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "audio/sound_bank.h"

class GameData;
class Mixer;

// The Synth's sound effects (docs/audio_sfx.md): up to 5 bank slots looked
// up in order, sfx (all of a name's layers play together), fire-and-forget
// play_sfx, and .seq sequences (a new instance per play_sequence).
class Synth {
public:
    Synth(Mixer& mixer, const GameData& data);
    ~Synth();

    // BankManager: loads `path` into slot `slot`, unloading the higher slots.
    void loadSlot(int slot, const std::string& path);
    void clearSlots();
    // Menus: sfx/metagame. Game: the venue's bank, the song's bank, sfx/ingame.
    void loadMenuBanks();
    void loadGameBanks(const std::string& venueBank, const std::string& songBank);

    // {play_sfx name (volume v) (transpose semitones) (pan p) (stop B)}.
    void playSfx(const std::string& name, float volume = 1, float transpose = 0, float pan = 0, bool stop = false);
    void stopSfx(const std::string& name);
    // {synth play_sequence name (volume v) (transpose t) (pan p)}; false if unknown.
    bool playSequence(const std::string& name, float volume = 1, float transpose = 0, float pan = 0);
    void stopAll();
    void pauseAll(bool paused);
    // Once per frame: sequence polling and finished sounds.
    void poll(float dtMs);

    class Seq;
    struct Sound;  // one sfx instance's voices

private:
    friend class SfxSeq;
    std::unique_ptr<Sound> start(const std::string& name, float volume, float transpose, float pan);
    std::unique_ptr<Seq> build(const DataNode& def);

    Mixer& mixer_;
    const GameData& data_;
    std::array<std::unique_ptr<SoundBank>, 5> slots_;
    std::vector<std::unique_ptr<Sound>> fireAndForget_;
    std::vector<std::unique_ptr<Seq>> sequences_;
    std::mt19937 rng_{std::random_device{}()};
    float clockMs_ = 0;
};
