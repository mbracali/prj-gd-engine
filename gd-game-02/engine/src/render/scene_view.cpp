// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/scene_view.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <vector>

#include "app_ids.h"
#include "data/game_data.h"
#include "devaids/screenshot.h"
#include "render/canvas.h"
#include "render/scene_gpu.h"
#include "render/ui_view.h"
#include "scene/milo.h"

namespace {

using gfx::Mat4;
using gfx::Vec3;

using gfx::toMat4;
using gfx::transOf;

// An object's world transform: its local one under its parent's world when
// the parent is in this scene, else the world transform stored with it.
scene::Xfm worldXfm(const scene::Object* o, int depth = 0) {
    const scene::Trans* t = transOf(o);
    if (!t) return {};
    if (!t->parent.empty() && depth < 32 && o->dir)
        if (const scene::Object* p = o->dir->resolve(t->parent); p && p != o && transOf(p))
            return scene::compose(t->local, worldXfm(p, depth + 1));
    return t->world;
}

struct Drawn {
    gfx::GpuMesh gpu;  // the cached geometry (skinned per its bones)
    gfx::Material mat;
    Mat4 bones[4];
    int boneCount = 1;
    Vec3 center;
    bool blended = false;
};

class SceneView {
public:
    int run(const std::string& dataPath, const std::string& miloPath);

private:
    void build();
    void environment();
    void frame();

    Canvas canvas_;
    std::unique_ptr<GameData> data_;
    std::unique_ptr<scene::Milo> milo_;
    std::string miloPath_;
    std::unique_ptr<gfx::SceneCache> cache_;
    std::vector<Drawn> drawn_;
    std::vector<scene::Cam*> cams_;
    size_t cam_ = 0;
    gfx::Camera autoCam_;
    gfx::LightEnv env_;
    std::string envName_;
    // A UI panel (PanelDir): drawn with its components (render/ui_view.h).
    std::unique_ptr<scene::MiloLibrary> library_;
    scene::UiStyles styles_;
    scene::Locale locale_;
    std::unique_ptr<gfx::UiView> ui_;
    void startUi();
};

// GD02_VIEW_LANG=eng|fre|deu|ita|esl picks the locale, GD02_VIEW_FOCUS=<button>
// the focused component, GD02_VIEW_UI=0 draws a PanelDir as a plain scene.
void SceneView::startUi() {
    const char* off = SDL_getenv("GD02_VIEW_UI");
    if (milo_->root().cls != "PanelDir" || (off && std::string(off) == "0")) return;
    try {
        library_ = std::make_unique<scene::MiloLibrary>(*data_);
        styles_.load(*data_);
        const char* lang = SDL_getenv("GD02_VIEW_LANG");
        locale_.load(*data_, lang ? lang : "eng");
        ui_ = std::make_unique<gfx::UiView>(*data_, *library_, styles_, locale_, *cache_);
        ui_->load(miloPath_);
        if (const char* f = SDL_getenv("GD02_VIEW_FOCUS")) ui_->setFocus(f);
        env_ = ui_->environment();
        envName_ = "panel";
    } catch (const std::exception& e) {
        SDL_Log("view: UI panel: %s", e.what());
        ui_.reset();
    }
}

void SceneView::build() {
    Vec3 lo, hi;
    for (scene::Mesh* m : milo_->root().all<scene::Mesh>()) {
        if (!m->decoded || !m->draw.showing) continue;
        const gfx::GpuMesh* geomGpu = cache_->mesh(m);
        if (!geomGpu) continue;
        const scene::Mesh* geom = m;
        if (m->verts.empty() && !m->geomOwner.empty() && m->dir)
            if (auto* owner = m->dir->resolve<scene::Mesh>(m->geomOwner)) geom = owner;

        Drawn d;
        d.gpu = *geomGpu;
        d.gpu.skinned = false;

        const Mat4 world = toMat4(worldXfm(m));
        d.bones[0] = world;
        if (!geom->bones.empty()) {
            d.gpu.skinned = true;
            d.boneCount = int(std::min<size_t>(geom->bones.size(), 4));
            for (int i = 0; i < d.boneCount; ++i) {
                const auto& bone = geom->bones[size_t(i)];
                const scene::Object* bo = m->dir ? m->dir->resolve(bone.name) : nullptr;
                d.bones[i] = bo && transOf(bo) ? toMat4(scene::compose(bone.offset, worldXfm(bo))) : world;
            }
        }

        if (auto* mat = m->dir ? m->dir->resolve<scene::Mat>(m->mat) : nullptr) d.mat = cache_->material(*mat);
        d.blended = d.mat.blend != gfx::MatBlend::Src;

        Vec3 sum;
        for (const auto& v : geom->verts) sum = sum + Vec3{v.pos[0], v.pos[1], v.pos[2]};
        d.center = world.transformPoint(sum * (1.0f / float(geom->verts.size())));
        drawn_.push_back(d);
    }
    // A camera that frames most meshes (their centres between the 10th and
    // 90th percentile on each axis, so a sky dome does not set the size),
    // from the front (-y) and a little above.
    if (!drawn_.empty()) {
        auto pct = [&](float Vec3::*axis, float q) {
            std::vector<float> v;
            for (const Drawn& d : drawn_) v.push_back(d.center.*axis);
            std::sort(v.begin(), v.end());
            return v[size_t(q * float(v.size() - 1))];
        };
        lo = {pct(&Vec3::x, 0.1f), pct(&Vec3::y, 0.1f), pct(&Vec3::z, 0.1f)};
        hi = {pct(&Vec3::x, 0.9f), pct(&Vec3::y, 0.9f), pct(&Vec3::z, 0.9f)};
    }
    const Vec3 center = (lo + hi) * 0.5f;
    const float radius = std::max(1.0f, std::sqrt(gfx::dot(hi - lo, hi - lo)) * 0.5f);
    autoCam_ = gfx::Camera::lookAt(center + Vec3{0, -1.8f * radius, 0.5f * radius}, center);
    autoCam_.nearPlane = radius * 0.01f;
    autoCam_.farPlane = radius * 50;
    for (scene::Cam* c : milo_->root().all<scene::Cam>())
        if (c->decoded) cams_.push_back(c);
}

void SceneView::environment() {
    scene::Environ* e = nullptr;
    for (scene::Environ* x : milo_->root().all<scene::Environ>())
        if (x->decoded) {
            e = x;
            break;
        }
    if (!e) return;
    envName_ = e->name;
    env_ = cache_->lightEnv(*e);
}

void SceneView::frame() {
    gfx::Camera cam = autoCam_;
    std::string camName = "auto";
    if (ui_ && cam_ == 0) {
        cam = ui_->camera();
        camName = ui_->cameraName();
    } else if (ui_ && cam_ - 1 < cams_.size()) {
        const scene::Cam* c = cams_[cam_ - 1];
        cam.world = toMat4(worldXfm(c));
        cam.yFov = c->yFov, cam.nearPlane = c->nearPlane, cam.farPlane = c->farPlane;
        camName = c->name;
    } else if (!ui_ && cam_ < cams_.size()) {
        const scene::Cam* c = cams_[cam_];
        cam.world = toMat4(worldXfm(c));
        cam.yFov = c->yFov, cam.nearPlane = c->nearPlane, cam.farPlane = c->farPlane;
        camName = c->name;
    }
    const SDL_Color clear = env_.fog ? SDL_Color{Uint8(env_.fogColor[0] * 255), Uint8(env_.fogColor[1] * 255), Uint8(env_.fogColor[2] * 255), 255}
                                     : ui_ ? SDL_Color{0, 0, 0, 255} : SDL_Color{24, 24, 30, 255};
    canvas_.begin(clear);
    gfx::Renderer& r = canvas_.renderer();
    int vx, vy, vw, vh;
    r.pictureRect(vx, vy, vw, vh);
    r.materials().setFrame(cam, env_, vw, vh);
    const Vec3 eye = cam.position();
    if (ui_) {
        ui_->draw(r.materials());
        char info[256];
        std::snprintf(info, sizeof info, "%s  camera %s  %d meshes, %d texts  locale %s", miloPath_.c_str(), camName.c_str(),
                      ui_->meshesDrawn(), ui_->textsDrawn(), locale_.language().c_str());
        if (!SDL_getenv("GD02_VIEW_CLEAN")) canvas_.text(12, 8, 1.5f, {235, 235, 240, 255}, info);
        return;
    }
    std::vector<const Drawn*> blended;
    for (const Drawn& d : drawn_) {
        if (d.blended) blended.push_back(&d);
        else r.materials().draw(d.gpu, d.bones, d.mat, {d.boneCount});
    }
    // Blended meshes far to near.
    std::sort(blended.begin(), blended.end(), [&](const Drawn* a, const Drawn* b) {
        const Vec3 da = a->center - eye, db = b->center - eye;
        return gfx::dot(da, da) > gfx::dot(db, db);
    });
    for (const Drawn* d : blended) r.materials().draw(d->gpu, d->bones, d->mat, {d->boneCount});

    char line[256];
    std::snprintf(line, sizeof line, "%s  camera %s (%zu/%zu, Left/Right)  %zu meshes  environ %s, %d lights", miloPath_.c_str(),
                  camName.c_str(), cams_.empty() ? 0 : cam_ + 1, cams_.size(), drawn_.size(), envName_.empty() ? "none" : envName_.c_str(),
                  env_.lightCount);
    canvas_.text(12, 8, 1.5f, {235, 235, 240, 255}, line);
}

int SceneView::run(const std::string& dataPath, const std::string& miloPath) {
    miloPath_ = miloPath;
    try {
        data_ = std::make_unique<GameData>(dataPath);
        milo_ = scene::loadMilo(*data_, miloPath);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "%s --view: %s\n", app::kProgram, ex.what());
        return 2;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "%s --view: cannot start video: %s\n", app::kProgram, SDL_GetError());
        return 2;
    }
    SDL_Window* window = SDL_CreateWindow(app::kWindowTitle, int(kScreenW), int(kScreenH), SDL_WINDOW_RESIZABLE | gfx::prepareWindow());
    std::string error;
    if (!window || !canvas_.init(window, error)) {
        std::fprintf(stderr, "%s --view: cannot open the window: %s\n", app::kProgram, window ? error.c_str() : SDL_GetError());
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 2;
    }
    cache_ = std::make_unique<gfx::SceneCache>(canvas_.renderer().device());
    build();
    environment();
    startUi();
    if (ui_)
        for (const std::string& p : ui_->problems()) SDL_Log("view: UI: %s", p.c_str());
    SDL_Log("view: %s: %d meshes, %d textures (%d not decodable), %d cameras, environ '%s' with %d %s lights",
            miloPath.c_str(), int(drawn_.size()), int(cache_->textureCount()), cache_->skippedTextures(), int(cams_.size()), envName_.c_str(), env_.lightCount,
            env_.directional ? "directional" : "point");
    if (const char* c = SDL_getenv("GD02_VIEW_CAM")) cam_ = size_t(std::atoi(c));  // development aid: start camera
    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = false;
            if (e.type != SDL_EVENT_KEY_DOWN) continue;
            const size_t n = cams_.size() + 1;  // the cameras, then the framing one (UI: the panel's camera first)
            if (e.key.key == SDLK_ESCAPE) running = false;
            if (e.key.key == SDLK_RIGHT) cam_ = (cam_ + 1) % n;
            if (e.key.key == SDLK_LEFT) cam_ = (cam_ + n - 1) % n;
        }
        frame();
        if (captureDebugScreenshot(canvas_, false, 0)) running = false;
        canvas_.end();
    }
    ui_.reset();
    cache_.reset();
    canvas_.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

}  // namespace

int runSceneView(const std::string& dataPath, const std::string& miloPath) { return SceneView().run(dataPath, miloPath); }
