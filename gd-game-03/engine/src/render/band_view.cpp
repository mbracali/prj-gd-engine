// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/band_view.h"

#include <functional>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <set>

#include "char/character.h"
#include "game/band.h"
#include "game/world.h"

using gfx::Mat4;

float screenSize(const gfx::Camera& camera, const std::array<float, 4>& sphere) {
    // The camera looks down its local +y (GH2's view axis, render/camera.h).
    const Mat4& w = camera.world;
    const float eye[3] = {w.m[12], w.m[13], w.m[14]};
    const float fwd[3] = {w.m[4], w.m[5], w.m[6]};
    const float d[3] = {sphere[0] - eye[0], sphere[1] - eye[1], sphere[2] - eye[2]};
    float depth = d[0] * fwd[0] + d[1] * fwd[1] + d[2] * fwd[2];
    const float fl = std::sqrt(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
    if (fl > 0) depth /= fl;
    if (depth < 1e-3f) return 1e30f;
    const float t = std::tan(std::max(0.01f, camera.yFov) * 0.5f);
    return std::fabs(sphere[3] / (depth * t));
}

BandView::BandView(World& world, gfx::SceneCache& cache) : world_(&world), cache_(cache) {
    dynamic_ = std::make_unique<gfx::DynamicMesh>(cache.device());
}

BandView::BandView(gfx::SceneCache& cache) : cache_(cache) { dynamic_ = std::make_unique<gfx::DynamicMesh>(cache.device()); }

BandView::~BandView() = default;

bool BandView::draw(gfx::MaterialRenderer& mr, const scene::Object* slot, const scene::Environ* fallback, const gfx::Camera& camera,
                    bool hdr, const std::function<void(const scene::Environ*)>& selectEnv) {
    Band* band = world_ ? world_->band : nullptr;
    Band::Member* m = band ? band->memberOf(slot) : nullptr;
    if (!m || !m->character) return false;
    chr::Character& c = *m->character;
    if (!c.visible) return true;
    selectEnv(c.slotEnviron ? c.slotEnviron : fallback);
    drawCharacter(mr, c, camera, hdr);
    if (!m->fx.empty()) drawFx(mr, c, m, camera);
    return true;
}

void BandView::drawFx(gfx::MaterialRenderer& mr, chr::Character& c, const void* member, const gfx::Camera& camera) {
    const auto* m = static_cast<const Band::Member*>(member);
    if (!particles_) particles_ = std::make_unique<gfx::ParticleRenderer>(cache_.device());
    particles_->setHdr(hdr_);
    const double now = world_ ? world_->seconds() : 0.0;
    for (const auto& [name, f] : m->fx) {
        if (!f || !f->obj || !f->milo) continue;
        FxState& st = fx_[f.get()];
        if (st.parts.empty()) {
            uint32_t seed = 11;
            for (const auto& o : f->milo->root().objects)
                if (o->as<scene::ParticleSys>() || o->as<scene::ParticleSysAnim>()) st.parts.add(o.get(), seed++);
            if (st.parts.empty()) continue;
        }
        // WorldFx (0x00272b78): start (0x00272668) sets every animatable in
        // it to frame 0 and turns it on; while on, its poll (0x00272580)
        // moves each one on by the task clock's step in its units times its
        // rate (the effects' anims run at 30 frames a second); stop
        // (0x00272890) turns it off, the anims holding their frame, and
        // frees every live particle of its systems (0x001d0580) [C]. The
        // time is the world clock's forward steps (the band enters before a
        // song resets that clock).
        if (st.start != f->start) st.start = f->start, st.elapsed = 0, st.last = now;
        if (f->on && now > st.last && now - st.last < 1.0) st.elapsed += now - st.last;
        st.last = now;
        if (st.on && !f->on) st.parts.reset();
        st.on = f->on;
        const float frame = float(st.elapsed * 30.0);
        // The MatAnims run through their AnimFilters (cheat_headflames_0n.filt
        // over .mnm) when the effect has filters [I: the filters' targets].
        bool filters = false;
        for (const auto& o : f->milo->root().objects) filters = filters || o->cls == "AnimFilter";
        for (const auto& o : f->milo->root().objects)
            if (o->cls == "AnimFilter" || (!filters && o->as<scene::MatAnim>()) || o->as<scene::ParticleSysAnim>() || o->as<scene::ParticleSys>())
                st.pose.setFrame(o.get(), frame);
        scene::Xfm parent;
        if (!c.worldOf(f->obj->rnd.trans.parent, parent)) continue;
        const scene::Xfm base = scene::compose(f->obj->rnd.trans.local, parent);
        st.parts.sync(st.pose);
        st.parts.setEmitting(f->on);
        st.parts.update(st.pose, base, float(now * 30.0));
        const int before = particles_->drawnParticles();
        st.parts.draw(*particles_, mr, cache_, camera, Mat4::identity(), &st.pose);
        fxParticles_ += particles_->drawnParticles() - before;
    }
}

void BandView::drawCharacter(gfx::MaterialRenderer& mr, chr::Character& c, const gfx::Camera& camera, bool hdr) {
    hdr_ = hdr;
    // Character::DrawShowing 0x82153090: the LOD, then the directory.
    c.updateLod(screenSize(camera, c.worldSphere()));
    for (const scene::Mesh* m : meshesToDraw(c, world_ ? &world_->pose() : nullptr)) drawMesh(mr, c, *m);
}

void BandView::drawShadow(gfx::MaterialRenderer& mr, chr::Character& c, const std::array<float, 3>& dir, bool hdr) {
    const scene::Group* g = c.shadowGroup();
    if (!g) return;
    hdr_ = hdr;
    flatten_ = true, flatDir_ = dir, flatZ_ = c.xfm().pos[2] + 1.5f;
    std::vector<const scene::Mesh*> meshes;
    std::function<void(const scene::Group*, int)> walk = [&](const scene::Group* grp, int depth) {
        if (depth > 8) return;
        for (const std::string& n : grp->objects) {
            const scene::Object* o = c.dir().get(n);
            if (!o) continue;
            if (auto* m = o->as<scene::Mesh>()) meshes.push_back(m);
            else if (auto* sub = o->as<scene::Group>(); sub && sub != grp) walk(sub, depth + 1);
        }
    };
    walk(g, 0);
    static bool logged = false;  // the material the reading assumes (adds black, writes depth): logged once
    if (!logged && !meshes.empty() && meshes[0]->dir)
        if (const scene::Mat* mat = meshes[0]->dir->resolve<scene::Mat>(meshes[0]->mat)) {
            logged = true;
            SDL_Log("band shadow: %s, %zu meshes, %s blend %d zmode %d colour %.2f %.2f %.2f %.2f", c.dir().name.c_str(), meshes.size(),
                    meshes[0]->mat.c_str(), mat->blend, mat->zMode, double(mat->color[0]), double(mat->color[1]), double(mat->color[2]),
                    double(mat->color[3]));
        }
    for (const scene::Mesh* m : meshes) drawMesh(mr, c, *m);
    flatten_ = false;
}

// A world transform pushed along the light onto z = flatZ_: each row of the
// rotation loses its height through the light's direction, as the shear of
// GH2's Character::DrawShadow (P' = P - dir (P.z - plane) / dir.z).
scene::Xfm BandView::flattened(scene::Xfm x) const {
    if (!flatten_) return x;
    const float dx = flatDir_[0] / flatDir_[2], dy = flatDir_[1] / flatDir_[2];
    for (int r = 0; r < 3; ++r) {
        const float h = x.rot[size_t(r * 3 + 2)];
        x.rot[size_t(r * 3)] -= dx * h, x.rot[size_t(r * 3 + 1)] -= dy * h, x.rot[size_t(r * 3 + 2)] = 0;
    }
    const float h = x.pos[2] - flatZ_;
    x.pos[0] -= dx * h, x.pos[1] -= dy * h, x.pos[2] = flatZ_;
    return x;
}

namespace {

void collect(const chr::Character& c, const gfx::ScenePose* pose, const scene::Object* o, int depth, std::vector<const scene::Mesh*>& out) {
    // The character's own showing (Character::DrawShowing: its LOD switch
    // sets the group's flag, 0x82151DE0) wins over the world pose's, which
    // only knows the saved flag (lod1.grp is saved hidden): otherwise a
    // lower LOD (set_min_lod 1 in the band_POV shots, or distance) drew
    // nothing and the band vanished, filmed from behind.
    if (!o || depth > 16 || !c.showing(o)) return;
    if (pose && !c.showingSet(o) && !pose->showing(o)) return;
    if (auto* m = o->as<scene::Mesh>()) {
        out.push_back(m);
    } else if (auto* g = o->as<scene::Group>()) {
        if (g == c.shadowGroup()) return;
        for (const std::string& n : g->objects)
            if (const scene::Object* x = c.dir().get(n); x && x != g) collect(c, pose, x, depth + 1, out);
    }
}

}  // namespace

std::vector<const scene::Mesh*> BandView::meshesToDraw(const chr::Character& c, const gfx::ScenePose* pose) {
    const scene::Directory& d = c.dir();
    std::set<const scene::Object*> children;
    std::vector<const scene::Object*> roots;
    for (const auto& o : d.objects) {
        if (auto* g = o->as<scene::Group>())
            for (const std::string& n : g->objects)
                if (const scene::Object* x = d.get(n); x && x != g) children.insert(x);
    }
    for (const auto& o : d.objects) {
        if (!o->decoded || children.count(o.get())) continue;
        if (o.get() == c.shadowGroup()) continue;
        if (o->as<scene::Mesh>() || o->as<scene::Group>()) roots.push_back(o.get());
    }
    std::stable_sort(roots.begin(), roots.end(), [](const scene::Object* a, const scene::Object* b) {
        const scene::Draw* da = gfx::drawOf(a);
        const scene::Draw* db = gfx::drawOf(b);
        return (da ? da->order : 0) < (db ? db->order : 0);
    });
    std::vector<const scene::Mesh*> out;
    for (const scene::Object* o : roots) collect(c, pose, o, 0, out);
    return out;
}

void BandView::drawMesh(gfx::MaterialRenderer& mr, chr::Character& c, const scene::Mesh& m) {
    const gfx::GpuMesh* cached = cache_.mesh(&m);
    if (!cached || !m.dir) return;
    gfx::GpuMesh g = *cached;
    const scene::Mesh* geom = &m;
    if (m.verts.empty() && !m.geomOwner.empty())
        if (auto* owner = m.dir->resolve<scene::Mesh>(m.geomOwner)) geom = owner;
    const gfx::ScenePose* pose = world_ ? &world_->pose() : nullptr;
    if (pose)
        if (const gfx::ScenePose::MeshAnimState* ma = pose->meshAnim(&m); ma && ma->anim) {
            const std::vector<float> v = gfx::meshAnimVertices(*geom, *ma->anim, ma->frame);
            std::vector<uint16_t> idx;
            idx.reserve(geom->faces.size() * 3);
            for (const auto& f : geom->faces) idx.insert(idx.end(), {f[0], f[1], f[2]});
            g = dynamic_->upload(v, idx);
            g.skinned = !geom->bones.empty();
        }
    chr::Skeleton& sk = c.skeleton();
    Mat4 bones[4];
    const int self = sk.find(m.name);
    const scene::Xfm world = self >= 0 ? sk.world(self) : sk.root();
    bones[0] = gfx::toMat4(flattened(world));
    int boneCount = 1;
    if (g.skinned && !geom->bones.empty()) {
        boneCount = int(std::min<size_t>(geom->bones.size(), 4));
        for (int i = 0; i < boneCount; ++i) {
            const auto& bone = geom->bones[size_t(i)];
            const int b = sk.find(bone.name);
            bones[i] = b >= 0 ? gfx::toMat4(flattened(scene::compose(bone.offset, sk.world(b)))) : bones[0];
        }
    } else {
        g.skinned = false;
    }
    gfx::DrawContext ctx;
    ctx.bones = boneCount;
    ctx.hdr = hdr_;
    const scene::Mat* mat = m.dir->resolve<scene::Mat>(m.mat);
    if (!mat) {
        mr.draw(g, bones, gfx::Material{}, ctx);
        ++meshes_;
        return;
    }
    for (int pass = 0; mat && pass < 4; ++pass) {
        gfx::Material gm = cache_.material(*mat, matPose ? matPose : pose);
        if (forceAlphaWrite) gm.alphaWrite = true;
        mr.draw(g, bones, gm, ctx);
        if (mat->nextPass.empty() || !mat->dir) break;
        const scene::Mat* next = mat->dir->resolve<scene::Mat>(mat->nextPass);
        mat = next != mat ? next : nullptr;
    }
    ++meshes_;
}
