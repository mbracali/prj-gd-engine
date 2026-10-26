// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "char/character.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "data/game_data.h"
#include "render/scene_gpu.h"
#include "scene/anim.h"

namespace chr {

namespace {

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

// 0x821A50E8 / 0x8217B1C0 / 0x8214C240: a rotation about one axis.
std::array<float, 9> axisRot(int axis, float a) {
    const float c = std::cos(a), s = std::sin(a);
    switch (axis) {
    case 0: return {1, 0, 0, 0, c, s, 0, -s, c};
    case 1: return {c, 0, -s, 0, 1, 0, s, 0, c};
    default: return {c, s, 0, -s, c, 0, 0, 0, 1};
    }
}

std::array<float, 9> mul3(const std::array<float, 9>& a, const std::array<float, 9>& b) {
    std::array<float, 9> r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            float s = 0;
            for (int k = 0; k < 3; ++k) s += a[size_t(i * 3 + k)] * b[size_t(k * 3 + j)];
            r[size_t(i * 3 + j)] = s;
        }
    return r;
}

// Copies a duplicate's loaded data over the character's object (kMerge).
template <class T> bool copyAs(scene::Object& dst, const scene::Object& src) {
    auto* d = dst.as<T>();
    const auto* s = src.as<T>();
    if (!d || !s) return false;
    scene::Directory* dir = d->dir;
    *d = *s;
    d->dir = dir;
    return true;
}

}  // namespace

// ---- ClipLibrary -------------------------------------------------------------

ClipSet* ClipLibrary::get(const std::string& fromArkPath, const std::string& rel) {
    if (rel.empty()) return nullptr;
    const std::string p = scene::findMiloRelative(milos_.data(), fromArkPath, rel);
    auto it = sets_.find(p);
    if (it != sets_.end()) return it->second.get();
    if (!milos_.data().exists(p)) {
        SDL_Log("char: no clip set %s (from %s)", p.c_str(), fromArkPath.c_str());
        sets_.emplace(p, nullptr);
        return nullptr;
    }
    try {
        auto set = std::make_unique<ClipSet>(milos_.load(p));
        ClipSet* s = set.get();
        sets_.emplace(p, std::move(set));
        return s;
    } catch (const std::exception& e) {
        SDL_Log("char: clip set %s: %s", p.c_str(), e.what());
        sets_.emplace(p, nullptr);
        return nullptr;
    }
}

// ---- Character -------------------------------------------------------------------

Character::Character(const GameData& data, const std::string& path, ClipLibrary& clips, const std::map<std::string, std::string>& outfits)
    : path_(scene::miloArkPath(path)) {
    milo_ = scene::loadMilo(data, path_);
    scene::Directory& d = milo_->root();
    name_ = d.name;
    if (!d.self) throw std::runtime_error(path_ + ": no directory record");
    own_ = scene::decodeCharacterDir(*d.self);
    // Outfits (OutfitLoader, merged as they load).
    for (const auto& [loader, file] : outfits)
        if (!file.empty() && d.get(loader)) loadOutfit(data, file);
    skeleton_.build(d);
    // LOD and shadow groups.
    for (const auto& l : own_->lods) lodGroups_.push_back(d.get(l.group) ? d.get(l.group)->as<scene::Group>() : nullptr);
    if (!own_->shadow.empty())
        if (auto* g = d.get(own_->shadow)) shadow_ = g->as<scene::Group>();
    // The rig.
    rig_ = std::make_unique<Rig>(d, skeleton_);
    // Drivers: their clip sets' channels into the servo.
    for (const auto& o : d.objects) {
        auto* drv = o->as<scene::CharDriver>();
        if (!drv) continue;
        ClipSet* set = clips.get(path_, drv->clips);
        auto cd = std::make_unique<ClipDriver>(drv->name, set);
        cd->realign = drv->realign;
        cd->skipScaleDown = drv->unknown55;
        const std::string wname = drv->name;
        Rig* rig = rig_.get();
        cd->weight = [rig, wname] { return rig->weightOf(wname); };
        cd->runScript = [this](const std::string& s) {
            if (runScript) runScript(s);
        };
        cd->sendStarved = [this](const std::string& m) {
            if (sendMessage) sendMessage(m);
        };
        if (set) set->listChannels(bones_, set->data().moveSelf);
        driverByName_[drv->name] = cd.get();
        drivers_.push_back(std::move(cd));
    }
    // The servo's root channels and the pelvis (0x821884D8); "move self"
    // when the clips carry bone_facing_delta (0x821895C8).
    facingDeltaPos_ = bones_.find("bone_facing_delta.pos");
    facingDeltaRot_ = bones_.find("bone_facing_delta.rotz");
    facingPos_ = bones_.find("bone_facing.pos");
    facingRot_ = bones_.find("bone_facing.rotz");
    pelvis_ = skeleton_.find("bone_pelvis.trans");
    if (pelvis_ < 0) pelvis_ = skeleton_.find("bone_pelvis.mesh");
    moveSelf_ = facingDeltaPos_ >= 0;
    // The first LOD shown.
    updateLod(1e30f);
}

Character::~Character() = default;

ClipDriver* Character::driver(const std::string& name) {
    auto it = driverByName_.find(name);
    return it == driverByName_.end() ? nullptr : it->second;
}

void Character::loadOutfit(const GameData& data, const std::string& outfitPath) {
    std::unique_ptr<scene::Milo> m;
    try {
        m = scene::loadMilo(data, outfitPath);
    } catch (const std::exception& e) {
        SDL_Log("char %s: outfit %s: %s", name_.c_str(), outfitPath.c_str(), e.what());
        return;
    }
    scene::Directory& d = milo_->root();
    scene::Directory& od = m->root();
    std::unique_ptr<scene::CharacterObj> oown;
    try {
        if (od.self) oown = scene::decodeCharacterDir(*od.self);
    } catch (const std::exception&) {
    }
    // 0x82158F30 per object of the outfit.
    std::vector<std::unique_ptr<scene::Object>> keep;
    for (auto& o : od.objects) {
        scene::Object* existing = d.get(o->name);
        if (!existing) {
            keep.push_back(std::move(o));  // kReplace: added
            continue;
        }
        if (startsWith(o->name, "bone_")) {
            // A group among the duplicates gets the outfit's members.
            if (auto* g = existing->as<scene::Group>())
                if (auto* og = o->as<scene::Group>())
                    for (const auto& n : og->objects)
                        if (std::find(g->objects.begin(), g->objects.end(), n) == g->objects.end()) g->objects.push_back(n);
            continue;  // kKeep: the character's bone
        }
        const scene::Trans* t = gfx::transOf(o.get());
        if (t && startsWith(t->parent, "bone_")) {
            // kMerge: the character's object takes the outfit's data.
            copyAs<scene::TransObj>(*existing, *o) || copyAs<scene::Mesh>(*existing, *o) || copyAs<scene::Group>(*existing, *o);
        }
    }
    for (auto& o : keep) {
        o->dir = &d;
        ++outfitObjects_;
        d.add(std::move(o));
    }
    // The outfit's LOD and shadow groups merged into the character's by index.
    if (oown) {
        for (size_t i = 0; i < oown->lods.size() && i < own_->lods.size(); ++i) {
            auto* cg = d.get(own_->lods[i].group);
            auto* og = d.get(oown->lods[i].group);
            if (!cg || !og || cg == og) continue;
            auto* g = cg->as<scene::Group>();
            auto* h = og->as<scene::Group>();
            if (!g || !h) continue;
            for (const auto& n : h->objects) g->objects.push_back(n);
            setShowing(og, false);
        }
        if (!oown->shadow.empty() && !own_->shadow.empty()) {
            auto* cg = d.get(own_->shadow);
            auto* og = d.get(oown->shadow);
            if (cg && og && cg != og)
                if (auto* g = cg->as<scene::Group>(); g && og->as<scene::Group>())
                    for (const auto& n : og->as<scene::Group>()->objects) g->objects.push_back(n);
        }
    }
    outfits_.push_back(std::move(m));
}

void Character::setXfm(const scene::Xfm& x) { skeleton_.setRoot(x); }

void Character::teleport(const scene::Xfm& w) {
    // 0x82151D08: the waypoint's world transform, scale removed.
    scene::Xfm x = w;
    normalizeRows(x);
    setXfm(x);
    rig_->resetHair();
}

bool Character::worldOf(const std::string& name, scene::Xfm& out) {
    const int i = skeleton_.find(name);
    if (i < 0) return false;
    out = skeleton_.world(i);
    return true;
}

bool Character::showing(const scene::Object* o) const {
    auto it = showing_.find(o);
    if (it != showing_.end()) return it->second;
    const scene::Draw* d = gfx::drawOf(o);
    return !d || d->showing;
}

void Character::setShowing(const std::string& name, bool on) {
    if (const scene::Object* o = dir().get(name)) setShowing(o, on);
}

void Character::updateLod(float screenSize) {
    const int n = int(own_->lods.size());
    if (n == 0) return;
    int i = std::clamp(minLod, 0, n - 1);
    for (; i < n - 1; ++i) {
        const float f = i < lod_ ? 1.09f : 0.91f;
        if (f * own_->lods[size_t(i)].screenSize <= screenSize) break;
    }
    if (i == lod_) return;
    // FUN_82151DE0: the old group hidden, the new shown.
    if (lod_ >= 0 && lod_ < int(lodGroups_.size()) && lodGroups_[size_t(lod_)]) setShowing(lodGroups_[size_t(lod_)], false);
    lod_ = i;
    for (int k = 0; k < int(lodGroups_.size()); ++k)
        if (lodGroups_[size_t(k)]) setShowing(lodGroups_[size_t(k)], k == lod_);
}

std::array<float, 4> Character::worldSphere() {
    const scene::Draw& dr = own_->rnd.draw;
    scene::Xfm base = skeleton_.root();
    if (!own_->sphereBase.empty())
        if (int i = skeleton_.find(own_->sphereBase); i >= 0) base = skeleton_.world(i);
    const auto c = xformPoint(base, {dr.sphere[0], dr.sphere[1], dr.sphere[2]});
    return {c[0], c[1], c[2], dr.sphere[3]};
}

// PoseMeshes 0x821A51E0: the channels into the bones' local transforms.
void Character::poseMeshes() {
    auto& ch = bones_.channels();
    if (channelsMapped_ != ch.size()) {
        channelBone_.assign(ch.size(), -1);
        for (size_t i = 0; i < ch.size(); ++i) {
            std::string base = scene::CharBonesSamples::boneOf(ch[i].name);
            if (startsWith(base, "bone_facing")) continue;
            int b = skeleton_.find(base + ".trans");  // 0x82192DD8
            if (b < 0) b = skeleton_.find(base + ".mesh");
            channelBone_[i] = b;
        }
        channelsMapped_ = ch.size();
    }
    std::vector<std::array<float, 3>> scales;
    for (size_t i = 0; i < ch.size(); ++i) {
        const int b = channelBone_[i];
        if (b < 0) continue;
        const auto& c = ch[i];
        scene::Xfm x = skeleton_.local(b);
        switch (c.type) {
        case scene::CharBonesSamples::kPos:
            x.pos = {c.v[0], c.v[1], c.v[2]};
            break;
        case scene::CharBonesSamples::kQuat: {
            scene::Quat q{c.v[0], c.v[1], c.v[2], c.v[3]};
            const float l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
            if (l > 1e-12f)
                for (float& v : q) v /= l;
            else
                q = {0, 0, 0, 1};
            scene::quatToRot(q, x.rot);
            break;
        }
        case scene::CharBonesSamples::kRotX:
        case scene::CharBonesSamples::kRotY:
        case scene::CharBonesSamples::kRotZ:
            x.rot = axisRot(c.type - scene::CharBonesSamples::kRotX, c.v[0]);
            break;
        case scene::CharBonesSamples::kDRotX:
        case scene::CharBonesSamples::kDRotY:
        case scene::CharBonesSamples::kDRotZ:
            // Delta rotations (none on the disc): the rotation turned further.
            x.rot = mul3(axisRot(c.type - scene::CharBonesSamples::kDRotX, c.v[0]), x.rot);
            break;
        default:
            continue;
        }
        skeleton_.setLocal(b, x);
    }
    // Scale last: each axis row to the channel's length.
    for (size_t i = 0; i < ch.size(); ++i) {
        const int b = channelBone_[i];
        if (b < 0 || ch[i].type != scene::CharBonesSamples::kScale) continue;
        scene::Xfm x = skeleton_.local(b);
        for (int r = 0; r < 3; ++r) {
            float* row = &x.rot[size_t(r * 3)];
            const float l = std::sqrt(row[0] * row[0] + row[1] * row[1] + row[2] * row[2]);
            if (l > 1e-12f)
                for (int k = 0; k < 3; ++k) row[k] *= ch[i].v[r] / l;
        }
        skeleton_.setLocal(b, x);
    }
}

// CharServoBone Poll 0x82189408 after PoseMeshes: the root motion.
void Character::servoRoot() {
    auto& ch = bones_.channels();
    if (moveSelf_) {
        // 0x821886E8: the character moves by the root delta, in its own frame.
        scene::Xfm d;
        const float* p = ch[size_t(facingDeltaPos_)].v;
        d.pos = {p[0], p[1], p[2]};
        if (facingDeltaRot_ >= 0) d.rot = axisRot(2, ch[size_t(facingDeltaRot_)].v[0]);
        scene::Xfm r = scene::compose(d, skeleton_.root());
        normalizeRows(r);
        skeleton_.setRoot(r);
    } else if (pelvis_ >= 0 && facingPos_ >= 0) {
        // 0x82188640: bone_facing under the pelvis.
        scene::Xfm f;
        const float* p = ch[size_t(facingPos_)].v;
        f.pos = {p[0], p[1], p[2]};
        if (facingRot_ >= 0) f.rot = axisRot(2, ch[size_t(facingRot_)].v[0]);
        scene::Xfm x = scene::compose(skeleton_.local(pelvis_), f);
        skeleton_.setLocal(pelvis_, x);
    }
    // FUN_821885D0: the deltas cleared.
    if (facingDeltaPos_ >= 0) ch[size_t(facingDeltaPos_)].v[0] = ch[size_t(facingDeltaPos_)].v[1] = ch[size_t(facingDeltaPos_)].v[2] = 0;
    if (facingDeltaRot_ >= 0) ch[size_t(facingDeltaRot_)].v[0] = 0;
}

void Character::poll(RigContext ctx) {
    const double beat = ctx.beat;
    const float dB = ctx.dBeats, dS = ctx.dSeconds;
    ctx.pollDriver = [this, beat, dB, dS](const std::string& name) {
        if (ClipDriver* d = driver(name)) d->poll(beat, dB, dS, &bones_);
    };
    ctx.pollDrivers = [this, beat, dB, dS] {
        for (auto& d : drivers_) d->poll(beat, dB, dS, &bones_);
    };
    ctx.pollServo = [this] {
        poseMeshes();
        servoRoot();
    };
    ctx.driverFlagWeight = [this](const std::string& drv, uint32_t flags) {
        ClipDriver* d = driver(drv);
        return d ? d->flagWeight(flags) : 0.0f;
    };
    rig_->poll(ctx);
}

std::vector<std::string> Character::unresolvedBones() const {
    std::vector<std::string> v;
    dir().forEach([&](scene::Object& o) {
        auto* m = o.as<scene::Mesh>();
        if (!m) return;
        for (const auto& b : m->bones)
            if (!m->dir || !m->dir->resolve(b.name)) v.push_back(m->name + ":" + b.name);
    });
    return v;
}

}  // namespace chr
