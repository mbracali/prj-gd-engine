// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/scene_gpu.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <set>
#include <cmath>

#include "render/image_enhance.h"
#include "render/texture_codec.h"
#include "scene/milo.h"
#include "scene/ui_objects.h"

namespace gfx {

Mat4 toMat4(const scene::Xfm& x) { return Mat4::fromRnd(x.rot.data(), x.pos.data()); }

const scene::Trans* transOf(const scene::Object* o) {
    if (!o) return nullptr;
    if (auto* m = o->as<scene::Mesh>()) return &m->trans;
    if (auto* t = o->as<scene::TransObj>()) return &t->trans;
    if (auto* g = o->as<scene::Group>()) return &g->trans;
    if (auto* c = o->as<scene::Cam>()) return &c->trans;
    if (auto* l = o->as<scene::Light>()) return &l->trans;
    if (auto* p = o->as<scene::ParticleSys>()) return &p->trans;
    if (auto* t = o->as<scene::Text>()) return &t->trans;
    if (auto* d = o->as<scene::DirObj>(); d && d->hasRnd) return &d->rnd.trans;
    if (auto* u = o->as<scene::UIComponent>()) return &u->trans;
    if (auto* b = o->as<scene::BandPlacer>()) return &b->trans;
    // The venue's (scene/world_objects.h): spotlights, flares, waypoints.
    if (auto* s = o->as<scene::Spotlight>()) return &s->trans;
    if (auto* f = o->as<scene::Flare>()) return &f->trans;
    if (auto* w = o->as<scene::Waypoint>()) return &w->trans;
    return nullptr;
}

const scene::Draw* drawOf(const scene::Object* o) {
    if (!o) return nullptr;
    if (auto* m = o->as<scene::Mesh>()) return &m->draw;
    if (auto* g = o->as<scene::Group>()) return &g->draw;
    if (auto* p = o->as<scene::ParticleSys>()) return &p->draw;
    if (auto* t = o->as<scene::Text>()) return &t->draw;
    if (auto* w = o->as<scene::TrackWidget>()) return &w->draw;
    if (auto* d = o->as<scene::DirObj>(); d && d->hasRnd) return &d->rnd.draw;
    if (auto* u = o->as<scene::UIComponent>()) return &u->draw;
    if (auto* m = o->as<scene::ScreenMask>()) return &m->draw;
    if (auto* b = o->as<scene::BandPlacer>()) return &b->draw;
    if (auto* c = o->as<scene::WorldCrowd>()) return &c->draw;
    if (auto* s = o->as<scene::Spotlight>()) return &s->draw;
    if (auto* f = o->as<scene::Flare>()) return &f->draw;
    if (auto* w = o->as<scene::Waypoint>()) return &w->draw;
    return nullptr;
}

// ---- ScenePose ----------------------------------------------------------

namespace {

// Rows of a row-vector rotation scaled by s (Harmonix Scale(s, m, m)).
void scaleRows(std::array<float, 9>& m, const std::array<float, 3>& s) {
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) m[size_t(r * 3 + c)] *= s[size_t(r)];
}

void normalizeRows(std::array<float, 9>& m) {
    for (int r = 0; r < 3; ++r) {
        float l = std::sqrt(m[size_t(r * 3)] * m[size_t(r * 3)] + m[size_t(r * 3 + 1)] * m[size_t(r * 3 + 1)] +
                            m[size_t(r * 3 + 2)] * m[size_t(r * 3 + 2)]);
        if (l > 0)
            for (int c = 0; c < 3; ++c) m[size_t(r * 3 + c)] /= l;
    }
}

// Euler angles (radians) to a row-vector rotation: x, then y, then z [I].
std::array<float, 9> eulerRot(const std::array<float, 3>& e) {
    const float cx = std::cos(e[0]), sx = std::sin(e[0]), cy = std::cos(e[1]), sy = std::sin(e[1]);
    const float cz = std::cos(e[2]), sz = std::sin(e[2]);
    scene::Xfm rx, ry, rz;
    rx.rot = {1, 0, 0, 0, cx, sx, 0, -sx, cx};
    ry.rot = {cy, 0, -sy, 0, 1, 0, sy, 0, cy};
    rz.rot = {cz, sz, 0, -sz, cz, 0, 0, 0, 1};
    return scene::compose(scene::compose(rx, ry), rz).rot;
}

template <class A> const A* keysOf(const A& a) {
    if (a.keysOwner.empty() || a.keysOwner == a.name || !a.dir) return &a;
    const A* o = a.dir->template resolve<A>(a.keysOwner);
    return o ? o : &a;
}

}  // namespace

void ScenePose::clear() {
    local_.clear();
    mats_.clear();
    showing_.clear();
    meshAnims_.clear();
    animFrames_.clear();
    envs_.clear();
    lights_.clear();
}

void ScenePose::forget(const std::unordered_set<const scene::Object*>& objects) {
    if (objects.empty()) return;
    auto drop = [&](auto& map) {
        for (auto it = map.begin(); it != map.end();) it = objects.count(it->first) ? map.erase(it) : std::next(it);
    };
    drop(local_), drop(mats_), drop(showing_), drop(animFrames_), drop(envs_), drop(lights_);
    for (auto it = meshAnims_.begin(); it != meshAnims_.end();)
        it = objects.count(it->first) || objects.count(it->second.anim) ? meshAnims_.erase(it) : std::next(it);
}

const ScenePose::EnvState* ScenePose::env(const scene::Environ* e) const {
    auto it = envs_.find(e);
    return it == envs_.end() ? nullptr : &it->second;
}

const ScenePose::LightState* ScenePose::light(const scene::Light* l) const {
    auto it = lights_.find(l);
    return it == lights_.end() ? nullptr : &it->second;
}

scene::Xfm ScenePose::local(const scene::Object* o) const {
    if (auto it = local_.find(o); it != local_.end()) return it->second;
    const scene::Trans* t = transOf(o);
    return t ? t->local : scene::Xfm{};
}

namespace {

using V = std::array<float, 3>;
V rowOf(const scene::Xfm& x, int r) { return {x.rot[size_t(r * 3)], x.rot[size_t(r * 3 + 1)], x.rot[size_t(r * 3 + 2)]}; }
void setRow(scene::Xfm& x, int r, const V& v) {
    for (size_t i = 0; i < 3; ++i) x.rot[size_t(r) * 3 + i] = v[i];
}
V crossV(const V& a, const V& b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
float lenV(const V& a) { return std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); }
V normV(const V& a) {
    const float l = lenV(a);
    return l > 1e-9f ? V{a[0] / l, a[1] / l, a[2] / l} : a;
}

}  // namespace

// The world transform (0x821AF988): constraint 1 LocalRotate keeps the local
// rotation at the parent-carried position, 2 ParentWorld is the parent's, the
// others the local one composed; then 0x821AEC78 against the camera being
// drawn: 3 LookAtTarget (y at the target), 5 BillboardZ (y away from the
// camera about the z kept), 6 BillboardXZ, 7 BillboardXYZ (y away, the
// camera's z), 8 FastBillboardXYZ (the camera's rotation); preserve_scale
// keeps the rows' lengths. 4 ShadowTarget (flattened on (shadow_plane)) has
// no object on the disc's venues [C data].
scene::Xfm ScenePose::world(const scene::Object* o, const scene::Xfm& own) const {
    scene::Xfm x = own;
    const scene::Trans* t = transOf(o);
    // A proxy file's object whose parent is the file's own root (loading.milo's
    // load_icon_guitar: guitar.mesh under "loading") hangs from the directory,
    // so from the object that holds it (RndTransformable's parent is the dir).
    const bool toDir = t && o->dir && !o->dir->proxyRoot.empty() && t->parent == o->dir->proxyRoot;
    if (toDir) {
        if (auto it = dirBase_.find(o->dir); it != dirBase_.end() && it->second && it->second != o) x = scene::compose(own, world(it->second));
    } else if (t && t->parent.empty() && o->dir && !dirBase_.empty())
        if (auto it = dirBase_.find(o->dir); it != dirBase_.end() && it->second && it->second != o) x = scene::compose(own, world(it->second));
    if (t && !t->parent.empty() && o->dir && !toDir) {
        const scene::Object* p = o->dir->resolve(t->parent);
        if (p && p != o && transOf(p)) {
            const scene::Xfm pw = world(p);
            if (t->constraint == 2) {
                x = pw;
            } else if (t->constraint == 1) {
                x.pos = scene::compose(own, pw).pos;
            } else {
                x = scene::compose(own, pw);
            }
        }
    }
    if (!t || t->constraint < 3 || t->constraint == 4 || !hasCamera_) return x;
    const int c = t->constraint;
    const float sx = lenV(rowOf(x, 0)), sy = lenV(rowOf(x, 1)), sz = lenV(rowOf(x, 2));
    const V pos = x.pos;
    const V cam = camera_.pos;
    V y{pos[0] - cam[0], pos[1] - cam[1], pos[2] - cam[2]};
    if (c == 3) {
        const scene::Object* to = !t->target.empty() && o->dir ? o->dir->resolve(t->target) : nullptr;
        if (!to || !transOf(to)) return x;
        const V tp = world(to).pos;
        y = {tp[0] - pos[0], tp[1] - pos[1], tp[2] - pos[2]};
    }
    if (c == 8) {
        x.rot = camera_.rot;
    } else if (c == 5) {
        const V z = t->preserveScale ? normV(rowOf(x, 2)) : rowOf(x, 2);
        const V xr = normV(crossV(y, z));
        setRow(x, 0, xr), setRow(x, 2, z), setRow(x, 1, crossV(z, xr));
    } else if (c == 6) {
        y = normV(y);
        const V xr = normV(crossV(y, rowOf(x, 2)));
        setRow(x, 0, xr), setRow(x, 1, y), setRow(x, 2, crossV(xr, y));
    } else {  // 3, 7: y, with the z kept or the camera's, made orthonormal (0x8214F7F0)
        const V z0 = c == 7 ? rowOf(camera_, 2) : rowOf(x, 2);
        y = normV(y);
        const V xr = normV(crossV(y, z0));
        setRow(x, 0, xr), setRow(x, 1, y), setRow(x, 2, normV(crossV(xr, y)));
    }
    if (t->preserveScale) {
        const float s[3] = {sx, sy, sz};
        for (int r = 0; r < 3; ++r)
            for (int i = 0; i < 3; ++i) x.rot[size_t(r * 3 + i)] *= s[r];
    }
    return x;
}

const ScenePose::MatState* ScenePose::mat(const scene::Mat* m) const {
    auto it = mats_.find(m);
    return it == mats_.end() ? nullptr : &it->second;
}

bool ScenePose::showing(const scene::Object* o) const {
    if (auto it = showing_.find(o); it != showing_.end()) return it->second;
    const scene::Draw* d = drawOf(o);
    return !d || d->showing;
}

namespace {
// The inline directory an object of `dir` holds (a RndDir as an object), or nullptr.
const scene::Directory* heldDir(const scene::Object* o) {
    if (!o || !o->dir || !o->as<scene::DirObj>()) return nullptr;
    for (const auto& [owner, sub] : o->dir->subdirs)
        if (sub && owner == o->name) return sub.get();
    return nullptr;
}
bool dirAnimatable(const scene::Object& o) {
    return o.as<scene::TransAnim>() || o.as<scene::MatAnim>() || o.as<scene::MeshAnim>() || o.as<scene::LightAnim>() || o.as<scene::EnvAnim>();
}
}  // namespace

float ScenePose::endFrame(const scene::Object* anim) {
    if (!anim) return 0;
    if (auto* t = anim->as<scene::TransAnim>()) {
        const scene::TransAnim* k = keysOf(*t);
        return scene::lastFrame(k->rotKeys, k->transKeys, k->scaleKeys);
    }
    if (auto* m = anim->as<scene::MatAnim>()) {
        const scene::MatAnim* k = keysOf(*m);
        float f = scene::lastFrame(k->colorKeys, k->alphaKeys, k->transKeys, k->scaleKeys, k->rotKeys);
        if (!k->texKeys.empty()) f = std::max(f, k->texKeys.back().frame);
        return f;
    }
    if (auto* f = anim->as<scene::AnimFilter>()) return f->dir && f->dir->resolve(f->anim) ? f->endFrame() : 0.0f;
    if (auto* l = anim->as<scene::LightAnim>()) return scene::lastFrame(keysOf(*l)->colorKeys);
    if (auto* e = anim->as<scene::EnvAnim>()) {
        const scene::EnvAnim* k = keysOf(*e);
        return scene::lastFrame(k->ambientKeys, k->fogColorKeys, k->fogRangeKeys);
    }
    if (auto* g = anim->as<scene::Group>()) {
        float f = 0;
        for (const std::string& n : g->objects)
            if (const scene::Object* o = g->dir ? g->dir->resolve(n) : nullptr; o && o != g) f = std::max(f, endFrame(o));
        return f;
    }
    if (const scene::Directory* d = heldDir(anim)) {
        float f = 0;
        for (const auto& o : d->objects)
            if (dirAnimatable(*o)) f = std::max(f, endFrame(o.get()));
        return f;
    }
    return 0;
}

void ScenePose::setFrame(const scene::Object* anim, float frame) {
    if (!anim) return;
    if (auto* t = anim->as<scene::TransAnim>()) {
        transAnim(*t, frame);
    } else if (auto* m = anim->as<scene::MatAnim>()) {
        matAnim(*m, frame);
    } else if (auto* f = anim->as<scene::AnimFilter>()) {
        if (const scene::Object* a = f->dir ? f->dir->resolve(f->anim) : nullptr; a && a != f) setFrame(a, f->filter(frame, f->animatable.rate == 1 ? 480.0f : f->animatable.rate == 3 ? 1.0f : 30.0f));
    } else if (auto* g = anim->as<scene::Group>()) {
        for (const std::string& n : g->objects)
            if (const scene::Object* o = g->dir ? g->dir->resolve(n) : nullptr; o && o != g) setFrame(o, frame);
    } else if (auto* ma = anim->as<scene::MeshAnim>()) {
        if (const scene::Mesh* m = ma->dir ? ma->dir->resolve<scene::Mesh>(ma->mesh) : nullptr) meshAnims_[m] = {ma, frame};
    } else if (auto* la = anim->as<scene::LightAnim>()) {
        // RndLightAnim::SetFrame [I: GH1's]: the colour keys set the light's colour.
        if (const scene::Light* l = la->dir ? la->dir->resolve<scene::Light>(la->light) : nullptr) {
            std::array<float, 4> c;
            if (scene::sampleLinear(keysOf(*la)->colorKeys, frame, c)) {
                LightState& st = lights_[l];
                st.hasColor = true;
                st.color = c;
            }
        }
    } else if (auto* ea = anim->as<scene::EnvAnim>()) {
        // RndEnvAnim::SetFrame [I: GH1's, plus the fog keys of v2 / v3].
        if (const scene::Environ* e = ea->dir ? ea->dir->resolve<scene::Environ>(ea->environName) : nullptr) {
            const scene::EnvAnim& k = *keysOf(*ea);
            EnvState& st = envs_[e];
            std::array<float, 4> c;
            if (scene::sampleLinear(k.ambientKeys, frame, c)) st.hasAmbient = true, st.ambient = c;
            if (scene::sampleLinear(k.fogColorKeys, frame, c)) st.hasFogColor = true, st.fogColor = c;
            std::array<float, 2> r;
            if (scene::sampleLinear(k.fogRangeKeys, frame, r)) st.hasFogRange = true, st.fogStart = r[0], st.fogEnd = r[1];
        }
    } else if (anim->as<scene::ParticleSys>() || anim->as<scene::ParticleSysAnim>()) {
        animFrames_[anim] = frame;
    } else if (const scene::Directory* d = heldDir(anim)) {
        // RndDir::SetFrame: the directory's anims at the frame [I: which of
        // its objects count as its anims] (loading.milo's band icons, the
        // AnimFilters' load_icon_* targets: MatAnims swapping textures).
        for (const auto& o : d->objects)
            if (dirAnimatable(*o)) setFrame(o.get(), frame);
    }
}

const float* ScenePose::animFrame(const scene::Object* o) const {
    auto it = animFrames_.find(o);
    return it == animFrames_.end() ? nullptr : &it->second;
}

const ScenePose::MeshAnimState* ScenePose::meshAnim(const scene::Mesh* m) const {
    auto it = meshAnims_.find(m);
    return it == meshAnims_.end() ? nullptr : &it->second;
}

std::vector<float> meshAnimVertices(const scene::Mesh& geometry, const scene::MeshAnim& anim, float frame) {
    std::vector<float> v = packVertices(geometry);
    const scene::MeshAnim* k = &anim;
    if (!anim.keysOwner.empty() && anim.keysOwner != anim.name && anim.dir)
        if (auto* o = anim.dir->resolve<scene::MeshAnim>(anim.keysOwner)) k = o;
    const auto& keys = k->pointKeys;
    if (keys.empty()) return v;
    size_t a = 0, b = 0;
    float t = 0;
    if (frame >= keys.back().frame) {
        a = b = keys.size() - 1;
    } else if (frame > keys.front().frame) {
        while (b < keys.size() && keys[b].frame < frame) ++b;
        a = b - 1;
        const float span = keys[b].frame - keys[a].frame;
        t = span > 0 ? (frame - keys[a].frame) / span : 0;
    }
    const size_t n = std::min({geometry.verts.size(), keys[a].values.size(), keys[b].values.size()});
    for (size_t i = 0; i < n; ++i)
        for (int c = 0; c < 3; ++c)
            v[i * 16 + size_t(c)] = keys[a].values[i][size_t(c)] + (keys[b].values[i][size_t(c)] - keys[a].values[i][size_t(c)]) * t;
    return v;
}

// RndTransAnim::SetFrame [I: GH1's order, same fields in GH2; the splines
// [C] 0x821B6B00]: rotation keys
// replace the rotation (scale keys then scale it), scale keys alone rescale
// the normalised rotation, position keys replace the position.
void ScenePose::transAnim(const scene::TransAnim& a, float frame) {
    const scene::Object* target = a.dir ? a.dir->resolve(a.trans) : nullptr;
    if (!target || !transOf(target)) return;
    const scene::TransAnim& k = *keysOf(a);
    scene::Xfm x = local(target);
    std::array<float, 3> s{1, 1, 1};
    const bool hasScale = scene::sampleTrans(k.scaleKeys, frame, k.scaleSpline, s);
    scene::Quat q;
    if (scene::sampleRotation(k.rotKeys, frame, k.rotSlerp, q)) {
        scene::quatToRot(q, x.rot);
        if (hasScale) scaleRows(x.rot, s);
    } else if (hasScale) {
        normalizeRows(x.rot);
        scaleRows(x.rot, s);
    }
    std::array<float, 3> p;
    if (scene::sampleTrans(k.transKeys, frame, k.transSpline, p)) x.pos = p;  // trans_spline: 0x8226E560
    local_[target] = x;
}

void ScenePose::matAnim(const scene::MatAnim& a, float frame) {
    const scene::Mat* target = a.dir ? a.dir->resolve<scene::Mat>(a.mat) : nullptr;
    if (!target) return;
    const scene::MatAnim& k = *keysOf(a);
    MatState& st = mats_[target];
    std::array<float, 4> c;
    if (scene::sampleLinear(k.colorKeys, frame, c)) {
        st.hasColor = true;
        st.rgb[0] = c[0], st.rgb[1] = c[1], st.rgb[2] = c[2];
    }
    std::array<float, 1> al;
    if (scene::sampleLinear(k.alphaKeys, frame, al)) st.hasAlpha = true, st.alpha = al[0];
    if (!k.transKeys.empty() || !k.scaleKeys.empty() || !k.rotKeys.empty()) {
        scene::Xfm x = st.hasXfm ? st.texXfm : target->texXfm;
        std::array<float, 3> v;
        if (scene::sampleLinear(k.transKeys, frame, v)) x.pos = v;
        if (!k.scaleKeys.empty() || !k.rotKeys.empty()) {
            std::array<float, 3> s{1, 1, 1}, r{0, 0, 0};
            scene::sampleLinear(k.scaleKeys, frame, s);
            scene::sampleLinear(k.rotKeys, frame, r);
            x.rot = eulerRot(r);
            scaleRows(x.rot, s);
        }
        st.hasXfm = true;
        st.texXfm = x;
    }
    if (const scene::TexKey* t = scene::sampleStep(k.texKeys, frame))
        if (auto* tex = a.dir->resolve<scene::Tex>(t->tex)) st.tex = tex;
}

// The texture-coordinate matrix the game uploads (gTexCoord) for a Mat's
// tex_xfm T and tex_gen, from the Mat setup 0x8230A358 [C] (row vectors):
// - 0 None: identity;
// - 1 Xfm / 4 XfmOrigin (0x82300AC0): rows (m0, -m1), (-m4, m5) of T on the
//   mesh uv; Xfm turns about the uv centre: u' = m0 (u - 0.5 - p.x) -
//   m4 (v - 0.5 + p.y) + 0.5, v' = -m1 (u - 0.5 - p.x) + m5 (v - 0.5 + p.y)
//   + 0.5; XfmOrigin adds (p.x, p.y) instead;
// - 5 Environ: T^T then S = x -> 0.5 u, z -> -0.5 v, plus (0.5, 0.5) on the
//   world reflection vector (the camera terms the game multiplies in cancel);
// - 2 Sphere: the same on the world normal [simplified: the game halves the
//   camera's pitch and yaw relative to T first, 0x822695C8];
// - 3 Projected: T^-1 then (x, -z) on the world position.
scene::Xfm texCoordXfm(const scene::Xfm& t, int texGen) {
    scene::Xfm m;
    const auto& r = t.rot;
    switch (texGen) {
    case 1:
    case 4: {
        m.rot = {r[0], -r[1], 0, -r[3], r[4], 0, 0, 0, 1};
        const float px = t.pos[0], py = t.pos[1];
        if (texGen == 1) {
            const float a = -0.5f - px, b = -0.5f + py;
            m.pos = {r[0] * a - r[3] * b + 0.5f, -r[1] * a + r[4] * b + 0.5f, 0};
        } else {
            m.pos = {px, py, 0};
        }
        break;
    }
    case 2:
    case 5: {
        scene::Xfm tt;
        tt.rot = {r[0], r[3], r[6], r[1], r[4], r[7], r[2], r[5], r[8]};
        scene::Xfm sphere;
        sphere.rot = {0.5f, 0, 0, 0, 0, 1, 0, -0.5f, 0};
        m = scene::compose(tt, sphere);
        m.pos = {0.5f, 0.5f, 0};
        break;
    }
    case 3: {
        // Inverse of T (rotation may be scaled): through the column-vector helper.
        const Mat4 inv = toMat4(t).inverseAffine();
        scene::Xfm ti;
        for (int row = 0; row < 3; ++row)
            for (int c = 0; c < 3; ++c) ti.rot[size_t(row * 3 + c)] = inv.at(c, row);
        ti.pos = {inv.at(0, 3), inv.at(1, 3), inv.at(2, 3)};
        scene::Xfm swap;
        swap.rot = {1, 0, 0, 0, 0, 1, 0, -1, 0};
        m = scene::compose(ti, swap);
        break;
    }
    default: break;
    }
    return m;
}

// ---- SceneCache ---------------------------------------------------------

SceneCache::~SceneCache() {
    for (auto& [tex, id] : textures_)
        if (id) device_.destroyTexture(id);
    for (auto& [cube, id] : cubes_)
        if (id) device_.destroyTexture(id);
    for (BufferId b : buffers_) device_.destroyBuffer(b);
}

bool SceneCache::stale(const scene::Object* o) {
    auto [it, added] = owners_.try_emplace(o, o->uid);
    if (added || it->second == o->uid) return false;
    it->second = o->uid;
    return true;
}

TextureId SceneCache::texture(const scene::Object* owner, const std::string& name) {
    if (name.empty() || !owner || !owner->dir) return {};
    return texture(owner->dir->resolve<scene::Tex>(name));
}

TextureId SceneCache::texture(const scene::Tex* tex) {
    if (!tex) return {};
    if (auto it = textures_.find(tex); it != textures_.end()) {
        if (!stale(tex)) return it->second;
        if (it->second) device_.destroyTexture(it->second);
        textures_.erase(it);
    }
    stale(tex);
    TextureId id;
    const scene::Bitmap& b = tex->bitmap;
    TexFormat format = TexFormat::RGBA8;
    bool known = true;
    switch (b.format) {
    case scene::Bitmap::Format::DXT1: format = TexFormat::BC1; break;
    case scene::Bitmap::Format::DXT5: format = TexFormat::BC3; break;
    case scene::Bitmap::Format::ATI2: format = TexFormat::BC5; break;
    case scene::Bitmap::Format::RGBA8: format = TexFormat::RGBA8; break;
    default: known = false; break;
    }
    std::vector<uint8_t> sharp;
    int sw = b.width, sh = b.height;
    // Enhanced Options > Sharper Text and Sprites (the menus' cache): the
    // texture decoded, upscaled (render/image_enhance.h) and mipmapped anew.
    if (known && !b.empty() && enhance_ && format != TexFormat::BC5 && decodeToRgba8(format, b.level(0), b.levels[0].size, b.width, b.height, sharp) &&
        enhanceImage(sharp, sw, sh)) {
        const std::vector<std::vector<uint8_t>> mips = mipChain(sharp, sw, sh);
        std::vector<TexLevel> levels{{sharp.data(), sharp.size()}};
        for (const auto& m : mips) levels.push_back({m.data(), m.size()});
        std::string error;
        id = device_.createTexture({sw, sh, int(levels.size()), TexFormat::RGBA8, TexKind::Tex2D}, levels.data(), error);
        if (id) ++enhancedTextures_;
    }
    if (!id && known && !b.empty()) {
        std::vector<TexLevel> levels;
        for (size_t i = 0; i < b.levels.size(); ++i) levels.push_back({b.level(i), b.levels[i].size});
        std::string error;
        id = device_.createTexture({b.width, b.height, int(levels.size()), format, TexKind::Tex2D}, levels.data(), error);
        if (!id) SDL_Log("render: texture %s: %s", tex->name.c_str(), error.c_str());
    } else {
        ++skippedTextures_;
    }
    textures_[tex] = id;
    return id;
}

// RndCubeTex (PostLoad 0x82209618): six inline bitmaps of one size and
// format, uploaded as a cube (faces right, left, top, bottom, front, back =
// +x -x +y -y +z -z, Direct3D's order, which OpenGL samples the same way).
TextureId SceneCache::cubeTexture(const scene::CubeTex* cube) {
    if (!cube) return {};
    if (auto it = cubes_.find(cube); it != cubes_.end()) {
        if (!stale(cube)) return it->second;
        if (it->second) device_.destroyTexture(it->second);
        cubes_.erase(it);
    }
    stale(cube);
    TextureId id;
    const scene::Bitmap& f0 = cube->faces[0];
    bool ok = cube->inlineBitmaps && !f0.empty();
    TexFormat format = TexFormat::BC1;
    switch (f0.format) {
    case scene::Bitmap::Format::DXT1: format = TexFormat::BC1; break;
    case scene::Bitmap::Format::DXT5: format = TexFormat::BC3; break;
    default: ok = false; break;
    }
    // Levels down to 4x4 (smaller ones are shorter than a block on the disc).
    int mips = 0;
    for (const auto& l : f0.levels)
        if (l.width >= 4 && l.height >= 4) ++mips;
    for (const auto& f : cube->faces)
        if (f.format != f0.format || f.width != f0.width || f.height != f0.height || int(f.levels.size()) < mips) ok = false;
    if (ok && mips > 0) {
        std::vector<TexLevel> levels;
        for (const auto& f : cube->faces)
            for (int i = 0; i < mips; ++i) levels.push_back({f.level(size_t(i)), f.levels[size_t(i)].size});
        std::string error;
        id = device_.createTexture({f0.width, f0.height, mips, format, TexKind::Cube}, levels.data(), error);
        if (!id) SDL_Log("render: cube texture %s: %s", cube->name.c_str(), error.c_str());
    } else {
        ++skippedTextures_;
    }
    cubes_[cube] = id;
    return id;
}

std::vector<float> packVertices(const scene::Mesh& m) {
    const size_t n = m.verts.size();
    std::vector<Vec3> tan(n), bit(n);
    for (const auto& f : m.faces) {
        if (f[0] >= n || f[1] >= n || f[2] >= n) continue;
        const scene::MeshVertex &a = m.verts[f[0]], &b = m.verts[f[1]], &c = m.verts[f[2]];
        const Vec3 e1{b.pos[0] - a.pos[0], b.pos[1] - a.pos[1], b.pos[2] - a.pos[2]};
        const Vec3 e2{c.pos[0] - a.pos[0], c.pos[1] - a.pos[1], c.pos[2] - a.pos[2]};
        const float du1 = b.uv[0] - a.uv[0], dv1 = b.uv[1] - a.uv[1], du2 = c.uv[0] - a.uv[0], dv2 = c.uv[1] - a.uv[1];
        const float det = du1 * dv2 - du2 * dv1;
        if (std::fabs(det) < 1e-12f) continue;
        const float r = 1.0f / det;
        const Vec3 t = (e1 * dv2 - e2 * dv1) * r, s = (e2 * du1 - e1 * du2) * r;
        for (int k = 0; k < 3; ++k) tan[f[k]] = tan[f[k]] + t, bit[f[k]] = bit[f[k]] + s;
    }
    std::vector<float> out;
    out.reserve(n * 16);
    for (size_t i = 0; i < n; ++i) {
        const scene::MeshVertex& v = m.verts[i];
        const Vec3 nrm{v.normal[0], v.normal[1], v.normal[2]};
        Vec3 t = normalize(tan[i] - nrm * dot(nrm, tan[i]));
        if (dot(t, t) < 0.5f) t = normalize(cross(nrm, std::fabs(nrm.z) < 0.9f ? Vec3{0, 0, 1} : Vec3{1, 0, 0}));
        const float w = dot(cross(nrm, t), bit[i]) < 0 ? -1.0f : 1.0f;
        out.insert(out.end(), {v.pos[0], v.pos[1], v.pos[2], v.normal[0], v.normal[1], v.normal[2], v.color[0], v.color[1],
                               v.color[2], v.color[3], v.uv[0], v.uv[1], t.x, t.y, t.z, w});
    }
    return out;
}

const GpuMesh* SceneCache::mesh(const scene::Mesh* m) {
    if (!m || !m->decoded) return nullptr;
    const scene::Mesh* geom = m;
    if (m->verts.empty() && !m->geomOwner.empty() && m->dir)
        if (auto* owner = m->dir->resolve<scene::Mesh>(m->geomOwner)) geom = owner;
    if (auto it = meshes_.find(geom); it != meshes_.end()) {
        if (!stale(geom)) return it->second.get();
        meshes_.erase(it);  // its buffers stay in buffers_ until the cache goes
    }
    stale(geom);
    std::unique_ptr<GpuMesh> g;
    if (!geom->verts.empty() && !geom->faces.empty()) {
        g = std::make_unique<GpuMesh>();
        const std::vector<float> verts = packVertices(*geom);
        g->vertices = device_.createBuffer(BufferKind::Vertex, verts.size() * sizeof(float), verts.data(), false);
        g->indices = device_.createBuffer(BufferKind::Index, geom->faces.size() * 6, geom->faces.data(), false);
        buffers_.push_back(g->vertices), buffers_.push_back(g->indices);
        g->indexCount = int(geom->faces.size() * 3);
        g->tangents = true;
        g->skinned = !geom->bones.empty();
    }
    const GpuMesh* r = g.get();
    meshes_[geom] = std::move(g);
    return r;
}

Material SceneCache::material(const scene::Mat& mat, const ScenePose* pose) {
    Material g;
    g.blend = MatBlend(std::clamp(mat.blend, 0, 7));
    g.color[0] = mat.color[0], g.color[1] = mat.color[1], g.color[2] = mat.color[2], g.alpha = mat.color[3];
    g.useEnviron = mat.useEnviron, g.prelit = mat.prelit;
    g.alphaCut = mat.alphaCut, g.alphaWrite = mat.alphaWrite;
    g.intensify = mat.intensify, g.cull = mat.cull;
    g.perPixelLit = mat.perPixelLit, g.normalize = mat.normalize;
    g.zMode = ZMode(std::clamp(mat.zMode, 0, 4));
    g.texGen = TexGen(std::clamp(mat.texGen, 0, 5));
    g.texWrap = TexWrap(std::clamp(mat.texWrap, 0, 4));
    scene::Xfm texXfm = mat.texXfm;
    g.emissiveMultiplier = mat.emissiveMultiplier;
    for (int k = 0; k < 3; ++k) g.specularRgb[k] = mat.specularRgb[size_t(k)];
    g.specularPower = mat.specularPower;
    g.diffuseTex = texture(&mat, mat.diffuseTex);
    g.normalMap = texture(&mat, mat.normalMap);
    g.emissiveMap = texture(&mat, mat.emissiveMap);
    g.specularMap = texture(&mat, mat.specularMap);
    g.customDiffuseMap = texture(&mat, mat.customDiffuseMap);
    // The environment map: a CubeTex.
    if (!mat.environMap.empty() && mat.dir) g.environMap = cubeTexture(mat.dir->resolve<scene::CubeTex>(mat.environMap));
    if (const ScenePose::MatState* s = pose ? pose->mat(&mat) : nullptr) {
        if (s->hasColor) g.color[0] = s->rgb[0], g.color[1] = s->rgb[1], g.color[2] = s->rgb[2];
        if (s->hasAlpha) g.alpha = s->alpha;
        if (s->hasXfm) texXfm = s->texXfm;
        if (s->tex) g.diffuseTex = texture(s->tex);
        if (s->texId.id) g.diffuseTex = s->texId;
    }
    if (!g.diffuseTex && !mat.diffuseTex.empty() && mat.dir)
        if (const auto* t = mat.dir->resolve<scene::Tex>(mat.diffuseTex); t && t->bitmap.empty() && t->file.empty()) g.unfilled = true;
    scene::Xfm uvXfm = texCoordXfm(texXfm, mat.texGen);
    if (const ScenePose::MatState* s = pose ? pose->mat(&mat) : nullptr; s && s->flipV) {
        scene::Xfm flip;
        flip.rot = {1, 0, 0, 0, -1, 0, 0, 0, 1};
        flip.pos = {0, 1, 0};
        uvXfm = scene::compose(uvXfm, flip);
    }
    g.texXfm = toMat4(uvXfm);
    return g;
}

LightEnv SceneCache::lightEnv(const scene::Environ& e, const ScenePose* pose) {
    LightEnv env;
    env.envAmbient = {e.ambientColor[0], e.ambientColor[1], e.ambientColor[2]};
    env.fog = e.fogEnable;
    for (int k = 0; k < 4; ++k) env.fogColor[k] = e.fogColor[size_t(k)];
    env.fogStart = e.fogStart, env.fogEnd = e.fogEnd;
    if (const ScenePose::EnvState* st = pose ? pose->env(&e) : nullptr) {
        if (st->hasAmbient) env.envAmbient = {st->ambient[0], st->ambient[1], st->ambient[2]};
        if (st->hasFogEnable) env.fog = st->fogEnable;
        if (st->hasFogColor)
            for (int k = 0; k < 4; ++k) env.fogColor[k] = st->fogColor[size_t(k)];
        if (st->hasFogRange) env.fogStart = st->fogStart, env.fogEnd = st->fogEnd;
    }
    env.fade = e.fadeOut, env.fadeStart = e.fadeStart, env.fadeEnd = e.fadeEnd;
    int point = 0, dir = 0;
    // Rock Band's environ select (RB ~0x827D7580) [C]: with any point light
    // (type 0) only the point lights, at most 4 (0x827D6F00); else the
    // directional ones (type 1, 0x827D7090) up to 7. Fake spots (2) need the
    // drawn object's direction and floor spots (3) the projected slots: not
    // drawn yet [I]; shadow refs (4) never light. A light whose colour is 0
    // in 8 bits is skipped.
    bool rbEnv = false, rbHasPoint = false;
    for (const std::string& name : e.lights)
        if (auto* l = e.dir ? e.dir->resolve<scene::Light>(name) : nullptr; l && l->bigEndian) {
            rbEnv = true;
            const bool lit = std::max({l->color[0], l->color[1], l->color[2]}) * 255.0f >= 0.5f;
            if (l->type == 0 && lit && (!pose || pose->showing(l))) rbHasPoint = true;
        }
    for (const std::string& name : e.lights) {
        auto* l = e.dir ? e.dir->resolve<scene::Light>(name) : nullptr;
        if (!l || env.lightCount >= 7) continue;
        if (pose && !pose->showing(l)) continue;
        if (rbEnv && l->bigEndian) {
            if (rbHasPoint ? (l->type != 0 || point >= 4) : l->type != 1) continue;
        }
        const scene::Xfm w = pose ? pose->world(l) : l->trans.world;
        Light& g = env.lights[env.lightCount++];
        g.color = {l->color[0], l->color[1], l->color[2]};
        g.range = l->range;
        g.falloffStart = l->bigEndian ? std::clamp(l->falloffStart, 0.0f, std::max(0.0f, l->range)) : 0.0f;
        if (const ScenePose::LightState* st = pose ? pose->light(l) : nullptr) {
            if (st->hasColor) g.color = {st->color[0], st->color[1], st->color[2]};
            if (st->hasRange) g.range = st->range;
        }
        if (rbEnv && std::max({g.color.x, g.color.y, g.color.z}) * 255.0f < 0.5f) {
            --env.lightCount;  // RB skips a light black in 8 bits [C]
            continue;
        }
        // [I] GH1's light types: 0 point, 1 directional. A directional light
        // shines along its +y axis, so the vector towards it is -y.
        if (l->type == 1) {
            g.vec = normalize(Vec3{-w.rot[3], -w.rot[4], -w.rot[5]});
            ++dir;
        } else {
            g.vec = {w.pos[0], w.pos[1], w.pos[2]};
            ++point;
        }
    }
    // GH2 picks one light kind per environment. RB mixes them: with point
    // lights present the environment is a point one, its directional lights
    // flagged (render/material.h Light::directional) [I].
    env.directional = dir >= point;
    bool rb = false;
    for (const std::string& name : e.lights)
        if (auto* l = e.dir ? e.dir->resolve<scene::Light>(name) : nullptr) rb = rb || l->bigEndian;
    if (rb && !rbEnv && point > 0 && dir > 0) {
        env.directional = false;
        int k = 0;
        for (const std::string& name : e.lights) {
            auto* l = e.dir ? e.dir->resolve<scene::Light>(name) : nullptr;
            if (!l || (pose && !pose->showing(l))) continue;
            if (k >= env.lightCount) break;
            env.lights[k++].directional = l->type == 1;
        }
    }
    if (SDL_getenv("GD05_ENVLOG")) {
        static std::set<const scene::Environ*> seen;
        if (seen.insert(&e).second) {
            SDL_Log("environ %s: ambient %.2f %.2f %.2f, %d lights (first %.2f %.2f %.2f range %.0f), color adjust %d (rot %.2f %.2f %.2f / %.2f %.2f %.2f / %.2f %.2f %.2f, pos %.2f %.2f %.2f; b %.2f c %.2f)",
                    e.name.c_str(), double(env.envAmbient.x), double(env.envAmbient.y), double(env.envAmbient.z), env.lightCount,
                    env.lightCount ? double(env.lights[0].color.x) : 0.0, env.lightCount ? double(env.lights[0].color.y) : 0.0,
                    env.lightCount ? double(env.lights[0].color.z) : 0.0, env.lightCount ? double(env.lights[0].range) : 0.0, int(e.useColorAdjust),
                    double(e.colorXfm.xfm.rot[0]), double(e.colorXfm.xfm.rot[1]), double(e.colorXfm.xfm.rot[2]), double(e.colorXfm.xfm.rot[3]),
                    double(e.colorXfm.xfm.rot[4]), double(e.colorXfm.xfm.rot[5]), double(e.colorXfm.xfm.rot[6]), double(e.colorXfm.xfm.rot[7]),
                    double(e.colorXfm.xfm.rot[8]), double(e.colorXfm.xfm.pos[0]), double(e.colorXfm.xfm.pos[1]), double(e.colorXfm.xfm.pos[2]),
                    double(e.colorXfm.brightness), double(e.colorXfm.contrast));
        }
    }
    return env;
}

DynamicMesh::~DynamicMesh() {
    if (vb_) device_.destroyBuffer(vb_);
    if (ib_) device_.destroyBuffer(ib_);
}

GpuMesh DynamicMesh::upload(const std::vector<float>& verts, const std::vector<uint16_t>& indices) {
    GpuMesh g;
    if (verts.empty() || indices.empty()) return g;
    const size_t vbytes = verts.size() * sizeof(float), ibytes = indices.size() * sizeof(uint16_t);
    if (vbytes > vbSize_) {
        if (vb_) device_.destroyBuffer(vb_);
        vbSize_ = std::max<size_t>(vbytes * 2, 64 * 1024);
        vb_ = device_.createBuffer(BufferKind::Vertex, vbSize_, nullptr, true);
    }
    if (ibytes > ibSize_) {
        if (ib_) device_.destroyBuffer(ib_);
        ibSize_ = std::max<size_t>(ibytes * 2, 16 * 1024);
        ib_ = device_.createBuffer(BufferKind::Index, ibSize_, nullptr, true);
    }
    device_.updateBuffer(vb_, 0, verts.data(), vbytes);
    device_.updateBuffer(ib_, 0, indices.data(), ibytes);
    g.vertices = vb_, g.indices = ib_;
    g.indexCount = int(indices.size());
    g.tangents = true;
    return g;
}

void drawMesh(MaterialRenderer& r, SceneCache& cache, const scene::Mesh& mesh, const Mat4& world, const ScenePose* pose,
              const scene::Mat* mat, const float* tint, const GpuMesh* geometry) {
    const GpuMesh* g = geometry ? geometry : cache.mesh(&mesh);
    if (!g || !mesh.dir) return;
    if (!mat) mat = mesh.dir->resolve<scene::Mat>(mesh.mat);
    const Mat4 bones[1] = {world};
    if (!mat) {
        Material plain;
        r.draw(*g, bones, plain, {1});
        return;
    }
    // The material and its next passes (a loop guard: at most four).
    for (int pass = 0; mat && pass < 4; ++pass) {
        Material m = cache.material(*mat, pose);
        if (tint) {
            for (int k = 0; k < 3; ++k) m.color[k] *= tint[k];
            m.alpha *= tint[3];
        }
        r.draw(*g, bones, m, {1});
        if (mat->nextPass.empty() || !mat->dir) break;
        const scene::Mat* next = mat->dir->resolve<scene::Mat>(mat->nextPass);
        mat = next != mat ? next : nullptr;
    }
}

}  // namespace gfx
