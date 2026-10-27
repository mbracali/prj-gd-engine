// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// gd01's model explorer for the Guitar Dream Universal Launcher's Explore
// models screen (gd_game_explore, ../../sdk/gd_host.h), written from here
// with gd-game-01's own loaders and renderer, unchanged (gd-game-01 is
// frozen): GameData reads the disc, Scene decodes the .rnd_ps2 files,
// SceneRenderer draws them, and Character poses the guitarists and the band
// with their clips. The launcher draws the list and drives the camera; this
// draws the model and never presents the frame (the shims keep
// SDL_GL_SwapWindow from presenting).
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "data/dta.h"
#include "data/game_data.h"
#include "gd_explore_helpers.h"
#include "gd_host.h"
#include "gd_module_shim.h"
#include "render/math.h"
#include "render/renderer.h"
#include "render/scene_renderer.h"
#include "scene/character.h"
#include "scene/scene.h"

namespace {

using V3 = std::array<float, 3>;

constexpr float kFov = 0.8f;
constexpr const char* kKeyLight = "gdul_key.lit";

V3 add(V3 a, V3 b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
V3 mul(V3 a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
V3 cross(V3 a, V3 b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
V3 normalize(V3 a) {
    const float l = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    return l > 0 ? mul(a, 1 / l) : V3{0, 0, 1};
}

// "hair_metal" -> "Hair Metal", "crowd_female01" -> "Crowd Female 01".
std::string pretty(const std::string& id) {
    std::string out;
    bool start = true;
    for (size_t i = 0; i < id.size(); ++i) {
        const char c = id[i];
        if (c == '_') {
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

bool endsWith(const std::string& s, const std::string& end) {
    return s.size() >= end.size() && s.compare(s.size() - end.size(), end.size(), end) == 0;
}

// The first symbol named *_ANIMS below `n` (as the venue reads band_chars.dta).
std::string animsMacro(const DataNode& n) {
    if (n.isText() && n.s.size() > 6 && n.s.compare(n.s.size() - 6, 6, "_ANIMS") == 0) return n.s;
    for (const DataNode& c : n.children)
        if (std::string m = animsMacro(c); !m.empty()) return m;
    return {};
}

struct Item {
    std::string group, name, path;
    bool character = false;
    bool venue = false;
    bool guitar = false;  // stood up (gdexplore::uprightRows)
    std::string folder;  // a character's charsys folder
};

Item makeItem(std::string group, std::string name, std::string path) {
    Item it;
    it.group = std::move(group), it.name = std::move(name), it.path = std::move(path);
    return it;
}

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

private:
    void list();
    void unload();
    std::string loadStatic(const Item& it);
    std::string loadCharacter(const Item& it);
    const SceneObject* lookup(const std::string& name) const;

    SDL_Window* window_ = nullptr;
    std::unique_ptr<GameData> data_;
    Renderer renderer_;
    bool rendererUp_ = false;
    std::vector<Item> items_;
    std::map<std::string, Character::Spec> bandSpecs_;  // band_chars.dta: folder -> spec

    std::unique_ptr<Scene> scene_;
    std::unique_ptr<SceneRenderer> sceneRenderer_;
    std::vector<std::string> roots_;
    std::unique_ptr<Character> char_;
    std::vector<int> clips_;
    std::vector<std::string> clipNames_;
    int clipPlaying_ = -2;
    float lastSeconds_ = 0;
    double animTime_ = 0;
    // A key light from over the viewer's shoulder, in the default environment.
    SceneObject keyLight_;
    EnvironObj env_;
    V3 center_{0, 0, 0};
    float radius_ = 1;
    Mat4 place_;  // applied after the objects' world transforms (a guitar stood up)
};

bool Explorer::open(const GdHost* host) {
    window_ = static_cast<SDL_Window*>(host->window);
    const std::string disc = host->argc > 0 ? host->argv[host->argc - 1] : "";
    try {
        data_ = std::make_unique<GameData>(disc);
    } catch (const std::exception& e) {
        SDL_Log("explore: %s", e.what());
        return false;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) return false;
    std::string error;
    if (!renderer_.init(window_, error)) {
        SDL_Log("explore: cannot start the renderer: %s", error.c_str());
        return false;
    }
    rendererUp_ = true;
    keyLight_.type = "Light", keyLight_.name = kKeyLight;
    keyLight_.light = std::make_unique<LightObj>();
    keyLight_.light->type = 1;
    keyLight_.light->color = {0.95f, 0.93f, 0.88f, 1};
    env_.lights = {kKeyLight};
    env_.ambient = {0.42f, 0.41f, 0.44f, 1};
    list();
    SDL_Log("explore: %s: %d models", disc.c_str(), count());
    return true;
}

void Explorer::list() {
    // The band members of band_chars.dta ((<role> (outfit (directory "charsys/<model>")) ... <X>_ANIMS)).
    try {
        DataNode chars = parseDtb(data_->read(dtbPath("charsys/band_chars.dta")));
        for (size_t k = 0; k + 1 < chars.children.size(); ++k) {
            if (chars.children[k].type != DataNode::Type::Define) continue;
            const DataNode& def = chars.children[k + 1];
            if (def.children.empty() || !def.children[0].isList()) continue;
            const DataNode& body = def.children[0];
            const DataNode* outfit = body.find("outfit");
            const DataNode* dir = outfit ? outfit->find("directory") : nullptr;
            if (!dir) continue;
            std::string folder = dir->textAt(1);
            folder = folder.substr(folder.rfind('/') + 1);
            bandSpecs_[folder] = Character::Spec{folder, "", "charsys/band_anims.dta", animsMacro(body), 1, 0, "", 1};
        }
    } catch (const std::exception& e) {
        SDL_Log("explore: band_chars.dta: %s", e.what());
    }
    std::vector<std::string> paths = data_->paths();
    std::sort(paths.begin(), paths.end());
    std::vector<Item> chars, band, crowd, guitars, venues;
    for (const std::string& p : paths) {
        if (!endsWith(p, ".rnd_ps2")) continue;
        const std::string s = stem(p);
        if (p.rfind("charsys/", 0) == 0) {
            const std::string folder = p.substr(8, p.find('/', 8) - 8);
            if (folder == "guitars") {
                Item it = makeItem("Guitars", pretty(s.rfind("guitar_", 0) == 0 ? s.substr(7) : s), p);
                it.guitar = true;
                guitars.push_back(it);
            } else if (folder == "crowd") {
                if (s.rfind("crowd_", 0) == 0) crowd.push_back(makeItem("Crowd", pretty(s.substr(6)), p));
            } else if (s == folder) {
                Item it = makeItem(bandSpecs_.count(folder) ? "Band" : "Guitarists", pretty(s), p);
                it.character = true, it.folder = folder;
                (bandSpecs_.count(folder) ? band : chars).push_back(it);
            }
        } else if (p.rfind("venues/", 0) == 0) {
            const std::string venue = p.substr(7, p.find('/', 7) - 7);
            if (s == venue) {
                Item it = makeItem("Venues", pretty(venue), p);
                it.venue = true;
                venues.push_back(it);
            }
        }
    }
    for (auto* g : {&chars, &band, &crowd, &guitars, &venues}) items_.insert(items_.end(), g->begin(), g->end());
}

const SceneObject* Explorer::lookup(const std::string& name) const {
    if (name == kKeyLight) return &keyLight_;
    return scene_ ? scene_->find(name) : nullptr;
}

void Explorer::unload() {
    char_.reset();
    sceneRenderer_.reset();
    scene_.reset();
    roots_.clear();
    place_ = Mat4{};
    clips_.clear();
    clipNames_.clear();
    clipPlaying_ = -2;
}

void Explorer::close() {
    gdshim_make_current();
    unload();
    if (rendererUp_) renderer_.shutdown();
    rendererUp_ = false;
    data_.reset();
}

int Explorer::select(int index, char* info, int infoSize) {
    gdshim_make_current();
    unload();
    std::string result;
    bool ok = false;
    if (index >= 0 && index < count()) {
        const Item& it = items_[size_t(index)];
        try {
            result = it.character ? loadCharacter(it) : loadStatic(it);
            ok = true;
        } catch (const std::exception& e) {
            if (it.character) {  // without its clips: the body as a scene, in its bind pose
                SDL_Log("explore: %s as a character: %s", it.path.c_str(), e.what());
                unload();
                try {
                    result = loadStatic(it);
                    ok = true;
                } catch (const std::exception& e2) {
                    result = e2.what();
                }
            } else {
                result = e.what();
            }
        }
    } else {
        result = "no such model";
    }
    if (!ok) unload();
    if (info && infoSize > 0) SDL_snprintf(info, size_t(infoSize), "%s", result.c_str());
    return ok ? 0 : 1;
}

std::string Explorer::loadStatic(const Item& it) {
    scene_ = std::make_unique<Scene>(data_->read(it.path));
    if (size_t slash = it.path.rfind('/'); slash != std::string::npos) scene_->loadExternalTextures(*data_, it.path.substr(0, slash));
    sceneRenderer_ = std::make_unique<SceneRenderer>(renderer_, *scene_);
    sceneRenderer_->setLookup([this](const std::string& n) { return lookup(n); });
    // What to draw: the views no other view draws (as gd01 --view does), else every mesh.
    std::set<std::string> contained;
    for (const SceneObject& o : scene_->objects())
        if (o.view) contained.insert(o.view->draw.draws.begin(), o.view->draw.draws.end());
    for (const SceneObject& o : scene_->objects())
        if (o.view && !contained.count(o.name)) roots_.push_back(o.name);
    int meshes = 0;
    for (const SceneObject& o : scene_->objects()) meshes += o.mesh ? 1 : 0;
    if (roots_.empty())
        for (const SceneObject& o : scene_->objects())
            if (o.mesh && !o.name.starts_with("bone_") && !o.name.starts_with("spot_")) roots_.push_back(o.name);
    // The framing: the meshes' vertices (a venue: their meshes' centres
    // between the 10th and 90th percentile, so a sky does not set the size).
    std::vector<V3> pts, centres;
    for (const SceneObject& o : scene_->objects()) {
        if (!o.mesh || o.mesh->verts.empty() || o.name.starts_with("bone_") || o.name.starts_with("spot_") || !o.mesh->draw.showing) continue;
        const Xfm& w = o.mesh->trans.world;
        V3 sum{0, 0, 0};
        for (const MeshVertex& mv : o.mesh->verts) {
            V3 q = w.pos;
            for (int j = 0; j < 3; ++j)
                for (int k = 0; k < 3; ++k) q[size_t(j)] += mv.pos[k] * w.rot[size_t(k * 3 + j)];
            pts.push_back(q);
            sum = add(sum, q);
        }
        centres.push_back(mul(sum, 1.0f / float(o.mesh->verts.size())));
    }
    if (pts.empty()) throw std::runtime_error("nothing in " + it.path + " can be drawn");
    if (it.guitar) {
        // GH1's guitars hang from the hand bone with the front up (+z), the headstock towards -x +y.
        const std::array<float, 9> rows = gdexplore::uprightRows(pts, {-1, 1, 0}, {0, 0, 1});
        place_ = Mat4::fromRowXfm(rows, {0, 0, 0});
        for (std::vector<V3>* list : {&pts, &centres})
            for (V3& p : *list) {
                const V3 q = p;
                for (int j = 0; j < 3; ++j) p[size_t(j)] = q[0] * rows[size_t(j)] + q[1] * rows[size_t(3 + j)] + q[2] * rows[size_t(6 + j)];
            }
    }
    V3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    const std::vector<V3>& use = it.venue && centres.size() > 4 ? centres : pts;
    for (int a = 0; a < 3; ++a) {
        std::vector<float> v;
        for (const V3& p : use) v.push_back(p[size_t(a)]);
        std::sort(v.begin(), v.end());
        const float q = it.venue && centres.size() > 4 ? 0.1f : 0.0f;
        lo[size_t(a)] = v[size_t(q * float(v.size() - 1))];
        hi[size_t(a)] = v[size_t((1 - q) * float(v.size() - 1))];
    }
    center_ = mul(add(lo, hi), 0.5f);
    const V3 d{hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]};
    radius_ = std::max(1.0f, std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) * 0.5f);
    char line[160];
    std::snprintf(line, sizeof line, "%d meshes, %zu objects, %zu views drawn", meshes, scene_->objects().size(), roots_.size());
    return line;
}

std::string Explorer::loadCharacter(const Item& it) {
    auto band = bandSpecs_.find(it.folder);
    const Character::Spec spec =
        band != bandSpecs_.end() ? band->second : Character::guitaristSpec(*data_, it.folder, "gibson_sg", 1);
    char_ = std::make_unique<Character>(renderer_, *data_, spec);
    char_->setWorldLookup([this](const std::string& n) { return lookup(n); });
    // Every clip, the idle ones first.
    clips_ = char_->clipsWith(0, false);
    std::stable_sort(clips_.begin(), clips_.end(), [&](int a, int b) {
        const std::string& na = char_->clipNameOf(a);
        const std::string& nb = char_->clipNameOf(b);
        const bool ia = na.find("idle") != std::string::npos, ib = nb.find("idle") != std::string::npos;
        return ia != ib ? ia : na < nb;
    });
    for (int c : clips_) clipNames_.push_back(char_->clipNameOf(c));
    // A standing figure, from its feet at the origin.
    center_ = {0, 0, 40};
    radius_ = 48;
    char line[160];
    std::snprintf(line, sizeof line, "%s, %zu animations", band != bandSpecs_.end() ? "band member" : "guitarist", clips_.size());
    return line;
}

void Explorer::draw(const GdExploreView& v) {
    if (!gdshim_make_current()) return;
    const SDL_Color bg{Uint8(v.background[0] * 255), Uint8(v.background[1] * 255), Uint8(v.background[2] * 255), 255};
    renderer_.beginFrame(bg);
    renderer_.beginWide(8.0f);  // the window's width: the launcher's panels cover its sides
    int ww = 0, wh = 0;
    SDL_GetWindowSizeInPixels(window_, &ww, &wh);
    const int vw = std::max(1, renderer_.viewportWidth()), vh = std::max(1, renderer_.viewportHeight());
    const float vx0 = float(ww - vw) / 2, vy0 = float(wh - vh) / 2;
    if (!char_ && !sceneRenderer_) {
        renderer_.endWide();
        renderer_.endFrame();
        return;
    }
    renderer_.clearDepth();

    // Animation: the clip the launcher asks for, on a 120 beats a minute clock.
    float dt = std::clamp(v.seconds - lastSeconds_, 0.0f, 0.1f);
    if (v.seconds < lastSeconds_) dt = 0;
    lastSeconds_ = v.seconds;
    if (char_ && v.clip != clipPlaying_) {
        clipPlaying_ = v.clip;
        animTime_ = 0;
        if (v.clip >= 0 && v.clip < clipCount()) char_->playClip(clips_[size_t(v.clip)], Character::kAnimNow | Character::kAnimLoop);
    }
    if (char_ && clipPlaying_ >= 0) animTime_ += dt;
    if (char_) char_->poll(float(animTime_ * 2.0), float(animTime_ * 1000.0));

    // The orbit camera, framing the model in the launcher's viewer area.
    const float rx = float(v.x) - vx0, ry = float(v.y) - vy0, rw = std::max(1.0f, float(v.w)), rh = std::max(1.0f, float(v.h));
    const float aspect = float(vw) / float(vh);
    const float tanV = std::tan(kFov / 2), tanH = tanV * aspect;
    const float t = std::min(tanV * rh / float(vh), tanH * rw / float(vw)) * 0.9f;
    const float dist = radius_ / std::sin(std::atan(t)) * std::max(0.02f, v.zoom);
    const float front = char_ ? 3.1415927f : 0.0f;  // characters face +y (bind pose and clips)
    const float yaw = front + v.yaw, pitch = std::clamp(v.pitch, -1.45f, 1.45f);
    const V3 offset{std::sin(yaw) * std::cos(pitch) * dist, -std::cos(yaw) * std::cos(pitch) * dist, std::sin(pitch) * dist};
    const V3 fwd = normalize(mul(offset, -1));
    const V3 right = normalize(cross(fwd, V3{0, 0, 1}));
    const V3 up = cross(right, fwd);
    const V3 target = add(center_, mul(add(mul(right, v.pan_x), mul(up, v.pan_y)), radius_));
    const float ncx = (rx + rw / 2) / float(vw) * 2 - 1, ncy = 1 - (ry + rh / 2) / float(vh) * 2;
    const V3 shift = add(mul(right, -ncx * dist * tanH), mul(up, -ncy * dist * tanV));
    const V3 eye = add(add(target, offset), shift);
    const std::array<float, 9> rows{right[0], right[1], right[2], fwd[0], fwd[1], fwd[2], up[0], up[1], up[2]};
    const float nearPlane = std::max(0.05f, std::min(dist * 0.05f, radius_ * 0.01f));
    const Mat4 viewProj = Mat4::perspective(kFov, aspect, nearPlane, dist + radius_ * 40) * Mat4::cameraView(rows, eye);

    // The key light travels along its +y: from the viewer's side towards the model.
    const V3 travel = normalize(add(add(fwd, mul(right, -0.4f)), V3{0, 0, -0.5f}));
    const V3 lr = normalize(cross(travel, V3{0, 0, 1}));
    const V3 lu = cross(lr, travel);
    keyLight_.light->trans.world.rot = {lr[0], lr[1], lr[2], travel[0], travel[1], travel[2], lu[0], lu[1], lu[2]};
    keyLight_.light->trans.world.pos = eye;

    Xfm camera;
    camera.rot = rows, camera.pos = eye;
    if (char_) {
        char_->draw(viewProj, &env_);
    } else {
        sceneRenderer_->setCamera(camera);
        sceneRenderer_->resetDynamic();
        for (const std::string& r : roots_) sceneRenderer_->draw(r, viewProj, place_, &env_);
    }
    renderer_.endWide();
    renderer_.endFrame();  // the shims keep it from presenting: the launcher does
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
    // As for a game run: the launcher's GPU (no restart), no desktop entry.
    gdshim_setenv("GD01_GPU", "system");
    gdshim_setenv("GD01_NO_DESKTOP_ENTRY", "1");
    auto* ex = new Explorer;
    if (!ex->open(host)) {
        ex->close();
        delete ex;
        gdshim_end();
        return nullptr;
    }
    if (host->log) host->log(host->log_user, "gd01 " GD01_VERSION " model explorer opened");
    g_api = GdExplorer{};
    g_api.size = sizeof(GdExplorer);
    g_api.self = ex;
    g_api.item_count = &itemCount;
    g_api.item = &itemAt;
    g_api.select = &selectItem;
    g_api.clip_count = &clipCount;
    g_api.clip_name = &clipName;
    g_api.draw = &drawView;
    g_api.close = &closeExplorer;
    return &g_api;
}
