// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/selftest.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <functional>
#include <memory>
#include <string>

#include "audio/fast_pitch.h"
#include "data/disc.h"
#include "data/qbkey.h"
#include "game/chart.h"
#include "game/song_db.h"
#include "script/globals.h"
#include "tests/selftest_play.h"

// The whammy's pitch shifter on a 440 Hz sine: untouched at rest, down by the
// script's most (pitch 0.943, 415 Hz) under the bar, the lag jumping back
// past its limit, and exact again after a release.
static void checkFastPitch(const std::function<void(const std::string&)>& fail) {
    constexpr float kRate = 44100, kTau = 6.2831853f;
    auto sine = [&](int i) { return std::sin(kTau * 440.0f * float(i) / kRate); };
    auto fp = std::make_unique<FastPitch>();
    int at = 0;
    for (; at < 1000; ++at) {
        float l = sine(at), r = l;
        fp->process(l, r, 1);
        if (l != sine(at)) return fail("fast pitch: changed the sound at rest");
    }
    int rising = 0, first = -1, last = -1;
    float prev = 0, peak = 0;
    bool wrapped = false;
    for (int i = 0; i < 60000; ++i, ++at) {
        float l = sine(at), r = l;
        fp->process(l, r, 0.943f);
        if (i > 2000 && prev < 0 && l >= 0) {
            if (first < 0) first = i;
            last = i, ++rising;
        }
        prev = l;
        peak = std::max(peak, fp->lag());
    }
    const float hz = float(rising - 1) * kRate / float(last - first);
    std::printf("fast pitch: %.1f Hz at pitch 0.943, lag %.0f\n", hz, peak);
    if (std::fabs(hz - 414.9f) > 1.5f) fail("fast pitch: wrong pitch");
    for (int i = 0; i < 20000; ++i, ++at) {
        float l = sine(at), r = l;
        const float before = fp->lag();
        fp->process(l, r, 0.943f);
        if (fp->lag() < before) wrapped = true;
    }
    if (!wrapped || fp->lag() > FastPitch::kMaxLag) fail("fast pitch: the lag did not jump back");
    for (int i = 0; i < 200; ++i, ++at) {
        float l = sine(at), r = l;
        fp->process(l, r, 1);
        if (i >= FastPitch::kMaxFade && l != sine(at)) return fail("fast pitch: not back to the input");
    }
}

// --selftest <disc>: the data checks (globals, song list, every chart), then
// every chart auto-played at every difficulty and part (selftest_play.cpp).
// Prints "selftest: all charts fully hit" when everything passed.
int runSelfTest(const std::string& discPath) {
    std::string error;
    auto disc = Disc::open(discPath, error);
    if (!disc) {
        std::fprintf(stderr, "selftest: %s\n", error.c_str());
        return 2;
    }
    disc->loadNames();
    int failures = 0;
    auto fail = [&](const std::string& what) {
        if (++failures <= 40) std::printf("FAIL %s\n", what.c_str());
    };
    checkFastPitch(fail);

    Globals globals;
    try {
        globals.load(*disc);
    } catch (const std::exception& e) {
        std::printf("FAIL globals: %s\n", e.what());
        return 1;
    }
    std::printf("globals: %zu from %zu files\n", globals.size(), globals.files());
    SongDb db;
    if (!db.load(globals)) {
        std::printf("FAIL song list: gh3_songlist / gh3_songlist_props missing\n");
        return 1;
    }
    std::printf("songs: %zu\n", db.songs().size());

    PlayTotals totals;
    for (const SongInfo& s : db.songs()) {
        Chart c;
        try {
            c = loadChart(*disc, s.name);
        } catch (const std::exception& e) {
            fail(s.name + ": " + e.what());
            continue;
        }
        if (!disc->exists("DATA/MUSIC/" + s.name + ".fsb.xen")) fail(s.name + ": no DATA/MUSIC/" + s.name + ".fsb.xen");
        for (size_t p = 0; p < size_t(Part::Count); ++p) {
            for (int d = 0; d < kDifficulties; ++d) {
                const auto& notes = c.parts[p].notes[size_t(d)];
                for (size_t i = 1; i < notes.size(); ++i)
                    if (notes[i].time < notes[i - 1].time) {
                        fail(s.name + " " + partName(Part(p)) + " " + difficultyName(d) + ": notes out of order");
                        break;
                    }
                for (const ChartNote& n : notes)
                    if (n.frets == 0) {
                        fail(s.name + " " + partName(Part(p)) + " " + difficultyName(d) + ": a note without frets");
                        break;
                    }
            }
        }
        std::string why;
        if (!playChart(globals, s, c, totals, why)) fail(s.name + ": " + why);
    }
    printPlayTotals(totals);
    if (failures) {
        std::printf("selftest: %d FAILED\n", failures);
        return 1;
    }
    std::printf("selftest: %s\n", totals.rulesImplemented ? "all charts fully hit" : "data checks passed (no gameplay rules yet)");
    return 0;
}
