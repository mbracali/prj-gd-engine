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
#include "render/core_backend.h"
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
    const char* songEnv = SDL_getenv("GD07_VENUETEST_SONG");
    const std::string song = songEnv ? songEnv : "aggro";
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
    const int frames = SDL_getenv("GD07_VENUETEST_FRAMES") ? std::max(1, std::atoi(SDL_getenv("GD07_VENUETEST_FRAMES"))) : 240;
    const double drawAt = SDL_getenv("GD07_VENUETEST_AT") ? std::atof(SDL_getenv("GD07_VENUETEST_AT")) : 15000.0;
    const char* shots = SDL_getenv("GD07_VENUETEST_SHOTS");

    std::vector<std::string> zones;
    if (const char* z = SDL_getenv("GD07_VENUE")) zones.push_back(z);
    else zones = venue::venueZones(g);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("venuetest: cannot start video: %s\n", SDL_GetError());
        return 2;
    }
    // GD07_WINDOW_SIZE=<w>x<h>: another window (the screen effects' quarter
    // takes its resample path off 1280 x 720: GD10-066).
    int winW = 1280, winH = 720;
    if (const char* ws = SDL_getenv("GD07_WINDOW_SIZE")) std::sscanf(ws, "%dx%d", &winW, &winH);
    // GD07_GRAPHICS_BACKEND=<id>: draw on that backend (local's OpenGL by default).
    const gfx::Backend backend = SDL_getenv("GD07_GRAPHICS_BACKEND") ? gfx::chosenBackend("") : gfx::Backend::Local;
    gfx::setRendererBackend(backend);
    SDL_Window* window = SDL_CreateWindow("gd07 venue test", winW, winH, gfx::prepareWindowFor(backend) | SDL_WINDOW_HIDDEN);
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
        // GD07_VENUETEST_BAND=1: only where the band stands (GD10-065): every
        // 100 ms of the song, each member's pelvis against the xz box of the
        // zone's TRG_Waypoint nodes (how far outside) and the closest two
        // members (pelvis to pelvis, xz).
        if (SDL_getenv("GD07_VENUETEST_BAND")) {
            VenueStage s;
            if (!s.start(*disc, g, zone, chart, songKey, kLeadInMs, nullptr, error) || !s.band()) {
                fail(error.empty() ? "no band" : error);
                continue;
            }
            float lo[2] = {1e9f, 1e9f}, hi[2] = {-1e9f, -1e9f};
            for (const venue::Node& n : s.venue().nodes)
                if (qbkey::names().get(n.name).find("TRG_Waypoint") != std::string::npos)
                    for (int c = 0; c < 2; ++c) {
                        lo[c] = std::min(lo[c], n.pos[c * 2]);
                        hi[c] = std::max(hi[c], n.pos[c * 2]);
                    }
            Band& band = *s.band();
            const size_t nm = band.members().size();
            std::vector<float> worstOut(nm, 0.0f);
            std::vector<double> worstOutAt(nm, 0);
            float closest = 1e9f;
            double closestAt = 0;
            std::string closestWho;
            long samples = 0, outside = 0, tooClose = 0, pastFront = 0;
            float front = 0;  // the farthest any member gets past the front-most waypoint (+z, toward the crowd)
            double step = -kLeadInMs;
            for (double t = 0; t <= songEnd; t += 100.0) {
                while (step + kStepMs <= t) {
                    step += kStepMs;
                    s.update(step, healthAt(step));
                }
                s.advance(t);
                band.evaluate(t);
                std::vector<venue::Vec3> at(nm);
                std::vector<bool> ok(nm, false);
                for (size_t m = 0; m < nm; ++m) {
                    const auto& e = band.members()[m];
                    ok[m] = band.bonePos(e.model->member.object, qbkey::key("bone_pelvis"), at[m]);
                    if (!ok[m]) continue;
                    float out = 0;
                    for (int c = 0; c < 2; ++c) out = std::max({out, lo[c] - at[m][c * 2], at[m][c * 2] - hi[c]});
                    if (out > 0.25f) ++outside;
                    if (out > worstOut[m]) worstOut[m] = out, worstOutAt[m] = t;
                    front = std::max(front, at[m][2] - hi[1]);
                    if (at[m][2] - hi[1] > 1.0f) ++pastFront;
                }
                for (size_t i = 0; i < nm; ++i)
                    for (size_t j = i + 1; j < nm; ++j) {
                        if (!ok[i] || !ok[j]) continue;
                        const float dx = at[i][0] - at[j][0], dz = at[i][2] - at[j][2];
                        const float d = std::sqrt(dx * dx + dz * dz);
                        if (d < 0.5f) ++tooClose;
                        if (d < closest) {
                            closest = d, closestAt = t;
                            closestWho = band.members()[i].model->member.profileName + "/" + band.members()[j].model->member.profileName;
                        }
                    }
                ++samples;
                if (std::atoi(SDL_getenv("GD07_VENUETEST_BAND")) >= 2 && std::fmod(t, 1000.0) < 1.0) {
                    std::printf("  %6.0f", t);
                    for (size_t m = 0; m < nm; ++m) {
                        const auto& e = band.members()[m];
                        const std::string& node = e.anim.startNodeAt(t);
                        std::printf("  %s(%s) %.2f %.2f %.2f", e.model->member.profileName.c_str(), node.empty() ? "own" : node.c_str(),
                                    double(at[m][0]), double(at[m][1]), double(at[m][2]));
                    }
                    std::printf("\n");
                }
            }
            if (std::atoi(SDL_getenv("GD07_VENUETEST_BAND")) >= 2)
                for (const venue::Node& n : s.venue().nodes)
                    if (qbkey::names().get(n.name).find("TRG_Waypoint") != std::string::npos)
                        std::printf("  node %s %.2f %.2f %.2f angles %.1f %.1f %.1f\n", qbkey::names().get(n.name).c_str(), double(n.pos[0]),
                                    double(n.pos[1]), double(n.pos[2]), double(n.angles[0]), double(n.angles[1]), double(n.angles[2]));
            for (size_t m = 0; m < nm; ++m)
                std::printf("venuetest band %s: %s at most %.2f m outside the waypoints' box (at %.0f ms)\n", zone.c_str(),
                            band.members()[m].model->member.profileName.c_str(), double(worstOut[m]), worstOutAt[m]);
            std::printf("venuetest band %s: %ld samples; %ld member samples > 0.25 m outside the box, %ld > 1 m past its front "
                        "(farthest %.2f m); %ld pairs < 0.5 m apart; closest %.2f m (%s at %.0f ms)\n",
                        zone.c_str(), samples, outside, pastFront, double(front), tooClose, double(closest), closestWho.c_str(), closestAt);
            continue;
        }
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
        if (SDL_getenv("GD07_VENUETEST_LOG"))
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
        int shadowLight = -1;
        bool shadowMoved = false;
        long shadowFrames = 0, bothCascades = 0;  // frames with a shadow light; of them, both cascades drawn
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
                // The dynamic shadow keeps one light (GD10-103: it followed the light show).
                if (st.shadowLight() >= 0) {
                    if (shadowLight >= 0 && st.shadowLight() != shadowLight) shadowMoved = true;
                    shadowLight = st.shadowLight();
                    // GH3's two cascades (GD10-106): the band in both maps.
                    ++shadowFrames;
                    if (st.shadowCascadeDraws()[0] > 0 && st.shadowCascadeDraws()[1] > 0) ++bothCascades;
                }
            }
            // The 240 fps pass replays the same window: restart the stage.
            if (fps == 60 && !d.start(*disc, g, zone, chart, songKey, kLeadInMs, &renderer.device(), error)) {
                fail(error);
                break;
            }
        }
        if (drawsMin == 0) fail("a frame drew nothing");
        if (shadowMoved) fail("the dynamic shadow changed its light between frames");
        if (d.band() && d.band()->view() && !SDL_getenv("GD07_NO_SHADOWS") && shadowFrames > 0 && bothCascades != shadowFrames)
            fail("the dynamic shadow drew " + std::to_string(bothCascades) + " of " + std::to_string(shadowFrames) +
                 " frames into both cascades");

        // 3. Screenshots: the middle of the first shots after the intro.
        int saved = 0;
        if (shots) {
            VenueStage s;
            if (s.start(*disc, g, zone, chart, songKey, kLeadInMs, &renderer.device(), error)) {
                double step = -kLeadInMs;
                std::vector<double> times;
                // GD07_VENUETEST_SEQ=<ms>,<count>[,<step ms>]: consecutive frames instead
                // (16.67 ms apart by default), to catch flicker.
                if (const char* seq = SDL_getenv("GD07_VENUETEST_SEQ")) {
                    double from = 0, step = 1000.0 / 60.0;
                    int count = 0;
                    if (std::sscanf(seq, "%lf,%d,%lf", &from, &count, &step) >= 2)
                        for (int i = 0; i < count && i < 600; ++i) times.push_back(from + step * i);
                } else {
                    for (const auto& h : ha)
                        if (h.start >= -kLeadInMs && times.size() < 8) times.push_back(std::min(h.end, h.start + 6000.0) * 0.5 + h.start * 0.5);
                }
                for (double t : times) {
                    while (step + kStepMs <= t) {
                        step += kStepMs;
                        s.update(step, healthAt(step));
                    }
                    if (SDL_getenv("GD07_VENUETEST_SEQ")) s.advance(t);  // as a played frame
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
