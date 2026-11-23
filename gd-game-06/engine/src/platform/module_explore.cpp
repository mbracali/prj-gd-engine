// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// gd06's model explorer for the Guitar Dream Universal Launcher's Explore
// screen (gd_game_explore, gd-engine-launcher/sdk/gd_host.h): the game's band
// members and instruments, loaded with the game's own .skin/.pak loaders
// (data/band.h) and drawn at their bind pose through the game's band renderer
// (render/band_view.h) with a brighter portrait fill. The launcher draws the
// list and drives the orbit camera; this renders the model into the viewer's
// region of the shared back buffer and never presents the frame.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "data/band.h"
#include "data/disc.h"
#include "data/venue.h"
#include "gd_host.h"
#include "gd_module_shim.h"
#include "render/band_anim.h"
#include "render/band_view.h"
#include "render/renderer.h"
#include "render/venue_camera.h"
#include "render/venue_lights.h"
#include "script/globals.h"

namespace {

using venue::Quat;
using venue::Vec3;

Vec3 sub(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec3 add(const Vec3& a, const Vec3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec3 scale(const Vec3& a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
float dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3 cross(const Vec3& a, const Vec3& b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
Vec3 norm(const Vec3& a) {
    const float l = std::sqrt(std::max(1e-12f, dot(a, a)));
    return {a[0] / l, a[1] / l, a[2] / l};
}

// A quaternion (x,y,z,w) for a camera whose local +z maps to `fwd`, +y to `up`
// and -x to `right` (render/band_view.cpp's CamPose convention). The rotation's
// columns are therefore [-right, up, fwd].
Quat quatFor(const Vec3& right, const Vec3& up, const Vec3& fwd) {
    const float R[3][3] = {{-right[0], up[0], fwd[0]}, {-right[1], up[1], fwd[1]}, {-right[2], up[2], fwd[2]}};
    const float tr = R[0][0] + R[1][1] + R[2][2];
    float x, y, z, w;
    if (tr > 0) {
        float s = std::sqrt(tr + 1.0f) * 2;
        w = 0.25f * s, x = (R[2][1] - R[1][2]) / s, y = (R[0][2] - R[2][0]) / s, z = (R[1][0] - R[0][1]) / s;
    } else if (R[0][0] > R[1][1] && R[0][0] > R[2][2]) {
        float s = std::sqrt(1.0f + R[0][0] - R[1][1] - R[2][2]) * 2;
        w = (R[2][1] - R[1][2]) / s, x = 0.25f * s, y = (R[0][1] + R[1][0]) / s, z = (R[0][2] + R[2][0]) / s;
    } else if (R[1][1] > R[2][2]) {
        float s = std::sqrt(1.0f + R[1][1] - R[0][0] - R[2][2]) * 2;
        w = (R[0][2] - R[2][0]) / s, x = (R[0][1] + R[1][0]) / s, y = 0.25f * s, z = (R[1][2] + R[2][1]) / s;
    } else {
        float s = std::sqrt(1.0f + R[2][2] - R[0][0] - R[1][1]) * 2;
        w = (R[1][0] - R[0][1]) / s, x = (R[0][2] + R[2][0]) / s, y = (R[1][2] + R[2][1]) / s, z = 0.25f * s;
    }
    return {x, y, z, w};
}

std::string label(const band::Member& m) {
    std::string s = m.profileName.empty() ? band::roleName(m.role) : m.profileName;
    if (m.outfit > 1 || m.style > 1) s += " #" + std::to_string(m.outfit) + "-" + std::to_string(m.style);
    return s;
}

struct Item {
    std::string group, name, detail;
    bool instrument = false;
    size_t idx = 0;  // index into bodies_ or instruments_
};

class Explorer {
public:
    bool open(const GdHost* host);
    void close();
    int count() const { return int(items_.size()); }
    const Item& item(int i) const { return items_[size_t(i)]; }
    int select(int index, char* info, int infoSize);
    void draw(const GdExploreView& v);

private:
    SDL_Window* window_ = nullptr;
    std::unique_ptr<Disc> disc_;
    std::unique_ptr<Globals> globals_;
    std::shared_ptr<band::Library> lib_;
    gfx::Renderer renderer_;
    std::vector<band::Member> bodies_, instruments_;
    std::vector<Item> items_;

    std::unique_ptr<band::Model> model_;
    std::unique_ptr<BandView> view_;
    std::vector<bandanim::Mat34> skin_;
    int memberId_ = -1;
    Vec3 center_{};
    float radius_ = 1;
};

bool Explorer::open(const GdHost* host) {
    window_ = static_cast<SDL_Window*>(host->window);
    const std::string disc = host->argc > 0 ? host->argv[host->argc - 1] : "";
    try {
        std::string error;
        disc_ = Disc::open(disc, error);
        if (!disc_) {
            SDL_Log("explore: %s", error.c_str());
            return false;
        }
        disc_->loadNames();
        globals_ = std::make_unique<Globals>();
        globals_->load(*disc_);
        lib_ = band::Library::shared(*disc_);
    } catch (const std::exception& e) {
        SDL_Log("explore: %s", e.what());
        return false;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) return false;
    // A brighter portrait fill so the models read well on the dark viewer (the
    // band renderer reads these once; the game process is separate).
    gdshim_setenv("GD06_BAND_FILL", "0.5");
    gdshim_setenv("GD06_BAND_EXPOSURE", "1.2");
    gfx::prepareWindow();
    std::string error;
    if (!renderer_.init(window_, error)) {
        SDL_Log("explore: cannot start the renderer: %s", error.c_str());
        return false;
    }
    try {
        bodies_ = band::everyBody(*globals_);
        instruments_ = band::everyInstrument(*globals_);
    } catch (const std::exception& e) {
        SDL_Log("explore: %s", e.what());
    }
    for (size_t i = 0; i < bodies_.size(); ++i)
        items_.push_back({"Characters", label(bodies_[i]), band::roleName(bodies_[i].role), false, i});
    for (size_t i = 0; i < instruments_.size(); ++i)
        items_.push_back({"Instruments", label(instruments_[i]), band::roleName(instruments_[i].role), true, i});
    SDL_Log("explore: %s: %d models (%zu bodies, %zu instruments)", disc.c_str(), count(), bodies_.size(), instruments_.size());
    return true;
}

void Explorer::close() {
    gdshim_make_current();
    view_.reset();
    model_.reset();
    skin_.clear();
    renderer_.shutdown();
    lib_.reset();
    globals_.reset();
    disc_.reset();
}

int Explorer::select(int index, char* info, int infoSize) {
    gdshim_make_current();
    view_.reset();
    model_.reset();
    skin_.clear();
    memberId_ = -1;
    std::string result;
    bool ok = false;
    if (index >= 0 && index < count()) {
        const Item& it = items_[size_t(index)];
        const band::Member& m = it.instrument ? instruments_[it.idx] : bodies_[it.idx];
        try {
            std::string error;
            model_ = band::loadModel(*disc_, *globals_, *lib_, m, "", error);
            if (!model_ || !model_->skeleton) throw std::runtime_error(error.empty() ? "no model" : error);
            skin_.assign(model_->skeleton->bones.size(), bandanim::Mat34{});  // the bind pose
            float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
            for (const band::Part& p : model_->parts)
                for (const scene::Mesh& mesh : p.scene.meshes)
                    for (size_t k = 0; k < mesh.positions.size(); ++k)
                        for (int a = 0; a < 3; ++a) {
                            const float c = mesh.positions[k][a];
                            lo[a] = std::min(lo[a], c), hi[a] = std::max(hi[a], c);
                        }
            if (lo[0] > hi[0]) throw std::runtime_error("the model has no vertices");
            center_ = {(lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f};
            const Vec3 d = {hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]};
            radius_ = std::max(0.4f, 0.5f * std::sqrt(dot(d, d)));
            view_ = std::make_unique<BandView>();
            memberId_ = view_->add(renderer_.device(), *model_, error);
            if (memberId_ < 0) throw std::runtime_error(error.empty() ? "the band renderer rejected the model" : error);
            char line[128];
            std::snprintf(line, sizeof line, "%zu parts, %zu textures", model_->parts.size(), model_->textures.size());
            result = line;
            ok = true;
        } catch (const std::exception& e) {
            result = e.what();
            view_.reset();
            model_.reset();
        }
    } else {
        result = "no such model";
    }
    if (info && infoSize > 0) SDL_snprintf(info, size_t(infoSize), "%s", result.c_str());
    return ok ? 0 : 1;
}

void Explorer::draw(const GdExploreView& v) {
    if (!gdshim_make_current()) return;
    const float clear[4] = {v.background[0], v.background[1], v.background[2], 1};
    renderer_.beginFrame(clear);
    gfx::RenderDevice& dev = renderer_.device();
    int dw = 0, dh = 0, ww = 0, wh = 0;
    dev.drawableSize(dw, dh);
    SDL_GetWindowSizeInPixels(window_, &ww, &wh);
    dev.setViewport(0, 0, dw, dh);
    dev.clear(clear, true);
    if (!model_ || !view_ || memberId_ < 0 || dw <= 0 || dh <= 0 || ww <= 0 || wh <= 0) {
        renderer_.endFrame();
        return;
    }
    // The viewer's region in drawable pixels (GL's origin is bottom-left).
    const float sx = float(dw) / float(ww), sy = float(dh) / float(wh);
    const int rw = std::max(1, int(float(v.w) * sx)), rh = std::max(1, int(float(v.h) * sy));
    const int rx = int(float(v.x) * sx);
    const int ry = std::max(0, dh - int(float(v.y) * sy) - rh);
    dev.setViewport(rx, ry, rw, rh);
    const float aspect = float(rw) / float(rh);

    // An orbit camera framing the model in the region. yaw 0 = the model's front (+z).
    const float fovDeg = 42.0f;
    const float hf = fovDeg * 3.14159265f / 180.0f;
    const float tanH = std::tan(hf * 0.5f);
    const float tanV = tanH / aspect;
    const float fit = std::min(tanH, tanV) * 0.9f;
    const float dist = radius_ / std::sin(std::atan(fit)) * std::max(0.05f, v.zoom);
    const float yaw = v.yaw, pitch = std::clamp(v.pitch, -1.45f, 1.45f);
    const Vec3 dir = {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
    Vec3 eye = add(center_, scale(dir, dist));
    Vec3 fwd = norm(sub(center_, eye));
    const Vec3 wup = std::fabs(fwd[1]) > 0.98f ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
    Vec3 right = norm(cross(fwd, wup));
    Vec3 up = cross(right, fwd);
    const Vec3 pan = add(scale(right, v.pan_x * radius_), scale(up, v.pan_y * radius_));
    eye = add(eye, pan);
    const Vec3 target = add(center_, pan);
    fwd = norm(sub(target, eye));
    right = norm(cross(fwd, wup));
    up = cross(right, fwd);

    venue::CamPose cam;
    cam.pos = eye;
    cam.fov = fovDeg;
    cam.rot = quatFor(right, up, fwd);

    view_->setView(aspect, false);
    view_->setTime(v.seconds);
    venue::Venue empty;
    venue::LightShow show;
    std::vector<venue::LightState> none;
    BandView::Instance in;
    in.member = memberId_;
    in.skin = &skin_;
    in.root = bandanim::Mat34{};
    view_->draw(cam, {in}, empty, none, show);
    renderer_.endFrame();
}

// ---- The C entry points ----

Explorer* self(void* p) { return static_cast<Explorer*>(p); }
int itemCount(void* p) { return self(p)->count(); }
int itemAt(void* p, int index, GdExploreItem* out) {
    if (!out || index < 0 || index >= self(p)->count()) return 1;
    const Item& it = self(p)->item(index);
    out->group = it.group.c_str(), out->name = it.name.c_str(), out->detail = it.detail.c_str();
    return 0;
}
int selectItem(void* p, int index, char* info, int infoSize) { return self(p)->select(index, info, infoSize); }
void drawView(void* p, const GdExploreView* v) {
    if (v) self(p)->draw(*v);
}
void closeExplorer(void* p) {
    self(p)->close();
    delete self(p);
    gdshim_end();
}

GdExplorer g_api;

}  // namespace

extern "C" GD_MODULE_EXPORT const GdExplorer* gd_game_explore(const GdHost* host) {
    if (!host || host->abi_version != GD_HOST_ABI_VERSION || !host->window) return nullptr;
    gdshim_begin(host);
    gdshim_set_exploring(true);
    auto* ex = new Explorer;
    if (!ex->open(host)) {
        ex->close();
        delete ex;
        gdshim_end();
        return nullptr;
    }
    if (host->log) host->log(host->log_user, "gd06 model explorer opened");
    g_api = GdExplorer{};
    g_api.size = sizeof(GdExplorer);
    g_api.self = ex;
    g_api.item_count = &itemCount;
    g_api.item = &itemAt;
    g_api.select = &selectItem;
    g_api.draw = &drawView;
    g_api.close = &closeExplorer;
    return &g_api;
}
