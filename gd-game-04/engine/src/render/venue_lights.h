// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "data/venue.h"
#include "game/chart.h"
#include "render/venue_camera.h"

class Globals;

// GH3's light show (scripts\fx\lightshow.qb + the zone's <zone>_snapshots
// and <zone>_lightshow_mapping, docs/venues.md "Light show"): the song's
// lightshow notes pick a mood (intro, verse, chorus, solo, blackout, flare,
// strobe) and step through the venue's light snapshots for the mood and the
// band's performance; each snapshot sets the colour, intensity and range of
// the venue's lights, blended over the note-set time. Like the cameras, a
// function of the song clock only.
namespace venue {

struct LightState {
    Vec3 color{1, 1, 1};  // 0..1
    float intensity = 0, attenStart = 0, attenEnd = 1;
};

class LightShow {
public:
    void init(const Venue& v, const Globals& g, const Chart& chart);
    // Advances to song time `ms`: the lightshow notes and the performance's
    // changes in time order.
    void update(double ms, const PerfTimeline& perf);
    // Every venue light's values at song time `ms` (indexed as Venue::lights).
    void evaluate(double ms, std::vector<LightState>& out) const;
    // A node flag (createdonprogress / createdfromvariable): LS_ALWAYS, the
    // LS_*_PRE progression flags and the performance / mood state flags.
    bool flag(uint32_t f) const { return flags_.count(f) != 0; }
    uint32_t mood() const { return mood_; }
    int snapshotChanges() const { return changes_; }
    uint32_t snapshot() const { return snapshot_; }

private:
    struct Target {
        LightState from, to;
        double start = 0, blend = 0;
    };
    void applyNote(const ChartEvent& e, double t);
    void enterMood(uint32_t mood, double t);
    void cycle(double t, bool save);
    void play(uint32_t snapshot, double t);
    const qb::Value* modeEntry() const;
    void setStateFlags();
    LightState at(size_t i, double t) const;

    const Venue* v_ = nullptr;
    const Globals* g_ = nullptr;
    std::vector<ChartEvent> notes_;
    size_t noteAt_ = 0;
    double now_ = -1e18;
    double offsetMs_ = 100;  // lightshow_offset_ms [C]
    double defaultBlend_ = 0.15, blend_ = 0.15;  // lightshow_defaultblendtime, LightShow_SetTime [C]
    bool timeEnabled_ = true;
    float overrideMix_ = 0.4f;  // lightshow_coloroverrideblend [C; I: a mix amount]
    bool override_ = false;
    Vec3 overrideColor_{};
    std::unordered_set<uint32_t> overrideExclude_;
    uint32_t mood_ = 0, processor_ = 0, snapshot_ = 0, savedSnapshot_ = 0;
    Performance perf_ = Performance::Medium;
    size_t perfAt_ = 0;  // the timeline's next change
    bool strobeOn_ = false;
    int changes_ = 0;
    std::vector<Target> lights_;
    std::unordered_set<uint32_t> flags_;
    const qb::Struct* mapping_ = nullptr;
    const qb::Struct* snapshots_ = nullptr;
};

}  // namespace venue
