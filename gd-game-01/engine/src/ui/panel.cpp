#include "ui/panel.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "data/bitmap.h"
#include "data/game_data.h"

Panel::Panel(Renderer& renderer, const GameData& data, UiResources& res, const std::string& name, Scene* shared)
    : renderer_(renderer), res_(res), name_(name), shared_(shared), data_(data) {
    // "x" is ghui/x.gh; a path ("hud/gen/hud.rnd_ps2") is used as is.
    std::string path = name.find('/') == std::string::npos ? "ghui/" + name + ".gh" : name;
    if (!data.exists(path)) throw std::runtime_error("no panel " + path);
    if (name.find('/') != std::string::npos) {
        name_ = name.substr(name.rfind('/') + 1);
        name_ = name_.substr(0, name_.find('.'));
    }
    scene_ = std::make_unique<Scene>(data.read(path));
    scene_->loadExternalTextures(data, path.rfind("ghui/", 0) == 0 ? "ghui" : path.substr(0, path.find('/')));
    sr_ = std::make_unique<SceneRenderer>(renderer, *scene_);
    sr_->setUiTextures(true);
    sr_->setCustomDraw([this](const SceneObject& o, const Mat4& vp, const Mat4& model) { drawComponent(o, vp, model); });
    camera_ = sr_->firstCamera();
    anims_ = std::make_unique<AnimPlayer>(std::vector<Scene*>{scene_.get()});
    // [I] The splash screen's legal lines carry the GH1RE notice: five lines
    // over the five legal labels (legal4 / legal5 are hidden in the original),
    // at the original size, a little lower and wider spaced so the extra
    // lines clear PRESS ANY BUTTON; each kept on one line.
    if (name_ == "splash") {
        static const char* const kLegal[][2] = {
            {"splash_legal4.lbl", "-GH1RE- offers a NON PROFIT, 100% free and open source labor of love"},
            {"splash_legal5.lbl", "for you to play a modern version of a timeless classic."},
            {"splash_legal2.lbl", "We do not endorse or support piracy, find an original copy of the game to play."},
            {"splash_legal3.lbl", "\xa9" "2005 RedOctane Inc.  ALL RIGHTS RESERVED"},
            {"splash_legal.lbl", "Original game engine by \xa9" "2005 Harmonix.  Protected by US Patent number 6,429,863."}};
        float z = -182;
        for (const auto& [label, line] : kLegal) {
            if (SceneObject* o = scene_->find(label); o && o->ui) o->ui->width = 2000;  // no wrapping
            setText(label, line);
            setShowing(label, true);
            setLocalPos(label, {0, 0, z});
            z -= 13.5f;
        }
    }
}

float Panel::endFrame(const std::string& object, int depth) {
    const SceneObject* o = scene_->find(object);
    if (!o || depth > 8) return 0;
    float end = anims_->endFrame(object);
    if (o->view)
        for (const std::string& a : o->view->anim.anims) end = std::max(end, endFrame(a, depth + 1));
    return end;
}

void Panel::setFrame(const std::string& object, float frame) {
    frames_[object] = frame;
    anims_->setFrame(object, frame);
    anims_->updateTransforms();
}

void Panel::setLocalPos(const std::string& object, const std::array<float, 3>& pos) {
    SceneObject* o = scene_->find(object);
    Transformable* t = o ? o->transformable() : nullptr;
    if (!t) return;
    t->local.pos = pos;
    anims_->markMoved(object);
    anims_->updateTransforms();
}

void Panel::setLocalXfm(const std::string& object, const Xfm& local) {
    SceneObject* o = scene_->find(object);
    Transformable* t = o ? o->transformable() : nullptr;
    if (!t) return;
    t->local = local;
    anims_->markMoved(object);
    anims_->updateTransforms();
}

void Panel::setLocalScale(const std::string& object, const std::array<float, 3>& scale) {
    SceneObject* o = scene_->find(object);
    Transformable* t = o ? o->transformable() : nullptr;
    if (!t) return;
    for (int r = 0; r < 3; ++r) {
        float* row = &t->local.rot[size_t(r * 3)];
        float l = std::sqrt(row[0] * row[0] + row[1] * row[1] + row[2] * row[2]);
        if (l <= 0) row[0] = r == 0, row[1] = r == 1, row[2] = r == 2, l = 1;
        for (int k = 0; k < 3; ++k) row[k] = row[k] / l * scale[size_t(r)];
    }
    anims_->markMoved(object);
    anims_->updateTransforms();
}

UiResources::State Panel::state(const std::string& component) const {
    auto it = states_.find(component);
    return it == states_.end() ? UiResources::Normal : it->second;
}

std::string Panel::text(const std::string& component) const {
    if (auto it = texts_.find(component); it != texts_.end()) return it->second;
    const SceneObject* o = scene_->find(component);
    if (o && o->ui) return res_.localize(o->ui->text);
    if (o && o->text) return o->text->text;
    return {};
}

void Panel::setShowing(const std::string& object, bool showing) {
    if (SceneObject* o = scene_->find(object); o && o->drawable()) o->drawable()->showing = showing;
}

void Panel::draw(const std::string& view) {
    Mat4 vp;
    if (!viewProj(vp)) return;
    // The shared ui.env ambient (ui.enm: the transition fade, turn_off_ambient /
    // reset_ambient) lights the panel's own ui.env.
    if (const SceneObject* se = sharedObject("ui.env"); se && se->environment)
        if (SceneObject* own = scene_->find("ui.env"); own && own->environment) own->environment->ambient = se->environment->ambient;
    // The camera for billboards and reflection-map texgen.
    if (const SceneObject* own = scene_->find(camera_); own && own->cam) {
        Xfm cam = own->cam->trans.world;
        if (const SceneObject* sc = sharedObject("meta.cam"); sc && sc->cam) cam.rot = compose(sc->cam->trans.local, Xfm{cam.rot, {}}).rot;
        sr_->setCamera(cam);
    }
    sr_->draw(view.empty() ? name_ + ".view" : view, vp);
}

void Panel::drawComponent(const SceneObject& o, const Mat4& viewProj, const Mat4& model) {
    const TextObj* tmpl = nullptr;
    TextStyle ts;
    std::string fontName;
    const Transformable* trans = o.transformable();
    std::array<float, 3> rgb{1, 1, 1};
    if (o.ui) {
        const UiObj& u = *o.ui;
        if (u.kind == UiObj::Kind::Picture || u.kind == UiObj::Kind::PictureToggle) {
            // RndPictureEx: the style's mesh (its disabled_mesh while kDisabled) at the component.
            const UiResources::Style& st = res_.style(u.kind == UiObj::Kind::Picture ? "pictureex" : "picturetoggle", u.style);
            const std::string& mesh = state(o.name) == UiResources::Disabled && !st.disabledMesh.empty() ? st.disabledMesh : st.mesh;
            auto tex = texFiles_.find(o.name);
            if (!mesh.empty() && trans)
                res_.drawMesh(mesh, viewProj, model * Mat4::fromRowXfm(trans->world.rot, trans->world.pos), tex == texFiles_.end() ? 0 : tex->second, ambient());
            return;
        }
        if (u.kind == UiObj::Kind::List || u.kind == UiObj::Kind::Slider) {
            if (listDraw_ && trans) listDraw_(o, viewProj, model * Mat4::fromRowXfm(trans->world.rot, trans->world.pos));
            return;
        }
        if (u.kind != UiObj::Kind::Label && u.kind != UiObj::Kind::Button && u.kind != UiObj::Kind::TextEntry) return;  // pictures etc. later
        const char* comp = u.kind == UiObj::Kind::Label ? "labelex" : u.kind == UiObj::Kind::Button ? "buttonex" : "textentry";
        const UiResources::Style& st = res_.style(comp, u.style);
        tmpl = res_.textTemplate(st.textTemplate);
        if (!tmpl) return;
        fontName = st.font.empty() ? tmpl->font : st.font;
        ts.size = tmpl->size;
        ts.leading = tmpl->leading;
        ts.align = tmpl->align;
        ts.wrapWidth = tmpl->wrapWidth;
        ts.italics = tmpl->italics;
        ts.capsMode = tmpl->capsMode;
        if (u.kind == UiObj::Kind::Button) {
            ts.align = 0x22;                  // ButtonEx centres its copy of button.txt (FUN_0012afb0)
            ts.capsMode = u.caps ? 2 : 0;     // FUN_0012c3c8 replaces the template's caps mode
        }
        if (u.kind == UiObj::Kind::Label) {  // LabelEx overrides (RndLabelEx load)
            ts.leading = u.leading;
            ts.align = u.align;
            ts.wrapWidth = u.width;
        }
        if (u.kind != UiObj::Kind::Button && u.caps) ts.capsMode = 2;
        rgb = st.colors[size_t(state(o.name))];
    } else {
        tmpl = o.text.get();
        fontName = tmpl->font;
        ts.size = tmpl->size;
        ts.leading = tmpl->leading;
        ts.align = tmpl->align;
        ts.wrapWidth = tmpl->wrapWidth;
        ts.italics = tmpl->italics;
        ts.capsMode = tmpl->capsMode;
        ts.markup = tmpl->markup;
        rgb = {tmpl->color[0], tmpl->color[1], tmpl->color[2]};
    }
    std::string str = text(o.name);
    if (str.empty()) return;
    Mat4 world = trans ? Mat4::fromRowXfm(trans->world.rot, trans->world.pos) : Mat4{};
    // [I] The band name (TextEntry style band_name): the original lays it out
    // left-aligned from the entry's left anchor; the port centres it on the
    // page (its labels' centre line, x 0), so it grows from the middle.
    if (o.ui && o.ui->kind == UiObj::Kind::TextEntry && o.ui->style == "band_name" && trans) {
        auto pos = trans->world.pos;
        pos[0] = 0;
        world = Mat4::fromRowXfm(trans->world.rot, pos);
        ts.align = 0x22;
    }
    drawLaidOut(o.name, str, fontName, ts, o.ui.get(), rgb, viewProj * model * world);
}

namespace {
constexpr int kTipLines = 6;  // fits the loading poster's text area
}  // namespace

float Panel::drawLaidOut(const std::string& key, const std::string& str, const std::string& fontName, TextStyle ts,
                         const UiObj* fitTo, const std::array<float, 3>& rgb, const Mat4& mvp, float alpha) {
    const FontMetrics* font = res_.font(fontName);
    if (!font || str.empty()) return 0;
    // Cached mesh per key, rebuilt when its text or look changes.
    char look[256];
    std::snprintf(look, sizeof look, "%s|%g|%x|%g|%d", fontName.c_str(), ts.size, ts.align, ts.wrapWidth, ts.capsMode);
    std::string fullKey = std::string(look) + "|" + str;
    CachedText& c = meshes_[key];
    if (c.key != fullKey) {
        TextMesh tm = layoutText(*font, str, ts);
        // LabelEx fit (RndLabelEx FUN_0011f868): the text is scaled to the fit
        // box, x = fit_width / widest line, z = fit_height / text height, always.
        c.sx = c.sz = 1;
        if (fitTo && fitTo->kind == UiObj::Kind::Button) {
            // RndButtonEx FUN_0012c408: the button's own text gets local
            // diag(box_width / widest line, 1, 1), stretching or squeezing it.
            if (fitTo->fit && tm.width > 0) c.sx = fitTo->fitWidth / tm.width;
        } else if (fitTo && fitTo->fit) {
            if (tm.width > 0 && fitTo->fitWidth > 0) c.sx = fitTo->fitWidth / tm.width;
            // FUN_001d93d0: first line z - (last line z - leading * size).
            float h = ts.leading * float(std::max(0, tm.lines - 1)) * tm.lineHeight + ts.leading * ts.size;
            if (h > 0 && fitTo->fitHeight > 0) c.sz = fitTo->fitHeight / h;
            // The fit *replaces* the label's own scale (FUN_001da700 sets the
            // local rotation; the label and its text share one transform), and
            // the saved local already holds the editor's fit: undo it.
            const auto& r = fitTo->trans.local.rot;
            float lx = std::hypot(r[0], r[1], r[2]), lz = std::hypot(r[6], r[7], r[8]);
            if (lx > 0) c.sx /= lx;
            if (lz > 0) c.sz /= lz;
        }
        // [I] The loading tip: the GH1RE tips run longer than the original's;
        // one over kTipLines lines is laid out wider and scaled down as a
        // whole until it fits that height.
        if (key == "tip.lbl" && name_ == "loading" && tm.lines > kTipLines && ts.wrapWidth > 0) {
            for (float s = 0.95f; s >= 0.5f; s -= 0.05f) {
                TextStyle wide = ts;
                wide.wrapWidth = ts.wrapWidth / s;
                tm = layoutText(*font, str, wide);
                c.sx = c.sz = s;
                if (float(tm.lines) * s <= float(kTipLines)) break;
            }
        }
        c.x0 = c.z0 = 1e30f, c.x1 = c.z1 = -1e30f;
        for (const Vertex& v : tm.vertices) {
            c.x0 = std::min(c.x0, v.x), c.x1 = std::max(c.x1, v.x);
            c.z0 = std::min(c.z0, v.z), c.z1 = std::max(c.z1, v.z);
        }
        if (c.mesh) renderer_.updateMesh(c.mesh, tm.vertices, tm.indices);
        else if (!tm.indices.empty()) c.mesh = renderer_.createMesh(tm.vertices, tm.indices);
        c.key = fullKey;
        c.width = tm.width * c.sx;
    }
    if (!c.mesh) return 0;
    Mat4 scaled = mvp;
    if (c.sx != 1 || c.sz != 1) {
        Mat4 sc;
        sc.at(0, 0) = c.sx;
        sc.at(2, 2) = c.sz;
        scaled = mvp * sc;
    }
    Renderer::RenderState st;
    st.texture = res_.fontTexture(fontName);
    // Text draws with its font's material (FUN_001c45e8): the fonts use
    // z-mode 0 (no depth) or 3 (Force: no test, writes depth), so text sits
    // on top of what was drawn before it in the draw list.
    st.blend = Blend::Alpha;
    int zmode = res_.fontZMode(fontName);
    st.depthTest = false;
    st.depthWrite = zmode == 3;
    st.alphaCutout = zmode == 3;
    st.tint[0] = rgb[0], st.tint[1] = rgb[1], st.tint[2] = rgb[2], st.tint[3] = alpha;
    // Lit fonts take the panel's ui.env ambient (the transition fade-in).
    if (res_.fontUsesEnviron(fontName))
        if (const SceneObject* env = scene_->find("ui.env"); env && env->environment)
            for (int k = 0; k < 3; ++k) st.tint[size_t(k)] *= std::clamp(env->environment->ambient[size_t(k)], 0.0f, 1.0f);
    renderer_.drawMesh(c.mesh, scaled.m.data(), st);
    // The text's screen rectangle, for the mouse.
    if (c.x1 > c.x0) {
        Hit h{key, 1e30f, 1e30f, -1e30f, -1e30f};
        bool ok = true;
        for (float x : {c.x0, c.x1})
            for (float z : {c.z0, c.z1}) {
                const auto& m = scaled.m;  // column-major
                float cx = m[0] * x + m[8] * z + m[12], cy = m[1] * x + m[9] * z + m[13], cw = m[3] * x + m[11] * z + m[15];
                if (cw <= 1e-6f) ok = false;
                float sx = (cx / cw + 1) * 0.5f * kScreenW, sy = (1 - cy / cw) * 0.5f * kScreenH;
                h.x0 = std::min(h.x0, sx), h.x1 = std::max(h.x1, sx), h.y0 = std::min(h.y0, sy), h.y1 = std::max(h.y1, sy);
            }
        if (ok) hits_.push_back(std::move(h));
    }
    return c.width;
}

const SceneObject* Panel::sharedObject(const std::string& name) const {
    if (!shared_) return nullptr;
    const SceneObject* o = shared_->find(name);
    return o && (o->cam || o->environment) ? o : nullptr;
}

bool Panel::viewProj(Mat4& vp) const {
    if (camera_.empty()) return false;
    const SceneObject* own = scene_->find(camera_);
    if (!own || !own->cam) return false;
    const CamObj& c = *own->cam;
    // The shared camera's transition rotation (meta_cam_*.tnm) turns the
    // panel's own camera about its position (inferred from the shared
    // meta.cam the transition anims target).
    std::array<float, 9> rot = c.trans.world.rot;
    if (const SceneObject* sc = sharedObject("meta.cam"); sc && sc->cam) rot = compose(sc->cam->trans.local, Xfm{rot, {}}).rot;
    float fovY = 2.0f * std::atan(std::tan(c.fov * 0.5f) / (4.0f / 3.0f));
    vp = Mat4::perspective(fovY, kScreenW / kScreenH, c.nearPlane, c.farPlane) * Mat4::cameraView(rot, c.trans.world.pos);
    return true;
}

void Panel::drawMesh(const std::string& mesh, const std::string& material, const Mat4& vp, const Mat4& world) {
    SceneRenderer::MeshOptions opt;
    opt.material = material;
    opt.useWorld = false;
    sr_->drawMesh(mesh, vp, world, opt);
}

float Panel::drawText(const std::string& key, const std::string& str, const std::string& templateName,
                      const std::array<float, 3>& rgb, const Mat4& vp, const Mat4& world, float alpha) {
    const TextObj* t = res_.textTemplate(templateName);
    if (!t) return 0;
    TextStyle ts;
    ts.size = t->size, ts.leading = t->leading, ts.align = t->align, ts.wrapWidth = t->wrapWidth;
    ts.italics = t->italics, ts.capsMode = t->capsMode;
    return drawLaidOut(key, str, t->font, ts, nullptr, rgb, vp * world, alpha);
}

float Panel::textWidth(const std::string& str, const std::string& templateName) {
    const TextObj* t = res_.textTemplate(templateName);
    const FontMetrics* font = t ? res_.font(t->font) : nullptr;
    if (!font || str.empty()) return 0;
    TextStyle ts;
    ts.size = t->size, ts.leading = t->leading, ts.align = t->align, ts.wrapWidth = t->wrapWidth;
    ts.italics = t->italics, ts.capsMode = t->capsMode;
    return layoutText(*font, str, ts).width;
}

std::array<float, 3> Panel::ambient() const {
    if (const SceneObject* env = scene_->find("ui.env"); env && env->environment)
        return {env->environment->ambient[0], env->environment->ambient[1], env->environment->ambient[2]};
    return {1, 1, 1};
}

void Panel::setTexFile(const std::string& component, const std::string& file) {
    std::string path = "ghui/image/gen/" + file + "_ps2";
    if (auto it = texCache_.find(path); it != texCache_.end()) {  // toggles flip between two pictures
        texFiles_[component] = it->second;
        return;
    }
    if (!data_.exists(path)) {
        SDL_Log("ui: no picture %s", path.c_str());
        return;
    }
    try {
        Bytes b = data_.read(path);
        texFiles_[component] = texCache_[path] = renderer_.createTexture(decodeBitmap(b.data(), b.size(), nullptr));
    } catch (const std::exception& ex) {
        SDL_Log("ui: picture %s: %s", path.c_str(), ex.what());
    }
}
