// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/touch_strip.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "data/qb.h"
#include "data/qbkey.h"
#include "script/globals.h"

namespace {
uint32_t K(const char* s) { return qbkey::key(s); }
}  // namespace

std::vector<TouchZone> TouchStrip::defaultZones() {
    // guitar_touch_patterns read from the World Tour disc (PAK/qb.pak.xen) [C
    // data]: nine zones, the five single-finger positions and the four
    // two-finger positions between them. touch_pattern is a per-finger bit
    // field (one nibble a fret). Used as the fallback / in tests.
    return {
        {-107, 0x10000}, {-80, 0x11000}, {-51, 0x1000}, {-26, 0x1100}, {26, 0x100},
        {46, 0x110},     {72, 0x10},     {99, 0x11},     {127, 0x1},
    };
}

bool TouchStrip::load(const Globals& g) {
    bool real = false;
    if (const qb::Value* v = g.find(K("guitar_touch_patterns")); v && v->type == qb::Type::Array && v->arr) {
        std::vector<TouchZone> zones;
        for (const qb::Value& e : v->arr->values) {
            if (e.type != qb::Type::Struct || !e.st) continue;
            TouchZone z;
            if (const qb::Value* t = g.field(*e.st, K("touch_value"))) {
                if (const qb::Value* r = g.resolve(*t)) z.value = int32_t(r->number());
            }
            if (const qb::Value* p = g.field(*e.st, K("touch_pattern"))) {
                if (const qb::Value* r = g.resolve(*p)) z.pattern = int32_t(r->number());
            }
            zones.push_back(z);
        }
        if (!zones.empty()) zones_ = std::move(zones), real = true;
    }
    // touch_pattern_switch_frames is a plain int global (0 on the disc).
    switchFrames_ = qbInt(g, K("touch_pattern_switch_frames"), switchFrames_);
    if (switchFrames_ < 0) switchFrames_ = 0;
    // enable_touch_strip is set by a script, so it is usually absent from the
    // boot data; the touch strip is a core World Tour feature, so default on
    // and let a present value (0) turn it off.
    enabled_ = qbInt(g, K("enable_touch_strip"), 1) != 0;
    return real;
}

int TouchStrip::nearestZone(int slider) const {
    int best = -1, bestDist = kWindow + 1;
    for (size_t i = 0; i < zones_.size(); ++i) {
        const int d = std::abs(slider - zones_[i].value);
        if (d <= kWindow && d < bestDist) bestDist = d, best = int(i);
    }
    return best;
}

void TouchStrip::reset() {
    committed_ = pending_ = -1;
    pendingFrames_ = 0;
    wasDown_ = false;
}

TouchStrip::Touch TouchStrip::frame(int slider, bool down) {
    Touch out;
    if (!down) {
        // The finger left the strip: no zone held, nothing committed.
        committed_ = pending_ = -1;
        pendingFrames_ = 0;
        wasDown_ = false;
        return out;
    }
    const int zoneHit = nearestZone(slider);
    if (zoneHit >= 0 && zoneHit != committed_) {
        // A newly matched zone has to persist switchFrames_ frames before it is
        // committed (the debounce). A dead band (zoneHit < 0) keeps the held zone,
        // so sliding across the gap between two zones stays one touch (hysteresis):
        // only a matched zone updates the pattern [C 0x8213e4b8 within +/-16].
        if (zoneHit == pending_) ++pendingFrames_;
        else pending_ = zoneHit, pendingFrames_ = 0;
        if (pendingFrames_ >= switchFrames_) {
            const bool wasHeld = committed_ >= 0;
            committed_ = zoneHit;
            pending_ = -1;
            pendingFrames_ = 0;
            out.changed = true;
            // A slide: the finger was already down on another zone and slid
            // across; a tap: the finger just came down (wasDown_ was false).
            out.slide = wasHeld && wasDown_;
        }
    } else {
        // On the held zone, or in a dead band: nothing to commit.
        pending_ = -1;
        pendingFrames_ = 0;
    }
    out.touched = committed_ >= 0;
    out.zone = committed_;
    out.pattern = committed_ >= 0 ? zones_[size_t(committed_)].pattern : 0;
    wasDown_ = true;
    return out;
}

void TapJudge::frame(double nowMs, const TouchStrip::Touch& t) {
    if (!t.changed) return;
    // The span whose window holds nowMs (with the edge grace), still needing taps.
    for (size_t i = open_; i < phrases_.size(); ++i) {
        const TapPhrase& p = phrases_[i];
        const double lo = p.time - kGraceMs, hi = p.time + p.length + kGraceMs;
        if (nowMs < lo) break;          // not reached yet (phrases are time-ordered)
        if (nowMs > hi) continue;       // this one has passed; look at the next
        if (per_[i] >= p.count) continue;
        ++per_[i];
        ++st_.taps;
        if (t.slide) ++st_.slides;
        st_.score += kPointsPerTap;
        return;
    }
}

TapJudge::Stats TapJudge::total(double endMs) {
    st_.spansTotal = int(phrases_.size());
    st_.tapsNeeded = 0;
    st_.spansCleared = 0;
    for (size_t i = 0; i < phrases_.size(); ++i) {
        st_.tapsNeeded += phrases_[i].count;
        if (per_[i] >= phrases_[i].count) ++st_.spansCleared;
        if (endMs >= phrases_[i].time + phrases_[i].length + kGraceMs) open_ = i + 1;
    }
    return st_;
}

std::string touchStripSelfTest(const Globals* g) {
    TouchStrip strip;
    if (g) strip.load(*g);
    strip.setSwitchFrames(0);  // the disc value; tests the immediate-commit path
    const auto& zones = strip.zones();
    if (zones.size() < 5) return "the touch-zone table has only " + std::to_string(zones.size()) + " zones";

    // 1. The nearest-zone matcher: a reading at a zone's centre matches it, and
    //    a reading past +/-16 from every zone (a dead band) matches none.
    for (size_t i = 0; i < zones.size(); ++i)
        if (strip.nearestZone(zones[i].value) != int(i)) return "zone " + std::to_string(i) + " not matched at its own value";
    for (size_t i = 0; i + 1 < zones.size(); ++i) {
        const int mid = (zones[i].value + zones[i + 1].value) / 2;
        if (std::abs(mid - zones[i].value) > TouchStrip::kWindow && std::abs(mid - zones[i + 1].value) > TouchStrip::kWindow)
            if (strip.nearestZone(mid) >= 0) return "a reading in a dead band between zones matched a zone";
    }

    // 2. A tap (finger down on a zone, then up) is one committed change, not a
    //    slide; holding it still raises no second change.
    strip.reset();
    int changes = 0, slides = 0;
    const int z0 = zones[0].value;
    TouchStrip::Touch tt = strip.frame(z0, true);
    changes += tt.changed, slides += tt.slide;
    for (int f = 0; f < 5; ++f) {  // held: no further change
        tt = strip.frame(z0, true);
        changes += tt.changed, slides += tt.slide;
    }
    tt = strip.frame(0, false);  // lift
    if (changes != 1 || slides != 0) return "a tap gave " + std::to_string(changes) + " changes, " + std::to_string(slides) + " slides (want 1, 0)";

    // 3. A slide: down on zone 0, move (finger down) to zone 2 -> two changes,
    //    the second a slide.
    strip.reset();
    changes = slides = 0;
    tt = strip.frame(zones[0].value, true);
    changes += tt.changed, slides += tt.slide;
    tt = strip.frame(zones[2].value, true);
    changes += tt.changed, slides += tt.slide;
    if (changes != 2 || slides != 1) return "a slide gave " + std::to_string(changes) + " changes, " + std::to_string(slides) + " slides (want 2, 1)";

    // 4. The tap judge: a three-tap span at 1000..3000 ms. Three taps in the
    //    window score 150; a tap before the window does not.
    {
        std::vector<TapPhrase> ph = {{1000, 2000, 3}};
        TapJudge judge(ph);
        TouchStrip s2;
        s2.setZones(strip.zones());
        s2.setSwitchFrames(0);
        const double frameMs = 1000.0 / 60.0;
        const double tapAt[] = {500.0, 1000.0, 1600.0, 2200.0};  // 500 is before the span
        size_t next = 0;
        int zoneToggle = 0;
        for (double now = 0; now < 3200.0; now += frameMs) {
            bool down = false;
            int slider = 0;
            // A single-frame press at each tapAt time, each on a fresh zone so
            // the judge sees a committed change (a tap).
            if (next < 4 && now >= tapAt[next]) {
                down = true;
                slider = s2.zones()[size_t(zoneToggle % int(s2.zones().size()))].value;
                ++zoneToggle;
                ++next;
            }
            TouchStrip::Touch r = s2.frame(slider, down);
            judge.frame(now, r);
        }
        TapJudge::Stats st = judge.total(3200.0);
        if (st.taps != 3) return "tap judge: " + std::to_string(st.taps) + " taps landed (want 3; the pre-span tap must not count)";
        if (st.score != 3 * TapJudge::kPointsPerTap) return "tap judge: score " + std::to_string(st.score) + " (want 150)";
        if (st.spansCleared != 1) return "tap judge: " + std::to_string(st.spansCleared) + " spans cleared (want 1)";
        if (st.tapsNeeded != 3) return "tap judge: tapsNeeded " + std::to_string(st.tapsNeeded) + " (want 3)";
    }
    return "";
}
