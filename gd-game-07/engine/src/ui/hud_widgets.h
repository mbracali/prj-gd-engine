// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "data/qb.h"

namespace qs {
class Vm;
}
namespace ui {
class UiSystem;
}

// gh4's in-song HUD widgets (GD10-063). setup_band_hud (start_gem_scroller)
// builds the HUD from a desc (update_hud_layout picks hud_1g, hud_2g, ...;
// z_in_game's uidesc_hud_1g holds the solo rock meter, the multiplier and
// the Star Power pieces) and the HUD_attach_widget_* scripts tie each desc
// prop to a player value with the native AttachHudWidget {Input = {object
// value} output(s) = {type desc_id property ...}} [C scripts guitar_hud.qb,
// hud_widgets.qb]. This is that native: each frame every widget's input is
// read from the judge's values and its outputs set on the desc. The output
// types' rules are the C's and not decoded: each is written from its
// parameters (value_range / range / textures / colors) [I], noted at each.
class HudWidgets {
public:
    // One player's values as the widgets' inputs read them (object
    // player<n>_status; the band's values without an object).
    struct Values {
        double score = 0;
        int streak = 0;
        int multiplier = 1;  // with Star Power's doubling
        bool spActive = false;
        float starPower = 0;  // 0 .. 100
        float health = 1;     // 0 .. 2
        float stars = 0;      // projected_song_stars, 0 .. 5
        bool missedNote = false;
    };
    // The band's values (band<n>_*, band_health, faceoff_ratio) with two or
    // more players (GD10-067); off: the band's values are player 1's (one player).
    struct Band {
        bool on = false;
        std::array<Values, 2> team{};  // band1_*, band2_* (band_*: team 1)
        float faceoffRatio = 0;        // -1 .. 1, player 1 ahead > 0
    };
    // AttachHudWidget on `vm`, its outputs on `ui`'s elements.
    void addNatives(qs::Vm& vm, ui::UiSystem& ui);
    void clear();
    // One frame: every widget's outputs from `players` (`count` of them) at song time `nowMs`.
    void update(const std::array<Values, 4>& players, int count, double nowMs, const Band& band);
    size_t widgets() const { return widgets_.size(); }
    uint64_t outputsSet() const { return outputsSet_; }
    // The desc props set so far, by name (walk checks).
    const std::vector<std::string>& propsSeen() const { return propsSeen_; }

private:
    struct Output {
        uint32_t type = 0;
        qb::Struct p;
        std::map<uint32_t, std::string> last;  // each prop's value last set (set only on a change)
    };
    struct Widget {
        int id = 0;
        int player = 0;      // 0-based; the band's values: player 0 [I]
        int team = -1;       // a band value's team (0: band1_ / band_ / faceoff_ratio, 1: band2_)
        uint32_t value = 0;  // the input's value name
        std::vector<Output> outs;
    };
    double input(const Widget& w, const std::array<Values, 4>& players, double nowMs, const Band& band) const;
    void apply(Widget& w, Output& o, double v, const Values& pl, double nowMs);
    void setProp(uint32_t descId, uint32_t prop, const qb::Value& v, Output& o);

    ui::UiSystem* ui_ = nullptr;
    std::vector<Widget> widgets_;
    int nextId_ = 1;
    uint64_t outputsSet_ = 0;
    std::vector<std::string> propsSeen_;
};
