// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/venuetest.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <vector>

#include "data/disc.h"
#include "data/qbkey.h"
#include "data/venue.h"
#include "game/chart.h"
#include "render/renderer.h"
#include "render/venue_stage.h"
#include "script/globals.h"

namespace {

constexpr double kStepMs = 1000.0 / 60.0;  // the game's 60 Hz step (ui/song_screen.cpp)
constexpr double kLeadInMs = 3000.0;
constexpr double kCheckMs = 250.0;

// A rock meter that swings through the three performance bands (a
// function of the 60 Hz step, as the judge's own health is).
float healthAt(double stepMs) { return 1.0f + 0.95f * float(std::sin(stepMs / 1000.0 * 2 * 3.14159265358979 / 47.0)); }

struct Sample {
    double t = 0;
    venue::CamPose pose;
    std::vector<venue::LightState> lights;
};

// Plays the stage from the lead-in to `endMs` at `fps` as the song screen
// does: the 60 Hz steps up to each frame's time, then the frame's own
// advance (more of them at 240 fps); every kCheckMs a sample.
std::vector<Sample> simulate(VenueStage& st, double endMs, int fps) {
    std::vector<Sample> out;
    const double t0 = -kLeadInMs;
    const int perCheck = int(std::lround(kCheckMs * fps / 1000.0));
    double step = t0;
    long n = 0;
    for (long f = 0;; ++f) {
        const bool check = f % perCheck == 0;
        const double t = check ? t0 + double(f / perCheck) * kCheckMs : t0 + double(f) * 1000.0 / fps;
        if (t > endMs) break;
        while (t0 + double(n + 1) * kStepMs <= t) {
            ++n;
            step = t0 + double(n) * kStepMs;
            st.update(step, healthAt(step));
        }
        st.advance(t);
        if (!check) {
            (void)st.camera(t);
            continue;
        }
        Sample s;
        s.t = t;
        s.pose = st.camera(t);
        st.lights().evaluate(t, s.lights);
        out.push_back(std::move(s));
    }
    return out;
}

bool same(const Sample& a, const Sample& b, std::string& why) {
    auto close = [](float x, float y) { return std::fabs(x - y) <= 1e-4f * std::max(1.0f, std::fabs(x)); };
    for (size_t i = 0; i < 3; ++i)
        if (!close(a.pose.pos[i], b.pose.pos[i])) return why = "camera position", false;
    for (size_t i = 0; i < 4; ++i)
        if (!close(a.pose.rot[i], b.pose.rot[i])) return why = "camera rotation", false;
    if (!close(a.pose.fov, b.pose.fov)) return why = "camera FOV", false;
    if (a.lights.size() != b.lights.size()) return why = "light count", false;
    for (size_t i = 0; i < a.lights.size(); ++i) {
        const venue::LightState &x = a.lights[i], &y = b.lights[i];
        if (!close(x.intensity, y.intensity) || !close(x.color[0], y.color[0]) || !close(x.color[1], y.color[1]) ||
            !close(x.color[2], y.color[2]) || !close(x.attenEnd, y.attenEnd))
            return why = "light " + std::to_string(i), false;
    }
    return true;
}

}  // namespace

int runVenueTest(const std::string& discPath) {
    std::string error;
    auto disc = Disc::open(discPath, error);
    if (!disc) {
        std::printf("venuetest: %s\n", error.c_str());
        return 2;
    }
    disc->loadNames();
    Globals g;
    try {
        g.load(*disc);
    } catch (const std::exception& e) {
        std::printf("venuetest: globals: %s\n", e.what());
        return 2;
    }
    const char* songEnv = SDL_getenv("GD04_VENUETEST_SONG");
    const std::string song = songEnv ? songEnv : "slowride";
    Chart chart;
    try {
        chart = loadChart(*disc, song);
    } catch (const std::exception& e) {
        std::printf("venuetest: %s: %s\n", song.c_str(), e.what());
        return 2;
    }
    double songEnd = 0;
    for (const ChartEvent& e : chart.cameras) songEnd = std::max(songEnd, double(e.time + e.length));
    for (const ChartEvent& e : chart.lightshow) songEnd = std::max(songEnd, double(e.time + e.length));
    songEnd += 2000.0;
    const int frames = SDL_getenv("GD04_VENUETEST_FRAMES") ? std::max(1, std::atoi(SDL_getenv("GD04_VENUETEST_FRAMES"))) : 240;
    const double drawAt = SDL_getenv("GD04_VENUETEST_AT") ? std::atof(SDL_getenv("GD04_VENUETEST_AT")) : 15000.0;
    const char* shots = SDL_getenv("GD04_VENUETEST_SHOTS");

    std::vector<std::string> zones;
    if (const char* z = SDL_getenv("GD04_VENUE")) zones.push_back(z);
    else zones = venue::venueZones(g);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("venuetest: cannot start video: %s\n", SDL_GetError());
        return 2;
    }
    SDL_Window* window = SDL_CreateWindow("gd04 venue test", 1280, 720, gfx::prepareWindow() | SDL_WINDOW_HIDDEN);
    gfx::Renderer renderer;
    if (!window || !renderer.init(window, error)) {
        std::printf("venuetest: no renderer: %s\n", window ? error.c_str() : SDL_GetError());
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 2;
    }
    const uint32_t songKey = qbkey::key(song);
    int failures = 0;
    std::printf("venuetest: %s, %zu venues, %d frames at 60 and at 240 fps from %.0f ms\n", song.c_str(), zones.size(), frames, drawAt);
    for (const std::string& zone : zones) {
        auto fail = [&](const std::string& why) {
            std::printf("venuetest %s: FAIL %s\n", zone.c_str(), why.c_str());
            ++failures;
        };
        // 1. The whole song without drawing, at 60 and at 240 fps.
        VenueStage a, b;
        if (!a.start(*disc, g, zone, chart, songKey, kLeadInMs, nullptr, error) ||
            !b.start(*disc, g, zone, chart, songKey, kLeadInMs, nullptr, error)) {
            fail(error);
            continue;
        }
        const std::vector<Sample> sa = simulate(a, songEnd, 60), sb = simulate(b, songEnd, 240);
        size_t agree = 0;
        std::string why;
        if (sa.size() != sb.size()) fail("sample counts differ");
        for (size_t i = 0; i < std::min(sa.size(), sb.size()); ++i) {
            if (sa[i].t != sb[i].t || !same(sa[i], sb[i], why)) {
                fail("at " + std::to_string(int(sa[i].t)) + " ms: " + why + " differs between 60 and 240 fps");
                break;
            }
            ++agree;
        }
        const auto& ha = a.cameras().history();
        if (SDL_getenv("GD04_VENUETEST_LOG"))
            for (const auto& h : ha)
                std::printf("  cut %8.0f..%8.0f %s[%d] %s %s %s\n", h.start, h.end, qbkey::names().get(h.array).c_str(), h.index,
                            qbkey::names().get(h.name).c_str(), qbkey::names().get(h.type).c_str(), qbkey::names().get(h.script).c_str());
        const auto& hb = b.cameras().history();
        bool cutsSame = ha.size() == hb.size();
        for (size_t i = 0; cutsSame && i < ha.size(); ++i)
            cutsSame = ha[i].start == hb[i].start && ha[i].array == hb[i].array && ha[i].index == hb[i].index;
        if (!cutsSame) fail("the camera cuts differ between 60 and 240 fps");
        size_t shotsFound = 0;
        for (const auto& s : ha) shotsFound += s.array != 0;
        if (shotsFound * 2 < ha.size()) fail("most cuts found no camera array");

        // 2. Drawn: `frames` frames at each rate from drawAt.
        VenueStage d;
        if (!d.start(*disc, g, zone, chart, songKey, kLeadInMs, &renderer.device(), error)) {
            fail(error);
            continue;
        }
        size_t drawsMin = ~size_t(0), drawsMax = 0;
        long drawn = 0;
        for (int fps : {60, 240}) {
            VenueStage& st = d;
            double step = -kLeadInMs;
            for (int f = 0; f < frames; ++f) {
                const double t = drawAt + double(f) * 1000.0 / fps;
                while (step + kStepMs <= t) {
                    step += kStepMs;
                    st.update(step, healthAt(step));
                }
                st.advance(t);
                const float black[4] = {0, 0, 0, 1};
                renderer.beginFrame(black);
                st.draw(t);
                renderer.endFrame();
                drawsMin = std::min(drawsMin, st.view()->stats().drawsLastFrame);
                drawsMax = std::max(drawsMax, st.view()->stats().drawsLastFrame);
                ++drawn;
            }
            // The 240 fps pass replays the same window: restart the stage.
            if (fps == 60 && !d.start(*disc, g, zone, chart, songKey, kLeadInMs, &renderer.device(), error)) {
                fail(error);
                break;
            }
        }
        if (drawsMin == 0) fail("a frame drew nothing");

        // 3. Screenshots: the middle of the first shots after the intro.
        int saved = 0;
        if (shots) {
            VenueStage s;
            if (s.start(*disc, g, zone, chart, songKey, kLeadInMs, &renderer.device(), error)) {
                double step = -kLeadInMs;
                std::vector<double> times;
                for (const auto& h : ha)
                    if (h.start >= -kLeadInMs && times.size() < 8) times.push_back(std::min(h.end, h.start + 6000.0) * 0.5 + h.start * 0.5);
                for (double t : times) {
                    while (step + kStepMs <= t) {
                        step += kStepMs;
                        s.update(step, healthAt(step));
                    }
                    const float black[4] = {0, 0, 0, 1};
                    renderer.beginFrame(black);
                    s.draw(t);
                    SDL_Surface* px = renderer.readPixels();
                    renderer.endFrame();
                    if (!px) continue;
                    char name[512];
                    std::snprintf(name, sizeof name, "%s/%s_%06d.bmp", shots, zone.c_str(), int(t));
                    if (SDL_SaveBMP(px, name)) ++saved;
                    SDL_DestroySurface(px);
                }
            }
        }
        const VenueView::Stats& vs = d.view()->stats();
        const std::string shotText = shots ? ", " + std::to_string(saved) + " screenshots" : std::string();
        std::printf("venuetest %s: %zu meshes, %zu triangles, %zu textures (%zu missing), %zu lights; %zu cuts, %d snapshot changes; "
                    "60/240 fps: %zu checkpoints identical; %ld frames drawn (%zu..%zu draws)%s\n",
                    zone.c_str(), vs.meshes, vs.triangles, vs.textures, vs.missingTextures, d.venue().lights.size(), ha.size(),
                    a.lights().snapshotChanges(), agree, drawn, drawsMin, drawsMax,
                    shotText.c_str());
    }
    renderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (failures) {
        std::printf("venuetest: %d failures\n", failures);
        return 1;
    }
    std::printf("venuetest: all %zu venues drawn; cameras and lights frame-rate independent\n", zones.size());
    return 0;
}
