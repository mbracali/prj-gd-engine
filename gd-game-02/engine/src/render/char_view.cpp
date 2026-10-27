// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The character viewer (render/band_view.h, runCharView).
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>

#include "app_ids.h"
#include "char/character.h"
#include "data/game_data.h"
#include "devaids/screenshot.h"
#include "render/band_view.h"
#include "render/canvas.h"
#include "render/renderer.h"

namespace {

// "char/alterna1", "alterna1" or a character milo -> its archive path.
std::string charMilo(const GameData& data, const std::string& arg) {
    std::string a = arg;
    if (a.rfind("char/", 0) == 0) a = a.substr(5);
    if (const size_t slash = a.find('/'); slash != std::string::npos) {
        // A full path: as it is.
        const std::string p = scene::miloArkPath(arg);
        return data.exists(p) ? p : "";
    }
    const std::string p = "char/" + a + "/ng/gen/" + a + ".milo_xbox";
    return data.exists(p) ? p : "";
}

}  // namespace

bool isCharPath(const std::string& path) {
    return path.rfind("char/", 0) == 0 && path.find("/anims/") == std::string::npos && path.rfind("char/ng/", 0) != 0;
}

int runCharView(const std::string& dataPath, const std::string& character) {
    std::unique_ptr<GameData> data;
    std::string path;
    try {
        data = std::make_unique<GameData>(dataPath);
        path = charMilo(*data, character);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s --view: %s\n", app::kProgram, e.what());
        return 2;
    }
    if (path.empty()) {
        std::fprintf(stderr, "%s --view: no character %s\n", app::kProgram, character.c_str());
        return 2;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "%s --view: cannot start video: %s\n", app::kProgram, SDL_GetError());
        return 2;
    }
    SDL_Window* window = SDL_CreateWindow(app::kWindowTitle, int(kScreenW), int(kScreenH), SDL_WINDOW_RESIZABLE | gfx::prepareWindow());
    Canvas canvas;
    std::string error;
    if (!window || !canvas.init(window, error)) {
        std::fprintf(stderr, "%s --view: cannot open the window: %s\n", app::kProgram, window ? error.c_str() : SDL_GetError());
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 2;
    }
    int code = 0;
    {
        chr::ClipLibrary clips(*data);
        std::unique_ptr<chr::Character> c;
        try {
            // The outfits a venue gives (guitar_sg for guitarists, the arena kit for drummers).
            auto probe = std::make_unique<chr::Character>(*data, path, clips);
            std::map<std::string, std::string> outfits;
            if (probe->dir().get("guitar.outfit")) outfits["guitar.outfit"] = "char/ng/guitars/gen/guitar_sg.milo_xbox";
            if (probe->dir().get("drums.outfit")) outfits["drums.outfit"] = "char/ng/drums/gen/dw_arena_drums.milo_xbox";
            probe.reset();
            c = std::make_unique<chr::Character>(*data, path, clips, outfits);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "%s --view: %s\n", app::kProgram, e.what());
            code = 2;
        }
        if (c) {
            auto cache = std::make_unique<gfx::SceneCache>(canvas.renderer().device());
            BandView view(*cache);
            const char* drvName = SDL_getenv("GD02_VIEW_DRIVER");
            chr::ClipDriver* drv = c->driver(drvName ? drvName : "main.drv");
            std::string clipName;
            const char* frameEnv = SDL_getenv("GD02_VIEW_FRAME");
            if (const char* cl = SDL_getenv("GD02_VIEW_CLIP"); cl && drv && drv->clipSet()) {
                scene::CharClipSamples* clip = drv->clipSet()->clip(cl);
                if (!clip && *cl >= '0' && *cl <= '9') clip = drv->clipSet()->lruByFlags(uint32_t(std::strtoul(cl, nullptr, 0)));  // flags
                if (!clip)
                    if (auto* g = drv->clipSet()->group(cl)) clip = drv->clipSet()->next(*g);
                if (clip) {
                    clipName = clip->name;
                    if (frameEnv) drv->playAt(clip, chr::kPlayNoBlend | chr::kPlayUserTime, float(std::atof(frameEnv)), 0, 0);
                    else drv->playClip(clip, chr::kPlayNoBlend | chr::kPlayLoop);
                } else {
                    SDL_Log("view: no clip %s in %s", cl, drv->clipSet()->path().c_str());
                }
            }
            // A guitarist's hands as the parsers' enter events leave them
            // (finger_open / strum_open), the fret hand at a neck spot
            // (GD02_VIEW_SPOT=<1..20>, default 5).
            if (!clipName.empty())
                for (const char* h : {"left_hand.drv", "right_hand.drv"})
                    if (chr::ClipDriver* hd = c->driver(h); hd && hd->clipSet())
                        if (auto* open = hd->clipSet()->clip(std::string(h) == "left_hand.drv" ? "finger_open" : "strum_open"))
                            hd->playClip(open, chr::kPlayNoBlend | chr::kPlayLoop);
            if (c->dir().get("fret.ik")) {
                char spot[64];
                std::snprintf(spot, sizeof spot, "spot_neck_fret%02d.mesh", SDL_getenv("GD02_VIEW_SPOT") ? std::atoi(SDL_getenv("GD02_VIEW_SPOT")) : 5);
                c->rig().ikMidi("fret.ik", spot, 0, 0.01, 0);
            }
            float yaw = SDL_getenv("GD02_VIEW_YAW") ? float(std::atof(SDL_getenv("GD02_VIEW_YAW"))) : clipName.empty() ? 180.0f : 90.0f;  // bind poses face +y, the clips -x
            float dist = SDL_getenv("GD02_VIEW_DIST") ? float(std::atof(SDL_getenv("GD02_VIEW_DIST"))) : 140.0f;
            const float height = SDL_getenv("GD02_VIEW_HEIGHT") ? float(std::atof(SDL_getenv("GD02_VIEW_HEIGHT"))) : 42.0f;
            SDL_Log("view: character %s (%s, %s): %d bones, %zu drivers, LODs %zu, clip %s", c->name().c_str(), path.c_str(), c->type().c_str(),
                    c->skeleton().size(), c->drivers().size(), c->own().lods.size(), clipName.empty() ? "(bind pose)" : clipName.c_str());
            double t = 0;
            uint64_t last = SDL_GetTicksNS();
            bool running = true, first = true;
            while (running) {
                SDL_Event e;
                while (SDL_PollEvent(&e)) {
                    if (e.type == SDL_EVENT_QUIT) running = false;
                    if (e.type != SDL_EVENT_KEY_DOWN) continue;
                    if (e.key.key == SDLK_ESCAPE) running = false;
                    if (e.key.key == SDLK_LEFT) yaw -= 15;
                    if (e.key.key == SDLK_RIGHT) yaw += 15;
                    if (e.key.key == SDLK_UP) dist = std::max(20.0f, dist * 0.85f);
                    if (e.key.key == SDLK_DOWN) dist *= 1.15f;
                }
                const uint64_t now = SDL_GetTicksNS();
                const double dt = first ? 0 : std::min(0.1, double(now - last) / 1e9);
                last = now;
                t += dt;
                chr::RigContext ctx;
                ctx.beat = t * 2.0, ctx.dBeats = float(dt * 2.0), ctx.dSeconds = float(dt), ctx.seconds = t;
                if (drv && !clipName.empty()) c->poll(ctx);
                first = false;
                // The camera on a circle around the character, looking at its chest.
                const float a = yaw * 3.14159265f / 180.0f;
                gfx::Camera cam = gfx::Camera::lookAt({std::sin(a) * dist, -std::cos(a) * dist, height}, {0, 0, height});
                cam.nearPlane = 1, cam.farPlane = 5000, cam.yFov = 0.8f;
                canvas.begin({40, 40, 48, 255});
                gfx::Renderer& r = canvas.renderer();
                int vx, vy, vw, vh;
                r.pictureRect(vx, vy, vw, vh);
                cam.aspect = float(vw) / float(std::max(1, vh));
                gfx::LightEnv env;
                env.ambient = {1, 1, 1};
                env.envAmbient = {0.35f, 0.35f, 0.38f};
                env.lightCount = 1;
                env.lights[0].vec = gfx::Vec3{std::sin(a) * 0.5f, -std::cos(a) * 0.8f, 0.6f};
                env.lights[0].color = {0.9f, 0.9f, 0.85f};
                r.materials().setFrame(cam, env, vw, vh);
                view.resetStats();
                view.drawCharacter(r.materials(), *c, cam, false);
                if (!SDL_getenv("GD02_VIEW_CLEAN")) {
                    char line[256];
                    std::snprintf(line, sizeof line, "%s  %s  LOD %d  %d meshes  clip %s  t %.1f", c->name().c_str(), c->type().c_str(), c->lod(),
                                  view.meshesDrawn(), clipName.empty() ? "-" : clipName.c_str(), t);
                    canvas.flush();
                    canvas.text(12, 8, 1.5f, {235, 235, 240, 255}, line);
                }
                if (captureDebugScreenshot(canvas, true, t * 1000.0)) running = false;
                canvas.end();
            }
        }
    }
    canvas.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return code;
}
