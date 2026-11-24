// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/chart.h"

class Globals;

// Guitar Hero: World Tour guitar TOUCH STRIP (the touch-sensitive slider above
// the five frets): the analog slider read into discrete tap zones, with slide
// detection and a per-frame switch debounce, and a judge for the song's
// _Tapping spans. This models the original's input path [C 0x8213e4b8,
// 0x8213e830, 0x82144c80; qb leftx_unscaled a64e101c, guitar_touch_patterns
// e52dcb67 ({touch_value 80eb06d4, touch_pattern 30c8fcf2, pattern_string
// a8e2b1e6}), touch_pattern_switch_frames 780da03d, enable_touch_strip
// 09383b1b]. Rendering/SDL-free and ENTIRELY SEPARATE from the strummed /
// HOPO guitar judge (game/player.cpp): it adds a tap path, it does not change
// normal guitar play. Fed one 60 Hz frame at a time, like Player.

// One entry of guitar_touch_patterns: a finger position on the strip.
struct TouchZone {
    int32_t value = 0;    // touch_value: the slider reading at the zone's centre (-128..127)
    int32_t pattern = 0;  // touch_pattern: the finger-pattern bits the zone raises
};

// The slider -> zone matcher with the slide / debounce state (Function_8213e4b8
// + Function_8213e830). One per playing guitar.
class TouchStrip {
public:
    // Loads guitar_touch_patterns and touch_pattern_switch_frames from the
    // globals. Without them it falls back to the nine zones found on the World
    // Tour disc so the model is always usable; returns whether the real table
    // was read. Also reads enable_touch_strip (default on when the key, which
    // the game sets from a script, is absent from the data).
    bool load(const Globals& g);
    // The nine-zone World Tour table [C data guitar_touch_patterns], used when
    // no globals are given (tests) and as the fallback in load().
    static std::vector<TouchZone> defaultZones();

    void setZones(std::vector<TouchZone> z) { zones_ = std::move(z); }
    void setSwitchFrames(int n) { switchFrames_ = n < 0 ? 0 : n; }
    void setEnabled(bool e) { enabled_ = e; }
    const std::vector<TouchZone>& zones() const { return zones_; }
    int switchFrames() const { return switchFrames_; }
    bool enabled() const { return enabled_; }

    // The nearest zone to a raw slider reading within +/-16 units, or -1
    // (between zones / off the strip) [C 0x8213e4b8: nearest within +/-16].
    int nearestZone(int slider) const;

    // The result of one frame.
    struct Touch {
        bool touched = false;  // a zone is held this frame
        int zone = -1;         // the committed zone index, or -1
        int32_t pattern = 0;   // its touch_pattern, or 0
        bool changed = false;  // the committed zone changed this frame: a tap (finger down) or a slide step
        bool slide = false;    // the change came from a slide (the finger stayed down and moved zones)
    };
    // One 60 Hz frame. `slider` = leftx_unscaled (-128..127); `down` = the
    // touch flag (record+0x18 + 0x4a). A new zone is committed after it has
    // been the nearest zone for switchFrames_ frames (0 = at once) [C
    // 0x8213e830 touch_pattern_switch_frames].
    Touch frame(int slider, bool down);
    void reset();

    static constexpr int kWindow = 16;  // +/-16 units [C 0x8213e4b8]

private:
    std::vector<TouchZone> zones_ = defaultZones();
    int switchFrames_ = 0;   // touch_pattern_switch_frames (0 on the disc)
    bool enabled_ = true;    // enable_touch_strip
    int committed_ = -1;     // the zone held now
    int pending_ = -1;       // a candidate zone building up its debounce
    int pendingFrames_ = 0;
    bool wasDown_ = false;
};

// Judges tap input against one part/difficulty's _Tapping spans. A span is
// cleared by `count` taps inside its [time, time+length] window; each tap
// scores like a note (points_per_tap). Kept apart from Player so the strummed
// score is never affected. Times in ms, fed per frame with the song time.
class TapJudge {
public:
    explicit TapJudge(std::vector<TapPhrase> phrases) : phrases_(std::move(phrases)) { per_.assign(phrases_.size(), 0); }

    // One frame at song time `nowMs` with the touch-strip output. A committed
    // change (a tap, or a slide step) inside an open span that still needs taps
    // is a hit.
    void frame(double nowMs, const TouchStrip::Touch& t);

    struct Stats {
        int taps = 0;        // taps that landed in a span
        int slides = 0;      // of those, from a slide
        int tapsNeeded = 0;  // sum of the spans' counts
        int spansCleared = 0;
        int spansTotal = 0;
        int64_t score = 0;
    };
    // Finalises the spans the song time has passed and returns the totals.
    Stats total(double endMs);

    static constexpr int kPointsPerTap = 50;  // a note's base, mirrored for a tap [I: 50 x fretCount, one finger]
    // The grace (ms) a span's window is widened by at each edge for a tap to
    // still count (a hit close to the edge).
    static constexpr double kGraceMs = 100.0;

private:
    std::vector<TapPhrase> phrases_;
    std::vector<int> per_;  // taps landed per span
    Stats st_;
    size_t open_ = 0;       // the first span not yet finalised
};

// A self-contained check of the slider->zone matcher, the slide/debounce and
// the tap judge against synthetic input. Returns "" on success or the first
// failure. Uses `g` for the real zone table when given, else the disc
// fallback. No disc or hardware needed.
std::string touchStripSelfTest(const Globals* g);
