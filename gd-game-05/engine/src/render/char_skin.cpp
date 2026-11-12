// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/char_skin.h"

#include <algorithm>
#include <functional>
#include <set>
#include <vector>

#include "char/character.h"
#include "scene/milo.h"
#include "scene/objects.h"
#include "scene/ui_objects.h"

namespace gfx {

int CharSkin::renders_ = 0;

namespace {

// Targets not drawn for this long are freed (a character gone, a menu left).
constexpr uint64_t kIdleMs = 20000;

// The V flip of a texture rendered at run time (stored bottom-up), on top
// of the material's own uv transform (SceneCache::material's flipV).
Mat4 flipV() {
    scene::Xfm flip;
    flip.rot = {1, 0, 0, 0, -1, 0, 0, 0, 1};
    flip.pos = {0, 1, 0};
    return toMat4(flip);
}

}  // namespace

CharSkin::CharSkin(SceneCache& cache) : cache_(cache), quad_(std::make_unique<DynamicMesh>(cache.device())) {}

CharSkin::~CharSkin() {
    for (auto& [tex, t] : targets_)
        if (t.id) cache_.device().destroyTarget(t.id);
}

CharSkin::Target* CharSkin::target(const scene::Tex& out) {
    if (out.width <= 0 || out.height <= 0) return nullptr;
    Target& t = targets_[&out];
    if (t.id && (t.texUid != out.uid || t.w != out.width || t.h != out.height)) {
        cache_.device().destroyTarget(t.id);
        t = Target{};
    }
    if (!t.id) {
        std::string error;
        t.id = cache_.device().createTarget(TargetDesc{out.width, out.height, true, TargetFormat::RGBA8}, error);
        if (!t.id) {
            SDL_Log("char skin: %s %dx%d: %s", out.name.c_str(), out.width, out.height, error.c_str());
            targets_.erase(&out);
            return nullptr;
        }
        t.w = out.width, t.h = out.height, t.texUid = out.uid;
    }
    t.lastUse = SDL_GetTicks();
    return &t;
}

void CharSkin::collect(uint64_t now) {
    for (auto it = targets_.begin(); it != targets_.end();) {
        if (now - it->second.lastUse > kIdleMs) {
            if (it->second.id) cache_.device().destroyTarget(it->second.id);
            it = targets_.erase(it);
        } else {
            ++it;
        }
    }
}

void CharSkin::update(MaterialRenderer& mr, chr::Character& c) {
    // GD05_SKIN_LEGACY=1: nothing rendered, as before D84 (the walks' "before").
    static const bool legacy = SDL_getenv("GD05_SKIN_LEGACY") != nullptr;
    if (legacy) return;
    collect(SDL_GetTicks());
    std::vector<const scene::TexRenderer*> renderers;
    c.forEachRoot([&](scene::Directory& root) {
        // The character's own TexRenderers (merged from its outfits into its
        // root); the PatchRenderers inside the proxies render with them.
        for (const auto& o : root.objects)
            if (const auto* t = o->as<scene::TexRenderer>(); t && !o->as<scene::PatchRenderer>() && o->decoded) renderers.push_back(t);
    });
    for (const scene::TexRenderer* t : renderers) {
        const scene::Tex* out = t->dir ? t->dir->resolve<scene::Tex>(t->outputTexture) : nullptr;
        const scene::Directory* proxy = t->drawName.empty() ? nullptr : c.proxyDir(t->drawName);
        if (!out || !proxy) continue;
        Target* tg = target(*out);
        if (!tg) continue;
        if (tg->serial != c.skinSerial || t->force) render(mr, c, *t, proxy, *tg);
    }
}

void CharSkin::render(MaterialRenderer& mr, chr::Character& c, const scene::TexRenderer& t, const scene::Directory* proxy, Target& tg) {
    RenderDevice& dev = cache_.device();
    const TargetId prev = dev.currentTarget();
    int vx, vy, vw, vh;
    dev.currentViewport(vx, vy, vw, vh);
    const Camera cam0 = mr.camera();
    const LightEnv env0 = mr.lightEnv();
    const int w0 = mr.viewportW(), h0 = mr.viewportH();
    const TextureId depth0 = mr.sceneDepth();
    const bool alpha0 = mr.forceAlphaWrite;
    // The proxy's materials and masks are drawn as they are: no lights, a
    // white ambient [I: RB's proxies hold no Environ].
    LightEnv env;
    env.ambient = {1, 1, 1};
    env.envAmbient = {1, 1, 1};
    env.lightCount = 0;
    mr.forceAlphaWrite = true;

    // The PatchRenderers first (PatchRenderer DrawShowing RB 0x822D2C70 ->
    // RndTexRenderer's): each into its output; without a patch directory
    // (its `proxy`) nothing is drawn, so the output stays clear [I: RB's
    // render target before any draw]. The proxy's materials that name the
    // output sample it.
    proxy->forEach([&](scene::Object& o) {
        const auto* p = o.as<scene::PatchRenderer>();
        const scene::Tex* out = p && o.dir ? o.dir->resolve<scene::Tex>(p->outputTexture) : nullptr;
        Target* pt = out ? target(*out) : nullptr;
        if (!pt) return;
        dev.setTarget(pt->id);
        dev.setViewport(0, 0, pt->w, pt->h);
        const float clear[4] = {0, 0, 0, 0};
        dev.clear(clear, true);
        pt->serial = c.skinSerial;
        proxy->forEach([&](scene::Object& m) {
            auto* mat = m.as<scene::Mat>();
            if (mat && mat->dir && mat->dir->resolve<scene::Tex>(mat->diffuseTex) == out) {
                ScenePose::MatState& st = pose_.matState(mat);
                st.texId = dev.targetTexture(pt->id);
                st.flipV = true;
            }
        });
    });

    dev.setTarget(tg.id);
    dev.setViewport(0, 0, tg.w, tg.h);
    const float clear[4] = {0, 0, 0, 0};
    dev.clear(clear, true);
    // RndTexRenderer::DrawShowing (RB 0x821ED620) [C]: without an impostor
    // height the draw's camera, else the first Cam in it.
    const scene::Cam* cam = nullptr;
    for (const auto& o : proxy->objects)
        if (!cam) cam = o->as<scene::Cam>();
    Camera gc;
    gc.aspect = float(tg.w) / float(tg.h);
    if (cam) {
        gc.world = toMat4(pose_.world(cam));
        gc.yFov = cam->yFov, gc.nearPlane = cam->nearPlane, gc.farPlane = cam->farPlane;
        gc.ortho = cam->yFov == 0;
    } else {
        gc.ortho = true, gc.nearPlane = 0.01f, gc.farPlane = 5;
    }
    aspect_ = gc.aspect;
    mr.setFrame(gc, env, tg.w, tg.h);
    drawList(mr, *proxy);

    tg.serial = c.skinSerial;
    ++renders_;
    if (SDL_getenv("GD05_SKINLOG"))
        SDL_Log("char skin: %s: %s -> %s %dx%d (%s camera)", c.name().c_str(), t.drawName.c_str(), t.outputTexture.c_str(), tg.w, tg.h,
                gc.ortho ? "parallel" : "perspective");
    // Development aid: GD05_SKINDUMP=<dir> saves each render as <character>_<texture>.bmp.
    if (const char* dump = SDL_getenv("GD05_SKINDUMP"))
        if (SDL_Surface* sf = dev.readTarget(tg.id, 0, 0, tg.w, tg.h)) {
            SDL_SaveBMP(sf, (std::string(dump) + "/" + c.name() + "_" + std::to_string(renders_) + "_" + t.outputTexture + ".bmp").c_str());
            SDL_DestroySurface(sf);
        }
    dev.setTarget(prev);
    dev.setViewport(vx, vy, vw, vh);
    mr.forceAlphaWrite = alpha0;
    mr.setFrame(cam0, env0, w0, h0, depth0);
}

// RndDir::DrawShowing: the drawables no Group holds, by draw order; a
// Group draws its members in its list's order.
void CharSkin::drawList(MaterialRenderer& mr, const scene::Directory& dir) {
    std::set<std::string> held;
    for (const auto& o : dir.objects)
        if (const auto* g = o->as<scene::Group>())
            for (const std::string& m : g->objects) held.insert(m);
    std::vector<const scene::Object*> roots;
    for (const auto& o : dir.objects)
        if (!held.count(o->name) && drawOf(o.get())) roots.push_back(o.get());
    std::stable_sort(roots.begin(), roots.end(), [](const scene::Object* a, const scene::Object* b) { return drawOf(a)->order < drawOf(b)->order; });
    for (const scene::Object* o : roots) drawObject(mr, *o, 0);
}

void CharSkin::drawObject(MaterialRenderer& mr, const scene::Object& o, int depth) {
    if (depth > 8) return;
    if (const scene::Draw* d = drawOf(&o); d && !d->showing) return;
    if (const auto* g = o.as<scene::Group>()) {
        for (const std::string& n : g->objects)
            if (const scene::Object* m = g->dir ? g->dir->resolve(n) : nullptr; m && m != g) drawObject(mr, *m, depth + 1);
        return;
    }
    if (const auto* mesh = o.as<scene::Mesh>()) {
        drawMesh(mr, cache_, *mesh, toMat4(pose_.world(mesh)), &pose_);
        return;
    }
    const auto* sm = o.as<scene::ScreenMask>();
    if (!sm || !sm->dir) return;
    // RndScreenMask (Load RB 0x8221A970) [C]: a rect over the target (x y
    // w h from the top left, 0..1) drawn with its material, the mask's
    // colour times the material's; the material's texture spans the rect
    // [I: the uvs]. No depth [I].
    const scene::Mat* mat = sm->dir->resolve<scene::Mat>(sm->mat);
    if (!mat) return;
    Material gm = cache_.material(*mat, &pose_);
    for (int k = 0; k < 3; ++k) gm.color[k] *= sm->color[size_t(k)];
    gm.alpha *= sm->color[3];
    gm.zMode = ZMode::Disable;
    gm.cull = false;
    const float x0 = sm->screenRect[0] * 2 - 1, x1 = (sm->screenRect[0] + sm->screenRect[2]) * 2 - 1;
    const float y0 = 1 - sm->screenRect[1] * 2, y1 = 1 - (sm->screenRect[1] + sm->screenRect[3]) * 2;
    // In the parallel camera's space: x across, z up scaled by the aspect,
    // halfway between the planes.
    const Camera& cam = mr.camera();
    const float mid = (cam.nearPlane + cam.farPlane) * 0.5f;
    const float a = aspect_ > 0 ? aspect_ : 1;
    scene::Mesh quad;
    quad.verts.resize(4);
    const float pos[4][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (int i = 0; i < 4; ++i) {
        scene::MeshVertex& v = quad.verts[size_t(i)];
        v.pos[0] = pos[i][0], v.pos[1] = mid, v.pos[2] = pos[i][1] / a;
        v.normal[0] = 0, v.normal[1] = -1, v.normal[2] = 0;
        v.color[0] = v.color[1] = v.color[2] = v.color[3] = 1;
        v.uv[0] = uv[i][0], v.uv[1] = uv[i][1];
    }
    quad.faces = {{0, 1, 2}, {0, 2, 3}};
    const GpuMesh g = quad_->upload(packVertices(quad), {0, 1, 2, 0, 2, 3});
    // The quad is in the camera's own space: drawn through its inverse.
    Camera screen = cam;
    screen.world = Mat4::identity();
    const LightEnv env = mr.lightEnv();
    const int w = mr.viewportW(), h = mr.viewportH();
    mr.setFrame(screen, env, w, h);
    const Mat4 bones[1] = {Mat4::identity()};
    DrawContext ctx;
    mr.draw(g, bones, gm, ctx);
    mr.setFrame(cam, env, w, h);
}

bool CharSkin::apply(const scene::Mat& mat, Material& gm) const {
    if (mat.diffuseTex.empty() || !mat.dir) return false;
    const scene::Tex* t = mat.dir->resolve<scene::Tex>(mat.diffuseTex);
    auto it = t ? targets_.find(t) : targets_.end();
    if (it == targets_.end() || it->second.texUid != t->uid || it->second.serial == ~0u) return false;
    gm.diffuseTex = cache_.device().targetTexture(it->second.id);
    gm.unfilled = false;
    gm.texXfm = flipV() * gm.texXfm;
    return true;
}

SDL_Surface* CharSkin::read(chr::Character& c, const std::string& tex) {
    const scene::Tex* found = nullptr;
    c.forEachObject([&](scene::Object& o) {
        if (!found && o.name == tex)
            if (const auto* t = o.as<scene::Tex>(); t && targets_.count(t)) found = t;
    });
    if (!found) return nullptr;
    const Target& t = targets_.at(found);
    if (t.texUid != found->uid || t.serial == ~0u) return nullptr;
    return cache_.device().readTarget(t.id, 0, 0, t.w, t.h);
}

}  // namespace gfx
