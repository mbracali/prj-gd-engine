// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/director_golden.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

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
#include "render/camera.h"

namespace {

// The song of the golden run: its quickplay venue is a big one with many
// presets and shots; the first song (career order) whose venue is arena.
constexpr const char* kVenue = "arena";
// The expected log: its line count and FNV-1a hash. A change in the
// director, the presets, the world's scripts or the chart's events moves
// them; check the log (GD03_GOLDEN_LOG=<file>) and update both.
// M9: the band on stage, cam_check_shot mask 0xc0 (kWalkSpot|kSoloWalkSpot)
// and guitarist0's character root (not a 192-unit radius over every waypoint).
// M9.6: guitarist0 walks (actually_walking -> walk_ok; the nearest walk spot
// changes) and the servo regulates the band to their waypoints.
// M9.9: the crowd's shuffles (iterate_frac) draw from the crowd's own
// RandomGen, no longer from the interpreter's the director's scripts use
// (with the crowd drawing from the interpreter again the old 329 / fbc0a753
// comes back).
// 2026-10-28: the camera shake draws from its own stream (so the frame rate
// no longer changes the picks); was 331 / 716ffdac.
// gd03 (this disc: 18andlife in arena): the baseline taken 2026-10-28 when
// the PS2 venues first ran (63 shots, 42 presets, 0 failed picks); not yet
// compared shot by shot with the original.
constexpr int kGoldenLines = 105;
constexpr uint32_t kGoldenHash = 0x094b806au;

uint32_t fnv(const std::string& s, uint32_t h) {
    for (unsigned char c : s) h = (h ^ c) * 16777619u;
    return h;
}

}  // namespace

int runDirectorGolden(const GameData& data) {
    const std::vector<SongInfo> songs = loadSongs(data);
    const SongInfo* info = nullptr;
    for (const SongInfo& s : songs)
        if (venues::forSong(data, s) == kVenue) {
            info = &s;
            break;
        }
    if (!info) {
        std::printf("director golden: no %s song\n", kVenue);
        return 1;
    }
    const Rules rules = loadRules(data);
    const Song song = loadChart(data, *info, rules);
    Venue venue(data, kVenue, VenueVariant::Single);
    World world(venue);
    world.interp().seed(0x60d1e5u);
    LightPresets lights(world, 0x4c504d31u);
    CameraDirector director(world, 0x43414d31u);
    director.provideGameObjects(false, "single");
    // The band on stage (M9): the director's targets, Star Power and walk
    // spots come from it.
    Band band(world, data, *info, Band::Config{});
    std::vector<std::string> log;
    auto line = [&](const std::string& what) {
        char b[32];
        std::snprintf(b, sizeof b, "%8.2f ", world.beats());
        log.push_back(b + what);
    };
    lights.onSwitch = line;
    director.onShot = line;
    Player p(song, Part::Guitar, Difficulty::Expert, rules, ControllerMode::Guitar, false);
    band.rating = [&](int) { return p.rockMeter(); };
    band.loadSong(parseMidi(data.read(info->audio.midiPath)), &song);
    Autoplay bot(p.chart());
    WorldDriver driver(true);
    const float lead = -3000;
    driver.reset(lead);
    world.setClock(lead / 1000.0, lead / 500.0);
    world.start(false);
    band.enter();
    for (float t = lead; t < song.endMs + 6000; t += 1000.0f / 60.0f) {
        bot.feed(p, t);
        p.update(t);
        const std::vector<PlayerEvent> events = p.takeEvents();
        driver.update(world, song, t, &p, events, nullptr, {});
        if (t >= song.endMs) driver.gameWon(world);
        world.update();
        lights.update();
        band.update(world.seconds(), world.beats());
        director.setStarPower(p.starPowerActive());
        director.update();
    }
    uint32_t h = 2166136261u;
    for (const std::string& l : log) h = fnv(l + "\n", h);
    int shots = 0, presets = 0;
    for (const std::string& l : log) (l.find(" shot ") != std::string::npos ? shots : presets)++;
    if (const char* out = SDL_getenv("GD03_GOLDEN_LOG"))
        if (FILE* f = std::fopen(out, "w")) {
            for (const std::string& l : log) std::fprintf(f, "%s\n", l.c_str());
            std::fclose(f);
        }
    const bool ok = int(log.size()) == kGoldenLines && h == kGoldenHash;
    std::printf("director golden: %s in %s: %d shots, %d presets, %d failed picks; log %d lines, hash %08x (expected %d, %08x)%s\n",
                info->id.c_str(), kVenue, shots, presets, director.failedPicks(), int(log.size()), unsigned(h), kGoldenLines, unsigned(kGoldenHash),
                ok ? "" : " MISMATCH");
    return ok ? 0 : 1;
}

namespace {

// One camera sample: the shot and the camera's position.
struct CamSample {
    std::string shot;
    float pos[3];
};

// `song` in `venue` (one player) for `seconds` from the lead-in with the
// world stepped `hz` times a second; the camera every 1/12 s.
std::vector<CamSample> cameraRun(const GameData& data, const SongInfo& info, const std::string& venueName, int hz, float seconds) {
    const Rules rules = loadRules(data);
    const Song song = loadChart(data, info, rules);
    Venue venue(data, venueName, VenueVariant::Single);
    World world(venue);
    world.interp().seed(0x60d1e5u);
    LightPresets lights(world, 0x4c504d31u);
    CameraDirector director(world, 0x43414d31u);
    director.provideGameObjects(false, "single");
    Band band(world, data, info, Band::Config{});
    std::string shot;
    director.onShot = [&](const std::string& s) { shot = s; };
    Player p(song, Part::Guitar, Difficulty::Expert, rules, ControllerMode::Guitar, false);
    band.rating = [&](int) { return p.rockMeter(); };
    band.loadSong(parseMidi(data.read(info.audio.midiPath)), &song);
    Autoplay bot(p.chart());
    WorldDriver driver(true);
    const float lead = -3000;
    driver.reset(lead);
    world.setClock(lead / 1000.0, lead / 500.0);
    world.start(false);
    band.enter();
    std::vector<CamSample> out;
    const int perSample = hz / 12;
    const int steps = int(std::lround((seconds - lead / 1000.0f) * float(hz)));
    for (int i = 0; i <= steps; ++i) {
        // Exact multiples of 1/hz s, so both rates meet on the 1/12 s grid.
        const float t = lead + float(double(i) * 1000.0 / hz);
        bot.feed(p, t);
        p.update(t);
        const std::vector<PlayerEvent> events = p.takeEvents();
        driver.update(world, song, t, &p, events, nullptr, {});
        world.update();
        lights.update();
        band.update(world.seconds(), world.beats());
        director.setStarPower(p.starPowerActive());
        director.update();
        if (i % perSample == 0 && director.hasCamera()) {
            const gfx::Vec3 c = director.camera().position();
            out.push_back({shot, {c.x, c.y, c.z}});
        }
    }
    return out;
}

}  // namespace

int runCameraRateCheck(const GameData& data) {
    const std::vector<SongInfo> songs = loadSongs(data);
    if (songs.empty()) return 1;
    const float seconds = SDL_getenv("GD03_CAMRATE_SECONDS") ? float(std::atof(SDL_getenv("GD03_CAMRATE_SECONDS"))) : 60.0f;
    int failures = 0;
    for (const std::string& v : venues::names()) {
        const SongInfo* info = &songs.front();
        for (const SongInfo& s : songs)
            if (venues::forSong(data, s) == v) {
                info = &s;
                break;
            }
        const std::vector<CamSample> a = cameraRun(data, *info, v, 60, seconds);
        const std::vector<CamSample> b = cameraRun(data, *info, v, 240, seconds);
        size_t shotDiffs = 0;
        float worst = 0, minZ = 1e30f;
        std::string worstShot, lowShot;
        for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
            if (a[i].shot != b[i].shot) {
                ++shotDiffs;
                continue;
            }
            // A shot's first sample: the shot may have started on that very
            // frame at 60 fps but up to three frames earlier at 240, and a
            // path whose first key jumps (arena's flr_near_lft03, battle's
            // balcony_rt02 on this disc) is then legitimately elsewhere.
            if (i > 0 && (a[i - 1].shot != a[i].shot || b[i - 1].shot != b[i].shot)) continue;
            const float d = std::sqrt((a[i].pos[0] - b[i].pos[0]) * (a[i].pos[0] - b[i].pos[0]) + (a[i].pos[1] - b[i].pos[1]) * (a[i].pos[1] - b[i].pos[1]) +
                                      (a[i].pos[2] - b[i].pos[2]) * (a[i].pos[2] - b[i].pos[2]));
            if (d > worst) worst = d, worstShot = a[i].shot;
            if (b[i].pos[2] < minZ) minZ = b[i].pos[2], lowShot = b[i].shot;
            if (a[i].pos[2] < minZ) minZ = a[i].pos[2], lowShot = a[i].shot;
        }
        // The camera follows the song's clock, not the frame rate: the same
        // shots and (within a few units: the shakes' and filters' rounding)
        // the same positions at 60 and 240 frames a second.
        if (SDL_getenv("GD03_CAMRATE_LOG"))
            for (size_t i = 0; i < std::min(a.size(), b.size()); ++i)
                std::printf("  %5.2f %-40s %-40s %8.1f %8.1f %8.1f | %8.1f %8.1f %8.1f\n", double(i) / 12 - 3, a[i].shot.c_str(), b[i].shot.c_str(),
                            double(a[i].pos[0]), double(a[i].pos[1]), double(a[i].pos[2]), double(b[i].pos[0]), double(b[i].pos[1]), double(b[i].pos[2]));
        const bool ok = a.size() == b.size() && shotDiffs == 0 && worst < 5.0f && std::isfinite(minZ);
        std::printf("camera rate %-8s %s: %zu samples, %zu shot differences, worst %.2f units (%s), lowest z %.1f (%s)%s\n", v.c_str(),
                    info->id.c_str(), a.size(), shotDiffs, double(worst), worstShot.c_str(), double(minZ), lowShot.c_str(), ok ? "" : "  FAILED");
        if (!ok) ++failures;
    }
    return failures;
}
