// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/crowd_view.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "char/character.h"
#include "game/crowd.h"
#include "game/world.h"
#include "render/band_view.h"

using gfx::Mat4;
using gfx::Vec3;

namespace {

constexpr int kCardW = 256, kCardH = 512;  // gfx_mode 1: 256 x 512, 32 bits (0x822496A0)

Vec3 row(const scene::Xfm& x, int r) { return {x.rot[size_t(r * 3)], x.rot[size_t(r * 3 + 1)], x.rot[size_t(r * 3 + 2)]}; }

// A world point on the card camera's screen: x right, y down, 0..1
// (Function_821D7030).
bool project(const Mat4& vp, const std::array<float, 3>& p, float& sx, float& sy) {
    const float* m = vp.m;
    const float cx = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12];
    const float cy = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13];
    const float cw = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
    if (std::fabs(cw) < 1e-6f) return false;
    sx = (cx / cw + 1) * 0.5f;
    sy = (1 - cy / cw) * 0.5f;
    return true;
}

}  // namespace

CrowdView::CrowdView(World& world, gfx::SceneCache& cache) : world_(world), cache_(cache) {
    chars_ = std::make_unique<BandView>(cache);
    dynamic_ = std::make_unique<gfx::DynamicMesh>(cache.device());
}

CrowdView::~CrowdView() {
    for (auto& [_, t] : targets_) cache_.device().destroyTarget(t);
}

gfx::TargetId CrowdView::target(const void* key) {
    auto it = targets_.find(key);
    if (it != targets_.end()) return it->second;
    std::string error;
    gfx::TargetDesc d;
    d.width = kCardW, d.height = kCardH, d.depth = true;
    const gfx::TargetId t = cache_.device().createTarget(d, error);
    if (!t.id) SDL_Log("crowd: card texture: %s", error.c_str());
    targets_[key] = t;
    return t;
}

CrowdView::Card CrowdView::cardFor(chr::Character& c, const scene::Xfm& mesh, float height, bool rotate, const gfx::Camera& camera) {
    Card card;
    const float h2 = height * 0.5f;
    const Mat4& w = camera.world;
    const Vec3 eye = camera.position();
    const Vec3 fwd{w.at(0, 1), w.at(1, 1), w.at(2, 1)};
    // The distance from the camera to the placement mesh's middle, at least
    // h / 2 past the near plane.
    const Vec3 d{eye.x - mesh.pos[0], eye.y - mesh.pos[1], eye.z - mesh.pos[2] - h2};
    const float dist = std::max(h2 + camera.nearPlane, std::sqrt(gfx::dot(d, d)));
    card.camera = camera;
    card.camera.world.m[12] = -dist * fwd.x;
    card.camera.world.m[13] = -dist * fwd.y;
    card.camera.world.m[14] = -dist * fwd.z + h2;
    card.camera.yFov = 2 * std::atan(h2 / dist);
    card.camera.aspect = float(kCardW) / float(kCardH);
    // The archetype's rotation at the origin.
    scene::Xfm turn = mesh;
    turn.pos = {0, 0, 0};
    if (rotate) {
        const Vec3 z = row(mesh, 2);
        Vec3 x = gfx::cross(z, fwd);
        if (gfx::dot(x, x) > 1e-12f) {
            x = gfx::normalize(x);
            const Vec3 y = gfx::cross(z, x);
            turn.rot = {x.x, x.y, x.z, y.x, y.y, y.z, z.x, z.y, z.z};
        }
    }
    card.turn = turn;
    c.setXfm(turn);
    // The texture rectangle: the feet, head and hands (forearms when there).
    const Mat4 vp = card.camera.viewProj(false);
    float minX = 3.4028235e38f, maxX = -3.4028235e38f, minY = 3.4028235e38f, maxY = -3.4028235e38f;
    auto add = [&](const std::array<float, 3>& p) {
        float sx, sy;
        if (!project(vp, p, sx, sy)) return;
        minX = std::min(minX, sx), maxX = std::max(maxX, sx);
        minY = std::min(minY, sy), maxY = std::max(maxY, sy);
    };
    add({0, 0, 0});
    for (const char* n : {"spot_head.trans", "spot_L-hand.trans", "spot_R-hand.trans", "spot_L-forearm.trans", "spot_R-forearm.trans"}) {
        scene::Xfm x;
        if (c.worldOf(n, x)) add(x.pos);
    }
    card.u0 = std::max(std::min(minX, 0.25f) - 0.05f, 0.0f);
    card.u1 = std::min(std::max(maxX, 0.75f) + 0.05f, 1.0f);
    card.v0 = std::max(std::min(minY, 0.4f) - 0.05f, 0.0f);
    card.v1 = std::min(std::max(maxY, 0.6f) + 0.05f, 1.0f);
    return card;
}

void CrowdView::cardQuad(const Card& k, float h, float xz[4][2], float uv[4][2]) {
    const float x0 = k.u0 * h * 0.5f - h * 0.25f, x1 = k.u1 * h * 0.5f - h * 0.25f;
    const float z0 = h * 0.5f - h * k.v0, z1 = h * 0.5f - h * k.v1;
    const float corners[4][4] = {{x0, z0, k.u0, k.v0}, {x0, z1, k.u0, k.v1}, {x1, z0, k.u1, k.v0}, {x1, z1, k.u1, k.v1}};
    for (int i = 0; i < 4; ++i) xz[i][0] = corners[i][0], xz[i][1] = corners[i][1], uv[i][0] = corners[i][2], uv[i][1] = corners[i][3];
}

void CrowdView::draw(gfx::MaterialRenderer& mr, const scene::WorldCrowd& c, const scene::Environ* env, const gfx::Camera& camera, bool hdr,
                     const std::function<void(const scene::Environ*)>& selectEnv) {
    Crowd& crowd = world_.crowd();
    const Crowd::State* s = nullptr;
    for (const Crowd::State& st : crowd.crowds())
        if (st.crowd == &c) s = &st;
    if (!s) return;
    // The placement mesh's world transform (the card camera's distance, the
    // archetypes' rotation).
    scene::Xfm mesh;
    if (!c.placementMesh.empty() && c.dir)
        if (const scene::Object* m = c.dir->resolve(c.placementMesh)) mesh = world_.pose().world(m);
    gfx::RenderDevice& dev = cache_.device();
    const double seconds = world_.seconds();
    for (const Crowd::Archetype& a : s->archetypes) {
        if (!a.actor || !a.actor->character) continue;
        chr::Character& ch = *a.actor->character;
        const scene::Environ* charEnv = a.actor->env ? a.actor->env : env;
        // The lighter flame's flicker: world_fx `start` (rnd_objects.dta)
        // is {$this animate (range 0 kHugeFloat)}: lighter_flame.filt (a
        // loop on its MatAnim) from frame 0 when the flame starts, 30
        // frames a second [C data; I: the clock].
        if (a.actor->lighter)
            if (const scene::Object* f = ch.dir().find("lighter_flame.filt"))
                flamePose_.setFrame(f, float(std::max(0.0, seconds - a.actor->lighterStart) * 30.0));
        int nCards = 0;
        if (!s->show3dOnly) nCards = std::min(int(a.cards.size()), int(float(a.cards.size()) * s->full2d));
        const int n3d = std::min(int(a.threeD.size()), int(float(a.threeD.size()) * s->full3d));
        if (nCards == 0 && n3d == 0) continue;
        const Card card = cardFor(ch, mesh, a.height, s->rotate, camera);
        if (nCards > 0) {
            // 1. The card texture: the archetype through the card camera.
            // (Making a target binds the window: the bound one is read first.)
            const gfx::TargetId prev = dev.currentTarget();
            int vx, vy, vw, vh;
            dev.currentViewport(vx, vy, vw, vh);
            const gfx::TargetId t = target(a.actor);
            if (t.id) {
                dev.setTarget(t);
                const float clear[4] = {0, 0, 0, 0};
                dev.clear(clear, true);
                gfx::LightEnv le;
                if (charEnv) le = cache_.lightEnv(*charEnv, &world_.pose());
                mr.setFrame(card.camera, le, kCardW, kCardH);
                chars_->forceAlphaWrite = true;
                chars_->drawCharacter(mr, ch, card.camera, false);
                if (a.actor->lighter) drawFlame(mr, ch, false);
                chars_->forceAlphaWrite = false;
                ++textures_;
            }
            dev.setTarget(prev);
            dev.setViewport(vx, vy, vw, vh);
            if (t.id) {
                selectEnv(env);
                // The cards: one quad per shown placement.
                float xz[4][2], uv[4][2];
                cardQuad(card, a.height, xz, uv);
                std::vector<float> verts;
                std::vector<uint16_t> idx;
                verts.reserve(size_t(nCards) * 64);
                idx.reserve(size_t(nCards) * 6);
                // The card mesh has the FastBillboardXYZ constraint (8) on the
                // card camera (0x822490F8 -> SetTransConstraint 0x821AFAA0),
                // so the RndMultiMesh draws it with multimesh.fx's BILLBOARD
                // variant (DxMultiMesh draw 0x8230F138: effect 0x13 when the
                // mesh's constraint is 8): each instance keeps only its
                // position, the quad turned like the camera (x its right, y
                // its forward, z its up) [C; C data multimesh.fx]. Cards
                // always face the camera, never edge-on.
                const Mat4& cw = camera.world;
                const Vec3 rx{cw.at(0, 0), cw.at(1, 0), cw.at(2, 0)}, ry{cw.at(0, 1), cw.at(1, 1), cw.at(2, 1)},
                    rz{cw.at(0, 2), cw.at(1, 2), cw.at(2, 2)};
                for (int i = 0; i < nCards && i < 16383; ++i) {
                    const scene::Xfm& p = a.placements[size_t(a.cards[size_t(i)])];
                    const uint16_t base = uint16_t(verts.size() / 16);
                    for (int k = 0; k < 4; ++k) {
                        const float x = xz[k][0], z = xz[k][1];
                        // GL's render target has its first row at the bottom.
                        verts.insert(verts.end(), {p.pos[0] + rx.x * x + rz.x * z, p.pos[1] + rx.y * x + rz.y * z, p.pos[2] + rx.z * x + rz.z * z,
                                                   -ry.x, -ry.y, -ry.z, 1, 1, 1, 1, uv[k][0], 1 - uv[k][1], rx.x, rx.y, rx.z, 1});
                    }
                    idx.insert(idx.end(), {base, uint16_t(base + 1), uint16_t(base + 2), uint16_t(base + 2), uint16_t(base + 1), uint16_t(base + 3)});
                }
                gfx::GpuMesh g = dynamic_->upload(verts, idx);
                g.skinned = false;
                gfx::Material m;
                // The card Mat (WorldCrowd ctor 0x82249368) [C]: use_environ
                // (+0x40) and prelit (+0xa0) off, blend 1 = src (+0x2c),
                // alpha_cut (+0xa1), tex_wrap 0 = clamp (+0x50); cull stays
                // the Mat default, TRUE (Mat ctor 0x821ECC78, +0xb0): the
                // billboard always shows the camera its front.
                m.useEnviron = false;
                m.prelit = false;
                m.blend = gfx::MatBlend::Src;
                m.alphaCut = true;
                m.cull = true;
                m.texWrap = gfx::TexWrap::Clamp;
                m.diffuseTex = dev.targetTexture(t);
                gfx::DrawContext ctx;
                ctx.bones = 1;
                ctx.hdr = hdr;
                const Mat4 id = Mat4::identity();
                mr.draw(g, &id, m, ctx);
                ++drawCalls_;
                cards_ += nCards;
                characters_ += nCards;
            }
        }
        // 2. The shot's 3D placements.
        if (n3d > 0) {
            selectEnv(charEnv);
            for (int i = 0; i < n3d; ++i) {
                scene::Xfm x = card.turn;
                const scene::Xfm& p = a.placements[size_t(a.threeD[size_t(i)])];
                x.pos = {p.pos[0], p.pos[1], p.pos[2] - a.height * 0.5f};
                ch.setXfm(x);
                const int before = chars_->meshesDrawn();
                chars_->drawCharacter(mr, ch, camera, hdr);
                if (a.actor->lighter) drawFlame(mr, ch, hdr);
                drawCalls_ += chars_->meshesDrawn() - before;
                ++characters_;
            }
            selectEnv(env);
        }
    }
}

// lighter_flame (the inline RndDir under bone_R-hand.mesh) and its mesh.
void CrowdView::drawFlame(gfx::MaterialRenderer& mr, chr::Character& c, bool hdr) {
    const scene::Directory& d = c.dir();
    const scene::DirObj* dir = nullptr;
    for (const auto& o : d.objects)
        if (o->name == "lighter_flame") dir = o->as<scene::DirObj>();
    if (!dir) return;
    scene::Xfm parent;
    if (!c.worldOf(dir->rnd.trans.parent, parent)) return;
    const scene::Xfm dw = scene::compose(dir->rnd.trans.local, parent);
    for (const auto& [owner, sub] : d.subdirs) {
        if (owner != dir->name || !sub) continue;
        for (const auto& o : sub->objects) {
            auto* m = o->as<scene::Mesh>();
            if (!m || !m->decoded || !m->draw.showing) continue;
            const gfx::GpuMesh* g = cache_.mesh(m);
            const scene::Mat* mat = m->dir ? m->dir->resolve<scene::Mat>(m->mat) : nullptr;
            if (!g || !mat) continue;
            const Mat4 w = gfx::toMat4(scene::compose(m->trans.local, dw));
            gfx::GpuMesh gm = *g;
            gm.skinned = false;
            gfx::Material material = cache_.material(*mat, &flamePose_);
            if (chars_->forceAlphaWrite) material.alphaWrite = true;
            gfx::DrawContext ctx;
            ctx.bones = 1;
            ctx.hdr = hdr;
            mr.draw(gm, &w, material, ctx);
            ++drawCalls_;
        }
    }
}
