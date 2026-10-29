// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/chartest.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "char/character.h"
#include "game/crowd.h"
#include "game/venue.h"
#include "game/world.h"
#include "render/crowd_view.h"
#include "script/script.h"

// --chartest's crowd part (docs/venues.md "Crowd"): arena's WorldCrowd
// through two minutes at 120 beats a minute, the game's excitement changing
// every 8 s (each change followed by a shot's crowd_update, as camshot.dta's
// start_shot does), lighters at Great from 64 s, half tempo from 96 s.
// Logged: each clip an archetype's main.drv starts, each hand change, and
// every 4 s each archetype's card rectangle for a fixed camera (CrowdView::
// cardFor, rounded). The crowd draws from its own RandomGen, so the world's,
// the band's and the director's numbers do not move it. A change in the
// drivers, the crowd scripts or the card rule moves the hash: check the log
// (GD03_CROWD_LOG=<file>) and update both.

namespace {

constexpr int kGoldenLines = 731;
constexpr uint32_t kGoldenHash = 0x30897aacu;

uint32_t fnv(const std::string& s, uint32_t h) {
    for (unsigned char c : s) h = (h ^ c) * 16777619u;
    return h;
}

int crowdRun(const GameData& data) {
    int failures = 0;
    Venue venue(data, "arena", VenueVariant::Single);
    World world(venue);
    world.interp().seed(0x63726f77u);
    Interp& in = world.interp();
    Crowd& crowd = world.crowd();
    std::vector<std::string> log;
    std::map<std::string, int> clips, hands;
    char b[160];
    crowd.onClip = [&](double beat, const std::string& a, const std::string& clip) {
        std::snprintf(b, sizeof b, "%8.2f %s %s", beat, a.c_str(), clip.c_str());
        log.push_back(b);
        ++clips[clip];
    };
    crowd.onHand = [&](double beat, const std::string& a, const std::string& hand) {
        std::snprintf(b, sizeof b, "%8.2f %s hand %s", beat, a.c_str(), hand.c_str());
        log.push_back(b);
        ++hands[hand];
    };
    // crowd.dta's {exists game} branch.
    ScriptObject game("game", "Game");
    in.registerObject(&game);
    world.setClock(0, 0);
    world.start(false);
    int loaded = 0, archetypes = 0;
    for (const Crowd::State& s : crowd.crowds())
        for (const auto& a : s.archetypes) ++archetypes, loaded += a.actor ? 1 : 0;
    // Hands at enter: clap shown, the others hidden (Character type crowd enter).
    int enterWrong = 0;
    for (const auto& a : crowd.actors()) {
        if (!a->character) continue;
        chr::Character& c = *a->character;
        for (const char* m : {"hand_L-clap.mesh", "hand_R-clap.mesh"})
            if (const scene::Object* o = c.dir().find(m); o && !c.showing(o)) ++enterWrong;
        for (const char* m : {"hand_L-fist.mesh", "hand_R-devil.mesh", "hand_R-lighter.mesh"})
            if (const scene::Object* o = c.dir().find(m); o && c.showing(o)) ++enterWrong;
        if (a->hand != "clap" || a->lighter) ++enterWrong;
    }
    world.message("intro_start_msg", {Value::integer(0), Value::integer(0)});
    // A fixed camera on the crowd: 600 back and 250 above the placements'
    // middle, looking at it.
    const Crowd::State* st = crowd.crowds().empty() ? nullptr : &crowd.crowds().front();
    gfx::Vec3 mid{0, 0, 0};
    int np = 0;
    if (st)
        for (const auto& a : st->archetypes)
            for (const scene::Xfm& x : a.placements) mid = mid + gfx::Vec3{x.pos[0], x.pos[1], x.pos[2]}, ++np;
    if (np) mid = mid * (1.0f / float(np));
    gfx::Camera cam = gfx::Camera::lookAt(mid + gfx::Vec3{0, -600, 250}, mid);
    cam.yFov = 0.7f, cam.nearPlane = 10, cam.farPlane = 10000;
    scene::Xfm mesh;
    if (st && !st->crowd->placementMesh.empty() && st->crowd->dir)
        if (const scene::Object* m = st->crowd->dir->resolve(st->crowd->placementMesh)) mesh = world.pose().world(m);
    const int levels[] = {2, 3, 1, 0, 2, 3, 3, 1, 3, 3, 3, 2, 3, 0, 1, 2};
    int nan = 0, lighterFrames = 0, cardsBad = 0;
    std::set<std::string> groups;
    for (int f = 1; f <= 120 * 30; ++f) {
        const double t = f / 30.0;
        if (f % 240 == 0) {
            const int lvl = levels[(f / 240) % 16];
            world.message("excitement", {Value::integer(lvl)});
            for (const Crowd::State& s : crowd.crowds()) in.send(s.script, "crowd_update", {}, false);
            for (const auto& a : crowd.actors())
                if (a->character) groups.insert(a->group);
        }
        if (f == 64 * 30)
            for (const Crowd::State& s : crowd.crowds()) in.send(s.script, "set", {Value::symbol("lighter"), Value::symbol("slow")}, false);
        if (f == 96 * 30) world.message("crowd_half_tempo");
        world.setClock(t, t * 2);
        world.update();
        for (const auto& a : crowd.actors()) {
            if (!a->character) continue;
            if (a->lighter) ++lighterFrames;
            scene::Xfm x;
            if (a->character->worldOf("spot_head.trans", x))
                for (float v : x.pos)
                    if (!std::isfinite(v)) ++nan;
        }
        if (f % 120 == 0 && st)
            for (const auto& a : st->archetypes) {
                if (!a.actor) continue;
                const CrowdView::Card k = CrowdView::cardFor(*a.actor->character, mesh, a.height, st->rotate, cam);
                if (!(k.u0 >= 0 && k.u0 <= 0.2f && k.u1 >= 0.8f && k.u1 <= 1 && k.v0 >= 0 && k.v0 <= 0.35f && k.v1 >= 0.65f && k.v1 <= 1)) ++cardsBad;
                std::snprintf(b, sizeof b, "%8.2f %s card %.3f %.3f %.3f %.3f", t * 2, a.name.c_str(), double(k.u0), double(k.v0), double(k.u1),
                              double(k.v1));
                log.push_back(b);
            }
    }
    in.unregisterObject(&game);
    uint32_t h = 2166136261u;
    for (const std::string& l : log) h = fnv(l + "\n", h);
    if (const char* out = SDL_getenv("GD03_CROWD_LOG"))
        if (FILE* fp = std::fopen(out, "w")) {
            for (const std::string& l : log) std::fprintf(fp, "%s\n", l.c_str());
            std::fclose(fp);
        }
    int started = 0;
    for (const auto& [_, n] : clips) started += n;
    std::string hs, gs;
    for (const auto& [k, n] : hands) hs += " " + k + " " + std::to_string(n);
    for (const std::string& g : groups) gs += " " + g;
    float beatScale = 0;
    for (const auto& a : crowd.actors())
        if (a->character) beatScale = a->beatScale;
    std::printf("chartest: crowd arena: %d of %d archetypes load (%zu characters), %d clips started (%zu distinct), groups%s; hands%s; "
                "%d lighter frames\n",
                loaded, archetypes, crowd.actors().size(), started, clips.size(), gs.c_str(), hs.c_str(), lighterFrames);
    if (loaded != archetypes || loaded == 0 || enterWrong) {
        std::printf("chartest: crowd: archetypes or their enter wrong (%d)\n", enterWrong);
        ++failures;
    }
    // Every level's group plays; the clips' events switch the hands; the
    // lighters show at Great once [lighter] is on; crowd_half_tempo.
    if (groups.size() < 4 || clips.size() < 10 || hands.size() < 2 || lighterFrames == 0 || beatScale != 0.5f) {
        std::printf("chartest: crowd: clips / hands / lighters / tempo wrong\n");
        ++failures;
    }
    if (nan || cardsBad) {
        std::printf("chartest: crowd: %d non-finite spots, %d card rectangles outside GH2's bounds\n", nan, cardsBad);
        ++failures;
    }
    const bool golden = int(log.size()) == kGoldenLines && h == kGoldenHash;
    std::printf("chartest: crowd golden: log %d lines, hash %08x (expected %d, %08x)%s\n", int(log.size()), unsigned(h), kGoldenLines,
                unsigned(kGoldenHash), golden ? "" : " MISMATCH");
    if (!golden) ++failures;
    return failures;
}

}  // namespace

int chartestCrowdChecks(const GameData& data) { return crowdRun(data); }
