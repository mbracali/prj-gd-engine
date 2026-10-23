// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <random>
#include <string>
#include <vector>

#include "data/dta.h"
#include "game/chart.h"

class Character;

// The guitarist's hand animation (docs/guitarist_hands.md): GuitarAnimInfo's
// timed events built once per song, and the per-frame hand logic of
// GuitarCharacter (FUN_002867b8) that places bone_fret on the neck and
// schedules the left / right hand clips.
class GuitarHands {
public:
    struct Event {
        float start, end;  // beats
        int value = 0;     // fret: spot index 0..19
        std::string clip;  // finger / strum clip
    };

    // `charsys` is charsys/charsys.dta (hand_events, hand_finger_mapping,
    // hand_strum_mapping); `chart` the guitarist's gems.
    GuitarHands(const Song& song, const Chart& chart, const DataNode& charsys);

    // Before the character's poll: `beat` is the song beat (tick / 480),
    // `active` whether the player is hitting (hit_gem; cleared by miss / pass).
    void update(float beat, bool active, Character& character);

    const std::vector<Event>& frets() const { return fret_; }
    const std::vector<Event>& fingers() const { return finger_; }
    const std::vector<Event>& strums() const { return strum_; }

private:
    void place(float beat, bool active, Character& c);
    static const Event* next(float beat, const std::vector<Event>& list);

    std::vector<Event> fret_, finger_, strum_;
    std::mt19937 rng_{5};
    int cur_ = 3, prev_ = 3;  // spot_neck_fret04 until the first event
    float holdEnd_ = -1e30f, moveStart_ = 0, moveTarget_ = 0, nextL_ = -1e30f, nextR_ = -1e30f;
};
