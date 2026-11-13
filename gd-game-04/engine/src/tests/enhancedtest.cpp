// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/enhancedtest.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#include "data/disc.h"
#include "data/qbkey.h"
#include "game/chart.h"
#include "render/particles.h"
#include "render/renderer.h"
#include "render/venue_camera.h"
#include "render/venue_stage.h"
#include "script/globals.h"
#include "ui/port_settings.h"

namespace {

constexpr double kStepMs = 1000.0 / 60.0;
constexpr double kLeadInMs = 3000.0;

struct Picture {
    std::vector<uint8_t> rgb;  // the frame, 3 bytes a pixel
    int w = 0, h = 0;
    int sceneH = 0;            // the 3D scene's height (the resolution it was drawn at)
    std::vector<std::string> passes;  // Enhanced's chain, last frame
};

// The mean absolute difference (0..255) and the share of pixels changed by
// more than 8 levels in any channel.
std::pair<double, double> compare(const Picture& a, const Picture& b) {
    if (a.rgb.size() != b.rgb.size() || a.rgb.empty()) return {255.0, 1.0};
    double sum = 0;
    size_t changed = 0;
    for (size_t i = 0; i < a.rgb.size(); i += 3) {
        int m = 0;
        for (size_t c = 0; c < 3; ++c) {
            const int d = std::abs(int(a.rgb[i + c]) - int(b.rgb[i + c]));
            sum += d;
            m = std::max(m, d);
        }
        if (m > 8) ++changed;
    }
    return {sum / double(a.rgb.size()), double(changed) / double(a.rgb.size() / 3)};
}

}  // namespace

int runEnhancedTest(const std::string& discPath) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);  // whole lines between the engine's own log
    std::string error;
    auto disc = Disc::open(discPath, error);
    if (!disc) {
        std::printf("enhancedtest: %s\n", error.c_str());
        return 2;
    }
    disc->loadNames();
    Globals g;
    Chart chart;
    try {
        g.load(*disc);
        chart = loadChart(*disc, "slowride");
    } catch (const std::exception& e) {
        std::printf("enhancedtest: %s\n", e.what());
        return 2;
    }
    const char* zoneEnv = SDL_getenv("GD04_VENUE");
    const std::string zone = zoneEnv ? zoneEnv : "z_artdeco";
    const double at = SDL_getenv("GD04_ENHANCEDTEST_AT") ? std::atof(SDL_getenv("GD04_ENHANCEDTEST_AT")) : 12000.0;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("enhancedtest: cannot start video: %s\n", SDL_GetError());
        return 2;
    }
    SDL_Window* window = SDL_CreateWindow("gd04 enhanced test", 1280, 720, gfx::prepareWindow() | SDL_WINDOW_HIDDEN);
    gfx::Renderer renderer;
    if (!window || !renderer.init(window, error)) {
        std::printf("enhancedtest: no renderer: %s\n", window ? error.c_str() : SDL_GetError());
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 2;
    }
    int failures = 0;
    {
        VenueStage st;
        if (!st.start(*disc, g, zone, chart, qbkey::key("slowride"), kLeadInMs, &renderer.device(), error)) {
            std::printf("enhancedtest: %s: %s\n", zone.c_str(), error.c_str());
            renderer.shutdown();
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 2;
        }
        for (double step = -kLeadInMs; step + kStepMs <= at; step += kStepMs) st.update(step + kStepMs, 1.0f);
        st.advance(at);
        // The options' values as the game keeps them (EnhancedStore, nothing
        // saved) and applied as the game applies them (applyEnhancedGlobals).
        EnhancedStore store;
        store.load("");
        for (const EnhancedSetting& e : enhancedSettings())
            if (!e.values.empty()) store.set(e.key, enhancedDefault(e.key));
        store.set("internal_res", "0");
        // One picture of the same song time: a few frames (TAA's history,
        // HDR's exposure settle), the last read back.
        int chainFrames = 0;
        auto picture = [&](bool starPower = false) {
            applyEnhancedGlobals(store);
            st.setStarPower(starPower);
            Picture p;
            for (int f = 0; f < 6; ++f) {
                const float black[4] = {0, 0, 0, 1};
                renderer.beginFrame(black);
                st.draw(at);
                SDL_Surface* px = f == 5 ? renderer.readPixels() : nullptr;
                renderer.endFrame();
                if (!px) continue;
                SDL_Surface* rgb = SDL_ConvertSurface(px, SDL_PIXELFORMAT_RGB24);
                SDL_DestroySurface(px);
                if (!rgb) continue;
                p.w = rgb->w, p.h = rgb->h;
                p.rgb.resize(size_t(p.w) * size_t(p.h) * 3);
                for (int y = 0; y < p.h; ++y)
                    std::memcpy(&p.rgb[size_t(y) * size_t(p.w) * 3], static_cast<const uint8_t*>(rgb->pixels) + size_t(y) * size_t(rgb->pitch),
                                size_t(p.w) * 3);
                SDL_DestroySurface(rgb);
            }
            p.sceneH = st.sceneHeight();
            if (st.enhancedFx() && st.enhancedFx()->frames() != chainFrames) p.passes = st.enhancedFx()->lastPasses();
            chainFrames = st.enhancedFx() ? st.enhancedFx()->frames() : 0;
            return p;
        };
        const Picture base = picture();
        const Picture again = picture();
        const auto [noise, noiseShare] = compare(base, again);
        std::printf("enhancedtest %s at %.0f ms: %dx%d, scene %d rows; the same settings twice: mean |d| %.3f, %.2f %% changed\n", zone.c_str(), at,
                    base.w, base.h, base.sceneH, noise, noiseShare * 100);
        // Every picture option and value but the default: the frame must
        // change (more than the repeat's noise), or the scene's size for the
        // resolution rows.
        struct Case {
            const char* key;
            const char* value;
            const char* with = nullptr;  // a second setting it needs
            const char* withValue = nullptr;
            bool starPower = false;
        };
        const std::vector<Case> cases = {
            {"internal_res", "1080"},    {"internal_res", "1440"},     {"antialiasing", "fxaa"},  {"antialiasing", "ssaa"},
            {"antialiasing", "taa"},     {"ai_upscaling", "fsr1"},     {"ai_upscaling_mode", "performance", "ai_upscaling", "fsr1"},
            {"hdr", "emulated"},         {"bokeh", "none"},            {"bokeh", "stronger"},     {"particles", "none"},
            {"particles", "2x"},         {"bloom", "none"},            {"bloom", "3x"},           {"cinematic_color", "on"},
            {"color_grade", "sepia"},    {"film_grain", "stronger"},   {"chromatic", "strong"},   {"ssao", "stronger"},
            {"sharpening", "high"},      {"star_blur", "stronger", nullptr, nullptr, true},       {"shadows_all", "on"},
            {"more_lights", "2.5x"},     {"light_dust", "2.5x"},       {"no_effects", "on"},
        };
        for (const Case& c : cases) {
            if (const char* only = SDL_getenv("GD04_ENHANCEDTEST_ONLY"); only && std::string(only) != c.key) continue;  // development aid
            const std::string before = store.get(c.key), beforeWith = c.with ? store.get(c.with) : std::string();
            store.set(c.key, c.value);
            if (c.with) store.set(c.with, c.withValue);
            // Particles start with the stage ("from the next song", its help):
            // None draws none; 2x: an Original stage and a 2x one in lockstep
            // through the song until particles are alive, their counts compared.
            Picture p;
            const bool particleRow = std::string(c.key) == "particles";
            size_t origQuads = 0, twoQuads = 0;
            double particleAt = -1;
            if (particleRow) {
                VenueStage a2, b2;
                store.set("particles", "original");
                applyEnhancedGlobals(store);
                // z_hell: emitters burning all song (z_artdeco's few pyro notes draw none at this rate).
                const std::string pz = "z_hell";
                const bool okA = a2.start(*disc, g, pz, chart, qbkey::key("slowride"), kLeadInMs, &renderer.device(), error);
                store.set(c.key, c.value);
                applyEnhancedGlobals(store);
                const bool okB = b2.start(*disc, g, pz, chart, qbkey::key("slowride"), kLeadInMs, &renderer.device(), error);
                double step = -kLeadInMs;
                for (double t = 0; okA && okB && t <= 120000; t += 250) {
                    while (step + kStepMs <= t) {
                        step += kStepMs;
                        a2.update(step, 1.0f), b2.update(step, 1.0f);
                    }
                    a2.advance(t), b2.advance(t);
                    const float black[4] = {0, 0, 0, 1};
                    Particles::density = 1;
                    renderer.beginFrame(black);
                    a2.draw(t);
                    renderer.endFrame();
                    applyEnhancedGlobals(store);
                    renderer.beginFrame(black);
                    b2.draw(t);
                    renderer.endFrame();
                    origQuads += a2.particleQuads(), twoQuads += b2.particleQuads();
                    if (particleAt < 0 && a2.particleQuads() > 0) particleAt = t;
                }
                p = base;
                        } else {
                p = picture(c.starPower);
            }
            const auto [d, share] = compare(base, p);
            if (const char* dir = SDL_getenv("GD04_ENHANCEDTEST_SHOTS"); dir && !p.rgb.empty()) {
                // GD04_ENHANCEDTEST_SHOTS=<dir>: each case's frame and the defaults' (development aid).
                for (const Picture* q : {&base, static_cast<const Picture*>(&p)}) {
                    SDL_Surface* sf = SDL_CreateSurfaceFrom(q->w, q->h, SDL_PIXELFORMAT_RGB24, const_cast<uint8_t*>(q->rgb.data()), q->w * 3);
                    const std::string name = std::string(dir) + "/" + (q == &base ? std::string("defaults") : std::string(c.key) + "_" + c.value) + ".bmp";
                    if (sf) SDL_SaveBMP(sf, name.c_str()), SDL_DestroySurface(sf);
                }
            }
            std::string passes;
            for (const std::string& s : p.passes) passes += (passes.empty() ? "" : " ") + s;
            const bool sized = p.sceneH != base.sceneH;
            // Particles 2x: twice the particles (the frame may show few of them).
            // Particles: the particles drawn over the song (the frame may show none).
            // Dust in Light Beams: motes of about a centimetre, a few hundred pixels.
            const bool faint = std::string(c.key) == "light_dust";
            const bool ok = particleRow ? (std::string(c.value) == "2x" ? origQuads > 0 && twoQuads >= origQuads * 3 / 2 : twoQuads == 0 && origQuads > 0)
                            : faint     ? d > noise && share > noiseShare + 0.0001
                                        : sized || (d > noise * 2 + 0.05 && share > noiseShare + 0.001);
            if (particleRow)
                std::printf("enhancedtest particles          %-12s %s: z_hell, particles drawn over the song (every 250 ms, first at %.0f ms): %zu with "
                            "Original, %zu with %s\n",
                            c.value, ok ? "changes the particles" : "FAIL: no change", particleAt, origQuads, twoQuads, c.value);
            if (!particleRow)
                std::printf("enhancedtest %-18s %-12s %s: mean |d| %.3f, %.2f %% changed, scene %d rows%s%s\n", c.key, c.value,
                            ok ? "changes the picture" : "FAIL: no change", d, share * 100, p.sceneH, passes.empty() ? "" : ", passes: ",
                            passes.c_str());
            if (!ok) ++failures;
            store.set(c.key, before);
            if (c.with) store.set(c.with, beforeWith);
        }
        // Disable Shaky Camera: the hand-held shots' sway [I: CameraDirector::noShake]:
        // the camera's rotation with it off against on, over 2 s.
        {
            std::vector<venue::CamPose> poses[2];
            for (int k = 0; k < 2; ++k) {
                store.set("no_shaky_cam", k ? "on" : "off");
                applyEnhancedGlobals(store);
                for (int f = 0; f <= 120; ++f) poses[k].push_back(st.camera(at + f * kStepMs));
            }
            double diff = 0;
            for (size_t f = 0; f < poses[0].size(); ++f)
                for (size_t i = 0; i < 4; ++i) diff += std::fabs(poses[0][f].rot[i] - poses[1][f].rot[i]);
            diff /= double(poses[0].size());
            const bool ok = diff > 1e-5;
            std::printf("enhancedtest no_shaky_cam      on           %s: the camera's rotation off vs on, mean |d| %.6f a frame over 2 s\n",
                        ok ? "changes the camera" : "FAIL: no change", diff);
            if (!ok) ++failures;
            store.set("no_shaky_cam", "off");
            applyEnhancedGlobals(store);
        }
    }
    renderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (failures) {
        std::printf("enhancedtest: %d options did not change the picture\n", failures);
        return 1;
    }
    std::printf("enhancedtest: every picture option changes the picture\n");
    return 0;
}
