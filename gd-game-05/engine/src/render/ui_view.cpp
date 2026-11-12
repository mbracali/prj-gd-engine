// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/ui_view.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <set>

#include "data/bytes.h"
#include "data/game_data.h"
#include "render/ui_cloth.h"
#include "scene/bitmap.h"
#include "scene/milo.h"
#include "scene/ui_objects.h"

namespace gfx {

namespace {

constexpr float kUiFovDeg = 34.516f, kUiCamY = -768, kUiNear = 1, kUiFar = 1000;

scene::Xfm scaleXfm(float sx, float sy, float sz) {
    scene::Xfm x;
    x.rot = {sx, 0, 0, 0, sy, 0, 0, 0, sz};
    return x;
}

scene::Xfm moveXfm(float x, float y, float z) {
    scene::Xfm m;
    m.pos = {x, y, z};
    return m;
}

const char* stateKey(scene::UiState s) {
    switch (s) {
    case scene::UiState::Focused: return "focused";
    case scene::UiState::Disabled: return "disabled";
    case scene::UiState::Selecting: return "selecting";
    default: return "normal";
    }
}

// (key a b c) -> {a, b, c}.
bool rgbOf(const ::DataNode* n, float out[3]) {
    if (!n || n->children.size() < 4) return false;
    for (int k = 0; k < 3; ++k) {
        const ::DataNode& c = n->children[size_t(k + 1)];
        if (c.type != ::DataNode::Type::Int && c.type != ::DataNode::Type::Float) return false;
        out[k] = c.type == ::DataNode::Type::Float ? c.f : float(c.i);
    }
    return true;
}

}  // namespace

// A label's laid-out text, kept until its string changes.
struct UiView::LabelText {
    std::string str;
    const scene::Font* font = nullptr;
    TextLayout layout;
    std::array<float, 2> fit{1, 1};
};

UiView::UiView(const GameData& data, scene::MiloLibrary& library, const scene::UiStyles& styles, const scene::Locale& locale,
               SceneCache& cache)
    : data_(data), lib_(library), styles_(styles), locale_(locale), cache_(cache), texts_(cache) {}

UiView::~UiView() {
    if (renderTarget_.id) cache_.device().destroyTarget(renderTarget_);
    for (auto& [o, t] : rdTargets_) cache_.device().destroyTarget(t);
}

namespace {
// The length-prefixed names in a raw record (a class the port keeps raw).
std::vector<std::string> recordNames(const Bytes& rec) {
    std::vector<std::string> out;
    for (size_t i = 0; i + 4 < rec.size(); ++i) {
        const uint32_t n = uint32_t(rec[i]) << 24 | uint32_t(rec[i + 1]) << 16 | uint32_t(rec[i + 2]) << 8 | uint32_t(rec[i + 3]);
        if (n < 3 || n > 64 || i + 4 + n > rec.size()) continue;
        bool printable = true;
        for (size_t k = 0; k < n && printable; ++k) printable = rec[i + 4 + k] >= 0x20 && rec[i + 4 + k] < 0x7f;
        if (!printable) continue;
        out.emplace_back(reinterpret_cast<const char*>(rec.data() + i + 4), n);
        i += 3 + n;
    }
    return out;
}
// A RenderDir record's RndTransformable (rev 9: local then world 3x4
// matrices, both with unit rows): the first such block in the record [I:
// found by its shape; the RenderDir class is not decoded].
bool renderDirXfm(const Bytes& r, scene::Xfm& out) {
    auto f = [&](size_t p) {
        const uint32_t b = uint32_t(r[p]) << 24 | uint32_t(r[p + 1]) << 16 | uint32_t(r[p + 2]) << 8 | r[p + 3];
        float v;
        std::memcpy(&v, &b, 4);
        return v;
    };
    for (size_t i = 0; i + 4 + 96 <= r.size(); ++i) {
        if (r[i] != 0 || r[i + 1] != 0 || r[i + 2] != 0 || r[i + 3] != 9) continue;
        bool ok = true;
        float m[24];
        for (int k = 0; k < 24 && ok; ++k) {
            m[k] = f(i + 4 + size_t(k) * 4);
            ok = std::isfinite(m[k]) && std::fabs(m[k]) < 1e6f;
        }
        for (int mat = 0; mat < 2 && ok; ++mat)
            for (int row = 0; row < 3 && ok; ++row) {
                const float* q = m + mat * 12 + row * 3;
                const float l = q[0] * q[0] + q[1] * q[1] + q[2] * q[2];
                ok = std::fabs(l - 1.0f) < 0.02f;
            }
        if (!ok) continue;
        for (size_t k = 0; k < 9; ++k) out.rot[k] = m[k];
        out.pos = {m[9], m[10], m[11]};
        return true;
    }
    return false;
}
}  // namespace

int UiView::clothPolls() const { return cloth_ ? cloth_->polls() : 0; }

// RB's RndDir draw list (RB 0x821B3A48 sorts it with 0x821FF100) [C]: by
// draw order; at the same order, objects of the same class by name
// (strcmp). Classes compare by their Symbol's address, which the port cannot
// know: different classes keep the directory's order [I].
// tour_band_welcome.milo's all.grp (blackmask.mesh, 85% black) draws
// before art.grp and the texts' group, as in RB; in the file's order it
// covered the popup ("Congratulations!" at 12% of its colour).
static void sortDraws(std::vector<const scene::Object*>& roots) {
    auto orderOf = [](const scene::Object* o) {
        const scene::Draw* d = drawOf(o);
        return d ? d->order : 0.0f;
    };
    std::stable_sort(roots.begin(), roots.end(), [&](const scene::Object* a, const scene::Object* b) { return orderOf(a) < orderOf(b); });
    for (size_t i = 0; i < roots.size();) {
        size_t j = i;
        while (j < roots.size() && orderOf(roots[j]) == orderOf(roots[i])) ++j;
        // Within [i, j): each class's places, refilled by name.
        std::map<std::string, std::vector<size_t>> places;
        for (size_t k = i; k < j; ++k) places[roots[k]->cls].push_back(k);
        for (auto& [cls, at] : places) {
            if (at.size() < 2) continue;
            std::vector<const scene::Object*> objs;
            for (size_t k : at) objs.push_back(roots[k]);
            std::sort(objs.begin(), objs.end(), [](const scene::Object* a, const scene::Object* b) { return std::strcmp(a->name.c_str(), b->name.c_str()) < 0; });
            for (size_t k = 0; k < at.size(); ++k) roots[at[k]] = objs[k];
        }
        i = j;
    }
}

bool UiView::drawRenderDir3d(MaterialRenderer& r) {
    const scene::Directory& outer = milo_->root();
    // The RenderDir's own pollables (the cloth: CharTransCopy, CharHair),
    // once a frame before it draws [C data; the 60 Hz step: rig.cpp].
    if (!clothChecked_) {
        clothChecked_ = true;
        cloth_ = UiCloth::make(outer, cache_.device());
        // The RenderDir's own RndTransformable (its record keeps RndDir rev
        // 10's transform after the ObjectDir part) [C data]: what its
        // objects parented to the directory hang from.
        scene::Xfm dx;
        if (outer.self && renderDirXfm(outer.self->record, dx)) pose_.setDirXfm(&outer, dx);
    }
    if (cloth_) {
        const double now = double(SDL_GetTicksNS()) / 1e6;
        const double dt = clothLastMs_ < 0 ? 0 : std::clamp(now - clothLastMs_, 0.0, 100.0);
        clothLastMs_ = now;
        cloth_->poll(pose_, dt / 1000.0);
    }
    const scene::Cam* outerCam = nullptr;
    const scene::Environ* outerEnv = nullptr;
    if (outer.self)
        for (const std::string& n : recordNames(outer.self->record)) {
            if (!outerCam) outerCam = outer.find<scene::Cam>(n);
            if (!outerEnv) outerEnv = outer.find<scene::Environ>(n);
        }
    if (!outerCam) return false;
    RenderDevice& dev = cache_.device();
    const TargetId prev = dev.currentTarget();
    int vx, vy, vw, vh;
    dev.currentViewport(vx, vy, vw, vh);
    const LightEnv screenEnv = r.lightEnv();
    const size_t firstHit = hits_.size();
    for (const auto& o : outer.objects) {
        if (o->cls != "TexRenderer") continue;
        const scene::Directory* panel = nullptr;
        const scene::Tex* out = nullptr;
        for (const std::string& n : recordNames(o->record)) {
            if (const scene::Tex* t = outer.find<scene::Tex>(n)) out = t;
            for (const auto& [owner, sub] : outer.subdirs)
                if (sub && (sub->name == n || owner == n)) panel = sub.get();
        }
        if (!panel || !out || out->width <= 0 || out->height <= 0) continue;
        texRendered_.insert(panel);
        const bool hidden = hiddenScopes_.count(panel->name) > 0;
        for (const auto& m : outer.objects)
            if (const auto* mesh = m->as<scene::Mesh>())
                if (const scene::Mat* mat = findMat(*mesh, mesh->mat); mat && mat->diffuseTex == out->name) pose_.setShowing(mesh, !hidden);
        if (hidden) continue;
        TargetId& t = rdTargets_[o.get()];
        if (!t.id) {
            std::string error;
            t = dev.createTarget(TargetDesc{out->width, out->height, true, TargetFormat::RGBA8}, error);
            if (!t.id) continue;
        }
        dev.setTarget(t);
        dev.setViewport(0, 0, out->width, out->height);
        const float clear[4] = {0, 0, 0, 0};
        dev.clear(clear, true);
        Camera ic = defaultCamera();
        LightEnv env = screenEnv;
        if (const auto* own = panel->self ? panel->self->as<scene::DirObj>() : nullptr) {
            if (own->hasPanel)
                if (const scene::Cam* c = panel->find<scene::Cam>(own->panel.cam)) {
                    ic.world = toMat4(pose_.world(c));
                    ic.yFov = c->yFov, ic.nearPlane = c->nearPlane, ic.farPlane = c->farPlane;
                }
            if (own->hasRnd)
                if (const scene::Environ* e = panel->find<scene::Environ>(own->rnd.environment)) env = cache_.lightEnv(*e, &pose_);
        }
        ic.aspect = float(out->width) / float(out->height);
        r.setFrame(ic, env, out->width, out->height);
        viewProj_ = ic.viewProj(false);
        r.forceAlphaWrite = true;
        drawRoots(r, *panel);
        r.forceAlphaWrite = false;
        for (const auto& m : outer.objects)
            if (auto* mat = m->as<scene::Mat>(); mat && mat->diffuseTex == out->name) {
                ScenePose::MatState& st = pose_.matState(mat);
                st.texId = dev.targetTexture(t);
                st.flipV = true;
            }
    }
    dev.setTarget(prev);
    dev.setViewport(vx, vy, vw, vh);
    dev.clear(nullptr, true);
    Camera oc;
    oc.world = toMat4(pose_.world(outerCam));
    oc.yFov = outerCam->yFov, oc.nearPlane = outerCam->nearPlane, oc.farPlane = outerCam->farPlane;
    r.setFrame(oc, outerEnv ? cache_.lightEnv(*outerEnv, &pose_) : screenEnv, vw, vh);
    viewProj_ = oc.viewProj(false);
    const size_t texHits = hits_.size();
    drawRoots(r, outer);
    // The texture's hits (the list's rows) to where the cloth shows them:
    // its texture over the cloth mesh's screen bounds [I: the sway's bend
    // is left out].
    if (cloth_ && clothX1_ > clothX0_)
        for (size_t i = firstHit; i < texHits; ++i) {
            Hit& h = hits_[i];
            const float sx = (clothX1_ - clothX0_) / 1280.0f, sy = (clothY1_ - clothY0_) / 720.0f;
            h.x0 = clothX0_ + h.x0 * sx, h.x1 = clothX0_ + h.x1 * sx, h.y0 = clothY0_ + h.y0 * sy, h.y1 = clothY0_ + h.y1 * sy;
        }
    return true;
}

void UiView::drawRoots(MaterialRenderer& r, const scene::Directory& root) {
    std::set<std::string> held;
    for (const auto& o : root.objects)
        if (const auto* g = o->as<scene::Group>())
            for (const std::string& m : g->objects) held.insert(m);
    std::vector<const scene::Object*> roots;
    for (const auto& o : root.objects)
        if (!held.count(o->name) && (drawOf(o.get()) || o->as<scene::Text>())) roots.push_back(o.get());
    sortDraws(roots);
    for (const scene::Object* o : roots) drawObject(r, *o, 0);
}

void UiView::problem(const std::string& what) {
    if (std::find(problems_.begin(), problems_.end(), what) == problems_.end()) {
        problems_.push_back(what);
        static const bool log = SDL_getenv("GD05_UILOG") != nullptr;
        if (log) SDL_Log("ui view: %s", what.c_str());
    }
}

scene::Milo* UiView::resource(const std::string& rel) {
    if (rel.empty()) return nullptr;
    const std::string path = scene::UiStyles::resourcePath(data_, rel);
    if (!data_.exists(path)) {
        problem("missing resource " + path);
        return nullptr;
    }
    try {
        scene::Milo& m = lib_.load(path);
        linkSubdirs(m, 0);
        return &m;
    } catch (const std::exception& e) {
        problem(path + ": " + e.what());
        return nullptr;
    }
}

std::vector<scene::Milo*> UiView::linkSubdirs(scene::Milo& milo, int depth) {
    std::vector<scene::Milo*> out;
    const auto* own = milo.root().self ? milo.root().self->as<scene::DirObj>() : nullptr;
    if (!own || depth > 4) return out;
    for (const std::string& sub : own->objectDir.subdirs) {
        const std::string p = scene::findMiloRelative(data_, milo.path(), sub);
        if (!data_.exists(p)) {
            problem("missing subdirectory " + p);
            continue;
        }
        try {
            scene::Milo& m = lib_.load(p);
            auto& linked = milo.root().linked;
            if (std::find(linked.begin(), linked.end(), &m.root()) == linked.end()) {
                linked.push_back(&m.root());
                linkSubdirs(m, depth + 1);
            }
            out.push_back(&m);
        } catch (const std::exception& e) {
            problem(p + ": " + e.what());
        }
    }
    // The inline proxied directories' own subdirectories (pat_rock_3.milo's
    // waves_skulls_03 names waves_skulls_2_common.milo, which holds the
    // skulls' textures; seldiff's slots name seldiffpart_common.milo).
    std::function<void(scene::Directory&, int)> inlineSubs = [&](scene::Directory& d, int level) {
        if (level > 4) return;
        for (auto& [owner, sub] : d.subdirs) {
            if (!sub) continue;
            if (const auto* so = sub->self ? sub->self->as<scene::DirObj>() : nullptr)
                for (const std::string& name : so->objectDir.subdirs) {
                    const std::string p = scene::findMiloRelative(data_, milo.path(), name);
                    if (!data_.exists(p)) continue;
                    try {
                        scene::Milo& m = lib_.load(p);
                        if (std::find(sub->linked.begin(), sub->linked.end(), &m.root()) == sub->linked.end()) sub->linked.push_back(&m.root());
                    } catch (const std::exception& e) {
                        problem(p + ": " + e.what());
                    }
                }
            inlineSubs(*sub, level + 1);
        }
    };
    if (depth == 0) inlineSubs(milo.root(), 0);
    return out;
}

const scene::Tex* UiView::textureFile(const std::string& rel) {
    if (rel.empty()) return nullptr;
    if (auto it = texFiles_.find(rel); it != texFiles_.end()) return it->second.get();
    // "image/og/x.bmp" -> "ui/image/og/gen/x.bmp_xbox" (og -> ng as for milos).
    std::string p = scene::resolveMiloRelative("ui/x.milo", rel);
    const size_t slash = p.rfind('/');
    p = p.substr(0, slash + 1) + "gen/" + p.substr(slash + 1) + "_xbox";
    std::string q = p;
    for (size_t i = q.find("/og/"); i != std::string::npos; i = q.find("/og/", i + 4)) q.replace(i, 4, "/ng/");
    const std::string path = data_.exists(p) ? p : q;
    std::unique_ptr<scene::Tex> tex;
    if (data_.exists(path)) {
        try {
            const Bytes b = data_.read(path);
            ByteReader r(b);
            tex = std::make_unique<scene::Tex>();
            tex->name = tex->file = rel;
            {
                milo_order::Scope loose(false);  // loose bitmap files are little-endian [C data]
                tex->bitmap = scene::readBitmap(r);
            }
            tex->width = tex->bitmap.width, tex->height = tex->bitmap.height;
            tex->decoded = true;
        } catch (const std::exception& e) {
            problem(path + ": " + e.what());
            tex.reset();
        }
    } else {
        problem("missing texture " + path);
    }
    const scene::Tex* t = tex.get();
    texFiles_[rel] = std::move(tex);
    return t;
}

void UiView::load(const std::string& path) {
    milo_ = &lib_.load(path);
    pose_.clear();
    cloth_.reset(), clothChecked_ = false, clothLastMs_ = -1;
    // The character select's door stands closed until a character swings it
    // (0x8228F250); the scripts hand it over at TRANSITION_COMPLETE, so it must
    // not show the milo's open pose before that (GD02-147).
    if (find("cs_door.mesh")) setLocalEuler("cs_door.mesh", 1.5707964f, 0, 0);
    labels_.clear();
    keyed_.clear();
    subdirs_ = linkSubdirs(*milo_, 0);
    // The panel has entered: its ui_enter triggers' animations at their end.
    for (const Trigger& t : triggers("ui_enter")) pose_.setFrame(t.anim, t.endFrame);
}

std::vector<UiView::Trigger> UiView::triggers(const std::string& event) const {
    std::vector<Trigger> out;
    if (!milo_) return out;
    std::vector<scene::Milo*> all{milo_};
    all.insert(all.end(), subdirs_.begin(), subdirs_.end());
    for (scene::Milo* m : all)
        for (scene::UITrigger* t : m->root().all<scene::UITrigger>()) {
            if (t->event != event || t->anim.empty()) continue;
            // Every anim of the trigger (dialog.milo's popup.trg: the panel's
            // popup.tnm, then popup_text.tnm and four fades), to its range's
            // end when it has one [C data].
            if (t->anims.empty()) {
                const scene::Object* a = t->dir ? t->dir->resolve(t->anim) : nullptr;
                if (!a) a = find(t->anim);
                if (a) {
                    Trigger tr;
                    tr.anim = a, tr.blockTransition = t->blockTransition, tr.endFrame = ScenePose::endFrame(a);
                    tr.blockSeconds = t->blockTransition ? tr.endFrame / 30.0f : 0;
                    out.push_back(tr);
                }
                continue;
            }
            float block = 0;
            if (t->blockTransition)
                for (const scene::UITrigger::Anim& an : t->anims) {
                    const scene::Object* a = t->dir ? t->dir->resolve(an.name) : nullptr;
                    if (!a) a = find(an.name);
                    if (a) block = std::max(block, ScenePose::endFrame(a) / 30.0f);
                }
            for (const scene::UITrigger::Anim& an : t->anims) {
                const scene::Object* a = t->dir ? t->dir->resolve(an.name) : nullptr;
                if (!a) a = find(an.name);
                if (!a || !an.enable) continue;
                // RndAnimatable::Animate (RB 0x8221BE38) [C]: start and end as
                // the record gives them (start: the current frame for dest);
                // fps = |end - start| / period, or the rate's fps (30 for
                // every rate but k480_fpb, 0x82884D14) x scale; loop for type
                // loop. A range of 0..0 sets frame 0 (main.milo's multi.tnm).
                Trigger tr;
                tr.anim = a;
                tr.blockTransition = t->blockTransition;
                tr.startFrame = an.start, tr.endFrame = an.end;
                tr.dest = an.type == "dest";
                tr.loop = an.type == "loop";
                tr.rate = int(an.rate);
                const float rateFps = an.rate == 1 ? 480.0f : 30.0f;
                tr.fps = an.period != 0 ? std::abs(tr.endFrame - tr.startFrame) / an.period : rateFps * an.scale;
                tr.delay = an.delay;
                tr.wait = an.wait;
                tr.blockSeconds = block;
                out.push_back(tr);
            }
        }
    return out;
}

scene::Object* UiView::find(const std::string& name) const {
    if (!milo_ || name.empty()) return nullptr;
    if (scene::Object* o = milo_->find(name)) return o;
    for (scene::Milo* m : subdirs_)
        if (scene::Object* o = m->find(name)) return o;
    return nullptr;
}

std::string UiView::cameraName() const {
    const auto* own = milo_ && milo_->root().self ? milo_->root().self->as<scene::DirObj>() : nullptr;
    if (own && own->hasPanel)
        if (const scene::Object* c = find(own->panel.cam); c && c->as<scene::Cam>()) return c->name;
    return "ui (default.dta)";
}

Camera UiView::defaultCamera() {
    Camera cam;
    const float rows[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}, pos[3] = {0, kUiCamY, 0};
    cam.world = Mat4::fromRnd(rows, pos);
    cam.yFov = kUiFovDeg * 3.14159265f / 180.0f, cam.nearPlane = kUiNear, cam.farPlane = kUiFar;
    return cam;
}

Camera UiView::camera() const {
    const auto* own = milo_ && milo_->root().self ? milo_->root().self->as<scene::DirObj>() : nullptr;
    // A RenderDir (RB's song list): the inline PanelDir its TexRenderer
    // draws into a texture (draw()) takes that panel's own camera.
    if (milo_ && milo_->root().cls == "RenderDir")
        for (const auto& [owner, sub] : milo_->root().subdirs)
            if (sub && sub->cls == "PanelDir" && sub->self)
                if (const auto* inner = sub->self->as<scene::DirObj>(); inner && inner->hasPanel && !inner->panel.cam.empty()) {
                    own = inner;
                    if (SDL_getenv("GD05_UILOG")) {
                        static bool once = false;
                        if (!once) once = true, SDL_Log("ui: RenderDir %s draws its panel with %s", milo_->root().name.c_str(), inner->panel.cam.c_str());
                    }
                    break;
                }
    const scene::Cam* c = own && own->hasPanel ? (find(own->panel.cam) ? find(own->panel.cam)->as<scene::Cam>() : nullptr) : nullptr;
    if (!c) return defaultCamera();
    Camera cam;
    cam.world = toMat4(pose_.world(c));
    cam.yFov = c->yFov, cam.nearPlane = c->nearPlane, cam.farPlane = c->farPlane;
    return cam;
}

namespace {
Vec3 gUiAmbient{1, 1, 1};  // config/default.dta (ui (env (ambient 1 1 1))) [C data]
}  // namespace

void UiView::setDefaultAmbient(float r, float g, float b) { gUiAmbient = {r, g, b}; }

LightEnv UiView::defaultEnvironment() {
    LightEnv env;
    env.envAmbient = gUiAmbient;
    return env;
}

LightEnv UiView::environment() {
    const auto* own = milo_ && milo_->root().self ? milo_->root().self->as<scene::DirObj>() : nullptr;
    if (own && own->hasRnd)
        if (const scene::Object* e = find(own->rnd.environment); e && e->as<scene::Environ>())
            return cache_.lightEnv(*e->as<scene::Environ>(), &pose_);
    return defaultEnvironment();
}

void UiView::setText(const std::string& component, const std::string& text) {
    // An empty string is a real (blank) text: set_localized_text "".
    overrides_[component] = text;
}

void UiView::setWrapWidth(const std::string& component, float width) {
    wrapWidths_[component] = width;
    labels_.clear();
}

std::string UiView::labelString(const scene::UILabel& l) const {
    if (auto it = overrides_.find(l.name); it != overrides_.end()) return it->second;
    if (l.localize) return l.textToken.empty() ? std::string() : locale_.text(l.textToken);
    return l.text;
}

std::string UiView::text(const std::string& component) const {
    const scene::Object* o = find(component);
    if (const auto* l = o ? o->as<scene::UILabel>() : nullptr) return labelString(*l);
    auto it = overrides_.find(component);
    return it == overrides_.end() ? std::string() : it->second;
}

void UiView::setFrame(const std::string& object, float frame) {
    const scene::Object* o = find(object);
    if (!o) return;
    frames_[object] = frame;
    pose_.setFrame(o, frame);
}

float UiView::endFrame(const std::string& object) const { return ScenePose::endFrame(find(object)); }

float UiView::frame(const std::string& object) const {
    auto it = frames_.find(object);
    return it == frames_.end() ? 0.0f : it->second;
}

namespace {
const scene::Directory* subdirNamed(const scene::Directory& d, const std::string& name, int depth) {
    if (depth > 4) return nullptr;
    for (const auto& [owner, sub] : d.subdirs) {
        if (!sub) continue;
        if (sub->name == name || owner == name) return sub.get();
        if (const scene::Directory* x = subdirNamed(*sub, name, depth + 1)) return x;
    }
    return nullptr;
}
}  // namespace

scene::Object* UiView::findIn(const std::string& scope, const std::string& name) const {
    if (!milo_) return nullptr;
    const scene::Directory* d = subdirNamed(milo_->root(), scope, 0);
    return d ? d->find(name) : nullptr;
}

std::vector<scene::Object*> UiView::byClassIn(const std::string& scope, const std::string& cls) const {
    std::vector<scene::Object*> out;
    if (!milo_) return out;
    if (const scene::Directory* d = subdirNamed(milo_->root(), scope, 0))
        for (const auto& o : d->objects)
            if (o->cls == cls) out.push_back(o.get());
    return out;
}

void UiView::setShowing(const std::string& object, bool showing) {
    if (const scene::Object* o = find(object)) pose_.setShowing(o, showing);
}

bool UiView::showing(const std::string& object) const {
    const scene::Object* o = find(object);
    return o && pose_.showing(o);
}

void UiView::setLocalPos(const std::string& object, float x, float y, float z) {
    const scene::Object* o = find(object);
    if (!o) return;
    scene::Xfm l = pose_.local(o);
    l.pos = {x, y, z};
    pose_.setLocal(o, l);
}

void UiView::setLocalEuler(const std::string& object, float x, float y, float z) {
    const scene::Object* o = find(object);
    if (!o) return;
    const float cx = std::cos(x), sx = std::sin(x), cy = std::cos(y), sy = std::sin(y), cz = std::cos(z), sz = std::sin(z);
    scene::Xfm l = pose_.local(o);
    l.rot = {cz * cy - sz * sx * sy, sz * cy + cz * sx * sy, -cx * sy,  //
             -sz * cx,               cz * cx,                sx,        //
             cz * sy + sz * sx * cy, sz * sy - cz * sx * cy, cx * cy};
    pose_.setLocal(o, l);
}

void UiView::setLocalScale(const std::string& object, float x, float y, float z) {
    const scene::Object* o = find(object);
    if (!o) return;
    scene::Xfm l = pose_.local(o);
    // Keep the rotation's directions, replace the scale of each axis (rows).
    const float s[3] = {x, y, z};
    for (int r = 0; r < 3; ++r) {
        float len = std::sqrt(l.rot[size_t(r * 3)] * l.rot[size_t(r * 3)] + l.rot[size_t(r * 3 + 1)] * l.rot[size_t(r * 3 + 1)] +
                              l.rot[size_t(r * 3 + 2)] * l.rot[size_t(r * 3 + 2)]);
        if (len <= 0) {
            l.rot[size_t(r * 3 + r)] = s[r];
            continue;
        }
        for (int c = 0; c < 3; ++c) l.rot[size_t(r * 3 + c)] *= s[r] / len;
    }
    pose_.setLocal(o, l);
}

void UiView::setState(const std::string& component, scene::UiState state) { states_[component] = state; }

scene::UiState UiView::state(const std::string& component) const {
    auto it = states_.find(component);
    if (it != states_.end()) return it->second;
    return component == focus_ ? scene::UiState::Focused : scene::UiState::Normal;
}

void UiView::setSlider(const std::string& component, int current, int steps) { sliders_[component] = {current, steps}; }

void UiView::setPictureTex(const std::string& component, const std::string& file) { pictureTex_[component] = file; }

void UiView::setTexFile(const std::string& texName, const std::string& file) {
    const scene::Tex* tex = textureFile(file);
    if (!tex || !milo_) return;
    const auto apply = [&](scene::Object& o) {
        if (auto* m = o.as<scene::Mat>(); m && m->diffuseTex == texName) pose_.matState(m).tex = tex;
    };
    milo_->root().forEach(apply);
    for (scene::Milo* m : subdirs_) m->root().forEach(apply);
}

bool UiView::worldXfm(const std::string& object, scene::Xfm& out) const {
    const scene::Object* o = find(object);
    if (!o) return false;
    out = pose_.world(o);
    return true;
}

LightEnv UiView::environment(const std::string& environName) {
    if (const scene::Object* e = find(environName); e && e->as<scene::Environ>()) return cache_.lightEnv(*e->as<scene::Environ>(), &pose_);
    return environment();
}

void UiView::setList(const std::string& component, std::vector<ListRow> rows, int selectedRow) {
    ListState& s = lists_[component];
    s.rows = std::move(rows);
    s.selected = selectedRow;
}

void UiView::setTextEntry(const std::string& component, const std::string& entered, const std::string& picking, bool done) {
    entries_[component] = {entered, picking, done};
}

const scene::Mat* UiView::findMat(const scene::Object& owner, const std::string& name) const {
    if (name.empty()) return nullptr;
    if (owner.dir)
        if (const scene::Mat* m = owner.dir->resolve<scene::Mat>(name)) return m;
    const scene::Object* o = find(name);
    return o ? o->as<scene::Mat>() : nullptr;
}

void UiView::draw(MaterialRenderer& r) {
    textsDrawn_ = meshesDrawn_ = 0;
    masks_.clear();
    hits_.clear();
    textProbes_.clear();
    if (!milo_) return;
    viewProj_ = camera().viewProj(false);
    const scene::Directory* drawRoot = &milo_->root();
    rendered_ = Rendered{};
    // A RenderDir is drawn as the original draws it (drawRenderDir3d: its
    // TexRenderers' panels into their textures, its own scene through its
    // camera: seldiff_solo's koi banner and the slots' cards). One whose
    // record names its inline panel as the one that takes the input
    // (qp_selsong's qp_sel_song, the list) is drawn flat below [I]: through
    // its banner camera (cloth.cam at lower3rd.trg's frame 3) the cloth's
    // top rows sit under the title bar, and its sway (CharHair) and the
    // list's scrolling there are not done (GD05-022).
    if (drawRoot->cls == "RenderDir") {
        bool cloth = false;
        if (drawRoot->self)
            for (const std::string& n : recordNames(drawRoot->self->record))
                for (const auto& [owner, sub] : drawRoot->subdirs)
                    if (sub && sub->cls == "PanelDir" && (sub->name == n || owner == n)) cloth = true;
        // The cloth (GD05-022, wave 04) is drawn as the others now: its
        // texture on the swaying banner through cloth.cam. Development aid:
        // GD05_SONGLIST_FLAT=1 shows the texture flat as wave 03 did.
        if (const char* e = SDL_getenv("GD05_SONGLIST_FLAT"); !e || *e != '1') cloth = false;
        if (!cloth && drawRenderDir3d(r)) {
            for (TextProbe& p : textProbes_) p.indirect = true;  // into a texture on a 3D banner
            texts_.collect();
            return;
        }
    }
    // RB's RenderDir panels (qp_selsong.milo): a TexRenderer draws the
    // inline PanelDir holding the list (through its meta.cam) into
    // cloth_texture_wide.tex, 480 x 552 [C data], which the RenderDir's
    // cloth banner shows through cloth.cam, swaying (CharHair). The port
    // draws the texture flat where the banner hangs, under the title bar
    // [I] (GD05-022: the banner's mesh, camera moves and sway).
    const scene::Directory* inner = nullptr;
    if (drawRoot->cls == "RenderDir")
        for (const auto& [owner, sub] : drawRoot->subdirs)
            if (sub && sub->cls == "PanelDir") {
                inner = sub.get();
                break;
            }
    const scene::Tex* outTex = nullptr;
    if (inner)
        for (const auto& o : drawRoot->objects)
            if (const auto* t = o->as<scene::Tex>(); t && t->bitmap.empty() && t->width > 0 && t->height > 0) outTex = t;
    if (inner && !outTex) drawRoot = inner;  // no output texture: the panel itself, on the screen
    RenderDevice& dev = cache_.device();
    TargetId prevTarget{};
    int vx = 0, vy = 0, vw = 0, vh = 0;
    if (inner && outTex) {
        texRendered_.insert(inner);
        if (!renderTarget_.id) {
            std::string error;
            renderTarget_ = dev.createTarget(TargetDesc{outTex->width, outTex->height, true, TargetFormat::RGBA8}, error);
            if (!renderTarget_.id) problem("RenderDir " + drawRoot->name + ": " + error);
        }
        if (renderTarget_.id) {
            prevTarget = dev.currentTarget();
            dev.currentViewport(vx, vy, vw, vh);
            dev.setTarget(renderTarget_);
            dev.setViewport(0, 0, outTex->width, outTex->height);
            const float clear[4] = {0, 0, 0, 0};
            dev.clear(clear, true);
            Camera ic = camera();
            ic.aspect = float(outTex->width) / float(outTex->height);
            LightEnv env = environment();
            if (const auto* own = inner->self ? inner->self->as<scene::DirObj>() : nullptr; own && own->hasRnd)
                if (const scene::Environ* e = inner->find<scene::Environ>(own->rnd.environment)) env = cache_.lightEnv(*e, &pose_);
            r.setFrame(ic, env, outTex->width, outTex->height);
            r.forceAlphaWrite = true;
            viewProj_ = ic.viewProj(false);
            // Where it shows: from under the title bar to above the help
            // bar, centred, at the texture's aspect [I].
            constexpr float kTop = 150, kH = 500;
            const float w = kH * float(outTex->width) / float(outTex->height);
            rendered_.texture = dev.targetTexture(renderTarget_);
            rendered_.x0 = (1280 - w) / 2, rendered_.x1 = rendered_.x0 + w, rendered_.y0 = kTop, rendered_.y1 = kTop + kH;
        }
        drawRoot = inner;
    }
    const scene::Directory& root = *drawRoot;
    // The drawables no group of the panel holds, by draw order.
    std::set<std::string> held;
    for (const auto& o : root.objects)
        if (const auto* g = o->as<scene::Group>())
            for (const std::string& m : g->objects) held.insert(m);
    std::vector<const scene::Object*> roots;
    for (const auto& o : root.objects)
        if (!held.count(o->name) && (drawOf(o.get()) || o->as<scene::Text>())) roots.push_back(o.get());
    sortDraws(roots);
    const size_t firstHit = hits_.size();
    for (const scene::Object* o : roots) drawObject(r, *o, 0);
    if (rendered_.texture.id) {
        r.forceAlphaWrite = false;
        dev.setTarget(prevTarget);
        dev.setViewport(vx, vy, vw, vh);
        // The hits from the texture's picture to its place on the screen.
        for (size_t i = firstHit; i < hits_.size(); ++i) {
            Hit& h = hits_[i];
            const float sx = (rendered_.x1 - rendered_.x0) / 1280.0f, sy = (rendered_.y1 - rendered_.y0) / 720.0f;
            h.x0 = rendered_.x0 + h.x0 * sx, h.x1 = rendered_.x0 + h.x1 * sx, h.y0 = rendered_.y0 + h.y0 * sy, h.y1 = rendered_.y0 + h.y1 * sy;
        }
    }
    // The subdirectories' screen masks (metacam's meta_edge.mask darkens the transitions).
    for (scene::Milo* m : subdirs_)
        for (scene::ScreenMask* sm : m->root().all<scene::ScreenMask>())
            if (pose_.showing(sm)) addMask(*sm);
    if (overlay) overlay(r);
    for (TextProbe& p : textProbes_) {
        p.viewProj = viewProj_;
        if (rendered_.texture.id) p.rect[0] = rendered_.x0, p.rect[1] = rendered_.y0, p.rect[2] = rendered_.x1, p.rect[3] = rendered_.y1;
    }
    texts_.collect();
}

void UiView::clothBounds(const scene::Mesh& mesh) {
    const std::vector<float>* v = cloth_ ? cloth_->vertices(mesh) : nullptr;
    if (!v) return;
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    const Mat4& m = viewProj_;
    for (size_t i = 0; i + 2 < v->size(); i += 16) {
        const float px = (*v)[i], py = (*v)[i + 1], pz = (*v)[i + 2];
        const float cx = m.at(0, 0) * px + m.at(0, 1) * py + m.at(0, 2) * pz + m.at(0, 3);
        const float cy = m.at(1, 0) * px + m.at(1, 1) * py + m.at(1, 2) * pz + m.at(1, 3);
        const float cw = m.at(3, 0) * px + m.at(3, 1) * py + m.at(3, 2) * pz + m.at(3, 3);
        if (cw <= 1e-4f) continue;
        const float sx = (cx / cw + 1) * 640.0f, sy = (1 - cy / cw) * 360.0f;
        x0 = std::min(x0, sx), x1 = std::max(x1, sx), y0 = std::min(y0, sy), y1 = std::max(y1, sy);
    }
    if (x1 > x0 && y1 > y0) clothX0_ = x0, clothY0_ = y0, clothX1_ = x1, clothY1_ = y1;
    if (SDL_getenv("GD05_CLOTHLOG") && cloth_->polls() % 60 == 1) {
        float zmin = 1e9f, zmax = -1e9f;
        for (size_t i = 0; i + 2 < v->size(); i += 16) zmin = std::min(zmin, (*v)[i + 2]), zmax = std::max(zmax, (*v)[i + 2]);
        SDL_Log("cloth %s: screen %.0f..%.0f x %.0f..%.0f, z %.1f..%.1f", mesh.name.c_str(), double(x0), double(x1), double(y0), double(y1), double(zmin), double(zmax));
    }
}

void UiView::addHit(const std::string& key, const float lo[3], const float hi[3], const Mat4& world) {
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int c = 0; c < 8; ++c) {
        const Vec3 p = world.transformPoint({c & 1 ? hi[0] : lo[0], c & 2 ? hi[1] : lo[1], c & 4 ? hi[2] : lo[2]});
        const Mat4& m = viewProj_;
        const float cx = m.at(0, 0) * p.x + m.at(0, 1) * p.y + m.at(0, 2) * p.z + m.at(0, 3);
        const float cy = m.at(1, 0) * p.x + m.at(1, 1) * p.y + m.at(1, 2) * p.z + m.at(1, 3);
        const float cw = m.at(3, 0) * p.x + m.at(3, 1) * p.y + m.at(3, 2) * p.z + m.at(3, 3);
        if (cw <= 1e-4f) return;  // behind the camera
        const float sx = (cx / cw + 1) * 640.0f, sy = (1 - cy / cw) * 360.0f;
        x0 = std::min(x0, sx), x1 = std::max(x1, sx), y0 = std::min(y0, sy), y1 = std::max(y1, sy);
    }
    if (x1 > x0 && y1 > y0) hits_.push_back({key, x0, y0, x1, y1});
}

void UiView::addLayoutHit(const std::string& key, const TextLayout& layout, const Mat4& world) {
    if (layout.verts.empty()) return;
    float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
    for (const scene::MeshVertex& v : layout.verts)
        for (int k = 0; k < 3; ++k) lo[k] = std::min(lo[k], v.pos[k]), hi[k] = std::max(hi[k], v.pos[k]);
    addHit(key, lo, hi, world);
}

void UiView::addMask(const scene::ScreenMask& m) {
    MaskDraw d;
    d.rgba = m.color;
    if (const scene::Mat* mat = findMat(m, m.mat)) {
        float rgb[3] = {mat->color[0], mat->color[1], mat->color[2]};
        float a = mat->color[3];
        if (const ScenePose::MatState* s = pose_.mat(mat)) {
            if (s->hasColor) rgb[0] = s->rgb[0], rgb[1] = s->rgb[1], rgb[2] = s->rgb[2];
            if (s->hasAlpha) a = s->alpha;
        }
        for (int k = 0; k < 3; ++k) d.rgba[size_t(k)] *= rgb[k];
        d.rgba[3] *= a;
        d.blend = mat->blend;
        // RndScreenMask draws its quad with the mat: a texture's alpha shapes it
        // (unlockvenue<n>.milo's matte.mask: black through light_gw.tex).
        if (!mat->diffuseTex.empty()) d.texture = cache_.texture(mat, mat->diffuseTex).id;
    }
    d.rect = m.screenRect;
    if (d.rgba[3] > 0.001f) masks_.push_back(d);
}

void UiView::drawObject(MaterialRenderer& r, const scene::Object& o, int depth) {
    if (depth > 16) return;
    // A proxy something draws through (the store's guitar.pxy) draws with
    // its group, whatever its own showing (store.milo's is off) [I: GH2's
    // GuitarDisplayPanel shows it as it loads the guitar].
    if (o.as<scene::UIProxy>())
        if (auto it = objectDraws.find(o.name); it != objectDraws.end() && it->second) return it->second(r);
    if (!pose_.showing(&o)) return;
    // An object holding an inline proxied directory (qp_endgame.milo's
    // player0..3) draws that directory's objects under its own transform;
    // one a TexRenderer draws into its texture (the song list, the slots)
    // is drawn there only.
    if (o.as<scene::DirObj>()) {
        if (!o.dir) return;
        for (const auto& [owner, sub] : o.dir->subdirs)
            if (sub && owner == o.name) {
                if (texRendered_.count(sub.get())) return;
                pose_.setDirBase(sub.get(), &o);
                // A RndDir with an environ draws its objects in it (RndDir's
                // `environ`, SyncProperty RB 0x821B3438) [C]: loading.milo's
                // four band icons (load_icon_*: intruments.env, a warm white
                // point light) were drawn in ornate_leader.grp's dim one.
                const auto* own = o.as<scene::DirObj>();
                const scene::Environ* env = nullptr;
                if (own->hasRnd && !own->rnd.environment.empty()) {
                    env = sub->find<scene::Environ>(own->rnd.environment);
                    if (!env) env = o.dir->resolve<scene::Environ>(own->rnd.environment);
                }
                const LightEnv saved = env ? r.lightEnv() : LightEnv{};
                if (env) r.setLightEnv(cache_.lightEnv(*env, &pose_));
                drawRoots(r, *sub);
                if (env) r.setLightEnv(saved);
                return;
            }
        return;
    }
    if (const auto* g = o.as<scene::Group>()) {
        // A group's environ lights its objects (RndGroup draws with its
        // environ selected; main.milo's buttons.grp: main_buttons.env, whose
        // lights give the buttons' metal words their look; without it the
        // panel's logo.env point light, near the words' plane, left their
        // first letters dark).
        const scene::Environ* env = nullptr;
        if (!g->environment.empty() && o.dir) env = o.dir->resolve<scene::Environ>(g->environment);
        const LightEnv saved = env ? r.lightEnv() : LightEnv{};
        if (env) r.setLightEnv(cache_.lightEnv(*env, &pose_));
        for (const std::string& name : g->objects)
            if (const scene::Object* c = o.dir ? o.dir->resolve(name) : find(name); c && c != &o) drawObject(r, *c, depth + 1);
        if (env) r.setLightEnv(saved);
        return;
    }
    if (const auto* m = o.as<scene::Mesh>()) {
        if (!cache_.mesh(m)) return;
        const auto mm = meshMat_.find(m->name);
        if (cloth_ && !m->bones.empty())
            if (const GpuMesh* g = cloth_->skinned(*m, pose_)) {
                drawMesh(r, cache_, *m, toMat4(scene::Xfm{}), &pose_, findMat(*m, mm != meshMat_.end() ? mm->second : m->mat), nullptr, g);
                ++meshesDrawn_;
                clothBounds(*m);
                return;
            }
        drawMesh(r, cache_, *m, toMat4(pose_.world(m)), &pose_, findMat(*m, mm != meshMat_.end() ? mm->second : m->mat));
        ++meshesDrawn_;
        return;
    }
    if (const auto* t = o.as<scene::Text>()) return drawText(r, *t);
    if (const auto* l = o.as<scene::BandLabel>()) {
        // Owner decision (D44): LOADING (loading.milo's loading.lbl, beside
        // loading.tnm) reads bright from the moment the loading screen shows:
        // its own focus colour (custom_colors 0xfdfdfd) in RB's UI environ
        // (ambient 1, no lights). RB draws it at its normal 0x3b3b3b under
        // ornate_leader.env (ambient 0), bright only while loading.tnm's
        // glint (loading.lit, range 35) crosses the word, ~4.5 s in.
        if (l->bigEndian && l->name == "loading.lbl" && l->textStyle == "custom_colors" && l->dir && l->dir->find<scene::TransAnim>("loading.tnm")) {
            std::array<float, 3> c{};
            if (styles_.textStyleColor(l->textStyle, l->customColors, scene::UiState::Focused, c)) {
                const LightEnv saved = r.lightEnv();
                r.setLightEnv(defaultEnvironment());
                drawLabel(r, *l, &l->band, c.data());
                r.setLightEnv(saved);
                return;
            }
        }
        // Rock Band: the text style's preset colour (BandTextComp) [C].
        std::array<float, 3> c{};
        if (l->bigEndian && styles_.textStyleColor(l->textStyle, l->customColors, state(l->name) == scene::UiState::Disabled ? scene::UiState::Disabled : scene::UiState::Normal, c))
            return drawLabel(r, *l, &l->band, c.data());
        return drawLabel(r, *l, &l->band, l->color.data());
    }
    if (const auto* b = o.as<scene::BandButton>()) {
        // RB's BandButton: its type's resource meshes per state (border,
        // inlay), then its text_mesh (a modelled word in the same resource,
        // e.g. button_main.milo's solo.mesh), then the label text (the
        // "null" font on the main menu) [C data: ui_objects.dtb BandButton
        // types; how the text mesh takes the state: I].
        drawStyleMeshes(r, o);
        if (!b->textMesh.empty()) {
            const scene::UiStyle style = styles_.style(b->cls, b->header.type);
            if (scene::Milo* res = resource(style.resourceFile.empty() ? style.classResourceFile : style.resourceFile))
                if (const scene::Mesh* m = res->find<scene::Mesh>(b->textMesh)) drawMeshAt(r, *m, nullptr, componentWorld(o));
        }
        return drawLabel(r, *b, &b->band, nullptr);
    }
    // RB labels (UILabel rev 8) carry their own text fields [C].
    if (const auto* u = o.as<scene::UILabel>()) return drawLabel(r, *u, u->bigEndian && u->labelRev >= 8 ? &u->style : nullptr, nullptr);
    if (o.as<scene::UIList>()) return drawList(r, o);
    if (o.as<scene::BandTextEntry>()) return drawTextEntry(r, o);
    if (const auto* sm = o.as<scene::ScreenMask>()) {
        const size_t before = masks_.size();
        addMask(*sm);
        if (maskNow && masks_.size() > before) {
            const MaskDraw d = masks_.back();
            masks_.pop_back();
            maskNow(d);
        }
        return;
    }
    if (o.cls == "BandHighlight") return drawHighlight(r, o);
    if (o.as<scene::UIProxy>() || o.as<scene::BandPlacer>()) {
        if (auto it = objectDraws.find(o.name); it != objectDraws.end() && it->second) it->second(r);
        return;
    }
    if (o.as<scene::UIPicture>() || o.as<scene::CheckBox>() || o.as<scene::BandSlider>()) return drawStyleMeshes(r, o);
}

// RB's BandHighlight (main_solomode.milo's highlight.hlit): its type's
// resource (resource/highlight.milo) under its panel's focused component:
// on a focus change it takes that component as its parent and slides there
// in anim_duration (0x822FF428 / 0x822FEE60 / 0x822FF4F8); each poll it is
// disabled unless its panel holds the UI's focus, then normal [C]. The
// state picks the bar's material by name, normal.mat (orange) or
// disabled.mat (white) [I: the resource's meshes name normal.mat; the
// slide is not drawn]. Only the resource's shown meshes draw: the bar is
// highlight_sub.mesh (its own textured material, inside the button's rim);
// highlight.mesh, the normal.mat frame, is hidden in the resource [C:
// resource/highlight.milo, its Draw showing flag].
void UiView::drawHighlight(MaterialRenderer& r, const scene::Object& o) {
    const scene::Object* target = nullptr;
    if (!focus_.empty() && o.dir) target = o.dir->resolve(focus_);
    if (!target || target == &o || !transOf(target)) return;
    const scene::UiStyle style = styles_.style(o.cls, o.header.type);
    scene::Milo* res = resource(style.resourceFile.empty() ? style.classResourceFile : style.resourceFile);
    if (!res) return;
    scene::Xfm world = componentWorld(o);
    const scene::Xfm at = componentWorld(*target);
    world.pos = at.pos;
    const scene::Mat* bar = res->find<scene::Mat>(uiFocus_ ? "normal.mat" : "disabled.mat");
    for (const auto& obj : res->root().objects)
        if (const auto* m = obj ? obj->as<scene::Mesh>() : nullptr) {
            // The mesh's own place in the resource (the bar stands upright
            // through its rotation), up its parents there.
            scene::Xfm local;
            int depth = 0;
            for (const scene::Object* p = m; p && depth < 8; ++depth) {
                const scene::Trans* t = transOf(p);
                if (!t) break;
                local = scene::compose(local, t->local);
                const scene::Object* up = t->parent.empty() ? nullptr : res->find(t->parent);
                p = up != p ? up : nullptr;
            }
            if (!m->draw.showing) continue;  // highlight.mesh (normal.mat's frame) is hidden in the resource
            drawMeshAt(r, *m, m->mat == "normal.mat" ? bar : nullptr, scene::compose(local, world));
        }
}

void UiView::drawText(MaterialRenderer& r, const scene::Text& t) {
    const scene::Font* font = t.dir && !t.font.empty() ? t.dir->resolve<scene::Font>(t.font) : nullptr;
    if (!font) return;
    // A plain Text component's string can be replaced too (set_text on a .txt).
    std::string str = t.text;
    if (auto it = overrides_.find(t.name); it != overrides_.end()) str = it->second;
    if (str.empty()) return;
    auto& lt = labels_[&t];
    if (!lt || lt->str != str) {
        if (!lt) lt = std::make_unique<LabelText>();
        lt->str = str;
        FontMetrics fm(*font);
        TextStyle st;
        st.size = t.size, st.leading = t.leading, st.wrapWidth = t.wrapWidth, st.italics = t.italics;
        st.align = t.align, st.capsMode = t.capsMode, st.markup = t.markup, st.color = t.color;
        lt->layout = layoutText(&fm, str, st);
        lt->font = font;
    }
    texts_.probeName = t.name;
    texts_.draw(r, &t, *font, lt->layout, toMat4(pose_.world(&t)));
    ++textsDrawn_;
}

scene::Xfm UiView::componentWorld(const scene::Object& o) const {
    scene::Xfm world = pose_.local(&o);
    const scene::Trans* t = transOf(&o);
    if (t && !t->parent.empty() && o.dir)
        if (const scene::Object* p = o.dir->resolve(t->parent); p && p != &o && transOf(p)) world = scene::compose(world, pose_.world(p));
    // BandButton selecting (RB 0x822D4848) [C]: its type's selecting_anim
    // (button_main.milo's selecting.tnm) sampled at endFrame x (select_frames
    // - left) / select_frames, its translation added to the button's.
    if (o.as<scene::BandButton>() && state(o.name) == scene::UiState::Selecting)
        if (const ::DataNode* a = styles_.typeProperty(o.cls, o.header.type, "selecting_anim"); a && a->children.size() > 1 && a->children[1].isText()) {
            const scene::UiStyle style = styles_.style(o.cls, o.header.type);
            if (scene::Milo* res = const_cast<UiView*>(this)->resource(style.resourceFile.empty() ? style.classResourceFile : style.resourceFile))
                if (const auto* ta = res->find<scene::TransAnim>(a->textAt(1))) {
                    const auto it = selectProgress_.find(o.name);
                    const float t01 = it != selectProgress_.end() ? std::clamp(it->second, 0.0f, 1.0f) : 0.0f;
                    std::array<float, 3> p{};
                    if (scene::sampleLinear(ta->transKeys, t01 * ScenePose::endFrame(ta), p))
                        for (size_t k = 0; k < 3; ++k) world.pos[k] += p[k];
                }
        }
    return world;
}

void UiView::drawLabel(MaterialRenderer& r, const scene::UILabel& l, const scene::BandText* band, const float* labelColor) {
    // The string: an override, the localized token, or the label's own text.
    const std::string str = labelString(l);
    if (str.empty()) return;

    auto& lt = labels_[&l];
    if (!lt || lt->str != str) {
        if (!lt) lt = std::make_unique<LabelText>();
        lt->str = str;
        lt->font = nullptr;
        // The template Text of the component's type (ui_styles.h).
        const scene::UiStyle style = styles_.style(l.cls, l.header.type);
        scene::Milo* res = resource(style.resourceFile);
        const scene::Text* tmpl = res && !style.textTemplate.empty() ? res->find<scene::Text>(style.textTemplate) : nullptr;
        if (!tmpl) {
            problem(l.name + ": no template text for type '" + l.header.type + "'");
            return;
        }
        const scene::Font* font = tmpl->dir ? tmpl->dir->resolve<scene::Font>(tmpl->font) : nullptr;
        if (!font) {
            problem(l.name + ": template " + tmpl->name + " has no font");
            return;
        }
        FontMetrics fm(*font);
        TextStyle st;
        // RB labels carry their own markup flag (UILabel rev 8) [C].
        st.italics = tmpl->italics, st.markup = tmpl->markup || (l.bigEndian && l.markup);
        if (band) {
            st.size = band->textSize, st.leading = band->leading, st.wrapWidth = band->wrapWidth;
            // kFitJust in a box two lines high or more (dialog.milo's
            // msg.lbl) wraps at its width, then shrinks to the box; in a
            // one-line box (the slots' PLAYER 1) it stays one line [I].
            if (band->fitText == 2 && band->width > 0 && band->height >= 1.8f * band->textSize * std::max(band->leading, 0.5f))
                st.wrapWidth = band->width;
            st.align = band->alignment, st.capsMode = band->allCaps ? 2 : 0;
            // RB's UILabel update (RB 0x8226E0A0) raises the font's kerning
            // (+0x1044) by the label's while it lays the text out, then puts
            // it back: the label's kerning adds to the font's [C].
            st.overrideKerning = true, st.kerning = fm.kerning() + band->kerning;
        } else {
            st.size = tmpl->size, st.leading = tmpl->leading, st.wrapWidth = tmpl->wrapWidth;
            st.align = tmpl->align, st.capsMode = tmpl->capsMode;
        }
        if (auto w = wrapWidths_.find(l.name); w != wrapWidths_.end()) st.wrapWidth = w->second;
        lt->layout = layoutText(&fm, str, st);
        if (band) {
            jitterText(lt->layout, band->jitterSeed, band->deltaX, band->deltaZ, band->deltaRot);
            lt->fit = fitScale(lt->layout, st, band->fitText, band->width, band->height);
        }
        lt->font = font;
    }
    if (!lt->font) return;

    // The component's world; a focused button grows by its panel's focus_scale.
    scene::Xfm local = pose_.local(&l);
    float color[3] = {1, 1, 1};
    const float* matColor = labelColor;
    const scene::Object* panel = milo_->root().self.get();
    if (const auto* b = l.as<scene::BandButton>(); b && panel) {
        const scene::UiState st = state(l.name);
        // Rock Band: the text style's preset colour for the state
        // (BandTextComp 0x822FFDF0, ui/color_presets.dta) [C].
        std::array<float, 3> c{};
        if (!(l.bigEndian && styles_.textStyleColor(l.textStyle, l.customColors, st, c))) c = styles_.buttonColor(*panel, st);
        color[0] = c[0], color[1] = c[1], color[2] = c[2];
        matColor = color;
        if (st == scene::UiState::Focused || st == scene::UiState::Selecting) {
            const float s = styles_.focusScale(*panel);
            local = scene::compose(scaleXfm(s, 1, s), local);
        }
    }
    scene::Xfm world = local;
    {
        // The parent chain above the component (pose_.world from its parent).
        const scene::Trans* t = transOf(&l);
        if (t && !t->parent.empty() && l.dir)
            if (const scene::Object* p = l.dir->resolve(t->parent); p && p != &l && transOf(p)) world = scene::compose(local, pose_.world(p));
    }
    world = scene::compose(scaleXfm(lt->fit[0], 1, lt->fit[1]), world);
    const Mat4 wm = toMat4(world);
    texts_.probeName = l.name;
    setProbeStyle(band, *lt);
    texts_.draw(r, &l, *lt->font, lt->layout, wm, matColor);
    // Buttons, and RB's labels too (the help bar's actions take clicks).
    if (l.as<scene::BandButton>() || l.cls == "UIButton" || l.bigEndian) addLayoutHit(l.name, lt->layout, wm);
    ++textsDrawn_;
}

// The label's text fields for the next text probe (the songlist_text walk).
void UiView::setProbeStyle(const scene::BandText* band, const LabelText& lt) {
    TextProbe& p = texts_.probeStyle;
    p = TextProbe{};
    if (!texts_.probes || !band) return;
    p.hasStyle = true, p.text = lt.str, p.font = lt.font;
    p.dataSize = band->textSize, p.leading = band->leading, p.boxW = band->width, p.boxH = band->height, p.kerning = band->kerning;
    p.fitType = band->fitText, p.align = band->alignment, p.capsMode = band->allCaps ? 2 : 0;
    p.fitX = lt.fit[0], p.fitZ = lt.fit[1];
}

float UiView::templateWidth(const scene::Text& tmpl, const std::string& str) {
    const scene::Font* font = tmpl.dir ? tmpl.dir->resolve<scene::Font>(tmpl.font) : nullptr;
    if (!font || str.empty()) return 0;
    FontMetrics fm(*font);
    TextStyle st;
    st.size = tmpl.size, st.leading = tmpl.leading, st.wrapWidth = tmpl.wrapWidth, st.italics = tmpl.italics;
    st.align = tmpl.align, st.capsMode = tmpl.capsMode, st.markup = tmpl.markup, st.color = tmpl.color;
    return layoutText(&fm, str, st).maxWidth();
}

float UiView::drawTemplate(MaterialRenderer& r, const std::string& key, const scene::Text& tmpl, const std::string& str,
                           const scene::Xfm& world, const float* rgb, float alpha, int align, const scene::BandText* style) {
    const scene::Font* font = tmpl.dir ? tmpl.dir->resolve<scene::Font>(tmpl.font) : nullptr;
    if (!font || str.empty()) return 0;
    auto& lt = keyed_[key];
    if (!lt || lt->str != str || lt->font != font) {
        if (!lt) lt = std::make_unique<LabelText>();
        lt->str = str;
        lt->font = font;
        FontMetrics fm(*font);
        TextStyle st;
        st.size = tmpl.size, st.leading = tmpl.leading, st.wrapWidth = tmpl.wrapWidth, st.italics = tmpl.italics;
        st.align = align >= 0 ? align : tmpl.align, st.capsMode = tmpl.capsMode, st.markup = tmpl.markup, st.color = tmpl.color;
        // An RB label's own text fields (UILabel rev 8: text_size, alignment,
        // leading, kerning, fit_type, width, height) [C].
        if (style) {
            st.size = style->textSize, st.leading = style->leading, st.wrapWidth = style->wrapWidth;
            st.align = align >= 0 ? align : int(style->alignment), st.capsMode = style->allCaps ? 2 : tmpl.capsMode;
            st.overrideKerning = true, st.kerning = fm.kerning() + style->kerning;  // RB 0x8226E0A0: added to the font's [C]
        }
        lt->layout = layoutText(&fm, str, st);
        lt->fit = style ? fitScale(lt->layout, st, style->fitText, style->width, style->height) : std::array<float, 2>{1, 1};
    }
    const float tint[4] = {1, 1, 1, alpha};
    const Mat4 wm = toMat4(scene::compose(scaleXfm(lt->fit[0], 1, lt->fit[1]), world));
    texts_.probeName = key;
    setProbeStyle(style, *lt);
    texts_.draw(r, lt.get(), *font, lt->layout, wm, rgb, alpha < 1 ? tint : nullptr);
    addLayoutHit(key, lt->layout, wm);
    ++textsDrawn_;
    return lt->layout.maxWidth();
}

float UiView::templateWidth(const std::string& key, const scene::Text& tmpl, const std::string& str) {
    const scene::Font* font = tmpl.dir ? tmpl.dir->resolve<scene::Font>(tmpl.font) : nullptr;
    if (!font || str.empty()) return 0;
    auto& lt = keyed_[key];
    if (!lt || lt->str != str || lt->font != font) {
        if (!lt) lt = std::make_unique<LabelText>();
        lt->str = str;
        lt->font = font;
        FontMetrics fm(*font);
        TextStyle st;
        st.size = tmpl.size, st.leading = tmpl.leading, st.wrapWidth = tmpl.wrapWidth, st.italics = tmpl.italics;
        st.align = tmpl.align, st.capsMode = tmpl.capsMode, st.markup = tmpl.markup, st.color = tmpl.color;
        lt->layout = layoutText(&fm, str, st);
    }
    return lt->layout.maxWidth();
}

void UiView::drawMeshAt(MaterialRenderer& r, const scene::Mesh& mesh, const scene::Mat* mat, const scene::Xfm& world) {
    if (!cache_.mesh(&mesh)) return;
    drawMesh(r, cache_, mesh, toMat4(world), &pose_, mat ? mat : findMat(mesh, mesh.mat));
    ++meshesDrawn_;
}

void UiView::drawStyleMeshes(MaterialRenderer& r, const scene::Object& c) {
    const scene::UiStyle style = styles_.style(c.cls, c.header.type);
    std::string file = style.resourceFile.empty() ? style.classResourceFile : style.resourceFile;
    scene::Milo* res = resource(file);
    if (!res || !style.node) return;
    const scene::UiState st = state(c.name);
    const bool checked = checks_.count(c.name) && checks_[c.name];
    // (mesh "x.mesh") with (mat_on ...) / (mat_off ...) for a check box;
    // (meshes ("x.mesh" (normal "x.mat") (focused ...) ...) ...) for a slider.
    std::vector<std::pair<std::string, std::string>> meshes;
    if (!style.mesh.empty()) {
        std::string mat;
        const char* key = c.as<scene::CheckBox>() && checked ? "mat_on" : "mat_off";
        if (const ::DataNode* m = style.node->find(key); m && m->children.size() > 1 && m->children[1].isText()) mat = m->textAt(1);
        meshes.push_back({style.mesh, mat});
    }
    if (const ::DataNode* list = style.node->find("meshes"))
        for (size_t i = 1; i < list->children.size(); ++i) {
            const ::DataNode& e = list->children[i];
            if (!e.isList() || e.children.empty() || !e.children[0].isText()) continue;
            std::string mat;
            const ::DataNode* n = e.find(stateKey(st));
            if (!n) n = e.find("normal");
            if (n && n->children.size() > 1 && n->children[1].isText()) mat = n->textAt(1);
            meshes.push_back({e.children[0].s, mat});
        }
    const scene::Xfm cw = componentWorld(c);
    // A picture's tex_file replaces its mesh's texture (UIPicture set_tex).
    std::string texRel = style.texFile;
    if (auto it = pictureTex_.find(c.name); it != pictureTex_.end()) texRel = it->second;
    const scene::Tex* tex = c.as<scene::UIPicture>() ? textureFile(texRel) : nullptr;
    // A slider's pod: its TransAnim (the type's resource "<x>_slider.tnm")
    // at current / (steps - 1) of its range.
    ScenePose podPose;
    const auto* slider = c.as<scene::BandSlider>();
    if (slider) {
        auto it = sliders_.find(c.name);
        const float frac = it != sliders_.end() && it->second.second > 1 ? float(it->second.first) / float(it->second.second - 1) : 0.0f;
        for (scene::TransAnim* ta : res->root().all<scene::TransAnim>()) podPose.setFrame(ta, frac * ScenePose::endFrame(ta));
    }
    for (const auto& [name, matName] : meshes) {
        const scene::Mesh* m = res->find<scene::Mesh>(name);
        if (!m || !cache_.mesh(m)) continue;
        const scene::Mat* mat = matName.empty() ? findMat(*m, m->mat) : findMat(*m, matName);
        ScenePose texPose;
        if (tex && mat) texPose.matState(mat).tex = tex;
        // A check box's / picture's mesh takes the component's place (the
        // component carries the same facing rotation the resource mesh has,
        // e.g. the video settings' lefty boxes) [I]; a slider's pod moves by
        // its TransAnim under the slider, through its resource parents
        // (slider_audio.milo: slider.grp turns the XY quads to face the
        // camera; the audio sliders have no turn of their own) [C data].
        const scene::Xfm world = slider ? scene::compose(podPose.world(m), cw) : cw;
        drawMesh(r, cache_, *m, toMat4(world), tex ? &texPose : nullptr, mat);
        if (!m->verts.empty()) {
            float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
            for (const scene::MeshVertex& v : m->verts)
                for (int k = 0; k < 3; ++k) lo[k] = std::min(lo[k], v.pos[k]), hi[k] = std::max(hi[k], v.pos[k]);
            addHit(c.name, lo, hi, toMat4(world));
        }
        ++meshesDrawn_;
    }
    // A slider's label: its text_token in the type's template text, placed
    // by the template's own transform under the slider.
    if (!slider || slider->textToken.empty() || style.textTemplate.empty()) return;
    const scene::Text* tmpl = res->find<scene::Text>(style.textTemplate);
    if (!tmpl) return;
    drawTemplate(r, c.name + "#label", *tmpl, locale_.text(slider->textToken), scene::compose(tmpl->trans.local, cw));
}

void UiView::drawList(MaterialRenderer& r, const scene::Object& o) {
    const auto* list = o.as<scene::UIList>();
    auto ls = lists_.find(o.name);
    if (!list || ls == lists_.end() || ls->second.rows.empty()) {
        problem(o.name + ": a list with no rows (no provider set)");
        return;
    }
    const scene::UiStyle style = styles_.style(o.cls, o.header.type);
    scene::Milo* res = resource(style.resourceFile.empty() ? style.classResourceFile : style.resourceFile);
    if (!res || !style.node) {
        problem(o.name + ": list type '" + o.header.type + "' has no resource");
        return;
    }
    // (element ((text "header.txt") (text "list.txt") (mesh "stars_gold.mesh") ...)):
    // per element its kind, name and colour column.
    struct Element {
        bool mesh = false;
        std::string name;
    };
    std::vector<Element> elements;
    if (const ::DataNode* el = style.node->find("element"); el && el->children.size() > 1 && el->children[1].isList())
        for (const ::DataNode& e : el->children[1].children)
            if (e.isList() && e.children.size() > 1 && e.children[0].isText() && e.children[1].isText())
                elements.push_back({e.children[0].s == "mesh", e.children[1].s});
    const ::DataNode* colors = style.node->find("colors");
    const ::DataNode* colorList = colors && colors->children.size() > 1 && colors->children[1].isList() ? &colors->children[1] : nullptr;
    bool vertical = true;
    if (const ::DataNode* v = style.node->find("vertical"); v && v->children.size() > 1) {
        const ::DataNode& c = v->children[1];
        vertical = !((c.type == ::DataNode::Type::Int && c.i == 0) || (c.isText() && c.s == "FALSE"));
    }
    const scene::UiState st = state(o.name);
    const scene::Xfm lw = componentWorld(o);
    ListState& s = ls->second;
    const int shown = list->displayNum > 0 ? list->displayNum : int(s.rows.size());
    // A list with more rows than its display_num shows display_num of them
    // from `first`, moved to keep the selected row in view (RB's song list:
    // display_num 7 of 60-odd rows; it only ever drew the first 7 and the
    // highlight left the list) [I: the window moves by the rows it must].
    if (int(s.rows.size()) > shown) {
        if (s.selected >= 0 && s.selected < s.first) s.first = s.selected;
        if (s.selected >= s.first + shown) s.first = s.selected - shown + 1;
        s.first = std::clamp(s.first, 0, int(s.rows.size()) - shown);
    } else {
        s.first = 0;
    }
    auto slotOf = [&](int row) { return s.rows[size_t(row)].slot > -1 ? s.rows[size_t(row)].slot : float(row); };
    const float firstSlot = s.first > 0 ? slotOf(s.first) : 0.0f;
    // The selected row's highlight: (highlight "highlight.mesh" (colors
    // (normal r g b) (selecting ...) (disabled ...))) [C data], the mesh at
    // its own place in the row, tinted by the list's state.
    if (const ::DataNode* hl = style.node->find("highlight"); hl && hl->children.size() > 1 && hl->children[1].isText() &&
                                                               s.selected >= s.first && s.selected < int(s.rows.size()) && s.selected < s.first + shown)
        if (const scene::Mesh* hm = res->find<scene::Mesh>(hl->children[1].s)) {
            float tint[4] = {1, 1, 1, 1};
            if (const ::DataNode* hc = hl->find("colors")) {
                const ::DataNode* c = hc->find(st == scene::UiState::Selecting ? "selecting" : st == scene::UiState::Disabled ? "disabled" : "normal");
                if (c && c->children.size() >= 4)
                    for (int k = 0; k < 3; ++k) tint[k] = c->floatAt(size_t(k + 1));
            }
            const float off = (slotOf(s.selected) - firstSlot) * list->spacing;
            const scene::Xfm at = scene::compose(hm->trans.local, scene::compose(vertical ? moveXfm(0, 0, -off) : moveXfm(off, 0, 0), lw));
            if (cache_.mesh(hm)) {
                drawMesh(r, cache_, *hm, toMat4(at), &pose_, findMat(*hm, hm->mat), tint);
                ++meshesDrawn_;
            }
        }
    for (int row = s.first; row < int(s.rows.size()) && row < s.first + shown; ++row) {
        const ListRow& lr = s.rows[size_t(row)];
        const float off = (slotOf(row) - firstSlot) * list->spacing;
        const scene::Xfm rowWorld = scene::compose(vertical ? moveXfm(0, 0, -off) : moveXfm(off, 0, 0), lw);
        const char* kind = lr.disabled ? "disabled" : row == s.selected ? "highlight" : "normal";
        for (size_t e = 0; e < elements.size(); ++e) {
            float rgb[3] = {1, 1, 1};
            bool hasRgb = false;
            if (colorList && e < colorList->children.size()) {
                const ::DataNode& col = colorList->children[e];
                const ::DataNode* k = col.find(kind);
                if (!k) k = col.find("normal");
                if (k) {
                    const ::DataNode* sc = k->find(stateKey(st));
                    if (!sc) sc = k->find("normal");
                    hasRgb = rgbOf(sc, rgb);
                }
            }
            if (elements[e].mesh) {
                auto it = lr.mats.find(elements[e].name);
                if (it == lr.mats.end() || it->second.empty()) continue;
                const scene::Mesh* m = res->find<scene::Mesh>(elements[e].name);
                if (!m) continue;
                drawMeshAt(r, *m, findMat(*m, it->second), scene::compose(m->trans.local, rowWorld));
                continue;
            }
            auto it = lr.texts.find(elements[e].name);
            if (it == lr.texts.end() || it->second.empty()) continue;
            const scene::Text* tmpl = res->find<scene::Text>(elements[e].name);
            scene::Xfm at = tmpl ? tmpl->trans.local : scene::Xfm{};
            const scene::BandText* style = nullptr;
            // RB's list elements are labels (list_song.milo's list.lbl): the
            // label's own type gives the template Text (and its font) [C data].
            if (!tmpl)
                if (const auto* lbl = res->find<scene::UILabel>(elements[e].name)) {
                    const scene::UiStyle ls = styles_.style(lbl->cls, lbl->header.type);
                    if (scene::Milo* lres = resource(ls.resourceFile.empty() ? ls.classResourceFile : ls.resourceFile))
                        tmpl = ls.textTemplate.empty() ? nullptr : lres->find<scene::Text>(ls.textTemplate);
                    at = lbl->trans.local;
                    style = &lbl->style;
                }
            if (!tmpl) continue;
            // A heading row's heading (list_song.milo's heading.lbl, 17 units
            // under list.lbl) in its own slot sits where the songs' names
            // do: lower, it ran into the first song [I].
            if (lr.header && elements[e].name == "heading.lbl")
                if (const scene::Object* first = res->find(elements[0].name)) {
                    if (const auto* fl = first->as<scene::UILabel>()) at.pos[2] = fl->trans.local.pos[2];
                    else if (const auto* ft = first->as<scene::Text>()) at.pos[2] = ft->trans.local.pos[2];
                }
            drawTemplate(r, o.name + "#" + std::to_string(row) + "#" + elements[e].name, *tmpl, it->second,
                         scene::compose(at, rowWorld), hasRgb ? rgb : nullptr, 1, -1, style);
        }
    }
}

void UiView::drawTextEntry(MaterialRenderer& r, const scene::Object& o) {
    auto es = entries_.find(o.name);
    if (es == entries_.end()) return;
    const scene::UiStyle style = styles_.style(o.cls, o.header.type);
    scene::Milo* res = resource(style.resourceFile.empty() ? style.classResourceFile : style.resourceFile);
    if (!res || !style.node) return;
    std::string tmplName;
    if (const ::DataNode* t = style.node->find("text"); t && t->children.size() > 1 && t->children[1].isText()) tmplName = t->children[1].s;
    const scene::Text* tmpl = tmplName.empty() ? nullptr : res->find<scene::Text>(tmplName);
    if (!tmpl) return;
    // The component's two Texts copy the template's font and style at the
    // component's place, aligned middle left (0x21, BandTextEntry Update
    // 0x82144558) whatever the template's (entry_profile.txt is centred,
    // 0x22), so the name grows left to right; the entered text in
    // entered_color, the character being picked in dynamic_color
    // (0x82141F78), moved by the entered text's width (0x821424B0) [C].
    const scene::Xfm cw = componentWorld(o);
    const EntryState& e = es->second;
    const auto* te = o.as<scene::BandTextEntry>();
    const float entered[3] = {te->enteredColor[0], te->enteredColor[1], te->enteredColor[2]};
    const float dynamic[3] = {te->dynamicColor[0], te->dynamicColor[1], te->dynamicColor[2]};
    constexpr int kMiddleLeft = 0x21;
    const float w = e.entered.empty() ? 0.0f : drawTemplate(r, o.name + "#entered", *tmpl, e.entered, cw, entered, 1, kMiddleLeft);
    if (e.done || e.picking.empty()) return;
    // The character being picked, after the text, between the up / down
    // arrows. GH2 moves the picked Text and both arrows by the entered
    // text's width (0x821424B0); the arrows keep their own x and are put
    // arrow_offset above (char_back) / below (char_next) the picked Text's
    // extent, scaled by text_scale about its middle (0x82142218), the
    // picked Text itself scaled by text_scale (0x82142120) [C].
    scene::Xfm scaled;  // text_scale on the picked Text (0x82142120)
    for (float& v : scaled.rot) v *= te->textScale;
    const scene::Xfm at = scene::compose(scaled, scene::compose(moveXfm(w, 0, 0), cw));
    drawTemplate(r, o.name + "#picking", *tmpl, e.picking, at, dynamic, 1, kMiddleLeft);
    float top = 0, bottom = 0;
    if (const auto it = keyed_.find(o.name + "#picking"); it != keyed_.end() && it->second) {
        TextStyle st;
        st.size = tmpl->size, st.leading = tmpl->leading;
        it->second->layout.extent(st, top, bottom);
        const float mid = (top + bottom) * 0.5f;
        // The bottom's "- mid" is GH2's (0x82142218): one line at the
        // template's place gives arrows symmetric about the entry's middle.
        top = (top - mid) * te->textScale + mid, bottom = (bottom - mid) * te->textScale - mid;
    }
    for (const char* arrow : {"char_back.mesh", "char_next.mesh"})
        if (const scene::Mesh* m = res->find<scene::Mesh>(arrow)) {
            scene::Xfm local = m->trans.local;
            local.pos[2] = arrow[5] == 'b' ? top + te->arrowOffset : bottom - te->arrowOffset;
            drawMeshAt(r, *m, nullptr, scene::compose(local, scene::compose(moveXfm(w, 0, 0), cw)));
        }
}

}  // namespace gfx
