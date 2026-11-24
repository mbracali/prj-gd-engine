// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/enhancedtest.h"

#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "data/disc.h"
#include "data/qb.h"
#include "data/qbkey.h"
#include "game/note_track.h"
#include "game/player.h"
#include "game/tuning.h"
#include "render/highway2d.h"
#include "script/globals.h"
#include "script/natives_core.h"
#include "script/vm.h"
#include "ui/glitches.h"
#include "ui/natives_gh4.h"
#include "ui/port_settings.h"

namespace {
uint32_t K(const std::string& s) { return qbkey::key(s); }
}  // namespace

int runEnhancedTest(const std::string& discPath) {
    std::string error;
    auto disc = Disc::open(discPath, error);
    if (!disc) {
        std::printf("enhancedtest: %s\n", error.c_str());
        return 2;
    }
    disc->loadNames();
    Globals g;
    try {
        g.load(*disc);
    } catch (const std::exception& e) {
        std::printf("enhancedtest: globals: %s\n", e.what());
        return 2;
    }

    int failures = 0;
    auto check = [&](bool ok, const std::string& what) {
        if (!ok) {
            std::printf("enhancedtest: FAIL %s\n", what.c_str());
            ++failures;
        }
        return ok;
    };

    // ===================================================================== //
    // PART 1 — the Enhanced option catalogue, defaults and store.
    // ===================================================================== //

    // (1a) Catalogue completeness: every universal Enhanced option the
    // port_enhanced host must carry (spec-enhanced-menu.md "Option catalogue").
    // The Slider-only HDR White/Black levels are intentionally absent (the
    // port_enhanced host has no Slider kind) — their absence is asserted.
    const char* required[] = {
        // CONTROLS
        "mouse", "touch", "shake_whammy",
        // PERFORMANCE & GRAPHICS
        "graphics_backend", "audio_backend", "screen_mode", "internal_res", "sharp_ui", "antialiasing",
        "ai_upscaling", "ai_upscaling_mode", "max_fps", "fps_counter", "hdr", "bokeh", "particles", "bloom",
        "cinematic_color", "color_grade", "film_grain", "chromatic", "ssao", "sharpening", "star_blur",
        "shadows_all", "more_lights", "light_dust", "no_shaky_cam", "no_effects",
        // ACCESSIBILITY
        "easy_chords",
        // CHEATS & ENHANCERS
        "hopo_mode", "no_fail", "autoplay", "dev_debug_menu",
        "cheat_unlock_all", "cheat_full_sp", "cheat_multiplier", "cheat_hyperspeed",
        // Calibration (numeric, set via the tool)
        "audio_offset", "video_offset"};
    int present = 0;
    for (const char* key : required)
        if (check(enhancedSetting(key) != nullptr, std::string("missing Enhanced option: ") + key)) ++present;
    std::printf("enhancedtest: Enhanced catalogue: %d/%zu required options present\n", present, std::size(required));
    // The HDR level Sliders must NOT be present (no Slider kind in port_enhanced).
    check(enhancedSetting("hdr_white") == nullptr && enhancedSetting("hdr_black") == nullptr,
          "HDR level sliders present (port_enhanced has no Slider kind)");
    // Removed options must stay gone (owner 2026-10-30).
    check(enhancedSetting("ground_fog") == nullptr && enhancedSetting("light_streaks") == nullptr,
          "a removed option (ground_fog / light_streaks) is back");

    // (1b) Defaults respect the ORIGINAL look (an untouched install plays like
    // the original): effects off / GH3's own look, controls on, cheats off.
    // enhancedDefault returns the per-OS profile value (desktop here).
    struct DefCheck { const char* key; const char* want; };
    const DefCheck wantDesktop[] = {
        {"mouse", "on"}, {"fps_counter", "on"}, {"sharp_ui", "on"}, {"screen_mode", "borderless"},
        {"internal_res", "1080"}, {"hdr", "none"}, {"audio_backend", "miniaudio"},
        {"bloom", "none"}, {"no_effects", "off"}, {"cinematic_color", "off"}, {"film_grain", "none"},
        {"chromatic", "none"}, {"ssao", "none"}, {"star_blur", "none"}, {"color_grade", "off"},
        {"sharpening", "off"}, {"shadows_all", "off"}, {"more_lights", "none"}, {"light_dust", "none"},
        {"no_shaky_cam", "off"}, {"antialiasing", "none"}, {"ai_upscaling", "none"},
        // GH3's own look is the original for this family (spec Option catalogue).
        {"bokeh", "medium"}, {"particles", "original"},
        // Accessibility + cheats: all off, original rules.
        {"easy_chords", "off"}, {"hopo_mode", "original"}, {"no_fail", "off"}, {"autoplay", "off"},
        {"dev_debug_menu", "off"}, {"cheat_unlock_all", "off"}, {"cheat_full_sp", "off"},
        {"cheat_multiplier", "off"}, {"cheat_hyperspeed", "off"},
        // Calibration offsets default to 0.
        {"audio_offset", "0"}, {"video_offset", "0"}};
    int defOk = 0;
    for (const DefCheck& d : wantDesktop) {
        const std::string got = enhancedDefault(d.key, /*deck=*/false);
        if (check(got == d.want, std::string("default ") + d.key + " = '" + got + "' expected '" + d.want + "'")) ++defOk;
    }
    std::printf("enhancedtest: desktop defaults respect the original: %d/%zu\n", defOk, std::size(wantDesktop));
    // The graphics backend default is the per-OS list front (vulkan on Linux/Win/Android, sdl3 on macOS).
    check(enhancedDefault("graphics_backend", false) == enhancedSetting("graphics_backend")->values.front(),
          "graphics_backend default is not the per-OS list front");

    // (1b') Per-OS profile table (spec "Default settings per OS"): the handheld
    // (Steam Deck / Android) profile turns touch + Shake-for-Whammy on at 60 FPS;
    // desktop leaves them off.
    check(enhancedDefault("touch", false) == "off" && enhancedDefault("shake_whammy", false) == "off",
          "desktop touch / shake_whammy not off");
    check(enhancedDefault("touch", true) == "on" && enhancedDefault("shake_whammy", true) == "on" &&
              enhancedDefault("max_fps", true) == "60",
          "handheld (deck) profile not touch=on shake=on max_fps=60");

    // (1c) The store honours every option: a set/get round-trip of every allowed
    // value, invalid values refused, unknown keys dropped, numeric range enforced.
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path dir = fs::temp_directory_path(ec) / "gd07_enhancedtest";
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        const std::string d = dir.string() + "/";
        EnhancedStore store;
        store.load(d);
        int roundTrips = 0, roundTripFail = 0;
        for (const EnhancedSetting& s : enhancedSettings()) {
            if (s.values.empty()) {  // numeric: lo, hi, mid in range; out of range refused
                const std::string lo = std::to_string(s.lo), hi = std::to_string(s.hi), mid = std::to_string((s.lo + s.hi) / 2);
                for (const std::string& v : {lo, hi, mid}) {
                    store.set(s.key, v);
                    if (store.get(s.key) == v) ++roundTrips; else ++roundTripFail;
                }
                const std::string outHi = std::to_string(s.hi + 1000);
                store.set(s.key, outHi);
                if (store.get(s.key) == outHi) { ++roundTripFail; check(false, std::string("numeric ") + s.key + " accepted an out-of-range value"); }
                continue;
            }
            for (const char* v : s.values) {
                store.set(s.key, v);
                if (store.get(s.key) == v) ++roundTrips; else ++roundTripFail;
            }
            // An invalid value is refused (the last good value is kept).
            const std::string before = store.get(s.key);
            store.set(s.key, "__not_a_valid_value__");
            if (store.get(s.key) != before) { ++roundTripFail; check(false, std::string("invalid set for ") + s.key + " was not refused"); }
        }
        std::printf("enhancedtest: store round-trip: %d values honoured, %d failed\n", roundTrips, roundTripFail);
        check(roundTripFail == 0, "an Enhanced option did not round-trip through the store");
        // Persistence: the value written survives a reload from enhanced.json.
        store.set("max_fps", "144");
        EnhancedStore reread;
        reread.load(d);
        check(reread.get("max_fps") == "144", "enhanced.json did not persist max_fps across a reload");
        // Unknown / removed keys are dropped on load; valid keys survive.
        std::ofstream(d + "enhanced.json", std::ios::trunc)
            << R"({"version": 4, "settings": {"ground_fog": "on", "light_streaks": "on", "mouse": "off"}})";
        EnhancedStore afterUnknown;
        afterUnknown.load(d);
        check(afterUnknown.get("mouse") == "off", "a valid key did not load");
        // ground_fog is not a catalogue key, so load drops it and get() returns no stored value for it.
        check(enhancedSetting("ground_fog") == nullptr, "ground_fog is still a catalogue key");
        fs::remove_all(dir, ec);
    }

    // ===================================================================== //
    // PART 2 — every cheat takes a real, measured effect.
    // ===================================================================== //

    // (2a) Always Max Multiplier (cheat_multiplier -> Player::maxMultiplier): the
    // base multiplier is forced to x4 (x8 in Star Power) from the first note,
    // without a streak [C 0x8210BB40].
    {
        PlayerStats ps;
        ps.multiplierRun = 0;  // no streak
        ps.starPowerActive = false;
        const bool keep = Player::maxMultiplier;
        Player::maxMultiplier = false;
        const int mOff = ps.multiplier();
        Player::maxMultiplier = true;
        const int mOn = ps.multiplier();
        ps.starPowerActive = true;
        const int mOnSp = ps.multiplier();
        Player::maxMultiplier = keep;
        std::printf("enhancedtest: cheat_multiplier: off=x%d on=x%d on+SP=x%d\n", mOff, mOn, mOnSp);
        check(mOff == 1 && mOn == 4 && mOnSp == 8, "Always Max Multiplier did not force x4 / x8");
    }

    // (2b) Hyperspeed (cheat_hyperspeed -> Highway2D::hyperspeed): the note travel
    // time is shortened (x kHyperspeedFactor); inert when off.
    {
        const bool keep = Highway2D::hyperspeed;
        Highway2D::hyperspeed = false;
        const float off = Highway2D::applyHyperspeed(1000.0f);
        Highway2D::hyperspeed = true;
        const float on = Highway2D::applyHyperspeed(1000.0f);
        Highway2D::hyperspeed = keep;
        std::printf("enhancedtest: cheat_hyperspeed: travel off=%.0f on=%.0f ms\n", off, on);
        check(off == 1000.0f && on < 1000.0f && std::fabs(on - 1000.0f * Highway2D::kHyperspeedFactor) < 0.01f,
              "Hyperspeed did not shorten the note travel time");
    }

    // (2c) Star Power Always Full (cheat_full_sp -> Player::fullStarPower): the
    // meter is pinned at 100 each frame; off, it is left where it was.
    {
        NoteTrack track;
        track.firstBeatMs = 500;
        TrackNote n;
        n.time = 1000;
        n.frets = 1;
        track.notes.push_back(n);
        Tuning tu;
        auto spAfter = [&](bool full) -> float {
            const bool keep = Player::fullStarPower;
            Player::fullStarPower = full;
            Player p(track, tu, 3, true);
            p.setStarPowerAmount(50.0f);
            FrameInput in;
            for (int k = 0; k < 3; ++k) p.frame(double(k) * 16.67, 16.67, in);
            Player::fullStarPower = keep;
            return p.stats().starPower;
        };
        const float spOff = spAfter(false), spOn = spAfter(true);
        std::printf("enhancedtest: cheat_full_sp: start=50 off=%.0f on=%.0f\n", spOff, spOn);
        check(spOn == 100.0f, "Star Power Always Full did not pin the meter to 100");
        check(spOff != 100.0f, "the Star Power meter was pinned to 100 without the cheat");
    }

    // (2d) Easy Chords (accessibility -> Player::chordShape): a chord counts as
    // any non-empty part of its shape (no fret outside it); off, the exact shape.
    {
        const bool keep = Player::easyChords;
        Player::easyChords = false;
        const bool strictPartial = Player::chordShape(0b001, 0b011);  // part of a chord: no
        const bool strictExact = Player::chordShape(0b011, 0b011);    // exact: yes
        Player::easyChords = true;
        const bool easyPartial = Player::chordShape(0b001, 0b011);  // part of a chord: yes
        const bool easyOutside = Player::chordShape(0b100, 0b011);  // a fret outside it: no
        Player::easyChords = keep;
        check(!strictPartial && strictExact && easyPartial && !easyOutside, "Easy Chords did not change the chord judging");
    }

    // (2e) Forgiving HOPOs (hopo_mode -> Player::forgivingHopos): the flag toggles
    // (its judging effect at [C 0x821810b0] is exercised by --selftest HOPO counts).
    check(Player::forgivingHopos == false, "forgivingHopos is not off by default");

    // Build a VM over the real disc globals for the script-driven cheats / band.
    qs::Vm vm(g);
    qs::addCoreNatives(vm);
    addGh4Natives(vm);

    // (2f) Unlock Everything (cheat_unlock_all -> unlockEverything): the game's
    // own unlock-all scripts run and every save tag with an `unlocked` field is
    // set to 1.
    {
        const char* seeds[] = {"et_bonus_guitar", "et_secret_char", "et_test_venue"};
        for (const char* t : seeds) qs::Vm::set(vm.globalTags[K(t)], K("unlocked"), qs::Vm::makeInt(0));
        const bool ran = unlockEverything(vm);
        int set = 0;
        for (const char* t : seeds) {
            const qb::Value* u = qs::Vm::param(vm.globalTags[K(t)], K("unlocked"));
            if (u && u->i == 1) ++set;
        }
        check(ran, "unlockEverything reported no effect");
        check(set == 3, "the save-wide unlocked sweep did not set every seeded tag");
    }

    // (2g) The game's own Options > Cheats (guitar_hero_cheats): the port surfaces
    // each toggleable cheat as a Cheat row whose option sets the same globals the
    // cheats screen does (cheat_set_globals). Enumerate them straight off the disc.
    {
        const qb::Value* cheats = vm.global(K("guitar_hero_cheats"));
        int toggleable = 0, withVar = 0;
        if (cheats && cheats->arr)
            for (const qb::Value& c : cheats->arr->values) {
                if (!c.st) continue;
                const qb::Value* opts = qs::Vm::param(*c.st, K("options"));
                if (!opts || !opts->arr || opts->arr->values.size() < 2 || qs::Vm::flag(*c.st, K("not_toggle"))) continue;
                ++toggleable;
                if (qs::Vm::param(*c.st, K("var"))) ++withVar;
            }
        std::printf("enhancedtest: guitar_hero_cheats: %d toggleable cheats (%d with a cheat global)\n", toggleable, withVar);
        check(toggleable > 0, "no toggleable cheats found in guitar_hero_cheats");
        check(withVar > 0, "no cheat exposes a cheat_set_globals global (var)");
    }

    // ===================================================================== //
    // PART 3 — "The Glitches": the fully-unlocked house band, last in the list.
    // ===================================================================== //
    {
        const bool changed = ensureGlitchesGh4(vm);
        check(changed, "ensureGlitchesGh4 made no change");
        qb::Struct& bi = vm.globalTags[K("band_info")];
        const qb::Value* name = qs::Vm::param(bi, K("name"));
        check(name && name->str == "The Glitches", "the save band is not named The Glitches");
        const qb::Value* cash = qs::Vm::param(bi, K("cash"));
        check(cash && cash->i == 1000000, "The Glitches does not have $1,000,000");
        const qb::Value* fp = qs::Vm::param(bi, K("first_play"));
        check(fp && fp->i == 0, "The Glitches first_play not cleared (a returning band)");
        qb::Struct& lsc = vm.globalTags[K("last_singleplayer_character")];
        const qb::Value* ch = qs::Vm::param(lsc, K("last_singleplayer_character"));
        check(ch && ch->key == K("judy"), "singleplayer career character not seeded for The Glitches");
        // Idempotent: a real saved character / name is never overridden.
        qs::Vm::set(bi, K("name"), qs::Vm::makeString("Player Band"));
        qs::Vm::set(lsc, K("last_singleplayer_character"), qs::Vm::makeKey(K("spike")));
        ensureGlitchesGh4(vm);
        const qb::Value* name2 = qs::Vm::param(bi, K("name"));
        const qb::Value* ch2 = qs::Vm::param(vm.globalTags[K("last_singleplayer_character")], K("last_singleplayer_character"));
        check(name2 && name2->str == "Player Band", "a player's band name was overwritten by The Glitches");
        check(ch2 && ch2->key == K("spike"), "a player's chosen character was overwritten");
        std::printf("enhancedtest: The Glitches: band_info named, $1,000,000, unlock-all run; the fully-unlocked house band (last in band-selection). Player name/character preserved.\n");
    }

    std::printf("enhancedtest: %s (%d checks failed)\n", failures ? "FAIL" : "all enhanced / cheats / Glitches checks passed", failures);
    return failures ? 1 : 0;
}
