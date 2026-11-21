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

// GD06-GA-002/004: the mean luma (0-255) of a framed performer's face/torso
// region under the venue's own lights, to confirm the band is no longer near
// black. refcaps/gd06/OBSERVATIONS.md: real front-lit performer face/torso sits
// around 150-170/255. `member` is the framed member (cameras().lookatMember()),
// `bone` its lookatbone (bone_chest if 0). Projects the chest and head / waist /
// shoulder offsets to screen, then averages the luma of the pixels in that box
// that are above a small floor (dropping the black background). Returns -1 when
// the performer is not a sizeable close-up on screen.
float framedPerformerLuma(const VenueStage& s, double t, uint32_t member, uint32_t bone, SDL_Surface* rgb, double& boxFrac) {
    boxFrac = 0;
    venue::Vec3 chest;
    if (!s.band() || !s.band()->bonePos(member, bone ? bone : qbkey::key("bone_chest"), chest)) return -1.0f;
    const venue::CamPose c = s.camera(t);
    const venue::Vec3 fwd = venue::rotate(c.rot, {0, 0, 1}), up = venue::rotate(c.rot, {0, 1, 0}), right = venue::rotate(c.rot, {-1, 0, 0});
    const int W = rgb->w, H = rgb->h;
    const float th = std::tan(c.fov * 0.5f * 3.14159265f / 180.0f), tv = th / (16.0f / 9.0f);
    auto project = [&](venue::Vec3 p, float& sx, float& sy) -> bool {
        const float d[3] = {p[0] - c.pos[0], p[1] - c.pos[1], p[2] - c.pos[2]};
        const float z = d[0] * fwd[0] + d[1] * fwd[1] + d[2] * fwd[2];
        if (z <= 0.05f) return false;
        const float x = d[0] * right[0] + d[1] * right[1] + d[2] * right[2];
        const float y = d[0] * up[0] + d[1] * up[1] + d[2] * up[2];
        sx = ((x / z / th) * 0.5f + 0.5f) * float(W);
        sy = (0.5f - (y / z / tv) * 0.5f) * float(H);
        return true;
    };
    // The face / torso box: head ~0.45 m above the chest, waist ~0.55 m below,
    // the shoulders ~0.35 m to each side (along the camera's right).
    const venue::Vec3 pts[5] = {chest,
                                {chest[0], chest[1] + 0.45f, chest[2]},
                                {chest[0], chest[1] - 0.55f, chest[2]},
                                {chest[0] + right[0] * 0.35f, chest[1] + right[1] * 0.35f, chest[2] + right[2] * 0.35f},
                                {chest[0] - right[0] * 0.35f, chest[1] - right[1] * 0.35f, chest[2] - right[2] * 0.35f}};
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    for (const venue::Vec3& p : pts) {
        float sx, sy;
        if (!project(p, sx, sy)) return -1.0f;
        minx = std::min(minx, sx), maxx = std::max(maxx, sx);
        miny = std::min(miny, sy), maxy = std::max(maxy, sy);
    }
    const int x0 = std::max(0, int(minx)), x1 = std::min(W - 1, int(maxx));
    const int y0 = std::max(0, int(miny)), y1 = std::min(H - 1, int(maxy));
    if (x1 <= x0 || y1 <= y0) return -1.0f;
    boxFrac = double(y1 - y0) / double(H);  // a close-up fills much of the frame's height
    // The lit face / torso surface: the luma of the box pixels above a small
    // floor (the black venue behind is dropped), then the mean of the brightest
    // 30% -- the lit skin / clothing the refcaps measured, calibrated to read
    // ~120-150 on the real front-lit closes (OBSERVATIONS.md's 150-170 is a
    // tighter manual crop of the same surface).
    std::vector<float> vals;
    const uint8_t* px = static_cast<const uint8_t*>(rgb->pixels);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const uint8_t* p = px + size_t(y) * size_t(rgb->pitch) + size_t(x) * 3;
            const float luma = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
            if (luma >= 8.0f) vals.push_back(luma);
        }
    if (vals.empty()) return -1.0f;
    std::sort(vals.begin(), vals.end());
    const size_t k = size_t(double(vals.size()) * 0.70);
    double sum = 0;
    for (size_t i = k; i < vals.size(); ++i) sum += vals[i];
    return float(sum / double(vals.size() - k));
}

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
    const char* songEnv = SDL_getenv("GD06_VENUETEST_SONG");
    // Aerosmith's disc has no slowride (GH3's song); dreampolice is the quickplay
    // set list's first song and carries full camera / light-show notes (73 cuts).
    const std::string song = songEnv ? songEnv : "dreampolice";
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
    const int frames = SDL_getenv("GD06_VENUETEST_FRAMES") ? std::max(1, std::atoi(SDL_getenv("GD06_VENUETEST_FRAMES"))) : 240;
    const double drawAt = SDL_getenv("GD06_VENUETEST_AT") ? std::atof(SDL_getenv("GD06_VENUETEST_AT")) : 15000.0;
    const char* shots = SDL_getenv("GD06_VENUETEST_SHOTS");

    std::vector<std::string> zones;
    if (const char* z = SDL_getenv("GD06_VENUE")) zones.push_back(z);
    else zones = venue::venueZones(g);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("venuetest: cannot start video: %s\n", SDL_GetError());
        return 2;
    }
    SDL_Window* window = SDL_CreateWindow("gd06 venue test", 1280, 720, gfx::prepareWindow() | SDL_WINDOW_HIDDEN);
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
    // The most of the band's lit texels allowed to be acne-prone (GD06-119).
    constexpr double kAcneMaxPct = 10.0;
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
        if (SDL_getenv("GD06_VENUETEST_LOG"))
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
        VenueStage::acneProbe = true, VenueStage::acne = {0, 0, 0};  // the band's self-shadowing (GD06-119)
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
                // The dynamic shadow keeps one light (GD06-103: it followed the light show).
                if (st.shadowLight() >= 0) {
                    if (shadowLight >= 0 && st.shadowLight() != shadowLight) shadowMoved = true;
                    shadowLight = st.shadowLight();
                    // GH3's two cascades (GD06-106): the band in both maps.
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
        if (d.band() && d.band()->view() && !SDL_getenv("GD06_NO_SHADOWS") && shadowFrames > 0 && bothCascades != shadowFrames)
            fail("the dynamic shadow drew " + std::to_string(bothCascades) + " of " + std::to_string(shadowFrames) +
                 " frames into both cascades");
        VenueStage::acneProbe = false;
        if (const VenueStage::Acne a = VenueStage::acne; a.area > 0) {
            // GD06-119: the band's lit surfaces whose depth changes more than
            // their caster's bias within a texel shadow themselves in stripes.
            // GH3's double_sided bias keeps it to the surfaces almost edge-on
            // to the light; the single_sided 64 x 2^-24 applied to every
            // caster before striped most of them.
            const double now = 100.0 * a.prone / a.area, old = 100.0 * a.proneOld / a.area;
            std::printf("venuetest %s: band shadow acne-prone %.1f %% of its lit texels (%.1f %% with the single-sided bias)\n", zone.c_str(), now, old);
            if (now > kAcneMaxPct) fail("the band shadows itself in stripes: " + std::to_string(now) + " % acne-prone");
        }

        // 3. Screenshots: the middle of the first shots after the intro.
        int saved = 0;
        if (shots) {
            VenueStage s;
            if (s.start(*disc, g, zone, chart, songKey, kLeadInMs, &renderer.device(), error)) {
                double step = -kLeadInMs;
                std::vector<double> times;
                // GD06_VENUETEST_SEQ=<ms>,<count>[,<step ms>]: consecutive frames instead
                // (16.67 ms apart by default), to catch flicker.
                if (const char* seq = SDL_getenv("GD06_VENUETEST_SEQ")) {
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
                    if (SDL_getenv("GD06_VENUETEST_SEQ")) s.advance(t);  // as a played frame
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
        // GD06-GA-002/004: the band's face/torso luma under venue lights, at the
        // director's close single-performer cuts, vs the real game's ~150-170/255.
        if (SDL_getenv("GD06_VENUETEST_LUMA")) {
            VenueStage s;
            if (s.start(*disc, g, zone, chart, songKey, kLeadInMs, &renderer.device(), error)) {
                struct Acc {
                    double sum = 0, lo = 1e9, hi = -1e9;
                    int n = 0;
                };
                Acc perMember[2];  // 0 guitarist, 1 vocalist
                const uint32_t who[2] = {qbkey::key("guitarist"), qbkey::key("vocalist")};
                double step = -kLeadInMs;
                int shotsSeen = 0;
                for (const auto& h : ha) {
                    if (h.start < -kLeadInMs) continue;
                    const double t = std::min(h.end, h.start + 6000.0) * 0.5 + h.start * 0.5;
                    if (t <= step) continue;
                    while (step + kStepMs <= t) {
                        step += kStepMs;
                        s.update(step, healthAt(step));
                    }
                    s.advance(t);
                    // The member this shot frames (the director's lookat at t).
                    const uint32_t member = s.cameras().lookatMember(), bone = s.cameras().lookatBone();
                    const int idx = member == who[0] ? 0 : member == who[1] ? 1 : -1;
                    if (idx < 0) continue;
                    const float black[4] = {0, 0, 0, 1};
                    renderer.beginFrame(black);
                    s.draw(t);
                    SDL_Surface* px = renderer.readPixels();
                    renderer.endFrame();
                    if (!px) continue;
                    SDL_Surface* rgb = SDL_ConvertSurface(px, SDL_PIXELFORMAT_RGB24);
                    SDL_DestroySurface(px);
                    if (!rgb) continue;
                    double boxFrac = 0;
                    const float luma = framedPerformerLuma(s, t, member, bone, rgb, boxFrac);
                    SDL_DestroySurface(rgb);
                    // Only sizeable close-ups (the performer fills much of the frame),
                    // matching the ground-truth face/torso close-up measurement.
                    if (luma < 0 || boxFrac < 0.33) continue;
                    ++shotsSeen;
                    Acc& a = perMember[idx];
                    a.sum += luma, a.n += 1;
                    a.lo = std::min(a.lo, double(luma)), a.hi = std::max(a.hi, double(luma));
                    if (SDL_getenv("GD06_VENUETEST_LOG"))
                        std::printf("  luma %s cut %.0f ms %s: face/torso %.1f (box %.0f%% of height)\n", zone.c_str(), t,
                                    qbkey::names().get(member).c_str(), luma, boxFrac * 100.0);
                }
                for (int i = 0; i < 2; ++i)
                    if (perMember[i].n)
                        std::printf("venuetest %s: %s lit face/torso luma mean %.1f (%.1f..%.1f over %d close-ups) [real front-lit ~120-150]\n",
                                    zone.c_str(), i == 0 ? "guitarist" : "singer", perMember[i].sum / perMember[i].n, perMember[i].lo,
                                    perMember[i].hi, perMember[i].n);
                if (!shotsSeen) std::printf("venuetest %s: no single-performer close-ups to measure\n", zone.c_str());
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
