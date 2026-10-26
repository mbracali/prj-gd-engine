// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/director_golden.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "data/game_data.h"
#include "game/autoplay.h"
#include "game/camera_director.h"
#include "game/light_presets.h"
#include "game/player.h"
#include "game/rules.h"
#include "game/song_db.h"
#include "game/song_setup.h"
#include "game/venue.h"
#include "game/world.h"
#include "game/world_driver.h"

namespace {

// The song of the golden run: its quickplay venue is a big one with many
// presets and shots; the first song (career order) whose venue is arena.
constexpr const char* kVenue = "arena";
// The expected log: its line count and FNV-1a hash. A change in the
// director, the presets, the world's scripts or the chart's events moves
// them; check the log (GD02_GOLDEN_LOG=<file>) and update both.
constexpr int kGoldenLines = 328;
constexpr uint32_t kGoldenHash = 0x1a4f4ffdu;

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
    std::vector<std::string> log;
    auto line = [&](const std::string& what) {
        char b[32];
        std::snprintf(b, sizeof b, "%8.2f ", world.beats());
        log.push_back(b + what);
    };
    lights.onSwitch = line;
    director.onShot = line;
    Player p(song, Part::Guitar, Difficulty::Expert, rules, ControllerMode::Guitar, false);
    Autoplay bot(p.chart());
    WorldDriver driver(true);
    const float lead = -3000;
    driver.reset(lead);
    world.setClock(lead / 1000.0, lead / 500.0);
    world.start(false);
    for (float t = lead; t < song.endMs + 6000; t += 1000.0f / 60.0f) {
        bot.feed(p, t);
        p.update(t);
        const std::vector<PlayerEvent> events = p.takeEvents();
        driver.update(world, song, t, &p, events, nullptr, {});
        if (t >= song.endMs) driver.gameWon(world);
        world.update();
        lights.update();
        director.setStarPower(p.starPowerActive());
        director.update();
    }
    uint32_t h = 2166136261u;
    for (const std::string& l : log) h = fnv(l + "\n", h);
    int shots = 0, presets = 0;
    for (const std::string& l : log) (l.find(" shot ") != std::string::npos ? shots : presets)++;
    if (const char* out = SDL_getenv("GD02_GOLDEN_LOG"))
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
