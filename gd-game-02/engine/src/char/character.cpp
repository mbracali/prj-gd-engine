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
    moveSelf_ = facingDeltaPos_ >= 0;  // 0x821895C8 (the servo's enter)
    for (const auto& o : d.objects)
        if (o->as<scene::CharWalk>()) walk_ = std::make_unique<CharWalk>(*this);
    // The lip servo: its viseme clip set's channels in its own buffer
    // (0x82190DB0 -> 0x821A8368) and its eye entries.
    for (const auto& o : d.objects) {
        auto* f = o->as<scene::FaceFxLipSyncServo>();
        if (!f) continue;
        faceServo_ = f;
        faceSet_ = clips.get(path_, f->visemeSet);
        if (faceSet_) {
            faceSet_->listChannels(faceBones_, false);
            visemes_ = faceSet_->clip("visemes");
            neutral_ = faceSet_->clip("neutral");
        }
        for (const auto& e : f->entries) faceEntries_.emplace_back(skeleton_.find(e.object), e.axis);
        break;
    }
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
void Character::poseMeshes(Bones& bones, std::vector<int>& channelBone_, size_t& channelsMapped_) {
    auto& ch = bones.channels();
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

// CharServoBone Poll 0x82189408 after PoseMeshes: the root motion. Only
// with bone_facing_delta channels (+0xbc; 0x821884D8 looks the root channels
// up only then).
void Character::servoRoot(float dBeats) {
    auto& ch = bones_.channels();
    if (facingDeltaPos_ >= 0) {
        // 0x82188640: bone_facing applied to a transform (turned, then moved).
        auto facing = [&](scene::Xfm& x) {
            scene::Xfm f;
            if (facingRot_ >= 0) f.rot = axisRot(2, ch[size_t(facingRot_)].v[0]);
            if (facingPos_ >= 0) f.pos = {ch[size_t(facingPos_)].v[0], ch[size_t(facingPos_)].v[1], ch[size_t(facingPos_)].v[2]};
            x = scene::compose(x, f);
            normalizeRows(x);
        };
        // 0x821886E8: the delta in the transform's own frame, then its turn.
        auto delta = [&](scene::Xfm& x) {
            const float* p = ch[size_t(facingDeltaPos_)].v;
            for (int j = 0; j < 3; ++j) x.pos[size_t(j)] += x.rot[size_t(j)] * p[0] + x.rot[size_t(3 + j)] * p[1] + x.rot[size_t(6 + j)] * p[2];
            if (facingDeltaRot_ >= 0) {
                x.rot = mul3(x.rot, axisRot(2, ch[size_t(facingDeltaRot_)].v[0]));
                normalizeRows(x);
            }
        };
        if (!moveSelf_) {
            if (pelvis_ >= 0) {
                scene::Xfm pl = skeleton_.local(pelvis_);
                if (!moveChanged_) {
                    facing(pl);
                    skeleton_.setLocal(pelvis_, pl);
                } else {
                    // Switched off: the root takes what the pelvis leaves.
                    scene::Xfm a = skeleton_.root();
                    delta(a);
                    const scene::Xfm e = scene::compose(pl, a);
                    facing(pl);
                    skeleton_.setLocal(pelvis_, pl);
                    skeleton_.setRoot(scene::compose(inverse(pl), e));
                }
            }
        } else {
            if (!moveChanged_) {
                scene::Xfm r = skeleton_.root();
                delta(r);
                skeleton_.setRoot(r);
            } else if (pelvis_ >= 0) {
                // Switched on: the facing moves from the pelvis to the root.
                scene::Xfm a = skeleton_.local(pelvis_);
                facing(a);
                a = scene::compose(a, skeleton_.root());
                skeleton_.setRoot(scene::compose(inverse(skeleton_.local(pelvis_)), a));
            }
            regulateRoot(dBeats);
        }
        moveChanged_ = false;
    }
    // FUN_821885D0: the deltas cleared.
    if (facingDeltaPos_ >= 0) ch[size_t(facingDeltaPos_)].v[0] = ch[size_t(facingDeltaPos_)].v[1] = ch[size_t(facingDeltaPos_)].v[2] = 0;
    if (facingDeltaRot_ >= 0) ch[size_t(facingDeltaRot_)].v[0] = 0;
}

// 0x82188DC8.
void Character::regulateRoot(float dBeats) {
    if (!hasRegulate_ && !override_.hasSpot) return;
    ClipDriver* d = mainDriver();
    if (!d || d->chain().size() < 2) return;
    // The clip playing (the oldest node) and the one waiting to follow it.
    const auto& oldest = d->chain()[0];
    const auto& next = d->chain()[1];
    if (!(0 < next.delay)) return;
    float ahead = next.delay;
    float span = std::max(ahead * 0.6666667f, 2.0f);
    const Spot* spot = hasRegulate_ ? &regulate_ : nullptr;
    if (override_.hasSpot && oldest.clip == override_.clip) {
        ahead = override_.frame - oldest.frame;
        if (ahead <= 0) return;
        span = ahead * 0.6666667f;
        spot = &override_.spot;
    }
    if (!spot) return;
    scene::Xfm root = skeleton_.root();
    const float here[4] = {root.pos[0], root.pos[1], root.pos[2], 1};
    RootMotion m;
    m.start(oldest.clip, here, facingOf(root));
    m.integrate(oldest.frame, oldest.frame + ahead);
    const float k = dBeats / span;
    float off[3] = {override_.offset[0], override_.offset[1], override_.offset[2]};
    {
        const float c = std::cos(m.angle), sn = std::sin(m.angle);
        const float x = off[0] * c - off[1] * sn, y = off[0] * sn + off[1] * c;
        off[0] = x, off[1] = y;
    }
    const float p[3] = {m.pos[0] + off[0], m.pos[1] + off[1], m.pos[2] + off[2]};
    const auto corr = spotCorrection(*spot, p);
    for (int j = 0; j < 3; ++j) root.pos[size_t(j)] += corr[size_t(j)] * k;
    root.rot = mul3(root.rot, axisRot(2, spotTurn(*spot, m.angle) * k));
    skeleton_.setRoot(root);
}

void Character::setMoveSelf(bool on) {
    if (moveSelf_ == on) return;
    moveSelf_ = on;
    moveChanged_ = true;
}

void Character::setRegulate(const Spot* s) {
    hasRegulate_ = s != nullptr;
    if (s) regulate_ = *s;
}

void Character::teleport(const Spot& w) {
    teleport(w.xfm);
    setRegulate(&w);
}

std::array<float, 3> Character::boneOffsetAt(const scene::CharClipSamples& clip, float frame, const std::string& bone) {
    // 0x82194450 / 0x82192AD8 [I]: the bone's position from the root with
    // the clip alone posed at `frame` (the root motion is the servo's).
    const int b = skeleton_.find(bone);
    if (b < 0) return {0, 0, 0};
    std::vector<scene::Xfm> saved;
    for (int i = 0; i < skeleton_.size(); ++i) saved.push_back(skeleton_.local(i));
    const scene::Xfm root = skeleton_.root();
    skeleton_.setRoot(scene::Xfm{});
    Bones tmp;
    for (const auto& c : bones_.channels()) tmp.add(c.name);
    tmp.scaleAdd(clip, 1, frame, 0);
    std::vector<int> map;
    size_t mapped = 0;
    poseMeshes(tmp, map, mapped);
    const scene::Xfm w = skeleton_.world(b);
    for (int i = 0; i < skeleton_.size(); ++i) skeleton_.setLocal(i, saved[size_t(i)]);
    skeleton_.setRoot(root);
    return {w.pos[0], w.pos[1], w.pos[2]};
}

void Character::setRegulateOverride(const scene::CharClipSamples* clip, float frame, const Spot* spot, const std::string& bone) {
    override_.clip = clip;
    override_.frame = frame;
    override_.hasSpot = spot != nullptr;
    if (spot) override_.spot = *spot;
    override_.offset = clip && !bone.empty() ? boneOffsetAt(*clip, frame, bone) : std::array<float, 3>{0, 0, 0};
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
    ctx.pollServo = [this, dB] {
        poseMeshes(bones_, channelBone_, channelsMapped_);
        servoRoot(dB);
    };
    ctx.pollWalk = [this, dB](const std::string&) {
        if (walk_) walk_->poll(dB);
    };
    ctx.pollFace = [this, &ctx](const std::string&) { pollFace(ctx); };
    ctx.driverFlagWeight = [this](const std::string& drv, uint32_t flags) {
        ClipDriver* d = driver(drv);
        return d ? d->flagWeight(flags) : 0.0f;
    };
    rig_->poll(ctx);
}

// FaceFxLipSyncServo Poll 0x8218E7E0 / 0x821A7978.
void Character::pollFace(const RigContext& ctx) {
    if (!lip_ || !visible || !visemes_) return;
    std::vector<float> values;
    for (const auto& [b, axis] : faceEntries_) values.push_back(b >= 0 ? facefx::eyeAngle(skeleton_.local(b).rot.data(), axis) : 0.0f);
    facefx::FacePose pose;
    if (!lip_->poll(ctx.faceSongSeconds, ctx.faceUiSeconds, values.data(), values.size(), pose)) return;
    faceBones_.scaleDown(*visemes_, 0);
    for (const auto& f : pose.frames) faceBones_.scaleAdd(*visemes_, f.weight, float(f.frame), 0);
    // The rest of the weight to the identity: w += residual (-= when w < 0).
    for (auto& c : faceBones_.channels())
        if (c.type == scene::CharBonesSamples::kQuat) c.v[3] += c.v[3] >= 0 ? pose.residual : -pose.residual;
    if (neutral_) faceBones_.rotateBy(*neutral_, neutral_->startFrame);
    poseMeshes(faceBones_, faceChannelBone_, faceMapped_);
    lastFace_ = std::move(pose);
    ++facePoses_;
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
