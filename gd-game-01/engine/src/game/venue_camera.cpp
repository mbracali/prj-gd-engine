// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/venue_camera.h"

#include <algorithm>
#include <cmath>

#include <SDL3/SDL.h>

#include "scene/anim.h"

namespace {

constexpr float kDegToRad = 0.017453292f;

float number(const DataNode& n) { return n.type == DataNode::Type::Float ? n.f : static_cast<float>(n.i); }
bool isNumber(const DataNode& n) { return n.type == DataNode::Type::Int || n.type == DataNode::Type::Float; }

// "arena::x" -> "x"; other namespaces (characters) stay qualified.
std::string localName(const std::string& s) { return s.rfind("arena::", 0) == 0 ? s.substr(7) : s; }

// The value of a cam_paths field: a string, or {if_else {exists <character
// object>} <object> <fallback>} as "object|fallback".
std::string pathField(const DataNode& v) {
    if (v.isText()) return localName(v.s);
    const auto& c = v.children;
    if (v.type == DataNode::Type::Command && c.size() == 4 && c[2].isText() && c[3].isText())
        return localName(c[2].s) + "|" + localName(c[3].s);
    return {};
}

std::array<float, 3> sub(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
std::array<float, 3> cross(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
float length(const std::array<float, 3>& a) { return std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); }
std::array<float, 3> normalized(std::array<float, 3> a) {
    float l = length(a);
    if (l > 1e-6f)
        for (float& v : a) v /= l;
    return a;
}

// config/gen/macros.dta camera flags.
constexpr unsigned kCamLeft = 1, kCamRight = 2, kCamCloseup = 4, kCamNear = 8, kCamFar = 16, kCamBehind = 32,
                   kCamSolo = 64, kCamNoSolo = 128, kCamSpecial = 256;

// arena/gen/camera.dta $camera.groups (group -> flags) and $camera.pools.
const std::map<std::string, unsigned> kGroupFlags = {
    {"flr_near_lft", kCamNoSolo | kCamNear | kCamLeft}, {"flr_near_rt", kCamNoSolo | kCamNear | kCamRight},
    {"flr_far_lft", kCamNoSolo | kCamFar | kCamLeft},   {"flr_far_rt", kCamNoSolo | kCamFar | kCamRight},
    {"band_POV", kCamNoSolo | kCamBehind},              {"balcony_lft", kCamNoSolo | kCamFar | kCamLeft},
    {"balcony_rt", kCamNoSolo | kCamFar | kCamRight},   {"SOLO_NEAR", kCamSolo | kCamCloseup},
    {"SOLO_FAR", kCamSolo | kCamNear},                  {"LOSE", kCamSpecial},
    {"WIN", kCamSpecial},                               {"INTRO", kCamSpecial},
};
struct PoolDef {
    float weight;
    std::vector<std::string> groups;
};
const PoolDef kPools[] = {
    {0.5f, {"flr_near_lft", "flr_near_rt"}}, {0.5f, {"flr_far_lft", "flr_far_rt"}}, {0.2f, {"band_POV"}},
    {0.3f, {"balcony_lft", "balcony_rt"}},   {1.0f, {"SOLO_NEAR", "SOLO_FAR"}},
};

}  // namespace

VenueCamera::VenueCamera(const DataNode& script, const DataNode& camPaths, const DataNode* durations) {
    // {set $camedit.<group> ({arena switch_cam <path> <shot> (key value...)...} ...)}
    for (const DataNode& n : script.children) {
        const auto& c = n.children;
        if (n.type != DataNode::Type::Command || c.size() < 3 || !c[0].isText() || c[0].s != "set" ||
            c[1].type != DataNode::Type::Var || !c[2].isList())
            continue;
        std::string var = c[1].s[0] == '$' ? c[1].s.substr(1) : c[1].s;
        if (var.rfind("camedit.", 0) != 0) continue;
        const std::string groupName = var.substr(8);
        std::vector<Shot>& group = groups_[groupName];
        for (const DataNode& cmd : c[2].children) {
            const auto& a = cmd.children;
            if (cmd.type != DataNode::Type::Command || a.size() < 4 || !a[1].isText() || a[1].s != "switch_cam" ||
                !a[2].isText())
                continue;
            Shot s;
            s.path = a[2].s;
            s.name = a[3].isText() ? a[3].s : s.path;
            for (size_t i = 4; i < a.size(); ++i) {
                const DataNode& kv = a[i];
                if (!kv.isList() || kv.children.size() < 2 || !kv.children[0].isText()) continue;
                const std::string& k = kv.children[0].s;
                auto f = [&](size_t j) { return j < kv.children.size() && isNumber(kv.children[j]) ? number(kv.children[j]) : 0.0f; };
                if (k == "start") s.start = f(1);
                else if (k == "end") s.end = f(1);
                else if (k == "duration") s.duration = f(1);
                else if (k == "loop") s.loop = f(1) != 0;
                else if (k == "real_time") s.realTime = f(1) != 0;
                else if (k == "offset_in") s.offsetIn = {f(1), f(2), f(3)};
                else if (k == "offset_out") s.offsetOut = {f(1), f(2), f(3)};
                else if (k == "singer_in") s.singerIn = {f(1), f(2)};
                else if (k == "singer_out") s.singerOut = {f(1), f(2)};
                else if (k == "near") s.nearPlane = f(1);
                else if (k == "far") s.farPlane = f(1);
                else if (k == "fov_in") s.fovIn = f(1) * kDegToRad;
                else if (k == "fov_out") s.fovOut = f(1) * kDegToRad;
                else if (k == "ease") s.ease = f(1);
                else if (k == "enable_dof") s.dof = f(1) != 0;
                else if (k == "walk_ok") s.walkOk = f(1) != 0;
                else if (k == "low_excitement_ok") s.lowExcitementOk = f(1) != 0;
                else if (k == "hide_crowd") s.hideCrowd = f(1) != 0;
                else if (k == "shaky") s.shaky = f(1) != 0;
                else if (k == "eyes") s.eyes = int(f(1));
                else if (k == "crowd_region") s.crowdRegion = int(f(1));
                else if (k == "bad_walk_spots")
                    for (size_t j = 1; j < kv.children.size(); ++j) {
                        const DataNode& v = kv.children[j];
                        if (isNumber(v)) s.badWalkSpots.push_back(int(number(v)));
                        for (const DataNode& e : v.children)
                            if (isNumber(e)) s.badWalkSpots.push_back(int(number(e)));
                    }
            }
            if (auto fl = kGroupFlags.find(groupName); fl != kGroupFlags.end()) s.flags = fl->second;
            group.push_back(std::move(s));
        }
        if (group.empty()) groups_.erase(groupName);
    }

    // cam_paths: (<path> (parent <obj>) (target <obj>))
    for (const DataNode& p : camPaths.children) {
        if (!p.isList() || p.children.empty() || !p.children[0].isText()) continue;
        PathInfo info;
        if (const DataNode* v = p.find("parent"); v && v->children.size() > 1) info.parent = pathField(v->children[1]);
        if (const DataNode* v = p.find("target"); v && v->children.size() > 1) info.target = pathField(v->children[1]);
        paths_[p.children[0].s] = info;
    }

    // (camera_durations (kExcitementBoot 3 6) ...), in excitement order.
    if (durations)
        for (size_t i = 1; i < durations->children.size(); ++i) {
            const DataNode& d = durations->children[i];
            if (d.isList() && d.children.size() >= 3) durations_.push_back({d.intAt(1), d.intAt(2)});
        }

    for (const PoolDef& def : kPools) {
        Pool p{def.weight, {}};
        for (const std::string& g : def.groups)
            if (auto it = groups_.find(g); it != groups_.end())
                for (Shot& shot : it->second) p.shots.push_back(&shot);
        if (!p.shots.empty()) pools_.push_back(std::move(p));
    }
}

// random_int lo hi: [lo, hi), the upper bound excluded (FUN_0024a1d0).
int VenueCamera::barsFor(int excitement) {
    if (durations_.empty()) return 4;
    const auto& d = durations_[std::clamp<size_t>(static_cast<size_t>(std::max(excitement, 0)), 0, durations_.size() - 1)];
    int lo = std::min(d[0], d[1]), hi = std::max(d[0], d[1]);
    return hi <= lo ? lo : std::uniform_int_distribution<int>(lo, hi - 1)(rng_);
}

void VenueCamera::start(AnimPlayer& anims, float ms, float ticks) {
    anims_ = &anims;
    ms_ = ms, ticks_ = ticks;
    started_ = true;
    pickerCurrent_ = nullptr;  // cam reset
    if (auto it = groups_.find(fixedGroup_); !fixedGroup_.empty() && it != groups_.end() && !it->second.empty()) {
        switchTo(it->second.front());
        ended_ = true;  // nothing else picks
        return;
    }
    // intro_start: the first INTRO shot for 6 bars.
    barsLeft_ = 6;
    if (auto it = groups_.find("INTRO"); it != groups_.end()) switchTo(it->second.front());
    else pick(kCamNoSolo);
}

// arena/gen/camera.dta downbeat: the bar countdown (it keeps counting in
// Star Power, but no cut is made then).
void VenueCamera::downbeat(int excitement) {
    if (ended_) return;
    if (barsLeft_ > 0) --barsLeft_;
    if (!state_.starMode && barsLeft_ <= 0) {
        barsLeft_ = barsFor(excitement);
        pick(solo_ ? kCamSolo : kCamNoSolo);
    }
}

// one_bar_to: remember whether a solo is coming, but pick a regular shot;
// solo shots come when that one times out. Ignored before the first beat.
void VenueCamera::upcomingSection(const std::string& section, int excitement) {
    if (ended_ || beatNumber_ <= 0) return;
    solo_ = section == "solo";
    barsLeft_ = barsFor(excitement);
    pick(kCamNoSolo);
}

// The beat handler: beat_check (FUN_00111aa8) re-picks at once when the
// picked shot became invalid, and an excitement change re-picks on the first
// beat with at most one beat left in the bar.
void VenueCamera::beat(int beatsLeft, int beatNumber, int excitement) {
    if (ended_) return;
    beatNumber_ = beatNumber;
    if (pickerCurrent_ && !compatibleNow(*pickerCurrent_)) pick(solo_ ? kCamSolo : kCamNoSolo);
    if (aboutToSwitch_ && beatsLeft <= 1) {
        aboutToSwitch_ = false;
        barsLeft_ = barsFor(excitement);
        pick(solo_ ? kCamSolo : kCamNoSolo);
    }
}

bool VenueCamera::compatibleNow(const Shot& s) const { return valid(s); }

// FUN_00110040: can this shot be used now?
bool VenueCamera::valid(const Shot& s) const {
    if (s.flags & kCamSpecial) return false;
    if (state_.walking && !s.walkOk) return false;
    if (std::find(s.badWalkSpots.begin(), s.badWalkSpots.end(), state_.walkSpot) != s.badWalkSpots.end()) return false;
    if (state_.specialClip && !(s.flags & kCamNear)) return false;  // "shot too far for star power"
    if (state_.excitement != 3 && !s.lowExcitementOk) return false;  // FUN_0010ff38
    return true;
}

// FUN_00110128: not the same shot, not the same side, and not the same
// distance (Closeup, Far, Behind; Far <-> Behind counts as the same).
// Only shots the picker chose count as current (none after cam reset; then
// the side test uses the venue's intro_camera_flags).
bool VenueCamera::compatible(const Shot& s) const {
    if (!valid(s)) return false;
    if (pickerCurrent_ == &s) return false;
    const unsigned side = pickerCurrent_ ? pickerCurrent_->flags : introFlags_;
    if ((side & 3) && (side & 3) == (s.flags & 3)) return false;  // FUN_0010ff60
    if (pickerCurrent_) {                                          // FUN_0010ffd0
        unsigned a = pickerCurrent_->flags & 0x3c, b = s.flags & 0x3c;
        if (a && ((a & b & 0x34) || (a | b) == 0x30)) return false;
    }
    return true;
}

// CameraSystem FUN_001117a0: candidates from every pool, fresh ones first
// (a pool whose valid shots are all used is refreshed), weighted by pool
// weight / candidate count; the chosen shot is marked used.
void VenueCamera::pick(unsigned wanted) {
    std::vector<std::pair<Shot*, float>> cands;
    float total = 0;
    for (Pool& p : pools_) {
        // A pool whose every shot is used starts a new round; otherwise only
        // its usable used shots are made fresh again when no fresh one fits.
        if (std::none_of(p.shots.begin(), p.shots.end(), [](const Shot* s) { return s->fresh; }))
            for (Shot* s : p.shots) s->fresh = true;
        std::vector<Shot*> fresh, used;
        for (Shot* s : p.shots)
            if ((s->flags & wanted) == wanted && compatible(*s)) (s->fresh ? fresh : used).push_back(s);
        if (fresh.empty() && !used.empty()) {
            for (Shot* s : used) s->fresh = true;
            fresh = used;
        }
        if (fresh.empty()) continue;
        for (Shot* s : fresh) cands.push_back({s, p.weight / float(fresh.size())});
        total += p.weight;
    }
    if (cands.empty()) return;  // "Couldn't find shot with flags": the shot stays
    float r = std::uniform_real_distribution<float>(0, total)(rng_);
    Shot* chosen = cands.back().first;
    for (auto& [s, w] : cands) {
        if (r < w) {
            chosen = s;
            break;
        }
        r -= w;
    }
    chosen->fresh = false;
    pickerCurrent_ = chosen;
    switchTo(*chosen);
}

void VenueCamera::activePlayers(int mask) {
    const char* group = mask == 1 ? "MULTIPLAYER_0" : mask == 2 ? "MULTIPLAYER_1" : "MULTIPLAYER";
    if (auto it = groups_.find(group); it != groups_.end() && !it->second.empty()) switchTo(it->second.front());
}

void VenueCamera::gameOver(bool won, bool finalWin, bool grim) {
    ended_ = true;
    barsLeft_ = 100;
    aboutToSwitch_ = false;
    auto play = [&](const char* group) {
        auto it = groups_.find(group);
        if (it == groups_.end() || it->second.empty()) return false;
        switchTo(it->second[std::uniform_int_distribution<size_t>(0, it->second.size() - 1)(rng_)]);
        return true;
    };
    if (finalWin && won && play(grim ? "FINAL_WIN_GRIM" : "FINAL_WIN")) return;
    if (won) winAtMs_ = ms_ + 1750;  // WIN_CAMERA_DELAY
    else play("LOSE");
}

void VenueCamera::switchTo(const Shot& picked) {
    // Development aid: GD01_SHOT=<shot name> holds that shot at every switch.
    const Shot* forced = nullptr;
    if (const char* want = SDL_getenv("GD01_SHOT"))
        for (const auto& [g, shots] : groups_)
            for (const Shot& x : shots)
                if (x.name == want) forced = &x;
    const Shot& shot = forced ? *forced : picked;
    if (preSwitch) preSwitch(shot.name);
    dofOverride_ = -1;
    shakeOn_ = shot.shaky;
    shakeStartMs_ = ms_;
    currentShot_ = &shot;
    current_ = shot;
    focusSet_ = false;  // the focus jumps to the head on a cut
    if (SDL_getenv("GD01_LOG_SHOTS")) SDL_Log("camera: %.1f s %s%s", ms_ / 1000.0f, shot.name.c_str(), shot.dof ? " (dof)" : "");
    // start / end are percentages of the path's length (FUN_0016f3b8).
    float len = anims_ ? anims_->endFrame(shot.path + ".tnm") : 0;
    f0_ = shot.start * (len / 100.0f);
    f1_ = shot.end * (len / 100.0f);
    t0_ = shot.realTime ? ms_ : ticks_;
    t1_ = t0_ + shot.duration;
}

// The shot's frame task: linear, or ATanInterpolator with `ease`.
float VenueCamera::shotFrame(float t) const {
    if (current_.loop && t1_ > t0_) {
        float span = t1_ - t0_;
        t = t0_ + std::fmod(std::fmod(t - t0_, span) + span, span);
    }
    if (t <= t0_) return f0_;
    if (t >= t1_) return f1_;
    float u = (t - t0_) / (t1_ - t0_);
    if (current_.ease == 0) return f0_ + (f1_ - f0_) * u;
    float e = current_.ease, x = -e + 2 * e * u;
    return (f0_ + f1_) * 0.5f + (f1_ - f0_) / (2 * std::atan(e)) * std::atan(x);
}

bool VenueCamera::worldOf(const std::string& field, Xfm& out) {
    if (field.empty() || !anims_) return false;
    std::string name = field;
    if (size_t bar = field.find('|'); bar != std::string::npos) {  // if_else {exists a} a b
        std::string a = field.substr(0, bar);
        if (a.find("::") != std::string::npos && characters_ && characters_(a)) name = a;
        else name = field.substr(bar + 1);
    }
    if (name.find("::") != std::string::npos) {
        const SceneObject* c = characters_ ? characters_(name) : nullptr;
        if (!c || !c->transformable()) return false;
        out = c->transformable()->world;
        return true;
    }
    SceneObject* o = anims_->find(name);
    if (!o || !o->transformable()) return false;
    out = o->transformable()->world;
    return true;
}

// The band focus (FUN_0016e080 / FUN_0016e390): the guitarist's bone_head,
// followed softly. Each frame t = cam_filter (0.1) * min(screen error, 1),
// where the error is between the head's screen position and the shot's
// singer position. With no guitarist: the second walk spot, 60 units up.
std::array<float, 3> VenueCamera::focus() {
    const SceneObject* head = characters_ ? characters_("guitarist0::bone_head.mesh") : nullptr;
    if (!head || !head->transformable()) {
        Xfm spot;
        if (worldOf("walk_spot_02.mesh", spot)) return {spot.pos[0], spot.pos[1], spot.pos[2] + 60};
        return {0, 0, 60};
    }
    const std::array<float, 3> h = head->transformable()->world.pos;
    float dt = std::clamp(ms_ - lastMs_, 0.0f, 100.0f);
    lastMs_ = ms_;
    if (!focusSet_) {
        focus_ = h, focusSet_ = true;
        return focus_;
    }
    const Mat4& m = lastVp_;
    float x = m.at(0, 0) * h[0] + m.at(0, 1) * h[1] + m.at(0, 2) * h[2] + m.at(0, 3);
    float y = m.at(1, 0) * h[0] + m.at(1, 1) * h[1] + m.at(1, 2) * h[2] + m.at(1, 3);
    float w = m.at(3, 0) * h[0] + m.at(3, 1) * h[1] + m.at(3, 2) * h[2] + m.at(3, 3);
    float err = 1;
    if (w > 1e-4f) {
        float sx = current_.singerOut[0], sy = current_.singerOut[1];
        float ex = (x / w + 1) * 0.5f - (sx + 1) * 0.5f, ey = (1 - y / w) * 0.5f - (1 - sy) * 0.5f;
        err = std::sqrt(ex * ex + ey * ey);
    }
    // 10 % per 60 Hz frame at full error, scaled to this frame's length.
    float t = 0.1f * std::min(err, 1.0f);
    t = 1.0f - std::pow(1.0f - t, dt * 0.06f);
    // The shake also adds into the focus every frame (60 Hz frames on the PS2),
    // which drifts it: the handheld sway.
    for (int k = 0; k < 3; ++k)
        focus_[size_t(k)] += (h[size_t(k)] - focus_[size_t(k)]) * t + shake_[size_t(k)] * dt * 0.06f;
    return focus_;
}

Mat4 VenueCamera::viewProj(float ms, float ticks, float aspect) {
    ms_ = ms, ticks_ = ticks;
    if (winAtMs_ >= 0 && ms >= winAtMs_) {
        winAtMs_ = -1;
        if (auto it = groups_.find("WIN"); it != groups_.end() && !it->second.empty())
            switchTo(it->second[std::uniform_int_distribution<size_t>(0, it->second.size() - 1)(rng_)]);
    }
    // Shaky cam (FUN_0016e390): shaky_cam1.tnm at (ms since the switch) mod 3200.
    shake_ = {0, 0, 0};
    if (shakeOn_ && shakyEnabled_ && anims_) {
        Xfm sx;
        if (anims_->transAt("shaky_cam1.tnm", std::fmod(std::max(0.0f, ms - shakeStartMs_), 3200.0f), sx)) shake_ = sx.pos;
    }
    const Shot& s = current_;
    float f = shotFrame(s.realTime ? ms : ticks);
    float lo = std::min(f0_, f1_), hi = std::max(f0_, f1_);
    float u = hi == lo ? 1.0f : std::clamp((f - lo) / (hi - lo), 0.0f, 1.0f);

    // Path transform plus the offset, then into the parent's space.
    Xfm cam;
    if (anims_) anims_->transAt(s.path + ".tnm", f, cam);
    // P = S * P: the shake moves the camera in the path's own axes.
    for (int i = 0; i < 3; ++i)
        cam.pos[size_t(i)] += shake_[0] * cam.rot[size_t(i)] + shake_[1] * cam.rot[size_t(3 + i)] + shake_[2] * cam.rot[size_t(6 + i)];
    for (int i = 0; i < 3; ++i) cam.pos[i] += s.offsetIn[i] + (s.offsetOut[i] - s.offsetIn[i]) * u;
    std::array<float, 3> focusPos = focus();
    PathInfo info;
    if (auto it = paths_.find(s.path); it != paths_.end()) info = it->second;
    Xfm parent;
    if (!worldOf(info.parent, parent)) parent.pos = focusPos;  // the band-focus object
    cam = compose(cam, parent);

    // Aim at the target, keeping the path's up axis.
    Xfm target;
    std::array<float, 3> aim = worldOf(info.target, target) ? target.pos : focusPos;
    std::array<float, 3> up{cam.rot[6], cam.rot[7], cam.rot[8]};
    std::array<float, 3> fwd = normalized(sub(aim, cam.pos));
    std::array<float, 3> right = cross(fwd, up);
    if (length(right) < 1e-4f) right = cross(fwd, {0, 0, 1});
    right = normalized(right);
    up = cross(right, fwd);

    // Shift so the target sits at the shot's "singer" screen position (x
    // right, y up). The game divides by its projection's y scale, which is
    // negative (PS2 screen y points down), hence the opposite signs.
    float fov = s.fovIn + (s.fovOut - s.fovIn) * u;
    float sx = 1.0f / std::tan(fov * 0.5f), sy = sx * (4.0f / 3.0f);
    projX_ = sx;
    float dist = length(sub(aim, cam.pos));
    float singerX = s.singerIn[0] + (s.singerOut[0] - s.singerIn[0]) * u;
    float singerY = s.singerIn[1] + (s.singerOut[1] - s.singerIn[1]) * u;
    float dx = -singerX * dist / sx, dz = -singerY * dist / sy;
    for (int i = 0; i < 3; ++i) cam.pos[i] += right[i] * dx + up[i] * dz;

    std::array<float, 9> rot{right[0], right[1], right[2], fwd[0], fwd[1], fwd[2], up[0], up[1], up[2]};
    xfm_.rot = rot;
    xfm_.pos = cam.pos;
    float fovY = 2.0f * std::atan(std::tan(fov * 0.5f) / (4.0f / 3.0f));
    float n = std::max(s.nearPlane, 0.1f), fr = std::max(s.farPlane, s.nearPlane + 1);
    // FUN_0016e370 / PsCam_v6: focus = 2 |subject - camera|, as a depth-buffer value.
    bool dof = dofOverride_ >= 0 ? dofOverride_ != 0 : s.dof;
    float d = focusScale_ * dist;
    dofDepth_ = dof && d > n && d < fr ? 0.5f * ((fr + n) / (fr - n) - 2.0f * fr * n / ((fr - n) * d)) + 0.5f : 0.0f;
    lastVp_ = Mat4::perspective(fovY, aspect, n, fr) * Mat4::cameraView(rot, cam.pos);
    return lastVp_;
}
