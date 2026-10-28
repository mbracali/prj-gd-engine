// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// gd02's model explorer for the Guitar Dream Universal Launcher's Explore
// models screen (gd_game_explore, gd-engine-launcher/sdk/gd_host.h): the
// disc's characters (posed by their own clips, as the character viewer does,
// render/band_view.h), crowd members, guitars and basses, drum kits, venue
// geometry and props (drawn as the scene viewer does, render/scene_view.h),
// through GH2's materials and standard shader. The launcher draws the list
// and drives the camera; this draws the model and never presents the frame.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "char/char_driver.h"
#include "char/character.h"
#include "data/game_data.h"
#include "gd_explore_helpers.h"
#include "gd_host.h"
#include "gd_module_shim.h"
#include "render/band_view.h"
#include "render/renderer.h"
#include "render/scene_gpu.h"
#include "scene/milo.h"
#include "scene/world_objects.h"

namespace {

using gfx::Mat4;
using gfx::toMat4;
using gfx::transOf;
using gfx::Vec3;

constexpr float kFov = 0.8f;

// "lp_dbl_white" -> "Lp Dbl White", "alterna1" -> "Alterna 1".
std::string pretty(const std::string& id) {
    std::string out;
    bool start = true;
    for (size_t i = 0; i < id.size(); ++i) {
        char c = id[i];
        if (c == '_' || c == '-') {
            out += ' ', start = true;
            continue;
        }
        const bool digit = c >= '0' && c <= '9';
        if (digit && i > 0 && !(id[i - 1] >= '0' && id[i - 1] <= '9') && id[i - 1] != '_') out += ' ';
        out += start && c >= 'a' && c <= 'z' ? char(c - 'a' + 'A') : c;
        start = false;
    }
    return out;
}

std::string stem(const std::string& path) {
    std::string f = path.substr(path.rfind('/') + 1);
    return f.substr(0, f.find('.'));
}

bool endsWith(const std::string& s, const std::string& end) { return s.size() >= end.size() && s.compare(s.size() - end.size(), end.size(), end) == 0; }

// An object's world transform (its local one under its parent's world when
// the parent is in the scene), as the scene viewer computes it.
scene::Xfm worldXfm(const scene::Object* o, int depth = 0) {
    const scene::Trans* t = transOf(o);
    if (!t) return {};
    if (!t->parent.empty() && depth < 32 && o->dir)
        if (const scene::Object* p = o->dir->resolve(t->parent); p && p != o && transOf(p))
            return scene::compose(t->local, worldXfm(p, depth + 1));
    return t->world;
}

enum class Kind { Character, Static };

struct Item {
    std::string group, name, path;
    Kind kind;
    bool venue = false;   // frames by the meshes' centres (a sky or a floor does not set the size)
    bool guitar = false;  // stood up (gdexplore::uprightRows)
};

struct Drawn {
    gfx::GpuMesh gpu;
    gfx::Material mat;
    Mat4 bones[4];
    int boneCount = 1;
    Vec3 center;
    bool blended = false;
};

class Explorer {
public:
    bool open(const GdHost* host);
    void close();
    int count() const { return int(items_.size()); }
    const Item& item(int i) const { return items_[size_t(i)]; }
    int select(int index, char* info, int infoSize);
    int clipCount() const { return int(clips_.size()); }
    const char* clipName(int i) const { return i >= 0 && i < clipCount() ? clipNames_[size_t(i)].c_str() : ""; }
    void draw(const GdExploreView& v);
    int hasInstrument() const { return char_ && instrument_ ? 1 : 0; }
    void showInstrument(bool shown);

private:
    void reloadCharacter();
    void unload();
    void list();
    std::string loadStatic(const Item& it);
    std::string loadCharacter(const Item& it);
    void playClip(int clip);

    const GdHost* host_ = nullptr;
    SDL_Window* window_ = nullptr;
    std::unique_ptr<GameData> data_;
    gfx::Renderer renderer_;
    std::vector<Item> items_;

    // The model shown.
    std::unique_ptr<gfx::SceneCache> cache_;
    std::unique_ptr<scene::Milo> milo_;
    std::vector<Drawn> drawn_;
    std::unique_ptr<chr::ClipLibrary> clipLib_;
    std::unique_ptr<chr::Character> char_;
    std::unique_ptr<BandView> band_;
    std::vector<scene::CharClipSamples*> clips_;
    std::vector<std::string> clipNames_;
    int clipPlaying_ = -2;
    int selected_ = -1;
    bool showInstrument_ = true;  // the launcher's "Show instrument"
    bool instrument_ = false;     // the selected character has a guitar, bass or drum kit
    float lastSeconds_ = 0;
    double animTime_ = 0;
    gfx::LightEnv env_;
    bool ownEnv_ = false;  // the scene's Environ (else a light from the camera)
    Vec3 center_;
    float radius_ = 1;
    float frontYaw_ = 0;  // the model's front, radians
};

bool Explorer::open(const GdHost* host) {
    host_ = host;
    window_ = static_cast<SDL_Window*>(host->window);
    const std::string disc = host->argc > 0 ? host->argv[host->argc - 1] : "";
    try {
        data_ = std::make_unique<GameData>(disc);
    } catch (const std::exception& e) {
        SDL_Log("explore: %s", e.what());
        return false;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) return false;
    gfx::prepareWindow();  // the GL attributes of gd02's context
    std::string error;
    if (!renderer_.init(window_, error)) {
        SDL_Log("explore: cannot start the renderer: %s", error.c_str());
        return false;
    }
    list();
    SDL_Log("explore: %s: %d models", disc.c_str(), count());
    return true;
}

void Explorer::list() {
    std::vector<std::string> paths = data_->paths();
    std::sort(paths.begin(), paths.end());
    std::vector<Item> chars, crowd, guitars, drums, venues, props;
    for (const std::string& p : paths) {
        if (!endsWith(p, ".milo_xbox")) continue;
        const std::string s = stem(p);
        // char/<c>/ng/gen/<c>.milo_xbox: a band member (not the menus' _ui copies).
        if (p.rfind("char/", 0) == 0 && p.find("/ng/gen/") != std::string::npos && p.find("/anims/") == std::string::npos) {
            const std::string folder = p.substr(5, p.find('/', 5) - 5);
            if (folder == "crowd") {
                if (s.rfind("crowd_", 0) == 0) crowd.push_back({"Crowd", pretty(s.substr(6)), p, Kind::Character});
            } else if (folder != "ng" && folder != "shared" && s == folder) {
                chars.push_back({"Characters", pretty(s), p, Kind::Character});
            }
        } else if (p.rfind("char/ng/guitars/gen/", 0) == 0) {
            Item it{"Guitars & basses", pretty(s), p, Kind::Static};
            it.guitar = true;
            guitars.push_back(it);
        } else if (p.rfind("char/ng/drums/gen/", 0) == 0) {
            drums.push_back({"Drum kits", pretty(s.rfind("dw_", 0) == 0 ? s.substr(3) : s), p, Kind::Static});
        } else if (p.rfind("world/", 0) == 0 && p.find("/ng/") != std::string::npos) {
            const std::string venue = p.substr(6, p.find('/', 6) - 6);
            if (endsWith(s, "_lighting")) continue;
            if (s.rfind(venue + "_geom", 0) == 0 && s.find("proxy") == std::string::npos && s.find("flashpot") == std::string::npos) {
                const std::string extra = s.substr(venue.size() + 5);
                Item it{"Venues", pretty(venue) + (extra == "_mp" ? " (face-off)" : extra.empty() ? "" : " (" + pretty(extra.substr(1)) + ")"), p,
                        Kind::Static};
                it.venue = true;
                venues.push_back(it);
            } else {
                props.push_back({"Venue props", pretty(venue) + ": " + pretty(s), p, Kind::Static});
            }
        }
    }
    for (auto* g : {&chars, &crowd, &guitars, &drums, &venues, &props}) items_.insert(items_.end(), g->begin(), g->end());
}

void Explorer::unload() {
    band_.reset();
    char_.reset();
    clipLib_.reset();
    drawn_.clear();
    milo_.reset();
    cache_.reset();
    clips_.clear();
    clipNames_.clear();
    clipPlaying_ = -2;
    instrument_ = false;
}

void Explorer::close() {
    gdshim_make_current();
    unload();
    renderer_.shutdown();
    data_.reset();
}

int Explorer::select(int index, char* info, int infoSize) {
    gdshim_make_current();
    unload();
    std::string result;
    bool ok = false;
    selected_ = index;
    if (index >= 0 && index < count()) {
        const Item& it = items_[size_t(index)];
        try {
            cache_ = std::make_unique<gfx::SceneCache>(renderer_.device());
            result = it.kind == Kind::Character ? loadCharacter(it) : loadStatic(it);
            ok = true;
        } catch (const std::exception& e) {
            result = e.what();
            unload();
        }
    } else {
        result = "no such model";
    }
    if (info && infoSize > 0) SDL_snprintf(info, size_t(infoSize), "%s", result.c_str());
    return ok ? 0 : 1;
}

std::string Explorer::loadStatic(const Item& it) {
    milo_ = scene::loadMilo(*data_, it.path);
    // A guitar stands up, its front to the viewer.
    scene::Xfm place;
    if (it.guitar) {
        std::vector<gdexplore::Vec> pts;
        for (scene::Mesh* m : milo_->root().all<scene::Mesh>()) {
            if (!m->decoded || !m->draw.showing || m->verts.empty()) continue;
            const Mat4 world = toMat4(worldXfm(m));
            for (const auto& v : m->verts) {
                const Vec3 p = world.transformPoint(Vec3{v.pos[0], v.pos[1], v.pos[2]});
                pts.push_back({p.x, p.y, p.z});
            }
        }
        // GH2's guitars hang from the hand bone with the front up (+z) and the headstock towards +x -y.
        place.rot = gdexplore::uprightRows(pts, {1, -1, 0}, {0, 0, 1});
    }
    auto placed = [&](const scene::Xfm& x) { return toMat4(scene::compose(x, place)); };
    Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    for (scene::Mesh* m : milo_->root().all<scene::Mesh>()) {
        if (!m->decoded || !m->draw.showing) continue;
        const gfx::GpuMesh* geomGpu = cache_->mesh(m);
        if (!geomGpu) continue;
        const scene::Mesh* geom = m;
        if (m->verts.empty() && !m->geomOwner.empty() && m->dir)
            if (auto* owner = m->dir->resolve<scene::Mesh>(m->geomOwner)) geom = owner;
        if (geom->verts.empty()) continue;
        Drawn d;
        d.gpu = *geomGpu;
        d.gpu.skinned = false;
        const Mat4 world = placed(worldXfm(m));
        d.bones[0] = world;
        if (!geom->bones.empty()) {
            d.gpu.skinned = true;
            d.boneCount = int(std::min<size_t>(geom->bones.size(), 4));
            for (int i = 0; i < d.boneCount; ++i) {
                const auto& bone = geom->bones[size_t(i)];
                const scene::Object* bo = m->dir ? m->dir->resolve(bone.name) : nullptr;
                d.bones[i] = bo && transOf(bo) ? placed(scene::compose(bone.offset, worldXfm(bo))) : world;
            }
        }
        if (auto* mat = m->dir ? m->dir->resolve<scene::Mat>(m->mat) : nullptr) d.mat = cache_->material(*mat);
        d.blended = d.mat.blend != gfx::MatBlend::Src;
        Vec3 sum;
        for (const auto& v : geom->verts) {
            const Vec3 p = world.transformPoint(Vec3{v.pos[0], v.pos[1], v.pos[2]});
            sum = sum + p;
            lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
            hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
        }
        d.center = sum * (1.0f / float(geom->verts.size()));
        drawn_.push_back(d);
    }
    if (drawn_.empty()) throw std::runtime_error("nothing in " + it.path + " can be drawn");
    if (it.venue && drawn_.size() > 4) {
        // The meshes' centres between the 10th and 90th percentile on each axis.
        auto pct = [&](float Vec3::*axis, float q) {
            std::vector<float> v;
            for (const Drawn& d : drawn_) v.push_back(d.center.*axis);
            std::sort(v.begin(), v.end());
            return v[size_t(q * float(v.size() - 1))];
        };
        lo = {pct(&Vec3::x, 0.1f), pct(&Vec3::y, 0.1f), pct(&Vec3::z, 0.1f)};
        hi = {pct(&Vec3::x, 0.9f), pct(&Vec3::y, 0.9f), pct(&Vec3::z, 0.9f)};
    }
    center_ = (lo + hi) * 0.5f;
    radius_ = std::max(1.0f, std::sqrt(gfx::dot(hi - lo, hi - lo)) * 0.5f);
    frontYaw_ = 0;
    ownEnv_ = false;
    for (scene::Environ* e : milo_->root().all<scene::Environ>())
        if (e->decoded) {
            env_ = cache_->lightEnv(*e);
            ownEnv_ = true;
            break;
        }
    char line[160];
    std::snprintf(line, sizeof line, "%zu meshes, %d textures%s", drawn_.size(), int(cache_->textureCount()), ownEnv_ ? ", its own lighting" : "");
    return line;
}

std::string Explorer::loadCharacter(const Item& it) {
    clipLib_ = std::make_unique<chr::ClipLibrary>(*data_);
    {
        // The outfits a venue gives (guitar_sg for guitarists, the arena kit for drummers).
        auto probe = std::make_unique<chr::Character>(*data_, it.path, *clipLib_);
        std::map<std::string, std::string> outfits;
        if (probe->dir().get("guitar.outfit")) outfits["guitar.outfit"] = "char/ng/guitars/gen/guitar_sg.milo_xbox";
        if (probe->dir().get("drums.outfit")) outfits["drums.outfit"] = "char/ng/drums/gen/dw_arena_drums.milo_xbox";
        probe.reset();
        instrument_ = !outfits.empty();
        if (!showInstrument_) outfits.clear();  // "Show instrument" off: the body alone
        char_ = std::make_unique<chr::Character>(*data_, it.path, *clipLib_, outfits);
    }
    band_ = std::make_unique<BandView>(*cache_);
    // Its main driver's clips, the idle ones first.
    if (chr::ClipDriver* drv = char_->mainDriver(); drv && drv->clipSet()) {
        for (scene::CharClipSamples* c : drv->clipSet()->clips())
            if (drv->clipSet()->alive(c)) clips_.push_back(c);
        std::stable_sort(clips_.begin(), clips_.end(), [](const scene::CharClipSamples* a, const scene::CharClipSamples* b) {
            const bool ia = a->name.find("idle") != std::string::npos, ib = b->name.find("idle") != std::string::npos;
            return ia != ib ? ia : a->name < b->name;
        });
        for (const scene::CharClipSamples* c : clips_) clipNames_.push_back(c->name);
    }
    // A standing figure: about 90 units tall, from its feet at the origin.
    center_ = {0, 0, 44};
    radius_ = 52;
    ownEnv_ = false;
    char line[200];
    std::snprintf(line, sizeof line, "%s, %d bones, %zu animations", char_->type().c_str(), char_->skeleton().size(), clips_.size());
    return line;
}

// The selected character again, in its rest pose (no clip played yet).
void Explorer::reloadCharacter() {
    if (selected_ < 0 || selected_ >= count() || items_[size_t(selected_)].kind != Kind::Character || !char_) return;
    band_.reset();
    char_.reset();
    clipLib_.reset();
    clips_.clear();
    clipNames_.clear();
    clipPlaying_ = -2;
    try {
        // A fresh cache, as select() makes: the old one's textures went with the character.
        cache_ = std::make_unique<gfx::SceneCache>(renderer_.device());
        loadCharacter(items_[size_t(selected_)]);
    } catch (const std::exception& e) {
        SDL_Log("explore: %s: %s", items_[size_t(selected_)].path.c_str(), e.what());
        band_.reset();
        char_.reset();
    }
}

void Explorer::showInstrument(bool shown) {
    if (shown == showInstrument_) return;
    showInstrument_ = shown;
    if (!gdshim_make_current()) return;
    reloadCharacter();
}

void Explorer::playClip(int clip) {
    clipPlaying_ = clip;
    animTime_ = 0;
    chr::ClipDriver* drv = char_ ? char_->mainDriver() : nullptr;
    if (!drv || clip < 0 || clip >= clipCount()) return;
    drv->playClip(clips_[size_t(clip)], chr::kPlayNoBlend | chr::kPlayLoop);
    // A guitarist's hands open (finger_open / strum_open), as the character viewer leaves them.
    for (const char* h : {"left_hand.drv", "right_hand.drv"})
        if (chr::ClipDriver* hd = char_->driver(h); hd && hd->clipSet())
            if (auto* open = hd->clipSet()->clip(std::string(h) == "left_hand.drv" ? "finger_open" : "strum_open"))
                hd->playClip(open, chr::kPlayNoBlend | chr::kPlayLoop);
}

void Explorer::draw(const GdExploreView& v) {
    if (!gdshim_make_current()) return;
    const float clear[4] = {v.background[0], v.background[1], v.background[2], 1};
    gfx::Renderer& r = renderer_;
    r.beginFrame(clear);
    // The whole window (no 16:9 bars): the launcher's panels cover its sides.
    int dw = 0, dh = 0, ww = 0, wh = 0;
    r.device().drawableSize(dw, dh);
    SDL_GetWindowSizeInPixels(window_, &ww, &wh);
    r.device().setViewport(0, 0, dw, dh);
    r.device().clear(clear, true);
    if ((!char_ && drawn_.empty()) || dw <= 0 || dh <= 0 || ww <= 0 || wh <= 0) {
        r.endFrame();
        return;
    }

    // Animation: the clip the launcher asks for, on a 120 beats a minute clock.
    float dt = std::clamp(v.seconds - lastSeconds_, 0.0f, 0.1f);
    if (v.seconds < lastSeconds_) dt = 0;
    lastSeconds_ = v.seconds;
    // Back to no clip (-1): the rest pose again, from a fresh character.
    if (char_ && v.clip < 0 && clipPlaying_ >= 0) reloadCharacter();
    if (char_ && v.clip != clipPlaying_) playClip(v.clip);
    if (char_ && clipPlaying_ >= 0) {
        animTime_ += dt;
        chr::RigContext ctx;
        ctx.beat = animTime_ * 2.0, ctx.dBeats = dt * 2.0f, ctx.dSeconds = dt, ctx.seconds = animTime_;
        char_->poll(ctx);
    }
    // Characters face +y (bind pose and clips, as the explorer poses them).
    frontYaw_ = char_ ? 3.1415927f : 0.0f;

    // The orbit camera, framing the model in the launcher's viewer area.
    const float sx = float(dw) / float(ww), sy = float(dh) / float(wh);
    const float rx = float(v.x) * sx, ry = float(v.y) * sy, rw = std::max(1.0f, float(v.w) * sx), rh = std::max(1.0f, float(v.h) * sy);
    const float aspect = float(dw) / float(dh);
    const float tanV = std::tan(kFov / 2), tanH = tanV * aspect;
    const float t = std::min(tanV * rh / float(dh), tanH * rw / float(dw)) * 0.9f;
    const float dist = radius_ / std::sin(std::atan(t)) * std::max(0.02f, v.zoom);
    const float yaw = frontYaw_ + v.yaw, pitch = std::clamp(v.pitch, -1.45f, 1.45f);
    const Vec3 offset{std::sin(yaw) * std::cos(pitch) * dist, -std::cos(yaw) * std::cos(pitch) * dist, std::sin(pitch) * dist};
    const Vec3 fwd = gfx::normalize(offset * -1.0f);
    const Vec3 right = gfx::normalize(gfx::cross(fwd, Vec3{0, 0, 1}));
    const Vec3 up = gfx::cross(right, fwd);
    const Vec3 target = center_ + (right * v.pan_x + up * v.pan_y) * radius_;
    // Moves the picture so the target lands in the middle of the viewer area.
    const float ncx = (rx + rw / 2) / float(dw) * 2 - 1, ncy = 1 - (ry + rh / 2) / float(dh) * 2;
    const Vec3 shift = right * (-ncx * dist * tanH) + up * (-ncy * dist * tanV);
    gfx::Camera cam = gfx::Camera::lookAt(target + offset + shift, target + shift);
    cam.yFov = kFov, cam.aspect = aspect;
    cam.nearPlane = std::max(0.05f, std::min(dist * 0.05f, radius_ * 0.01f));
    cam.farPlane = dist + radius_ * 40;

    gfx::LightEnv env = env_;
    if (!ownEnv_) {
        // A light from over the viewer's shoulder.
        env = gfx::LightEnv{};
        env.ambient = {1, 1, 1};
        env.envAmbient = {0.38f, 0.37f, 0.4f};
        env.lightCount = 1;
        env.lights[0].vec = gfx::normalize(offset * (1.0f / dist) + right * 0.4f + Vec3{0, 0, 0.5f});
        env.lights[0].color = {0.95f, 0.93f, 0.88f};
    }
    r.materials().setFrame(cam, env, dw, dh);
    if (char_) {
        band_->resetStats();
        band_->drawCharacter(r.materials(), *char_, cam, false);
    } else {
        gfx::DrawContext ctx;
        const Vec3 eye = cam.position();
        std::vector<const Drawn*> blended;
        for (const Drawn& d : drawn_) {
            if (d.blended) blended.push_back(&d);
            else ctx.bones = d.boneCount, r.materials().draw(d.gpu, d.bones, d.mat, ctx);
        }
        std::sort(blended.begin(), blended.end(), [&](const Drawn* a, const Drawn* b) {
            const Vec3 da = a->center - eye, db = b->center - eye;
            return gfx::dot(da, da) > gfx::dot(db, db);
        });
        for (const Drawn* d : blended) ctx.bones = d->boneCount, r.materials().draw(d->gpu, d->bones, d->mat, ctx);
    }
    r.endFrame();  // the shims keep it from presenting: the launcher does
}

// ---- The C entry points ----

Explorer* self(void* p) { return static_cast<Explorer*>(p); }

int itemCount(void* p) { return self(p)->count(); }

int itemAt(void* p, int index, GdExploreItem* out) {
    if (!out || index < 0 || index >= self(p)->count()) return 1;
    const Item& it = self(p)->item(index);
    out->group = it.group.c_str(), out->name = it.name.c_str(), out->detail = it.path.c_str();
    return 0;
}

int selectItem(void* p, int index, char* info, int infoSize) { return self(p)->select(index, info, infoSize); }
int clipCount(void* p) { return self(p)->clipCount(); }
int hasInstrument(void* p) { return self(p)->hasInstrument(); }
void showInstrument(void* p, int shown) { self(p)->showInstrument(shown != 0); }
const char* clipName(void* p, int index) { return self(p)->clipName(index); }
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
    if (host->log) host->log(host->log_user, "gd02 " GD02_VERSION " model explorer opened");
    g_api = GdExplorer{};
    g_api.size = sizeof(GdExplorer);
    g_api.self = ex;
    g_api.item_count = &itemCount;
    g_api.item = &itemAt;
    g_api.select = &selectItem;
    g_api.clip_count = &clipCount;
    g_api.clip_name = &clipName;
    g_api.has_instrument = &hasInstrument;
    g_api.show_instrument = &showInstrument;
    g_api.draw = &drawView;
    g_api.close = &closeExplorer;
    return &g_api;
}
