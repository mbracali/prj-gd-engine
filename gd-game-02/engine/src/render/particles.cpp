// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/particles.h"

#include <algorithm>
#include <cmath>

#include "render/material.h"
#include "scene/milo.h"

namespace gfx {
namespace {

constexpr float kTwoPi = 6.2831855f;
constexpr float kRpmToRadPerFrame = 0.0034906587f;  // 2 pi / (60 x 30) [C 0x82213E58]
constexpr int kPoolCap = 4096;                      // a guard; GH2's pools are the max_particles

Vec3 rowOf(const scene::Xfm& x, int r) { return {x.rot[size_t(r * 3)], x.rot[size_t(r * 3 + 1)], x.rot[size_t(r * 3 + 2)]}; }

// p * rot + pos (a Milo transform on a point) and v * rot (on a direction).
Vec3 xfmPoint(const scene::Xfm& x, Vec3 p) {
    return rowOf(x, 0) * p.x + rowOf(x, 1) * p.y + rowOf(x, 2) * p.z + Vec3{x.pos[0], x.pos[1], x.pos[2]};
}
Vec3 xfmDir(const scene::Xfm& x, Vec3 v) { return rowOf(x, 0) * v.x + rowOf(x, 1) * v.y + rowOf(x, 2) * v.z; }

scene::Xfm inverse(const scene::Xfm& x) {
    const Mat4 inv = toMat4(x).inverseAffine();
    scene::Xfm r;
    for (int row = 0; row < 3; ++row)
        for (int c = 0; c < 3; ++c) r.rot[size_t(row * 3 + c)] = inv.at(c, row);
    r.pos = {inv.at(0, 3), inv.at(1, 3), inv.at(2, 3)};
    return r;
}

template <size_t N> bool keyRange(const std::vector<scene::Key<N>>& keys, float frame, std::array<float, N>& out) {
    return scene::sampleLinear(keys, frame, out);
}

}  // namespace

// ---- ParticleSystem -----------------------------------------------------

ParticleSystem::ParticleSystem(const scene::ParticleSys& def, uint32_t seed) : def_(&def), rng_(seed ? seed : 1) {
    for (int k = 0; k < 3; ++k) forceRows_[k] = {k == 0 ? 1.0f : 0.0f, k == 1 ? 1.0f : 0.0f, k == 2 ? 1.0f : 0.0f};
    reset();
}

void ParticleSystem::reset() {
    const scene::ParticleSys& d = *def_;
    params_.life = d.life, params_.speed = d.speed, params_.emitRate = d.emitRate, params_.startSize = d.startSize;
    params_.startColorLow = d.startColorLow, params_.startColorHigh = d.startColorHigh;
    params_.endColorLow = d.endColorLow, params_.endColorHigh = d.endColorHigh;
    // SetPool(max_particles) (vtable +0x24 from the Load) [C].
    const int n = std::clamp(d.maxParticles, 0, kPoolCap);
    pool_.assign(size_t(n), Particle{});
    alive_.clear();
    free_.clear();
    for (int i = n - 1; i >= 0; --i) free_.push_back(uint32_t(i));
    emitCount_ = 0;
    started_ = false;
    warmUpPending_ = d.preSpawn;
    emitting_ = true;
}

float ParticleSystem::random() {
    // xorshift32: GH2's own generator is not reproduced [I]; only the ranges matter.
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return float(rng_ >> 8) * (1.0f / 16777216.0f);
}

void ParticleSystem::applyAnim(const scene::ParticleSysAnim& a, float frame) {
    const scene::ParticleSysAnim* k = &a;
    if (!a.keysOwner.empty() && a.keysOwner != a.name && a.dir)
        if (auto* o = a.dir->resolve<scene::ParticleSysAnim>(a.keysOwner)) k = o;
    std::array<float, 4> c;
    if (keyRange(k->startColorKeys, frame, c))
        for (size_t i = 0; i < 4; ++i) {
            params_.startColorHigh[i] = params_.startColorHigh[i] + c[i] - params_.startColorLow[i];
            params_.startColorLow[i] = c[i];
        }
    if (keyRange(k->endColorKeys, frame, c))
        for (size_t i = 0; i < 4; ++i) {
            params_.endColorHigh[i] = params_.endColorHigh[i] + c[i] - params_.endColorLow[i];
            params_.endColorLow[i] = c[i];
        }
    keyRange(k->emitRateKeys, frame, params_.emitRate);
    keyRange(k->speedKeys, frame, params_.speed);
    keyRange(k->lifeKeys, frame, params_.life);
    keyRange(k->startSizeKeys, frame, params_.startSize);
}

// Relative motion (0x82214538, 0x82212820) [C for 0 and 1; values between
// are blended by the game (0x822128D8) and taken as the nearer one here, I].
void ParticleSystem::setTransform(const scene::Xfm& world, const scene::Xfm& parentWorld) {
    local_ = def_->relativeMotion >= 0.5f;
    if (local_) {
        drawSpace_ = parentWorld;
        emission_ = scene::compose(world, inverse(parentWorld));
        // Force is a world vector taken into the parent's space with the
        // transposed rotation (0x82212A48 reads +0x1d0 by columns).
        for (int k = 0; k < 3; ++k) forceRows_[k] = rowOf(parentWorld, k);
    } else {
        drawSpace_ = scene::Xfm{};
        emission_ = world;
        for (int k = 0; k < 3; ++k) forceRows_[k] = {k == 0 ? 1.0f : 0.0f, k == 1 ? 1.0f : 0.0f, k == 2 ? 1.0f : 0.0f};
    }
}

void ParticleSystem::setBouncePlane(bool on, Vec3 n, float d) {
    bounce_ = on;
    if (!on) return;
    if (local_) {
        // Into the draw space: n . (p R + T) + d.
        const Vec3 nl{dot(n, rowOf(drawSpace_, 0)), dot(n, rowOf(drawSpace_, 1)), dot(n, rowOf(drawSpace_, 2))};
        const float l = std::sqrt(dot(nl, nl));
        if (l < 1e-6f) {
            bounce_ = false;
            return;
        }
        bounceN_ = nl * (1.0f / l);
        bounceD_ = (d + dot(n, Vec3{drawSpace_.pos[0], drawSpace_.pos[1], drawSpace_.pos[2]})) / l;
    } else {
        bounceN_ = n, bounceD_ = d;
    }
}

void ParticleSystem::setEmitterMesh(const scene::Mesh* mesh, const scene::Xfm& meshWorld) {
    emitterMesh_ = mesh && !mesh->faces.empty() ? mesh : nullptr;
    emitterWorld_ = meshWorld;
}

void ParticleSystem::init(Particle& p, float frame) {
    const scene::ParticleSys& d = *def_;
    p.birth = frame;
    p.death = frame + random(params_.life);
    bool placed = false;
    if (emitterMesh_) {
        const auto& f = emitterMesh_->faces[size_t(random() * float(emitterMesh_->faces.size())) % emitterMesh_->faces.size()];
        const size_t nv = emitterMesh_->verts.size();
        if (f[0] < nv && f[1] < nv && f[2] < nv) {
            const auto& a = emitterMesh_->verts[f[0]].pos;
            const auto& b = emitterMesh_->verts[f[1]].pos;
            const auto& c = emitterMesh_->verts[f[2]].pos;
            float u = random(), v = random();
            if (u + v > 1) u = 1 - u, v = 1 - v;
            const Vec3 A{a[0], a[1], a[2]}, B{b[0], b[1], b[2]}, C{c[0], c[1], c[2]};
            Vec3 pos = A + (B - A) * u + (C - A) * v;
            Vec3 dir = normalize(cross(B - A, C - A));
            pos = xfmPoint(emitterWorld_, pos);
            dir = normalize(xfmDir(emitterWorld_, dir));
            if (local_) {
                const scene::Xfm inv = inverse(drawSpace_);
                pos = xfmPoint(inv, pos);
                dir = xfmDir(inv, dir);
            }
            p.pos = pos, p.vel = dir;
            placed = true;
        }
    }
    if (!placed) {
        p.pos = {random(d.posLow[0], d.posHigh[0]), random(d.posLow[1], d.posHigh[1]), random(d.posLow[2], d.posHigh[2])};
        const float pitch = random(d.pitch), yaw = random(d.yaw);
        const float cp = std::cos(pitch);
        p.vel = {-std::sin(yaw) * cp, std::cos(yaw) * cp, std::sin(pitch)};
    }
    p.vel = p.vel * random(params_.speed);
    if (d.bubble) {
        p.bubbleFreq = kTwoPi / random(d.bubblePeriod);
        p.bubblePhase = random(0, kTwoPi);
        const float a = random(0, kTwoPi), s = random(d.bubbleSize);
        p.bubbleDir = {std::sin(a) * s, 0, std::cos(a) * s};
        p.pos = p.pos + p.bubbleDir * std::sin(p.bubblePhase);
        p.bubblePhase -= p.bubbleFreq * frame;
    } else {
        p.bubbleDir = {};
        p.bubbleFreq = p.bubblePhase = 0;
    }
    if (d.spin) {
        p.angle = random(0, kTwoPi);
        p.spinVel = random(d.spinSpeed) * kRpmToRadPerFrame;
        if (random() < 0.5f) p.spinVel = -p.spinVel;
    } else {
        p.angle = p.spinVel = 0;
    }
    float end[4];
    for (size_t i = 0; i < 4; ++i) {
        p.color[i] = random(params_.startColorLow[i], params_.startColorHigh[i]);
        end[i] = random(params_.endColorLow[i], params_.endColorHigh[i]);
    }
    p.size = random(params_.startSize);
    const float life = std::max(p.death - p.birth, 1e-4f);
    float sizeScale = 1.0f / life;
    p.sizeVel = random(d.deltaSize);
    if (p.sizeVel < -p.size) p.sizeVel = -p.size;
    if (d.type == 1) {
        if (d.growRatio == 0) {
            p.growEnd = p.birth, p.growVel = 0;
        } else {
            p.growEnd = p.birth + life * d.growRatio;
            p.growVel = p.size / std::max(p.growEnd - p.birth, 1e-4f);
        }
        if (d.shrinkRatio == 1) {
            p.shrinkStart = p.death, p.shrinkVel = 0;
        } else {
            p.shrinkStart = p.birth + life * d.shrinkRatio;
            const float span = p.shrinkStart - p.death;
            p.shrinkVel = std::fabs(span) > 1e-4f ? (p.sizeVel + p.size) / span : 0;
        }
        if (d.growRatio != 0) p.size = 0;
        p.midFrame = p.birth + life * d.midColorRatio;
        const float growSpan = p.shrinkStart - p.growEnd;
        sizeScale = std::fabs(growSpan) > 1e-4f ? 1.0f / growSpan : 0;
        float mid[4];
        for (size_t i = 0; i < 4; ++i) mid[i] = random(d.midColorLow[i], d.midColorHigh[i]);
        const float toEnd = p.death - p.midFrame;
        for (size_t i = 0; i < 4; ++i) {
            p.colorVel[i] = std::fabs(toEnd) > 1e-4f ? (end[i] - mid[i]) / toEnd : 0;
            p.midVel[i] = p.midFrame != p.birth ? (mid[i] - p.color[i]) / (p.midFrame - p.birth) : mid[i];
        }
    } else {
        for (size_t i = 0; i < 4; ++i) p.colorVel[i] = (end[i] - p.color[i]) / life;
    }
    p.sizeVel *= sizeScale;
    // A mesh emitter's points are already in the particles' space.
    if (!placed) {
        p.pos = xfmPoint(emission_, p.pos);
        p.vel = xfmDir(emission_, p.vel);
        if (d.bubble) p.bubbleDir = xfmDir(emission_, p.bubbleDir);
    }
}

void ParticleSystem::move(float frame, float dt) {
    if (dt == 0) return;
    const scene::ParticleSys& d = *def_;
    const float drag = d.drag > 0 ? std::pow(1.0f - d.drag, dt / 30.0f) : 1.0f;
    const float spinDamp = d.spin && d.spinDamping > 0 ? std::pow(1.0f - d.spinDamping, dt / 30.0f) : 1.0f;
    const Vec3 f{d.force[0] * dt, d.force[1] * dt, d.force[2] * dt};
    const Vec3 force{dot(f, forceRows_[0]), dot(f, forceRows_[1]), dot(f, forceRows_[2])};
    const bool fancy = d.type == 1;
    size_t w = 0;
    for (size_t r = 0; r < alive_.size(); ++r) {
        Particle& p = pool_[alive_[r]];
        if (p.death <= frame || frame <= p.birth) {
            free_.push_back(alive_[r]);
            continue;
        }
        p.pos = p.pos + p.vel * dt;
        if (d.bubble) p.pos = p.pos + p.bubbleDir * (std::cos(frame * p.bubbleFreq + p.bubblePhase) * dt * p.bubbleFreq);
        if (bounce_ && dot(p.pos, bounceN_) + bounceD_ < 0) {
            const float vn = dot(p.vel, bounceN_);
            if (vn < 0) p.vel = p.vel - bounceN_ * (2 * vn);
        }
        p.vel = p.vel + force;
        if (!fancy) {
            p.size += p.sizeVel * dt;
            for (int i = 0; i < 4; ++i) p.color[i] += p.colorVel[i] * dt;
        } else {
            p.vel = p.vel * drag;
            if (d.spin) {
                const float s = p.spinVel;
                p.spinVel *= spinDamp;
                p.angle += s * dt;
            }
            const float* cv = frame < p.midFrame ? p.midVel : p.colorVel;
            for (int i = 0; i < 4; ++i) p.color[i] += cv[i] * dt;
            const float sv = frame < p.growEnd ? p.growVel : frame < p.shrinkStart ? p.sizeVel : p.shrinkVel;
            p.size += sv * dt;
        }
        alive_[w++] = alive_[r];
    }
    alive_.resize(w);
}

void ParticleSystem::removeIf(const std::function<bool(const Vec3&)>& pred) {
    size_t w = 0;
    for (size_t r = 0; r < alive_.size(); ++r) {
        if (pred(pool_[alive_[r]].pos)) free_.push_back(alive_[r]);
        else alive_[w++] = alive_[r];
    }
    alive_.resize(w);
}

void ParticleSystem::emit(float frame, float dt) {
    if (!(dt > 0) || !emitting_) return;
    emitCount_ += random(params_.emitRate) * dt;
    while (emitCount_ >= 1) {
        if (free_.empty()) {
            emitCount_ = 0;
            return;
        }
        const uint32_t i = free_.back();
        free_.pop_back();
        init(pool_[i], frame);
        alive_.push_back(i);
        emitCount_ -= 1;
    }
}

// Pre-spawn (0x822146E8) [C]: over min(mean life, max / mean rate) frames
// before now, in steps of max(1 / mean rate, 1) frames.
void ParticleSystem::warmUp(float now) {
    warmUpPending_ = false;
    const float rate = (params_.emitRate[0] + params_.emitRate[1]) * 0.5f;
    if (rate < 0.0001f) return;
    const float inv = 1.0f / rate;
    const float span = std::min(float(pool_.size()) * inv, (params_.life[0] + params_.life[1]) * 0.5f);
    const float step = std::max(inv, 1.0f);
    for (float t = now - span; t <= now; t += step) {
        move(t, step);
        emit(t, step);
    }
}

void ParticleSystem::poll(float clockFrames) {
    if (!def_->preserved.empty()) return;
    const bool frameDriven = def_->frameDrive;
    const float now = frameDriven ? frame_ : clockFrames;
    if (warmUpPending_) {
        warmUp(now);
        lastFrame_ = frame_, lastClock_ = clockFrames;
        started_ = true;
        return;
    }
    float dt;
    if (!started_) {
        dt = 0;
        started_ = true;
    } else {
        dt = frameDriven ? frame_ - lastFrame_ : clockFrames - lastClock_;
    }
    // [I] The frame a poll ran on is consumed (lastFrame), so a system whose
    // frame stops changing stops moving.
    lastFrame_ = frame_, lastClock_ = clockFrames;
    move(now, dt);
    emit(now, dt);
}

void ParticleSystem::appendQuads(Vec3 camRight, Vec3 camUp, std::vector<float>& verts, std::vector<uint16_t>& indices) const {
    Vec3 right = camRight, up = camUp;
    if (local_) {
        // particles.fx's basis is in the particles' space, scaled by 0.5.
        const scene::Xfm inv = inverse(drawSpace_);
        right = xfmDir(inv, camRight), up = xfmDir(inv, camUp);
    }
    right = right * 0.5f, up = up * 0.5f;
    const Vec3 normal = normalize(cross(right, up));
    auto addParticle = [&](Vec3 pos, const float* color, float size, float angle) {
        if (verts.size() / 16 + 4 > 65535) return;
        const uint16_t b = uint16_t(verts.size() / 16);
        const float s = std::sin(angle) * size, c = std::cos(angle) * size;
        float col[4];
        for (int i = 0; i < 4; ++i) col[i] = std::clamp(color[i], 0.0f, 1.0f);
        static const float kCorner[4][2] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
        for (const auto& k : kCorner) {
            const float cx = k[0] * 2 - 1, cy = k[1] * 2 - 1;
            const Vec3 p = pos + right * (cx * c - cy * s) - up * (cx * s + cy * c);
            verts.insert(verts.end(), {p.x, p.y, p.z, normal.x, normal.y, normal.z, col[0], col[1], col[2], col[3], k[0], k[1],
                                       right.x, right.y, right.z, 1});
        }
        indices.insert(indices.end(), {b, uint16_t(b + 1), uint16_t(b + 2), b, uint16_t(b + 2), uint16_t(b + 3)});
    };
    if (!def_->preserved.empty()) {
        for (const auto& q : def_->preserved) addParticle({q.pos[0], q.pos[1], q.pos[2]}, q.color.data(), q.size, 0);
        return;
    }
    for (uint32_t i : alive_) {
        const Particle& p = pool_[i];
        if (p.size <= 0) continue;
        addParticle(p.pos, p.color, p.size, p.angle);
    }
}

// ---- ParticleRenderer ---------------------------------------------------

void ParticleRenderer::draw(MaterialRenderer& mr, SceneCache& cache, const ParticleSystem& sys, const Camera& camera,
                            const Mat4& offset, const ScenePose* pose) {
    verts_.clear(), indices_.clear();
    // The camera's right (x) and up (z) axes in world space.
    const Vec3 right{camera.world.at(0, 0), camera.world.at(1, 0), camera.world.at(2, 0)};
    const Vec3 up{camera.world.at(0, 2), camera.world.at(1, 2), camera.world.at(2, 2)};
    // `offset` moves the particles; the camera stays, so its axes go back
    // through the offset's rotation (none in practice: offsets translate).
    sys.appendQuads(right, up, verts_, indices_);
    if (indices_.empty()) return;
    const scene::ParticleSys& d = sys.def();
    const scene::Mat* mat = d.dir ? d.dir->resolve<scene::Mat>(d.mat) : nullptr;
    Material m;
    if (mat) m = cache.material(*mat, pose);
    else m.blend = MatBlend::Add, m.zMode = ZMode::Disable, m.useEnviron = false;
    m.cull = false;  // quads face the camera either way round
    const GpuMesh g = mesh_.upload(verts_, indices_);
    const Mat4 model[1] = {offset * (sys.localSpace() ? toMat4(sys.drawSpace()) : Mat4::identity())};
    DrawContext ctx;
    ctx.lit = false;
    ctx.particles = true;
    mr.draw(g, model, m, ctx);
    drawn_ += int(indices_.size() / 6);
}

// ---- ParticleSet --------------------------------------------------------

void ParticleSet::build(const scene::Object* root, uint32_t seed) {
    systems_.clear(), anims_.clear();
    std::vector<const scene::Object*> stack{root};
    std::vector<const scene::Object*> seen;
    while (!stack.empty()) {
        const scene::Object* o = stack.back();
        stack.pop_back();
        if (!o || std::find(seen.begin(), seen.end(), o) != seen.end()) continue;
        seen.push_back(o);
        if (auto* p = o->as<scene::ParticleSys>()) {
            systems_.emplace_back(*p, seed * 2654435761u + uint32_t(systems_.size()) * 40503u + 1);
        } else if (auto* a = o->as<scene::ParticleSysAnim>()) {
            anims_.push_back(a);
        } else if (auto* g = o->as<scene::Group>()) {
            for (auto it = g->objects.rbegin(); it != g->objects.rend(); ++it)
                if (const scene::Object* c = g->dir ? g->dir->resolve(*it) : nullptr; c && c != g) stack.push_back(c);
        } else if (auto* f = o->as<scene::AnimFilter>()) {
            if (const scene::Object* a2 = f->dir ? f->dir->resolve(f->anim) : nullptr) stack.push_back(a2);
        }
    }
}

void ParticleSet::reset() {
    for (ParticleSystem& s : systems_) s.reset();
}

void ParticleSet::sync(const ScenePose& pose) {
    for (const scene::ParticleSysAnim* a : anims_) {
        const float* f = pose.animFrame(a);
        if (!f || !a->dir) continue;
        const scene::ParticleSys* target = a->dir->resolve<scene::ParticleSys>(a->partSys);
        for (ParticleSystem& s : systems_)
            if (&s.def() == target) s.applyAnim(*a, *f);
    }
    for (ParticleSystem& s : systems_)
        if (const float* f = pose.animFrame(&s.def())) s.setFrame(*f);
}

void ParticleSet::setFrame(float frame) {
    for (ParticleSystem& s : systems_) s.setFrame(frame);
}

void ParticleSet::update(const ScenePose& pose, const scene::Xfm& base, float clockFrames) {
    for (ParticleSystem& s : systems_) {
        const scene::ParticleSys& d = s.def();
        const scene::Xfm world = scene::compose(pose.world(&d), base);
        scene::Xfm parent = world;
        if (!d.relativeParent.empty() && d.relativeParent != d.name && d.dir)
            if (const scene::Object* p = d.dir->resolve(d.relativeParent); p && transOf(p))
                parent = scene::compose(pose.world(p), base);
        s.setTransform(world, parent);
        bool bounce = false;
        if (!d.bounce.empty() && d.dir)
            if (const scene::Object* b = d.dir->resolve(d.bounce); b && transOf(b)) {
                const scene::Xfm bw = scene::compose(pose.world(b), base);
                const Vec3 n = normalize(rowOf(bw, 2));
                s.setBouncePlane(true, n, -dot(n, Vec3{bw.pos[0], bw.pos[1], bw.pos[2]}));
                bounce = true;
            }
        if (!bounce) s.setBouncePlane(false);
        if (!d.mesh.empty() && d.dir)
            if (const scene::Mesh* m = d.dir->resolve<scene::Mesh>(d.mesh))
                s.setEmitterMesh(m, scene::compose(pose.world(m), base));
        s.poll(clockFrames);
    }
}

void ParticleSet::setEmitting(bool on) {
    for (ParticleSystem& s : systems_) s.setEmitting(on);
}

void ParticleSet::draw(ParticleRenderer& pr, MaterialRenderer& mr, SceneCache& cache, const Camera& camera, const Mat4& offset,
                       const ScenePose* pose) {
    for (const ParticleSystem& s : systems_)
        if (s.count() > 0 || !s.def().preserved.empty())
            if (!pose || pose->showing(&s.def())) pr.draw(mr, cache, s, camera, offset, pose);
}

int ParticleSet::count() const {
    int n = 0;
    for (const ParticleSystem& s : systems_) n += s.count();
    return n;
}

}  // namespace gfx
