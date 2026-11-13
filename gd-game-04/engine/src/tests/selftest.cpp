// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/selftest.h"
#include "input/input_selftest.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>

#include "audio/fast_pitch.h"
#include "audio/guitar_fx.h"
#include "data/disc.h"
#include "data/qbkey.h"
#include "game/chart.h"
#include "game/song_db.h"
#include "render/highway2d.h"
#include "script/globals.h"
#include "tests/selftest_play.h"
#include "ui/port_settings.h"
#include "ui/song_screen.h"

// The whammy's pitch shifter on a 440 Hz sine: untouched at rest, down by the
// script's most (pitch 0.943, 415 Hz) under the bar, the lag jumping back
// past its limit, and exact again after a release.
// SetNewWhammyValue (0x821056F8): at the default travel time (1.5 s) a hold
// with 750 ms left keeps 128 / 1.5 x 0.75 + 2 = 66 entries; each call puts
// the value in the first whammy_wibble_speed (2) after moving the older ones
// down by 2; past the kept entries 1.0 (GD04-032).
static void checkWibble(const std::function<void(const std::string&)>& fail) {
    Highway2D h;
    h.setNewWhammyValue(2.0f, 750, 2);
    h.setNewWhammyValue(1.5f, 750, 2);
    const auto& w = h.wibble();
    std::printf("wibble: %.2f %.2f %.2f %.2f %.2f, [65] %.2f [66] %.2f\n", double(w[0]), double(w[1]), double(w[2]), double(w[3]), double(w[4]),
                double(w[65]), double(w[66]));
    if (w[0] != 1.5f || w[1] != 1.5f || w[2] != 2.0f || w[3] != 2.0f || w[4] != 1.0f) fail("SetNewWhammyValue did not shift the wibble by 2");
    h.setNewWhammyValue(2.0f, 0, 2);  // 2 entries kept: the rest back to 1
    if (w[0] != 2.0f || w[1] != 2.0f || w[2] != 1.0f) fail("SetNewWhammyValue kept entries past the hold's end");
    h.setAllWhammyValues(1.0f);
    for (float v : w)
        if (v != 1.0f) return fail("SetAllWhammyValues left an entry");
}

// #70 (GD04-115): the song's input from the keyboard. With Require Strum
// off a key's fret press strums on its own; the Whammy key holds the bar
// at 1 while a pad's resting bar (its value set every frame) reads 0.
static void checkSongKeys(const std::function<void(const std::string&)>& fail) {
    SongScreen s;
    InputEvent fret{Action::Fret0, true, 0, ControllerMode::Joypad, 0};
    fret.fretsStrum = true;
    s.handle(fret);
    if (!s.strumPending(0)) fail("song keys: a fret with Require Strum off did not strum");
    s.setWhammy(0, 0.0f);
    s.handle(InputEvent{Action::Whammy, true, 0, ControllerMode::Joypad, 0});
    s.setWhammy(0, 0.0f);  // the next frame's pad poll, its bar at rest
    if (s.whammy(0) != 1.0f) fail("song keys: a resting pad bar cancelled the Whammy key");
    s.handle(InputEvent{Action::Whammy, false, 0, ControllerMode::Joypad, 0});
    s.setWhammy(0, 0.4f);
    if (s.whammy(0) != 0.4f) fail("song keys: the Whammy key let go did not hand back to the bar");
}

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

// GH_Star_Power_Verb_On's guitar buss (echo Wetmix 0.5 at a 375 ms beat,
// ReflectionsLevel -1200, ReverbLevel -550 mB, Star_Power_BussSet -2 dB) on
// white noise: the wet paths' energy (~+2.3 dB) less the buss's 2 dB, so
// within 1 dB of the dry level; and no change at the dry defaults (GD04-098:
// the reverb was ~8 dB too loud, the guitar +3.2 dB and clipping).
static void checkGuitarFx(const std::function<void(const std::string&)>& fail) {
    for (int sp = 0; sp < 2; ++sp) {
        GuitarFx fx;
        fx.setRate(48000);
        GuitarFx::Params p = fx.params();
        if (sp) p.echoDelayMs = 375, p.echoWet = 0.5f, p.reflectionsLevelMb = -1200, p.reverbLevelMb = -550, p.bussDb = -2;
        fx.set(p, 0);
        uint32_t rng = 1;
        double in = 0, out = 0;
        for (int i = 0; i < 48000 * 4; ++i) {
            rng = rng * 1664525u + 1013904223u;
            const float x = (float(rng >> 8) / float(1 << 24) - 0.5f) * 0.3f;
            float l = x, r = x;
            fx.process(l, r);
            if (i >= 48000) in += double(x) * x, out += double(l) * l;
        }
        const double db = 10 * std::log10(out / in);
        if (std::fabs(db) > (sp ? 1.0 : 0.01))
            fail("guitar buss " + std::string(sp ? "in Star Power" : "dry") + ": " + std::to_string(db) + " dB on noise");
    }
}

// GDALL-050: the graphics backend defaults to Vulkan on Windows / Linux (local
// elsewhere); an enhanced.json older than version 3 has its "local" (the old
// default) turned into it once; local saved by this version stays.
static void checkBackendDefault(const std::function<void(const std::string&)>& fail) {
#if !defined(__APPLE__) && !defined(__ANDROID__)
    const std::string want = "vulkan";
#else
    const std::string want = "local";
#endif
    if (enhancedDefault("graphics_backend") != want || enhancedDefault("graphics_backend", true) != want || enhancedDefault("audio_backend") != "local")
        fail("graphics backend default: " + enhancedDefault("graphics_backend") + ", want " + want);
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("gdall050-" + std::to_string(std::rand()));
    std::filesystem::create_directories(dir);
    const std::string d = dir.string() + "/";
    std::ofstream(d + "enhanced.json") << R"({"version": 2, "settings": {"graphics_backend": "local", "audio_backend": "local"}})";
    EnhancedStore st;
    st.load(d);
    if (st.get("graphics_backend") != want || st.get("audio_backend") != "local")
        fail("an old enhanced.json's local graphics backend: " + st.get("graphics_backend") + ", want " + want);
    st.set("graphics_backend", "local");  // the player picks local: saved as version 3
    EnhancedStore again;
    again.load(d);
    if (again.get("graphics_backend") != "local") fail("local picked in a new enhanced.json became " + again.get("graphics_backend"));
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
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
    checkGuitarFx(fail);
    checkWibble(fail);
    checkSongKeys(fail);
    checkBackendDefault(fail);

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
    if (totals.easyChordRuns == 0) fail("easy chords (GDALL-039): no chord to check");
    // The input layer's rules on virtual controllers (gd03's checks: two slots,
    // no double inputs, the axis rest rules, Require Strum, controllers.json v2).
    std::printf("input checks (virtual controllers):\n");
    if (const int inputFailures = runInputChecks()) {
        std::printf("%d input check(s) failed\n", inputFailures);
        failures += inputFailures;
    } else {
        std::printf("all input checks passed\n");
    }
    if (failures) {
        std::printf("selftest: %d FAILED\n", failures);
        return 1;
    }
    std::printf("selftest: %s\n", totals.rulesImplemented ? "all charts fully hit" : "data checks passed (no gameplay rules yet)");
    return 0;
}
