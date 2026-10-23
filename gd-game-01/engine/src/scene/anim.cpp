// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "scene/anim.h"

#include "scene/particles.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>

namespace {

// Finds the keys around `frame`: frames outside the keys hold the end key.
template <size_t N>
bool bracket(const std::vector<AnimKey<N>>& keys, float frame, size_t& i0, size_t& i1, float& t) {
    if (keys.empty()) return false;
    t = 0;
    if (frame <= keys.front().frame) {
        i0 = i1 = 0;
        return true;
    }
    if (frame >= keys.back().frame) {
        i0 = i1 = keys.size() - 1;
        return true;
    }
    size_t lo = 0, hi = keys.size() - 1;
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        (frame < keys[mid].frame ? hi : lo) = mid;
    }
    i0 = lo, i1 = hi;
    float span = keys[hi].frame - keys[lo].frame;
    t = span > 0 ? (frame - keys[lo].frame) / span : 0;
    return true;
}

template <size_t N> bool linear(const std::vector<AnimKey<N>>& keys, float frame, std::array<float, N>& out) {
    size_t a, b;
    float t;
    if (!bracket(keys, frame, a, b, t)) return false;
    for (size_t i = 0; i < N; ++i) out[i] = keys[a].v[i] + (keys[b].v[i] - keys[a].v[i]) * t;
    return true;
}

// Spline tangent at key i (FUN_0024cd68).
std::array<float, 3> tangent(const std::vector<AnimKey<3>>& k, size_t i) {
    std::array<float, 3> d{};
    size_t n = k.size();
    for (int c = 0; c < 3; ++c) {
        if (n == 2) d[c] = k[1].v[c] - k[0].v[c];
        else if (i == 0) d[c] = (k[1].v[c] - k[0].v[c]) * 1.5f - (k[2].v[c] - k[0].v[c]) * 0.25f;
        else if (i == n - 1) d[c] = (k[i].v[c] - k[n - 2].v[c]) * 1.5f - (k[i].v[c] - k[n - 3].v[c]) * 0.25f;
        else d[c] = (k[i + 1].v[c] - k[i - 1].v[c]) * 0.5f;
    }
    return d;
}

// Vec3 keys, linear or cubic Hermite (FUN_0024d0d8).
bool vec3At(const std::vector<AnimKey<3>>& keys, float frame, bool spline, std::array<float, 3>& out) {
    size_t a, b;
    float t;
    if (!bracket(keys, frame, a, b, t)) return false;
    if (!spline || a == b || keys.size() < 2) {
        for (int i = 0; i < 3; ++i) out[i] = keys[a].v[i] + (keys[b].v[i] - keys[a].v[i]) * t;
        return true;
    }
    auto ta = tangent(keys, a), tb = tangent(keys, b);
    float t2 = t * t, t3 = t2 * t;
    float h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t, h01 = -2 * t3 + 3 * t2, h11 = t3 - t2;
    for (int i = 0; i < 3; ++i) out[i] = h00 * keys[a].v[i] + h10 * ta[i] + h01 * keys[b].v[i] + h11 * tb[i];
    return true;
}

std::array<float, 3> rowScale(const std::array<float, 9>& m) {
    std::array<float, 3> s;
    for (int r = 0; r < 3; ++r)
        s[r] = std::sqrt(m[r * 3] * m[r * 3] + m[r * 3 + 1] * m[r * 3 + 1] + m[r * 3 + 2] * m[r * 3 + 2]);
    return s;
}


template <size_t N> void mix(std::array<float, N>& cur, const std::array<float, N>& v, float blend, size_t n = N) {
    for (size_t i = 0; i < n; ++i) cur[i] = blend == 1.0f ? v[i] : cur[i] + (v[i] - cur[i]) * blend;
}

}  // namespace

AnimPlayer::AnimPlayer(std::vector<Scene*> scenes) : scenes_(std::move(scenes)) {
    // Order every transformable after its parent.
    std::map<std::string, std::pair<SceneObject*, Scene*>> all;
    for (Scene* s : scenes_)
        for (SceneObject& o : s->objects())
            if (o.transformable()) all.emplace(o.name, std::make_pair(&o, s));
    // Parents: the Transformable `children` lists. The last transformable
    // field names a transform *owner* (load 0x2eadc8 keeps it at xfm+0x10):
    // an owned object has no transform of its own and is drawn with its
    // owner's (PsMesh_v5), e.g. a venue's stage.1.mesh owned by stage.mesh.
    // Modelled as a child with an identity local.
    std::map<std::string, std::string> parentOf;
    for (const auto& [name, v] : all)
        for (const std::string& c : v.first->transformable()->children) parentOf.emplace(c, name);
    std::vector<std::string> owned;
    for (const auto& [name, v] : all) {
        const std::string& p = v.first->transformable()->parent;
        if (!p.empty() && p != name && all.count(p) && parentOf.emplace(name, p).second) owned.push_back(name);
    }
    std::set<std::string> visiting;
    std::function<void(const std::string&)> visit = [&](const std::string& name) {
        if (nodeIndex_.count(name) || visiting.count(name)) return;
        auto it = all.find(name);
        if (it == all.end()) return;
        visiting.insert(name);
        Transformable& t = *it->second.first->transformable();
        std::string parent = parentOf.count(name) ? parentOf[name] : std::string();
        if (!parent.empty()) visit(parent);
        Node n{it->second.first, it->second.second, parent, compose(inverse(t.local), t.world)};
        nodeIndex_[name] = order_.size();
        order_.push_back(std::move(n));
    };
    for (const auto& [name, v] : all) visit(name);
    dirty_.assign(order_.size(), 0);
    for (const std::string& name : owned) {
        Transformable& t = *all[name].first->transformable();
        t.local = Xfm{};
    }
    // Owned objects (and their subtrees) take their owners' worlds now.
    std::vector<char> own(order_.size(), 0);
    for (const std::string& name : owned) own[nodeIndex_[name]] = 1;
    for (size_t i = 0; i < order_.size(); ++i) {
        Node& n = order_[i];
        auto it = n.parent.empty() ? nodeIndex_.end() : nodeIndex_.find(n.parent);
        if (it == nodeIndex_.end()) continue;
        if (own[it->second]) own[i] = 1;
        if (!own[i]) continue;
        Transformable& t = *n.obj->transformable();
        t.world = compose(t.local, order_[it->second].obj->transformable()->world);
    }
}

SceneObject* AnimPlayer::find(const std::string& name) {
    if (auto it = cache_.find(name); it != cache_.end()) return it->second;
    SceneObject* found = nullptr;
    for (Scene* s : scenes_)
        if ((found = s->find(name))) break;
    cache_[name] = found;
    return found;
}

float AnimPlayer::filter(const std::string& name, float frame) {
    SceneObject* o = find(name);
    const Animatable* a = o ? (o->anim ? &o->anim->anim : o->view ? &o->view->anim : o->particles ? &o->particles->anim : nullptr) : nullptr;
    if (!a) return frame;
    for (const AnimFilter& f : a->filters) {
        if (f.type == 0) {
            frame = frame * f.a + f.b;  // ScaleOffset (FUN_002b5648)
        } else if (f.type == 1) {       // MinMaxLoop (FUN_001abe30)
            if (!f.loop) {
                frame = std::clamp(frame, f.a, f.b);
            } else {
                float span = f.b - f.a, r = 0;
                if (span != 0) {
                    r = std::fmod(frame - f.a, span);
                    if (r < 0) r += span;
                }
                frame = f.a + r;
            }
        }
        // Types 2..4 (smoothing filters) do not occur in the shipped data.
    }
    return frame;
}

void AnimPlayer::setFrame(const std::string& anim, float frame, float blend) {
    if (SceneObject* o = find(anim)) setFrameAt(*o, frame, blend, 0, true);
}

void AnimPlayer::setFrameUnfiltered(const std::string& anim, float frame, float blend) {
    if (SceneObject* o = find(anim)) setFrameAt(*o, frame, blend, 0, false);
}

void AnimPlayer::setFrameAt(SceneObject& o, float frame, float blend, int depth, bool filtered) {
    if (depth > 16) return;
    if (filtered) frame = filter(o.name, frame);
    apply(o, frame, blend);
    children(o, frame, blend, depth);
}

void AnimPlayer::children(SceneObject& o, float frame, float blend, int depth) {
    const Animatable* a = o.anim ? &o.anim->anim : o.view ? &o.view->anim : o.particles ? &o.particles->anim : nullptr;
    if (!a) return;
    for (const std::string& child : a->anims)
        if (SceneObject* c = find(child); c && c != &o) setFrameAt(*c, frame, blend, depth + 1, true);
}

void AnimPlayer::markMoved(const std::string& name) {
    find(name);  // builds the node index
    if (auto it = nodeIndex_.find(name); it != nodeIndex_.end()) dirty_[it->second] = 1;
}

void AnimPlayer::unhook(const std::string& anim) {
    for (Scene* s : scenes_)
        for (SceneObject& o : s->objects()) {
            Animatable* a = o.anim ? &o.anim->anim : o.view ? &o.view->anim : o.particles ? &o.particles->anim : nullptr;
            if (a) std::erase(a->anims, anim);
        }
}

// FUN_001de038(frame, blend, anim, xfm, reset): with `reset`, parts without
// keys become identity instead of keeping the transform's current value.
void AnimPlayer::evalTrans(const AnimObj& keys, float frame, float blend, Xfm& x, bool reset) {
    std::array<float, 3> v;
    if (vec3At(keys.transKeys, frame, keys.transSpline, v)) mix(x.pos, v, blend);
    else if (reset) x.pos = {0, 0, 0};
    std::array<float, 3> scale = rowScale(x.rot);
    auto unscaled = [&] {
        auto m = x.rot;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) m[r * 3 + c] /= scale[r] ? scale[r] : 1;
        return m;
    };
    if (!keys.rotKeys.empty()) {
        size_t a, b;
        float f;
        bracket(keys.rotKeys, frame, a, b, f);
        Quat q = nlerp(keys.rotKeys[a].v, keys.rotKeys[b].v, f);
        if (blend != 1.0f) q = nlerp(rotToQuat(unscaled()), q, blend);
        quatToRot(q, x.rot);
    } else if (reset) {
        x.rot = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    } else {
        x.rot = unscaled();  // keep the rotation, re-scale below
    }
    if (vec3At(keys.scaleKeys, frame, keys.scaleSpline, v)) mix(scale, v, blend);
    else if (reset || !keys.rotKeys.empty()) scale = {1, 1, 1};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) x.rot[r * 3 + c] *= scale[r];
}

bool AnimPlayer::transAt(const std::string& anim, float frame, Xfm& out) {
    SceneObject* o = find(anim);
    if (!o || !o->anim || o->anim->kind != AnimObj::Kind::Trans) return false;
    const AnimObj* keys = o->anim.get();
    if (!keys->owner.empty() && keys->owner != o->name)
        if (SceneObject* ow = find(keys->owner); ow && ow->anim && ow->anim->kind == AnimObj::Kind::Trans)
            keys = ow->anim.get();
    out = Xfm{};
    evalTrans(*keys, frame, 1.0f, out, true);
    return true;
}

float AnimPlayer::endFrame(const std::string& anim) {
    SceneObject* o = find(anim);
    if (!o || !o->anim) return 0;
    const AnimObj& a = *o->anim;
    float end = 0;
    auto last = [&](const auto& keys) {
        if (!keys.empty()) end = std::max(end, keys.back().frame);
    };
    last(a.rotKeys), last(a.transKeys), last(a.scaleKeys), last(a.colorKeys), last(a.floatKeys);
    last(a.fogColorKeys), last(a.fogRangeKeys);
    for (const auto* f : {&a.vert.pointFrames, &a.vert.uvFrames, &a.vert.colorFrames})
        if (!f->empty()) end = std::max(end, f->back());
    return end;
}

void AnimPlayer::apply(SceneObject& o, float frame, float blend) {
    if (o.particles) {  // PsParticleSys SetFrame: simulate up to `frame`
        particleSetFrame(*o.particles, frame, o.particles->trans.world);
        return;
    }
    if (!o.anim || blend <= 0) return;
    const AnimObj& self = *o.anim;
    const AnimObj* keys = &self;  // the keys owner (frames_owner / keys_owner)
    if (!self.owner.empty() && self.owner != o.name)
        if (SceneObject* ow = find(self.owner); ow && ow->anim && ow->anim->kind == self.kind) keys = ow->anim.get();
    SceneObject* target = find(self.target);
    if (!target) return;

    switch (self.kind) {
    case AnimObj::Kind::Trans: {  // RndTransAnim SetFrame
        Transformable* t = target->transformable();
        if (!t) return;
        evalTrans(*keys, frame, blend, t->local, false);
        if (auto it = nodeIndex_.find(target->name); it != nodeIndex_.end()) dirty_[it->second] = 1;
        break;
    }
    case AnimObj::Kind::Mat: {  // RndMatAnim SetFrame (RndMatAnim_v1 @ 0x1c2518)
        MatObj* m = target->mat.get();
        if (!m) return;
        std::array<float, 4> c;
        if (linear(keys->colorKeys, frame, c)) mix(m->color, c, blend, 3);  // key alpha is unused
        std::array<float, 1> a;
        if (linear(keys->floatKeys, frame, a)) m->color[3] = blend == 1.0f ? a[0] : m->color[3] + (a[0] - m->color[3]) * blend;
        for (size_t s = 0; s < keys->stages.size() && s < m->stages.size(); ++s) {
            const AnimObj::Stage& st = keys->stages[s];
            MatStage& ms = m->stages[s];
            std::array<float, 3> v;
            if (linear(st.trans, frame, v)) {
                std::array<float, 3> cur{ms.xfm[9], ms.xfm[10], ms.xfm[11]};
                mix(cur, v, blend);
                ms.xfm[9] = cur[0], ms.xfm[10] = cur[1], ms.xfm[11] = cur[2];
            }
            // Stage rotation (euler, z used by the UV transforms) and scale keys:
            // rows = scale * [[cos, sin], [-sin, cos]] (matches the stored stage matrices).
            std::array<float, 3> rot, sc;
            bool hasRot = linear(st.rot, frame, rot), hasScale = linear(st.scale, frame, sc);
            if ((hasRot || hasScale) && blend >= 0.5f) {
                std::array<float, 3> curScale{std::hypot(ms.xfm[0], ms.xfm[1], ms.xfm[2]), std::hypot(ms.xfm[3], ms.xfm[4], ms.xfm[5]),
                                              std::hypot(ms.xfm[6], ms.xfm[7], ms.xfm[8])};
                float angle = hasRot ? rot[2] : std::atan2(ms.xfm[1], ms.xfm[0]);
                if (!hasScale) sc = curScale;
                float c = std::cos(angle), sn = std::sin(angle);
                ms.xfm[0] = c * sc[0], ms.xfm[1] = sn * sc[0], ms.xfm[2] = 0;
                ms.xfm[3] = -sn * sc[1], ms.xfm[4] = c * sc[1], ms.xfm[5] = 0;
                ms.xfm[6] = 0, ms.xfm[7] = 0, ms.xfm[8] = sc[2];
            }
            if (!st.tex.empty() && blend >= 0.5f) {  // texture keys step
                size_t i = 0;
                while (i + 1 < st.tex.size() && st.tex[i + 1].second <= frame) ++i;
                ms.tex = st.tex[i].first;
            }
        }
        break;
    }
    case AnimObj::Kind::Light: {  // RndLightAnim_v1 @ 0x1f8760
        if (!target->light) return;
        std::array<float, 4> c;
        if (linear(keys->colorKeys, frame, c)) mix(target->light->color, c, blend);
        break;
    }
    case AnimObj::Kind::Env: {  // RndEnvAnim_v1 @ 0x1b5f70
        EnvironObj* e = target->environment.get();
        if (!e) return;
        std::array<float, 4> c;
        if (linear(keys->colorKeys, frame, c)) mix(e->ambient, c, blend);
        if (linear(keys->fogColorKeys, frame, c)) mix(e->fogColor, c, blend);
        std::array<float, 2> r;
        if (linear(keys->fogRangeKeys, frame, r)) {
            std::array<float, 2> cur{e->fogStart, e->fogEnd};
            mix(cur, r, blend);
            e->fogStart = cur[0], e->fogEnd = cur[1];
        }
        break;
    }
    case AnimObj::Kind::Particle: {  // RndParticleSysAnim_v1 @ 0x1fedf8
        ParticleSysObj* p = target->particles.get();
        if (!p) return;
        auto lerp = [&](float& cur, float v) { cur = blend == 1.0f ? v : cur + (v - cur) * blend; };
        std::array<float, 4> c;
        // Colours: low = key, high = key + the old spread.
        auto color = [&](const std::vector<AnimKey<4>>& k, std::array<float, 4>& low, std::array<float, 4>& high) {
            if (!linear(k, frame, c)) return;
            for (size_t i = 0; i < 4; ++i) {
                float spread = high[i] - low[i];
                lerp(low[i], c[i]);
                high[i] = low[i] + spread;
            }
        };
        color(keys->startColorKeys, p->startLow, p->startHigh);
        color(keys->endColorKeys, p->endLow, p->endHigh);
        std::array<float, 2> v;
        auto pair = [&](const std::vector<AnimKey<2>>& k, std::array<float, 2>& out) {
            if (linear(k, frame, v)) lerp(out[0], v[0]), lerp(out[1], v[1]);
        };
        pair(keys->emitKeys, p->emitRate);
        pair(keys->speedKeys, p->speed);
        pair(keys->lifeKeys, p->life);
        pair(keys->startSizeKeys, p->startSize);
        break;
    }
    case AnimObj::Kind::Mesh: {  // RndMeshAnim SetFrame: linear between the bracketing keys
        MeshObj* m = target->mesh.get();
        if (!m) return;
        auto apply = [&](const auto& values, const std::vector<float>& frames, auto write) {
            if (values.empty()) return;
            size_t i = 0;
            while (i + 1 < frames.size() && frames[i + 1] <= frame) ++i;
            size_t j = std::min(i + 1, frames.size() - 1);
            float t = j != i && frames[j] > frames[i] ? std::clamp((frame - frames[i]) / (frames[j] - frames[i]), 0.0f, 1.0f) : 0.0f;
            const auto& a = values[i];
            const auto& b = values[j];
            for (size_t v = 0; v < a.size() && v < b.size() && v < m->verts.size(); ++v) write(m->verts[v], a[v], b[v], t);
        };
        const AnimObj::VertKeys& vk = keys->vert;
        apply(vk.points, vk.pointFrames, [&](MeshVertex& mv, const auto& a, const auto& b, float t) {
            for (int k = 0; k < 3; ++k) mv.pos[k] += (a[size_t(k)] + (b[size_t(k)] - a[size_t(k)]) * t - mv.pos[k]) * blend;
        });
        apply(vk.uvs, vk.uvFrames, [&](MeshVertex& mv, const auto& a, const auto& b, float t) {
            for (int k = 0; k < 2; ++k) mv.uv[k] += (a[size_t(k)] + (b[size_t(k)] - a[size_t(k)]) * t - mv.uv[k]) * blend;
        });
        apply(vk.colors, vk.colorFrames, [&](MeshVertex& mv, const auto& a, const auto& b, float t) {
            for (int k = 0; k < 4; ++k) mv.extra[k] += (a[size_t(k)] + (b[size_t(k)] - a[size_t(k)]) * t - mv.extra[k]) * blend;
        });
        ++m->revision;
        break;
    }
    case AnimObj::Kind::Cam: {  // RndCamAnim
        if (!target->cam) return;
        std::array<float, 1> f;
        if (linear(keys->floatKeys, frame, f)) target->cam->fov += (f[0] - target->cam->fov) * blend;
        break;
    }
    }
}

void AnimPlayer::updateTransforms() {
    for (size_t i = 0; i < order_.size(); ++i) {
        Node& n = order_[i];
        Transformable& t = *n.obj->transformable();
        size_t parent = order_.size();
        if (!n.parent.empty())
            if (auto it = nodeIndex_.find(n.parent); it != nodeIndex_.end()) parent = it->second;
        if (parent < order_.size() && dirty_[parent]) dirty_[i] = 1;
        if (!dirty_[i]) continue;
        const Xfm& pw = parent < order_.size() ? order_[parent].obj->transformable()->world : n.parentWorld0;
        t.world = compose(t.local, pw);
    }
    // Moved objects stay dirty: their parents' worlds were rebuilt from the
    // locals, so keep recomputing them (and their subtrees) every frame.
    updateAttached();
}

void AnimPlayer::updateAttached() {
    for (auto [child, parent] : attached_)
        child->transformable()->world = compose(child->transformable()->local, parent->transformable()->world);
}

void AnimPlayer::attach(const std::string& child, const std::string& parent) {
    SceneObject* c = find(child);
    SceneObject* p = find(parent);
    if (c && p && c != p && c->transformable() && p->transformable()) attached_.push_back({c, p});
}
