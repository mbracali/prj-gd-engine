// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/chartest.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "char/character.h"
#include "char/midi_parser.h"
#include "data/game_data.h"
#include "data/midi.h"
#include "game/autoplay.h"
#include "game/band.h"
#include "game/camera_director.h"
#include "game/light_presets.h"
#include "game/player.h"
#include "game/rules.h"
#include "game/song_db.h"
#include "game/song_setup.h"
#include "game/venue.h"
#include "game/world.h"
#include "game/world_driver.h"
#include "tests/miditest.h"

namespace {

// The band's golden run: hangar18 in arena (the director golden's song),
// the band's main clips per (beat, slot) and the drum kit's triggers.
// A change in the drivers, the band scripts, the parsers or the director
// moves them; check the log (GD02_BAND_LOG=<file>) and update both.
constexpr const char* kSong = "hangar18";
constexpr const char* kVenue = "arena";
constexpr int kGoldenLines = 596;
constexpr uint32_t kGoldenHash = 0xaff9a814u;

uint32_t fnv(const std::string& s, uint32_t h) {
    for (unsigned char c : s) h = (h ^ c) * 16777619u;
    return h;
}

bool finite(const scene::Xfm& x) {
    for (float v : x.rot)
        if (!std::isfinite(v)) return false;
    for (float v : x.pos)
        if (!std::isfinite(v)) return false;
    return true;
}

// Every character milo, every guitar on alterna1, every kit on the drummer:
// they load and every skinned mesh's bones resolve.
int loadChecks(const GameData& data) {
    int failures = 0;
    chr::ClipLibrary clips(data);
    std::vector<std::string> chars;
    for (const std::string& p : data.paths()) {
        if (p.rfind("char/", 0) != 0 || p.find("/ng/gen/") == std::string::npos) continue;
        if (p.find("_ui") != std::string::npos || p.rfind("char/ng/", 0) == 0 || p.rfind("char/shared/", 0) == 0) continue;
        if (p.find("lighter_flame") != std::string::npos) continue;  // the crowd's lighter (a Character v9 effect, not a character)
        if (p.size() < 10 || p.compare(p.size() - 10, 10, ".milo_xbox") != 0) continue;
        chars.push_back(p);
    }
    int loaded = 0, bones = 0, drivers = 0;
    for (const std::string& p : chars) {
        try {
            chr::Character c(data, p, clips);
            ++loaded;
            for (const std::string& b : c.unresolvedBones()) {
                if (bones < 10) std::printf("chartest: %s: bone %s does not resolve\n", p.c_str(), b.c_str());
                ++bones;
            }
            for (const auto& d : c.drivers())
                if (!d->clipSet()) {
                    std::printf("chartest: %s: %s has no clip set\n", p.c_str(), d->name().c_str());
                    ++failures;
                }
            drivers += int(c.drivers().size());
        } catch (const std::exception& e) {
            std::printf("chartest: %s: %s\n", p.c_str(), e.what());
            ++failures;
        }
    }
    std::printf("chartest: characters: %d of %zu load, %d drivers, %d unresolved bones, %zu clip sets\n", loaded, chars.size(), drivers,
                bones, clips.sets().size());
    failures += bones;
    // Guitars and kits.
    int guitars = 0, kits = 0, outfitFail = 0;
    for (const std::string& p : data.paths()) {
        const bool guitar = p.rfind("char/ng/guitars/gen/", 0) == 0, kit = p.rfind("char/ng/drums/gen/dw_", 0) == 0 && p.find("_drums.") != std::string::npos;
        if (!guitar && !kit) continue;
        try {
            chr::Character c(data, guitar ? "char/alterna1/ng/gen/alterna1.milo_xbox" : "char/metal_drummer/ng/gen/metal_drummer.milo_xbox", clips,
                             {{guitar ? "guitar.outfit" : "drums.outfit", p}});
            if (c.outfitObjects() == 0 || !c.unresolvedBones().empty()) {
                std::printf("chartest: outfit %s: %d objects, %zu unresolved bones\n", p.c_str(), c.outfitObjects(), c.unresolvedBones().size());
                ++outfitFail;
            }
            (guitar ? guitars : kits)++;
        } catch (const std::exception& e) {
            std::printf("chartest: outfit %s: %s\n", p.c_str(), e.what());
            ++outfitFail;
        }
    }
    std::printf("chartest: outfits: %d guitars, %d kits merged, %d failed\n", guitars, kits, outfitFail);
    return failures + outfitFail;
}

// A scripted driver sequence (M9.2): alterna1's main.drv plays its idle
// group (kPlayLast | kPlayGraphLoop) for 32 beats at 120 beats a minute,
// then normal (kPlayNow) for 16; the (beat, clip, frame, weight, delay) log of every
// node each beat must match its golden line count and hash.
constexpr int kDriverLines = 82;
constexpr uint32_t kDriverHash = 0x0989cfaeu;
int driverRun(const GameData& data) {
    chr::ClipLibrary clips(data);
    chr::Character c(data, "char/alterna1/ng/gen/alterna1.milo_xbox", clips);
    chr::ClipDriver* d = c.mainDriver();
    if (!d || !d->clipSet()) {
        std::printf("chartest: driver: no main.drv clip set\n");
        return 1;
    }
    chr::Random rng(7);
    d->rng = &rng;
    std::vector<std::string> log;
    d->playGroup("idle", chr::kPlayLast | chr::kPlayGraphLoop);
    const float dt = 1.0f / 60.0f;
    double beat = 0;
    int lastWhole = -1;
    for (int f = 0; f < 48 * 30; ++f) {
        if (f == 32 * 30) d->playGroup("normal", chr::kPlayNow);
        chr::RigContext ctx;
        beat += dt * 2.0;
        ctx.beat = beat, ctx.dBeats = dt * 2.0f, ctx.dSeconds = dt, ctx.seconds = beat / 2;
        c.poll(ctx);
        if (int(beat) != lastWhole) {
            lastWhole = int(beat);
            for (const auto& n : d->chain()) {
                char b[160];
                std::snprintf(b, sizeof b, "%3d %s %.3f %.3f %.3f", lastWhole, n.clip->name.c_str(), double(n.frame), double(n.weight), double(n.delay));
                log.push_back(b);
            }
        }
    }
    uint32_t h = 2166136261u;
    for (const std::string& l : log) h = fnv(l + "\n", h);
    if (const char* out = SDL_getenv("GD02_DRIVER_LOG"))
        if (FILE* f = std::fopen(out, "w")) {
            for (const std::string& l : log) std::fprintf(f, "%s\n", l.c_str());
            std::fclose(f);
        }
    const bool ok = int(log.size()) == kDriverLines && h == kDriverHash;
    std::printf("chartest: driver golden: alterna1 idle / normal, log %d lines, hash %08x (expected %d, %08x)%s\n", int(log.size()), unsigned(h),
                kDriverLines, unsigned(kDriverHash), ok ? "" : " MISMATCH");
    return ok ? 0 : 1;
}

// The song through the world, director, lights and the band, autoplayed.
int bandRun(const GameData& data) {
    const std::vector<SongInfo> songs = loadSongs(data);
    const SongInfo* info = nullptr;
    for (const SongInfo& s : songs)
        if (s.id == kSong) info = &s;
    if (!info) {
        std::printf("chartest: band: no song %s\n", kSong);
        return 1;
    }
    const Rules rules = loadRules(data);
    const Song song = loadChart(data, *info, rules);
    const MidiFile midi = parseMidi(data.read(info->audio.midiPath));
    Venue venue(data, kVenue, VenueVariant::Single);
    World world(venue);
    world.interp().seed(0x60d1e5u);
    LightPresets lights(world, 0x4c504d31u);
    CameraDirector director(world, 0x43414d31u);
    director.provideGameObjects(false, "single");
    Band band(world, data, *info, Band::Config{});
    int failures = 0;
    for (const std::string& p : band.problems()) {
        std::printf("chartest: band: %s\n", p.c_str());
        ++failures;
    }
    std::vector<std::string> log;
    std::map<std::string, int> perSlot;
    band.onClip = [&](double beat, const std::string& slot, const std::string& clip) {
        char b[32];
        std::snprintf(b, sizeof b, "%8.2f ", beat);
        log.push_back(b + slot + " " + clip);
        ++perSlot[slot];
    };
    int shots = 0, bandShots = 0;
    director.onShot = [&](const std::string& s) {
        ++shots;
        if (s.find("guitarist") != std::string::npos || s.find("SOLO") != std::string::npos) ++bandShots;
    };
    std::map<std::string, int> triggers;
    Player p(song, Part::Guitar, Difficulty::Expert, rules, ControllerMode::Guitar, false);
    band.rating = [&](int) { return p.rockMeter(); };
    Autoplay bot(p.chart());
    WorldDriver driver(true);
    const float lead = -3000;
    driver.reset(lead);
    world.setClock(lead / 1000.0, song.tempo.msToTick(lead) / 480.0);
    band.loadSong(midi, &song);
    world.start(false);
    band.enter();
    int nan = 0, spFrames = 0;
    bool spWas = false;
    std::set<std::string> handClips, spots;
    float maxDrift = 0;
    chr::Character* g0 = band.character("guitarist0");
    const scene::Xfm start = g0 ? g0->xfm() : scene::Xfm{};
    for (float t = lead; t < song.endMs + 6000; t += 1000.0f / 60.0f) {
        bot.feed(p, t);
        p.update(t);
        std::vector<PlayerEvent> events = p.takeEvents();
        // Star Power as soon as it is ready (the bot does not tilt).
        for (const PlayerEvent& e : events)
            if (e.kind == PlayerEvent::Kind::StarPowerReady) p.starPowerButton(t);
        if (p.starPowerActive() != spWas) {
            spWas = p.starPowerActive();
            for (const PlayerEvent& e : p.takeEvents()) events.push_back(e);
        }
        driver.update(world, song, t, &p, events, nullptr, {});
        if (t >= song.endMs) driver.gameWon(world);
        world.update();
        lights.update();
        director.setStarPower(p.starPowerActive());
        director.update();
        band.update(world.seconds(), world.beats());
        for (const auto& m : band.members()) {
            chr::Character& c = *m->character;
            if (!finite(c.xfm())) ++nan;
            for (int i = 0; i < c.skeleton().size(); i += 7)
                if (!finite(c.skeleton().world(i))) ++nan;
            if (m->slot == "guitarist0") {
                if (auto* d = c.driver("left_hand.drv"); d && d->newest()) handClips.insert(d->newest()->name);
                const auto a = c.xfm().pos;
                maxDrift = std::max(maxDrift, std::hypot(a[0] - start.pos[0], a[1] - start.pos[1]));
            }
        }
        if (band.playingStarpower("guitarist0")) ++spFrames;
    }
    uint32_t h = 2166136261u;
    for (const std::string& l : log) h = fnv(l + "\n", h);
    if (const char* out = SDL_getenv("GD02_BAND_LOG"))
        if (FILE* f = std::fopen(out, "w")) {
            for (const std::string& l : log) std::fprintf(f, "%s\n", l.c_str());
            std::fclose(f);
        }
    std::string slots;
    for (const auto& [s, n] : perSlot) slots += " " + s + " " + std::to_string(n);
    std::string trig;
    for (const auto& [e, n] : band.triggerCounts()) trig += " " + e + " " + std::to_string(n);
    std::printf("chartest: band triggers:%s\n", trig.c_str());
    std::printf("chartest: band %s in %s: %zu members, clips:%s; %zu hand clips, guitarist0 moved %.1f, %d SP frames, %d shots\n", kSong,
                kVenue, band.members().size(), slots.c_str(), handClips.size(), double(maxDrift), spFrames, shots);
    if (nan) {
        std::printf("chartest: band: %d non-finite transforms\n", nan);
        ++failures;
    }
    if (band.members().size() != 4) {
        std::printf("chartest: band: expected guitarist0, singer, bassist, drummer\n");
        ++failures;
    }
    const bool golden = int(log.size()) == kGoldenLines && h == kGoldenHash;
    std::printf("chartest: band golden: log %d lines, hash %08x (expected %d, %08x)%s\n", int(log.size()), unsigned(h), kGoldenLines,
                unsigned(kGoldenHash), golden ? "" : " MISMATCH");
    if (!golden) ++failures;
    return failures;
}

}  // namespace

int chartestBandChecks(const GameData& data) {
    int failures = loadChecks(data);
    failures += driverRun(data);
    failures += runMidiParserTest(data);
    failures += bandRun(data);
    return failures;
}
