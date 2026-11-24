// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/exploretest.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "data/band.h"
#include "data/disc.h"
#include "data/pak.h"
#include "data/venue.h"
#include "render/band_anim.h"
#include "render/band_view.h"
#include "render/renderer.h"
#include "script/globals.h"

// --exploretest: the model explorer's categories, headless. This mirrors
// platform/module_explore.cpp (Explorer::open builds the item list, select()
// loads + render-adds one model). It does NOT include that file (it owns the
// launcher's GdExplorer C ABI); it reproduces the same enumeration and the
// same loadModel + BandView::add so a failure here is a failure the launcher
// would show. Groups: Venues / Guitars / Bass / Drums / Mics / Chars; "Songs"
// is the separate gd_game_songs path, covered by --selftest's song list.
namespace {

const char* roleGroup(band::Role r) {
    return r == band::Role::Bassist    ? "Bass"
           : r == band::Role::Drummer  ? "Drums"
           : r == band::Role::Vocalist ? "Mics"
                                       : "Guitars";
}

std::string label(const band::Member& m) {
    return m.profileName.empty() ? std::string(band::roleName(m.role)) : m.profileName;
}

// loadModel + a render-add, exactly as Explorer::select() does before drawing.
// Returns an empty string on success, else why it could not be shown.
std::string loadAndAdd(const Disc& disc, const Globals& g, band::Library& lib, gfx::Renderer& renderer, const band::Member& m,
                       size_t& verts) {
    std::string error;
    std::unique_ptr<band::Model> model = band::loadModel(disc, g, lib, m, "", error);
    if (!model || !model->skeleton) return error.empty() ? "no model" : error;
    // The bounding box of every skinned mesh (the explorer frames the model by
    // it): a model with no vertices cannot be shown.
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (const band::Part& p : model->parts)
        for (const scene::Mesh& mesh : p.scene.meshes) {
            verts += mesh.positions.size();
            for (const auto& pos : mesh.positions)
                for (int a = 0; a < 3; ++a) lo[a] = std::min(lo[a], pos[a]), hi[a] = std::max(hi[a], pos[a]);
        }
    if (lo[0] > hi[0]) return "the model has no vertices";
    BandView view;
    const int member = view.add(renderer.device(), *model, error);
    view.shutdown();
    if (member < 0) return error.empty() ? "the band renderer rejected the model" : error;
    return std::string();
}

}  // namespace

int runExploreTest(const std::string& discPath) {
    std::string error;
    auto disc = Disc::open(discPath, error);
    if (!disc) {
        std::printf("exploretest: %s\n", error.c_str());
        return 2;
    }
    disc->loadNames();
    Globals g;
    try {
        g.load(*disc);
    } catch (const std::exception& e) {
        std::printf("exploretest: globals: %s\n", e.what());
        return 2;
    }
    band::Library lib;
    try {
        lib.load(*disc);
    } catch (const std::exception& e) {
        std::printf("exploretest: library: %s\n", e.what());
        return 2;
    }

    SDL_setenv_unsafe("GD07_BAND_FILL", "0.85", 0);
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("exploretest: cannot start video: %s\n", SDL_GetError());
        return 2;
    }
    SDL_Window* window = SDL_CreateWindow("gd07 explore test", 1280, 720, gfx::prepareWindow() | SDL_WINDOW_HIDDEN);
    gfx::Renderer renderer;
    if (!window || !renderer.init(window, error)) {
        std::printf("exploretest: no renderer: %s\n", window ? error.c_str() : SDL_GetError());
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 2;
    }

    int failures = 0;
    std::map<std::string, int> groupOk, groupFail;
    auto fail = [&](const std::string& group, const std::string& what) {
        std::printf("exploretest: FAIL [%s] %s\n", group.c_str(), what.c_str());
        ++groupFail[group];
        ++failures;
    };

    // Venues: the same zones the explorer lists; each must have its loadable
    // zone pak on the disc (Explorer::select builds a VenueStage from it; the
    // stage's own render is proven by --chartest / --venuetest).
    try {
        for (const std::string& z : venue::venueZones(g)) {
            const std::string path = "DATA/COMPRESSED/ZONES/" + z + ".pak.xen";
            if (!disc->exists(path)) {
                fail("Venues", z + ": no " + path);
                continue;
            }
            try {
                Pak::load(*disc, path);  // parses the zone archive to its end
                ++groupOk["Venues"];
            } catch (const std::exception& e) {
                fail("Venues", z + ": " + e.what());
            }
        }
    } catch (const std::exception& e) {
        fail("Venues", std::string("enumerate: ") + e.what());
    }

    // Instruments, split into Guitars / Bass / Drums / Mics by role (one per
    // character per role), exactly as Explorer::open does.
    size_t verts = 0;
    try {
        std::vector<band::Member> instruments = band::everyInstrumentGh4(g);
        for (band::Role role : {band::Role::Guitarist, band::Role::Bassist, band::Role::Drummer, band::Role::Vocalist}) {
            std::vector<std::string> seen;
            for (const band::Member& m : instruments) {
                if (m.role != role || std::find(seen.begin(), seen.end(), m.profileName) != seen.end()) continue;
                seen.push_back(m.profileName);
                const std::string why = loadAndAdd(*disc, g, lib, renderer, m, verts);
                if (why.empty()) ++groupOk[roleGroup(role)];
                else fail(roleGroup(role), label(m) + ": " + why);
            }
        }
    } catch (const std::exception& e) {
        fail("Guitars", std::string("instruments: ") + e.what());
    }

    // Chars: the preset roster (one Member per named profile), as Explorer::open.
    try {
        std::vector<std::string> seen;
        for (const band::Member& m : band::everyTalentGh4(g, 0)) {
            if (std::find(seen.begin(), seen.end(), m.profileName) != seen.end()) continue;
            seen.push_back(m.profileName);
            const std::string why = loadAndAdd(*disc, g, lib, renderer, m, verts);
            if (why.empty()) ++groupOk["Chars"];
            else fail("Chars", label(m) + ": " + why);
        }
    } catch (const std::exception& e) {
        fail("Chars", std::string("chars: ") + e.what());
    }

    const char* groups[] = {"Venues", "Guitars", "Bass", "Drums", "Mics", "Chars"};
    int shown = 0;
    std::printf("exploretest: %s\n", discPath.c_str());
    for (const char* grp : groups) {
        const int ok = groupOk[grp], bad = groupFail[grp];
        shown += ok;
        std::printf("  %-8s %3d ok, %d failed%s\n", grp, ok, bad, (ok == 0 && bad == 0) ? "  (empty!)" : "");
        if (ok == 0 && bad == 0) fail(grp, "the category is empty");
    }
    std::printf("exploretest: %d models shown across 6 categories, %zu vertices loaded; Songs are the separate gd_game_songs path\n",
                shown, verts);

    renderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (failures) {
        std::printf("exploretest: %d FAILED\n", failures);
        return 1;
    }
    std::printf("exploretest: all explore categories show and load their models\n");
    return 0;
}
