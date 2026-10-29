// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/ui_view.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "data/bytes.h"
#include "data/game_data.h"
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

UiView::~UiView() = default;

void UiView::problem(const std::string& what) {
    if (std::find(problems_.begin(), problems_.end(), what) == problems_.end()) problems_.push_back(what);
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
    return out;
}

const scene::Tex* UiView::textureFile(const std::string& rel) {
    if (rel.empty()) return nullptr;
    if (auto it = texFiles_.find(rel); it != texFiles_.end()) return it->second.get();
    // "image/og/x.bmp" -> "ui/image/og/gen/x.bmp_ps2" (og -> ng as for milos).
    std::string p = scene::resolveMiloRelative("ui/x.milo", rel);
    const size_t slash = p.rfind('/');
    p = p.substr(0, slash + 1) + "gen/" + p.substr(slash + 1) + "_ps2";
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
            tex->bitmap = scene::readBitmap(r);
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
            const scene::Object* a = t->dir ? t->dir->resolve(t->anim) : nullptr;
            if (!a) a = find(t->anim);
            if (a) out.push_back({a, t->blockTransition, ScenePose::endFrame(a)});
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
    const scene::Cam* c = own && own->hasPanel ? (find(own->panel.cam) ? find(own->panel.cam)->as<scene::Cam>() : nullptr) : nullptr;
    if (!c) return defaultCamera();
    Camera cam;
    cam.world = toMat4(pose_.world(c));
    cam.yFov = c->yFov, cam.nearPlane = c->nearPlane, cam.farPlane = c->farPlane;
    return cam;
}

LightEnv UiView::environment() {
    const auto* own = milo_ && milo_->root().self ? milo_->root().self->as<scene::DirObj>() : nullptr;
    if (own && own->hasRnd)
        if (const scene::Object* e = find(own->rnd.environment); e && e->as<scene::Environ>())
            return cache_.lightEnv(*e->as<scene::Environ>(), &pose_);
    return LightEnv{};
}

void UiView::setText(const std::string& component, const std::string& text) {
    // An empty string is a real (blank) text: set_localized_text "".
    overrides_[component] = text;
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
    if (!milo_) return;
    const Camera cam = camera();
    viewProj_ = cam.viewProj(false);
    hitAspect_ = cam.aspect;
    const scene::Directory& root = milo_->root();
    // The drawables no group of the panel holds, by draw order.
    std::set<std::string> held;
    for (const auto& o : root.objects)
        if (const auto* g = o->as<scene::Group>())
            for (const std::string& m : g->objects) held.insert(m);
    std::vector<const scene::Object*> roots;
    for (const auto& o : root.objects)
        if (!held.count(o->name) && (drawOf(o.get()) || o->as<scene::Text>())) roots.push_back(o.get());
    std::stable_sort(roots.begin(), roots.end(), [](const scene::Object* a, const scene::Object* b) {
        const scene::Draw* da = drawOf(a);
        const scene::Draw* db = drawOf(b);
        return (da ? da->order : 0) < (db ? db->order : 0);
    });
    for (const scene::Object* o : roots) drawObject(r, *o, 0);
    // The subdirectories' screen masks (metacam's meta_edge.mask darkens the transitions).
    for (scene::Milo* m : subdirs_)
        for (scene::ScreenMask* sm : m->root().all<scene::ScreenMask>())
            if (pose_.showing(sm)) addMask(*sm);
    if (overlay) overlay(r);
    texts_.collect();
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
        // Menu units of the 16:9 frame: the picture (4:3 or 16:9) is centred in it.
        const float sx = 640.0f + cx / cw * 360.0f * hitAspect_, sy = (1 - cy / cw) * 360.0f;
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
    if (const auto* g = o.as<scene::Group>()) {
        for (const std::string& name : g->objects)
            if (const scene::Object* c = o.dir ? o.dir->resolve(name) : find(name); c && c != &o) drawObject(r, *c, depth + 1);
        return;
    }
    if (const auto* m = o.as<scene::Mesh>()) {
        if (!cache_.mesh(m)) return;
        const auto mm = meshMat_.find(m->name);
        drawMesh(r, cache_, *m, toMat4(pose_.world(m)), &pose_, findMat(*m, mm != meshMat_.end() ? mm->second : m->mat));
        ++meshesDrawn_;
        return;
    }
    if (const auto* t = o.as<scene::Text>()) return drawText(r, *t);
    if (const auto* l = o.as<scene::BandLabel>()) return drawLabel(r, *l, &l->band, l->color.data());
    if (const auto* b = o.as<scene::BandButton>()) return drawLabel(r, *b, &b->band, nullptr);
    if (const auto* u = o.as<scene::UILabel>()) return drawLabel(r, *u, nullptr, nullptr);
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
    if (o.as<scene::UIProxy>() || o.as<scene::BandPlacer>()) {
        if (auto it = objectDraws.find(o.name); it != objectDraws.end() && it->second) it->second(r);
        return;
    }
    if (o.as<scene::UIPicture>() || o.as<scene::CheckBox>() || o.as<scene::BandSlider>()) return drawStyleMeshes(r, o);
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
    texts_.draw(r, &t, *font, lt->layout, toMat4(pose_.world(&t)));
    ++textsDrawn_;
}

scene::Xfm UiView::componentWorld(const scene::Object& o) const {
    scene::Xfm world = pose_.local(&o);
    const scene::Trans* t = transOf(&o);
    if (t && !t->parent.empty() && o.dir)
        if (const scene::Object* p = o.dir->resolve(t->parent); p && p != &o && transOf(p)) world = scene::compose(world, pose_.world(p));
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
        st.italics = tmpl->italics, st.markup = tmpl->markup;
        if (band) {
            st.size = band->textSize, st.leading = band->leading, st.wrapWidth = band->wrapWidth;
            st.align = band->alignment, st.capsMode = band->allCaps ? 2 : 0;
            st.overrideKerning = true, st.kerning = band->kerning;
        } else {
            st.size = tmpl->size, st.leading = tmpl->leading, st.wrapWidth = tmpl->wrapWidth;
            st.align = tmpl->align, st.capsMode = tmpl->capsMode;
        }
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
        const auto c = styles_.buttonColor(*panel, st);
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
    texts_.draw(r, &l, *lt->font, lt->layout, wm, matColor);
    if (l.as<scene::BandButton>() || l.cls == "UIButton") addLayoutHit(l.name, lt->layout, wm);
    ++textsDrawn_;
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
                           const scene::Xfm& world, const float* rgb, float alpha, int align) {
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
        lt->layout = layoutText(&fm, str, st);
    }
    const float tint[4] = {1, 1, 1, alpha};
    const Mat4 wm = toMat4(world);
    texts_.draw(r, lt.get(), *font, lt->layout, wm, rgb, alpha < 1 ? tint : nullptr);
    addLayoutHit(key, lt->layout, wm);
    ++textsDrawn_;
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
        // its TransAnim under the slider.
        const scene::Xfm world = slider ? scene::compose(podPose.local(m), cw) : cw;
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
    if (!list || ls == lists_.end() || ls->second.rows.empty()) return;
    const scene::UiStyle style = styles_.style(o.cls, o.header.type);
    scene::Milo* res = resource(style.resourceFile.empty() ? style.classResourceFile : style.resourceFile);
    if (!res || !style.node) return;
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
    const ListState& s = ls->second;
    const int shown = list->displayNum > 0 ? list->displayNum : int(s.rows.size());
    for (int row = 0; row < int(s.rows.size()) && row < shown; ++row) {
        const ListRow& lr = s.rows[size_t(row)];
        const float off = (lr.slot > -1 ? lr.slot : float(row)) * list->spacing;
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
            if (!tmpl) continue;
            drawTemplate(r, o.name + "#" + std::to_string(row) + "#" + elements[e].name, *tmpl, it->second,
                         scene::compose(tmpl->trans.local, rowWorld), hasRgb ? rgb : nullptr);
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
