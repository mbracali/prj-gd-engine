// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/crowd_renderer.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "data/dta.h"
#include "data/game_data.h"
#include "game/venue_script.h"
#include "scene/anim.h"

namespace {

// arena/anims_macros.dta
constexpr uint32_t kIdle = 8, kBad = 16, kOk = 32, kGreat = 128;

int gCheatCrowd = 0;  // $cheat_crowd for the next venue load

std::map<std::string, int> intDefines(const DataNode& root) {
    std::map<std::string, int> out;
    for (size_t i = 0; i + 1 < root.children.size(); ++i) {
        const DataNode& d = root.children[i];
        const DataNode& v = root.children[i + 1];
        if (d.type == DataNode::Type::Define && v.isList() && v.children.size() == 1 &&
            v.children[0].type == DataNode::Type::Int)
            out[d.s] = v.children[0].i;
    }
    return out;
}

const DataNode* defineValue(const DataNode& root, const std::string& name) {
    for (size_t i = 0; i + 1 < root.children.size(); ++i)
        if (root.children[i].type == DataNode::Type::Define && root.children[i].s == name) return &root.children[i + 1];
    return nullptr;
}

}  // namespace

CrowdRenderer::CrowdRenderer(Renderer& renderer, const GameData& data, AnimPlayer& venue, const std::string& venueName)
    : renderer_(renderer), venue_(venue) {
    // Crowd01.mm .. Crowd06.mm (FUN_00171558: "arena::Crowd%02d.mm").
    for (int i = 1; i <= 6; ++i) {
        char name[32];
        std::snprintf(name, sizeof name, "Crowd%02d.mm", i);
        if (SceneObject* o = venue_.find(name); o && o->multiMesh) {
            o->multiMesh->draw.showing = false;  // drawn here with the impostor quad instead
            blocks_.push_back({o->multiMesh.get(), i - 1, {}, {}, {}});
        }
    }
    if (blocks_.empty()) throw std::runtime_error("venue has no crowd");
    // FUN_00171558: the crowd frame M = MakeRot(average forward of every
    // block's instances, z); the impostors are rendered as if facing it.
    {
        std::array<float, 3> f{0, 0, 0};
        for (const Block& b : blocks_)
            for (const Xfm& x : b.mm->instances)
                for (int k = 0; k < 3; ++k) f[size_t(k)] += x.rot[size_t(3 + k)];
        float l = std::hypot(f[0], f[1], f[2]);
        std::array<float, 3> y = l > 1e-6f ? std::array<float, 3>{f[0] / l, f[1] / l, f[2] / l} : std::array<float, 3>{0, 1, 0};
        std::array<float, 3> x{y[1], -y[0], 0};  // y x (0, 0, 1)
        float xl = std::hypot(x[0], x[1]);
        if (xl > 1e-6f) x = {x[0] / xl, x[1] / xl, 0};
        else x = {1, 0, 0};
        std::array<float, 3> z{x[1] * y[2] - x[2] * y[1], x[2] * y[0] - x[0] * y[2], x[0] * y[1] - x[1] * y[0]};
        crowdFrame_ = {x[0], x[1], x[2], y[0], y[1], y[2], z[0], z[1], z[2]};
    }

    // config arena.dta: (crowd_flat_height 100.8)
    if (std::string cfg = "../../system/run/config/gen/arena.dtb"; data.exists(cfg))
        if (const DataNode* h = parseDtb(data.read(cfg)).find("crowd_flat_height"); h && h->children.size() > 1)
            height_ = h->floatAt(1);

    // arena/crowd.dta: {crowd load_archetypes (CROWD_MALE1 ...)}; each macro is
    // ((crowd_male01 (rnd "charsys/crowd/crowd_male01.rnd") (remove (head (...) ...))
    //   (main.drv (anims main::crowd.cset) (restrict_flags C_MALE1)) ...)).
    DataNode script = parseDtb(data.read(dtbPath("arena/crowd.dta")));
    std::map<std::string, int> macros = intDefines(parseDtb(data.read(dtbPath("arena/anims_macros.dta"))));
    const DataNode* list = nullptr;
    for (const DataNode& c : script.children)
        if (c.type == DataNode::Type::Command && c.children.size() >= 3 && c.children[1].isText() &&
            c.children[1].s == "load_archetypes" && c.children[2].isList())
            list = &c.children[2];
    if (!list) throw std::runtime_error("crowd: no archetypes");
    struct ArchSpec {
        Character::Spec spec;
        std::vector<std::string> heads;
        std::vector<std::vector<std::string>> outfits;  // swap (mat (..) (..) (..))
    };
    std::vector<ArchSpec> specs;
    for (const DataNode& m : list->children) {
        const DataNode* def = m.isText() ? defineValue(script, m.s) : nullptr;
        if (!def || def->children.empty() || !def->children[0].isList()) continue;
        const DataNode& body = def->children[0];
        const DataNode* rnd = body.find("rnd");
        if (!rnd) continue;
        std::string path = rnd->textAt(1);
        std::string model = path.substr(path.rfind('/') + 1);
        model = model.substr(0, model.find('.'));
        ArchSpec a;
        uint32_t restrict = 0;
        if (const DataNode* drv = body.find("main.drv"))
            if (const DataNode* r = drv->find("restrict_flags"))
                for (size_t k = 1; k < r->children.size(); ++k)
                    if (auto it = macros.find(r->children[k].s); it != macros.end()) restrict |= uint32_t(it->second);
        if (const DataNode* rem = body.find("remove"))
            if (const DataNode* heads = rem->find("head"))
                for (size_t k = 1; k < heads->children.size(); ++k)
                    if (heads->children[k].isList() && !heads->children[k].children.empty())
                        a.heads.push_back(heads->children[k].children[0].s);
        if (const DataNode* sw = body.find("swap"))
            if (const DataNode* mats = sw->find("mat"))
                for (size_t k = 1; k < mats->children.size(); ++k)
                    if (mats->children[k].isList()) {
                        std::vector<std::string> set;
                        for (const DataNode& n : mats->children[k].children) set.push_back(n.s);
                        a.outfits.push_back(std::move(set));
                    }
        // Tempo 3 accepts every clip (the crowd's C_MALE1 / C_MALE2 bits overlap the tempo bits).
        a.spec = Character::Spec{model, "", "arena/anims_macros.dta", "CROWD_ANIMS", 3, restrict, "crowd", 0};
        specs.push_back(std::move(a));
    }

    // crowd.dta finish_loading: {crowd instance a (head h mat m)} x 17 (docs/crowd3d.md 1.1);
    // archetypes 0 male01 1 male02 2 male03 3 female01 4 female02 5 female03. The first
    // six are also the impostors of blocks 1..6. Fest stops at 10, arena / big_club /
    // theatre at 12.
    static const int kPool[17][3] = {{0, 0, 0}, {1, 4, 1}, {2, 5, 2}, {3, 1, 0}, {5, 5, 2}, {4, 0, 0},
                                     {0, 3, 0}, {5, 3, 0}, {1, 1, 0}, {2, 2, 0}, {4, 4, 1}, {3, 2, 0},
                                     {0, 5, 2}, {2, 4, 1}, {2, 1, 0}, {5, 1, 0}, {3, 5, 2}};
    int count = venueName == "fest" ? 10
              : venueName == "arena" || venueName == "big_club" || venueName == "theatre" ? 12 : 17;
    for (int k = 0; k < count; ++k) {
        Member mb;
        int arch = kPool[k][0];
        if (arch < int(specs.size())) {
            const ArchSpec& sp = specs[size_t(arch)];
            try {
                mb.character = std::make_unique<Character>(renderer, data, sp.spec);
                mb.character->setWorldLookup([this](const std::string& n) -> const SceneObject* { return venue_.find(n); });
                mb.heads = sp.heads;
                // $cheat_crowd 1 / 2: the same pool with head 6 (monkey) / 7 (skull).
                int head = gCheatCrowd == 1 ? 6 : gCheatCrowd == 2 ? 7 : kPool[k][1];
                for (size_t h = 0; h < mb.heads.size(); ++h)
                    mb.character->setShowing(mb.heads[h], h == size_t(head) % mb.heads.size());
                mb.character->swapMaterials(sp.outfits, kPool[k][2]);
                if (k < 6) mb.target = renderer.createRenderTarget(128, 256);
            } catch (const std::exception& ex) {
                SDL_Log("crowd member %s unavailable: %s", sp.spec.model.c_str(), ex.what());
                mb.character.reset();
            }
        }
        members_.push_back(std::move(mb));
        pool_.push_front(k);  // FUN_00172d88: to the front
    }
    if (std::none_of(members_.begin(), members_.end(), [](const Member& m) { return m.character != nullptr; }))
        throw std::runtime_error("crowd: no members");
    for (Block& bl : blocks_)
        for (int i = 0; i < int(bl.mm->instances.size()); ++i) bl.shown.push_back(i);
    buildRegions();

    // The flat member quad (FUN_00170160): height x height/2, centred on the
    // instance, in its XZ plane; transform constraint 8 (FastBillboardXYZ)
    // turns it to the camera. Render targets have v = 0 at the bottom.
    float hw = height_ * 0.25f, hh = height_ * 0.5f;
    std::vector<Vertex> v = {
        {-hw, 0, hh, 0, 1, 255, 255, 255, 255}, {-hw, 0, -hh, 0, 0, 255, 255, 255, 255},
        {hw, 0, hh, 1, 1, 255, 255, 255, 255},  {hw, 0, -hh, 1, 0, 255, 255, 255, 255}};
    quad_ = renderer.createMesh(v, {0, 1, 2, 1, 3, 2});
    // crowd.dta init: {crowd set_sizes 1 1} {animate_crowd 0 0.9 0.1 0}.
    animate({0, 0.9f, 0.1f, 0});
}

void CrowdRenderer::setCheatCrowd(int mode) { gCheatCrowd = mode; }

void CrowdRenderer::tempoEvent(const std::string& event) {
    if (event == "crowd_half_tempo") frameScale_ = 0.5f;
    else if (event == "crowd_double_tempo") frameScale_ = 2;
    else if (event == "crowd_normal_tempo") frameScale_ = 1;
}

// FUN_00173e70 / FUN_00173508 (docs/crowd3d.md 2.6): the active members and
// the whole pool, shuffled and split by the weights (bad, ok, great, idle) at
// round(cum * n / total); each takes the crowd-wide least recently used clip
// of its group, looping, at a random phase (bad +-5, ok +-0.25, great -1..2,
// idle +-0.25 beats).
void CrowdRenderer::animate(const std::array<float, 4>& weights) {
    std::vector<int> chars(active_.begin(), active_.end());
    chars.insert(chars.end(), pool_.begin(), pool_.end());
    std::shuffle(chars.begin(), chars.end(), rng_);
    static const uint32_t kGroups[4] = {kBad, kOk, kGreat, kIdle};
    static const float kPhase[4][2] = {{-5, 5}, {-0.25f, 0.25f}, {-1, 2}, {-0.25f, 0.25f}};
    float total = weights[0] + weights[1] + weights[2] + weights[3];
    if (total <= 0) return;
    const size_t n = chars.size();
    size_t from = 0;
    float cum = 0;
    for (int g = 0; g < 4; ++g) {
        cum += weights[size_t(g)];
        size_t to = std::min(n, size_t(std::lround(cum * float(n) / total)));
        for (size_t i = from; i < to; ++i) {
            Character* c = members_[size_t(chars[i])].character.get();
            if (!c) continue;
            int best = -1;
            uint64_t bestUsed = 0;
            for (int clip : c->clipsWith(kGroups[g], true)) {
                uint64_t used = lru_[c->clipNameOf(clip)];
                if (best < 0 || used < bestUsed) best = clip, bestUsed = used;
            }
            if (best < 0) continue;
            lru_[c->clipNameOf(best)] = ++lruClock_;
            float start = c->clipStart(best), len = c->clipEnd(best) - 1.0f - start;
            float off = std::uniform_real_distribution<float>(kPhase[g][0], kPhase[g][1])(rng_);
            float frame = len > 0 ? std::fmod(off, len) : 0;
            if (frame < 0) frame += len;
            c->startAt(best, Character::kAnimReset | Character::kAnimGraphLoop, start + frame);
        }
        from = to;
    }
}

// FUN_00172d88: back to the front of the pool.
void CrowdRenderer::returnToPool(int member) { pool_.push_front(member); }

// FUN_00170da8: the flat instances inside each crowd_limitsNN mesh (local
// 0 < z < flat height, XY inside a face), with the region's floor, centre and
// radius (the full AABB diagonal).
void CrowdRenderer::buildRegions() {
    for (int r = 0;; ++r) {
        char name[32];
        std::snprintf(name, sizeof name, "crowd_limits%02d.mesh", r);
        SceneObject* o = venue_.find(name);
        if (!o || !o->mesh) break;
        o->mesh->draw.showing = false;  // never drawn
        const MeshObj* geom = o->mesh.get();
        if (!geom->geomOwner.empty() && geom->geomOwner != name)
            if (SceneObject* g = venue_.find(geom->geomOwner); g && g->mesh) geom = g->mesh.get();
        const Xfm& w = o->mesh->trans.world;
        Xfm inv = inverse(w);
        Region reg;
        std::array<float, 3> lo{}, hi{};
        bool first = true;
        auto inside = [&](float px, float py) {  // FUN_001e3600, XY only
            for (const auto& f : geom->faces) {
                if (f[0] >= geom->verts.size() || f[1] >= geom->verts.size() || f[2] >= geom->verts.size()) continue;
                const float* a = geom->verts[f[0]].pos;
                const float* b = geom->verts[f[1]].pos;
                const float* c = geom->verts[f[2]].pos;
                float bx = b[0] - a[0], by = b[1] - a[1], cx = c[0] - a[0], cy = c[1] - a[1];
                float det = bx * cy - by * cx;
                if (std::fabs(det) < 1e-5f) continue;
                float qx = px - a[0], qy = py - a[1];
                float u = (qx * cy - qy * cx) / det, v = (bx * qy - by * qx) / det;
                if (u >= 0 && u <= 1 && v >= 0 && u + v <= 1) return true;
            }
            return false;
        };
        for (int bi = 0; bi < int(blocks_.size()); ++bi) {
            const auto& inst = blocks_[size_t(bi)].mm->instances;
            for (int ii = 0; ii < int(inst.size()); ++ii) {
                if (reg.insts.size() >= members_.size()) break;  // capped at the pool size
                const auto& p = inst[size_t(ii)].pos;
                std::array<float, 3> l;
                for (int j = 0; j < 3; ++j)
                    l[size_t(j)] = p[0] * inv.rot[size_t(j)] + p[1] * inv.rot[size_t(3 + j)] + p[2] * inv.rot[size_t(6 + j)] + inv.pos[size_t(j)];
                if (!(l[2] > 0 && l[2] < height_) || !inside(l[0], l[1])) continue;
                reg.insts.push_back({bi, ii});
                for (int j = 0; j < 3; ++j) {
                    lo[size_t(j)] = first ? p[size_t(j)] : std::min(lo[size_t(j)], p[size_t(j)]);
                    hi[size_t(j)] = first ? p[size_t(j)] : std::max(hi[size_t(j)], p[size_t(j)]);
                }
                first = false;
            }
        }
        if (!geom->verts.empty()) {
            const float* v0 = geom->verts[0].pos;
            reg.floorZ = v0[0] * w.rot[2] + v0[1] * w.rot[5] + v0[2] * w.rot[8] + w.pos[2];
        }
        for (int j = 0; j < 3; ++j) reg.centre[size_t(j)] = (lo[size_t(j)] + hi[size_t(j)]) * 0.5f;
        reg.radius = std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) + (hi[2] - lo[2]) * (hi[2] - lo[2]));
        regions_.push_back(std::move(reg));
    }
}

// A member stands on the region's instance `spot`: its rotation, (x, y, floorZ).
void CrowdRenderer::place(int member, size_t spot) {
    const Region& r = regions_[size_t(region_)];
    if (spot >= r.insts.size() || !members_[size_t(member)].character) return;
    const Xfm& in = blocks_[size_t(r.insts[spot].first)].mm->instances[size_t(r.insts[spot].second)];
    Xfm x;
    x.rot = in.rot;
    x.pos = {in.pos[0], in.pos[1], r.floorZ};
    members_[size_t(member)].character->setPlacement(x);
}

// FUN_001727b0: the best-scoring region (min(size, 15) / max(distance from the
// screen centre, 0.2)) or the shot's; its sprites leave the flat blocks.
void CrowdRenderer::switchRegion(int idx, const Xfm& camera, float projX) {
    if (regions_.empty()) return;
    int R = int(regions_.size()) - 1;
    if (idx < 0) {
        float best = 0;
        const auto& f = camera.rot;  // rows right, forward, up
        for (int i = 0; i < int(regions_.size()); ++i) {
            const Region& r = regions_[size_t(i)];
            if (r.insts.empty()) continue;  // uninitialised in the original [I]
            std::array<float, 3> d{r.centre[0] - camera.pos[0], r.centre[1] - camera.pos[1], r.centre[2] - camera.pos[2]};
            float depth = d[0] * f[3] + d[1] * f[4] + d[2] * f[5];
            if (std::fabs(depth) < 1e-3f) continue;
            float sx = (d[0] * f[0] + d[1] * f[1] + d[2] * f[2]) * projX / depth;
            float sy = (d[0] * f[6] + d[1] * f[7] + d[2] * f[8]) * projX * (4.0f / 3.0f) / depth;
            float dist = 0.5f * std::sqrt(sx * sx + sy * sy);
            float size = std::fabs(r.radius * projX / depth);
            float score = std::min(size, 15.0f) / std::max(dist, 0.2f);
            if (score > best) best = score, R = i;
        }
    } else {
        R = std::min(idx, int(regions_.size()) - 1);
    }
    for (int m : leaving_) returnToPool(m);
    leaving_.clear();
    if (R == region_) return;
    for (Block& b : blocks_) b.shown.splice(b.shown.begin(), b.removed);
    for (const auto& [bi, ii] : regions_[size_t(R)].insts) {
        Block& b = blocks_[size_t(bi)];
        for (std::list<int>* l : {&b.shown, &b.hidden})
            if (auto it = std::find(l->begin(), l->end(), ii); it != l->end()) {
                b.removed.splice(b.removed.begin(), *l, it);
                break;
            }
    }
    region_ = R;
    for (size_t i = 0; i < active_.size() && i < regions_[size_t(R)].insts.size(); ++i) place(active_[i], i);
    setSizes(full_, flat_);
}

// FUN_00172ec8: flat blocks show (int)((count - removed) * flat) instances
// (hiding / restoring at the front of the lists); (int)(region count * full) members.
void CrowdRenderer::setSizes(float full, float flat) {
    full_ = full, flat_ = flat;
    for (Block& b : blocks_) {
        int visible = int(float(int(b.mm->instances.size()) - int(b.removed.size())) * flat);
        int cur = int(b.shown.size());
        if (visible > cur) {
            auto e = b.hidden.begin();
            std::advance(e, std::min(visible - cur, int(b.hidden.size())));
            b.shown.splice(b.shown.begin(), b.hidden, b.hidden.begin(), e);
        } else if (visible < cur) {
            auto e = b.shown.begin();
            std::advance(e, cur - visible);
            b.hidden.splice(b.hidden.begin(), b.shown, b.shown.begin(), e);
        }
    }
    if (region_ < 0) return;
    int target = int(float(regions_[size_t(region_)].insts.size()) * full);
    while (int(active_.size()) > target) {
        // At Boot the cut members keep standing until they are off screen.
        if (excitement_ != VenueScript::Boot) returnToPool(active_.back());
        else leaving_.push_back(active_.back());
        active_.pop_back();
    }
    while (int(active_.size()) < target && (!pool_.empty() || !leaving_.empty())) {
        if (pool_.empty()) returnToPool(leaving_.front()), leaving_.erase(leaving_.begin());
        int m = pool_.front();
        pool_.pop_front();
        active_.push_back(m);
        place(m, active_.size() - 1);
    }
}

void CrowdRenderer::update(float beat, float ms, int excitement, bool newShot, int region, const Xfm& camera, float projX) {
    excitement_ = excitement;
    if (newShot) {
        switchRegion(region, camera, projX);
        // arena/crowd.dta update_crowd: the sizes, then animate_crowd's weights
        // (bad, ok, great, idle) per excitement (Boot, Bad, Okay, Great, Peak).
        static const float kSizes[] = {0.1f, 0.25f, 0.5f, 1.0f, 1.0f};
        static const std::array<float, 4> kWeights[] = {{1, 0, 0, 0}, {0.8f, 0.2f, 0, 0}, {0, 0.8f, 0.2f, 0}, {0, 0, 1, 0}, {0, 0, 1, 0}};
        int e = std::clamp(excitement, 0, 4);
        setSizes(kSizes[e], kSizes[e]);
        animate(kWeights[e]);
        static const bool log = SDL_getenv("GD01_LOG_SHOTS") != nullptr;
        if (log) SDL_Log("crowd: region %d, %d members, %d leaving", region_, int(active_.size()), int(leaving_.size()));
    }
    // FUN_001720e8: leaving members off screen go back to the pool; the
    // archetypes (impostors), the active and the leaving members are polled.
    for (size_t i = 0; i < leaving_.size();)
        if (!members_[size_t(leaving_[i])].visible) returnToPool(leaving_[i]), leaving_.erase(leaving_.begin() + long(i));
        else ++i;
    std::vector<int> polled;
    for (int k = 0; k < 6 && k < int(members_.size()); ++k) polled.push_back(k);
    for (const std::vector<int>* l : {&active_, &leaving_})
        for (int m : *l)
            if (m >= 6) polled.push_back(m);
    for (int k : polled) {
        Member& a = members_[size_t(k)];
        if (!a.character) continue;
        a.character->poll(beat, ms, frameScale_);
        if (int hand = a.character->hand(); hand >= 0 && hand != a.hand) {  // set_hand
            a.hand = hand;
            for (int h = 0; h < 3; ++h) {
                a.character->setShowing("hand" + std::to_string(h) + "_left.mesh", h == hand);
                a.character->setShowing("hand" + std::to_string(h) + "_right.mesh", h == hand);
            }
        }
    }
}

std::optional<std::array<float, 3>> CrowdRenderer::randomMemberTarget() {
    if (active_.empty()) return std::nullopt;
    int m = active_[std::uniform_int_distribution<size_t>(0, active_.size() - 1)(rng_)];
    if (!members_[size_t(m)].character) return std::nullopt;
    auto p = members_[size_t(m)].character->placement().pos;
    p[2] += 48;
    return p;
}

// FUN_00172350: the impostor camera has the main camera's world rotation,
// looking at the member from d = h/4 / tan(10 deg) back along its forward axis,
// aimed at (0, 0, h/2) above it; 20 deg horizontal FOV, near 10, far 1000
// (FUN_001b1df8), so the 128x256 target frames h/2 x h.
void CrowdRenderer::renderImpostors(const Xfm& camera) {
    constexpr float kHalfFov = 0.349f * 0.5f;
    const float d = height_ * 0.25f / std::tan(kHalfFov);
    const float fovY = 2 * std::atan(std::tan(kHalfFov) / 0.5f);
    camRot_ = camera.rot;
    const SceneObject* envObj = venue_.find("crowd.env");
    const EnvironObj* env = envObj && envObj->environment ? envObj->environment.get() : nullptr;
    for (int k = 0; k < 6 && k < int(members_.size()); ++k) {
        Member& a = members_[size_t(k)];
        if (!a.target || !a.character) continue;
        // FUN_00172350: R_imp = C * M^T * R_A (row vectors), the archetype seen
        // from the main camera's direction as if it faced the crowd frame M,
        // around its centre A.world * (0, 0, h/2).
        const Xfm& pa = a.character->placement();
        auto mul = [](const std::array<float, 9>& p, const std::array<float, 9>& q) {
            std::array<float, 9> r{};
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    for (int k = 0; k < 3; ++k) r[size_t(i * 3 + j)] += p[size_t(i * 3 + k)] * q[size_t(k * 3 + j)];
            return r;
        };
        std::array<float, 9> mt{};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) mt[size_t(i * 3 + j)] = crowdFrame_[size_t(j * 3 + i)];
        std::array<float, 9> rimp = mul(mul(camera.rot, mt), pa.rot);
        std::array<float, 3> eye = pa.pos;
        for (int i = 0; i < 3; ++i) eye[size_t(i)] += height_ * 0.5f * pa.rot[size_t(6 + i)] - d * rimp[size_t(3 + i)];
        Mat4 vp = Mat4::perspective(fovY, 0.5f, 10, 1000) * Mat4::cameraView(rimp, eye);
        renderer_.beginRenderTarget(a.target);
        a.character->draw(vp, env);
        renderer_.endRenderTarget();
    }
}

void CrowdRenderer::draw(const Mat4& viewProj) {
    // FUN_00172350: the members (lit by crowd.env; culled by their pelvis sphere,
    // radius 53), then the flat blocks.
    const SceneObject* envObj = venue_.find("crowd.env");
    const EnvironObj* env = envObj && envObj->environment ? envObj->environment.get() : nullptr;
    const auto& m = viewProj.m;
    std::vector<int> drawn(active_);
    drawn.insert(drawn.end(), leaving_.begin(), leaving_.end());
    for (int k : drawn) {
        Member& a = members_[size_t(k)];
        if (!a.character) continue;
        auto p = a.character->placement().pos;
        p[2] += 40;
        float cx = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12];
        float cy = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13];
        float cw = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
        a.visible = cw > -53 && std::fabs(cx) < cw + 106 && std::fabs(cy) < cw + 106;
        if (a.visible) a.character->draw(viewProj, env);
    }
    Renderer::RenderState st;
    st.blend = Blend::Alpha;
    st.alphaCutout = true;
    for (const Block& b : blocks_) {
        if (b.archetype >= int(members_.size()) || !members_[size_t(b.archetype)].target) continue;
        st.texture = members_[size_t(b.archetype)].target;
        const auto& inst = b.mm->instances;
        for (int i : b.shown) {
            // FastBillboardXYZ: the camera's rotation at the instance position.
            Mat4 mvp = viewProj * Mat4::fromRowXfm(camRot_, inst[size_t(i)].pos);
            renderer_.drawMesh(quad_, mvp.m.data(), st);
        }
    }
}
