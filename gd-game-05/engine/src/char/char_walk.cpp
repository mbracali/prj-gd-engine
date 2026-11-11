// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "char/char_walk.h"

#include <algorithm>
#include <cmath>

#include "char/char_driver.h"
#include "char/character.h"

namespace chr {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kNever = 1e30f;
constexpr size_t kMaxMarks = 256;  // the array at +0x64..+0xc64

// 0x82161CD0: (x, y) turned by a about z.
void turn(float a, float& x, float& y) {
    const float c = std::cos(a), s = std::sin(a);
    const float nx = x * c - y * s, ny = x * s + y * c;
    x = nx, y = ny;
}

float len3(const float* v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

// 0x8226A330: the rotation's axes turned by a about z.
void rotateZ(scene::Xfm& x, float a) {
    for (int r = 0; r < 3; ++r) turn(a, x.rot[size_t(r * 3)], x.rot[size_t(r * 3 + 1)]);
}

// The channel's sample at a frame (CharClip vtable +0x28): the set's floor
// and ceil samples at t = (frame - start) / (end - start), clamped.
void sampleAt(const scene::CharClipSamples& clip, const scene::CharBonesSamples& s, int bone, float frame, float* out, int n) {
    for (int k = 0; k < n; ++k) out[k] = 0;
    if (s.count == 0 || bone < 0) return;
    float t = 0;
    if (&s == &clip.full) {
        float len = clip.endFrame - clip.startFrame;
        if (len == 0) len = 1;
        t = std::clamp((frame - clip.startFrame) / len, 0.0f, 1.0f);
    }
    const float x = float(s.count - 1) * t;
    uint32_t i = uint32_t(x);
    float f = x - float(i);
    if (i >= s.count - 1) i = s.count - 1, f = 0;
    const float* a = s.sample(i) + s.offset[size_t(bone)];
    const float* b = f > 0 ? s.sample(i + 1) + s.offset[size_t(bone)] : a;
    for (int k = 0; k < n; ++k) out[k] = a[k] + (b[k] - a[k]) * f;
}

}  // namespace

float facingOf(const scene::Xfm& x) { return -std::atan2(x.rot[3], x.rot[4]); }

float wrapPi(float a) {
    float r = std::fmod(a + kPi, 2 * kPi);
    return r >= 0 ? r - kPi : r + kPi;
}

std::array<float, 3> spotCorrection(const Spot& s, const float p[3]) {
    const auto& w = s.xfm;
    if (s.yRadius <= 0) {
        const float dx = w.pos[0] - p[0], dy = w.pos[1] - p[1];
        const float d2 = dx * dx + dy * dy;
        if (d2 <= s.radius * s.radius) return {0, 0, 0};
        const float k = 1 - s.radius / std::sqrt(d2);
        return {dx * k, dy * k, 0};
    }
    const float l[3] = {p[0] - w.pos[0], p[1] - w.pos[1], p[2] - w.pos[2]};
    const float u = w.rot[0] * l[0] + w.rot[1] * l[1] + w.rot[2] * l[2];
    const float v = w.rot[3] * l[0] + w.rot[4] * l[1] + w.rot[5] * l[2];
    const float du = std::clamp(u, -s.radius, s.radius) - u, dv = std::clamp(v, -s.yRadius, s.yRadius) - v;
    return {w.rot[0] * du + w.rot[3] * dv, w.rot[1] * du + w.rot[4] * dv, w.rot[2] * du + w.rot[5] * dv};
}

float spotTurn(const Spot& s, float a) {
    const float e = wrapPi(facingOf(s.xfm) - a);
    return e - std::clamp(e, -s.angle, s.angle);
}

// ---- RootMotion ----------------------------------------------------------------

void RootMotion::setClip(const scene::CharClipSamples* clip) {
    if (clip == clip_) return;
    clip_ = clip;
    pos_ = rot_ = Channel{};
    if (!clip) return;
    auto find = [&](const char* name, Channel& ch) {
        for (const scene::CharBonesSamples* s : {&clip->full, &clip->one})
            for (size_t b = 0; b < s->bones.size(); ++b)
                if (s->bones[b].name == name) {
                    ch.set = s, ch.bone = int(b);
                    return;
                }
    };
    find("bone_facing.rotz", rot_);
    find("bone_facing.pos", pos_);
}

void RootMotion::start(const scene::CharClipSamples* clip, const float p[4], float a) {
    clip_ = nullptr;
    setClip(clip);
    for (int k = 0; k < 4; ++k) pos[k] = p[k];
    angle = a;
}

void RootMotion::samplePos(float frame, float out[4]) const {
    out[3] = 1;
    if (clip_ && pos_.set) sampleAt(*clip_, *pos_.set, pos_.bone, frame, out, 3);
    else out[0] = out[1] = out[2] = 0;
}

float RootMotion::sampleRot(float frame) const {
    float r = 0;
    if (clip_ && rot_.set) sampleAt(*clip_, *rot_.set, rot_.bone, frame, &r, 1);
    return r;
}

void RootMotion::raw(float f0, float f1) {
    float a[4];
    samplePos(f0, a);
    samplePos(f1, last_);
    for (int k = 0; k < 3; ++k) pos[k] = last_[k] - a[k];
}

void RootMotion::rawStep(float f) {
    float a[4] = {last_[0], last_[1], last_[2], last_[3]};
    samplePos(f, last_);
    for (int k = 0; k < 3; ++k) pos[k] = last_[k] - a[k];
    pos[3] = a[3];
}

void RootMotion::integrate(float f0, float f1) {
    float a[4];
    samplePos(f0, a);
    samplePos(f1, last_);
    const float r0 = sampleRot(f0);
    lastRot_ = sampleRot(f1);
    float d[3] = {last_[0] - a[0], last_[1] - a[1], last_[2] - a[2]};
    turn(wrapPi(angle - r0), d[0], d[1]);
    for (int k = 0; k < 3; ++k) pos[k] += d[k];
    angle = wrapPi(wrapPi(lastRot_ - r0) + angle);
}

void RootMotion::step(float f) {
    const float a[3] = {last_[0], last_[1], last_[2]};
    const float r0 = lastRot_;
    samplePos(f, last_);
    lastRot_ = sampleRot(f);
    float d[3] = {last_[0] - a[0], last_[1] - a[1], last_[2] - a[2]};
    turn(wrapPi(angle - r0), d[0], d[1]);
    for (int k = 0; k < 3; ++k) pos[k] += d[k];
    angle = wrapPi(wrapPi(lastRot_ - r0) + angle);
}

// ---- CharWalk ------------------------------------------------------------------

ClipDriver* CharWalk::driver() const { return c_.mainDriver(); }

void CharWalk::setSegment(int i) {
    segment_ = i;
    const P4& a = points_[size_t(i - 1)];
    const P4& b = points_[size_t(i)];
    float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float l = len3(d);
    for (int k = 0; k < 3; ++k) dir_[k] = l > 0 ? d[k] / l : 0;
}

bool CharWalk::walk(const std::vector<Spot>& path, uint32_t mode, uint32_t turnFlags, uint32_t turnFilter, uint32_t stopFilter) {
    plan_.clear();
    points_.clear();
    if (path.empty()) return false;
    const scene::Xfm& me = c_.xfm();
    points_.push_back({me.pos[0], me.pos[1], me.pos[2], 1});
    for (const Spot& s : path) points_.push_back({s.xfm.pos[0], s.xfm.pos[1], s.xfm.pos[2], 1});
    target_ = path.back();
    targetAngle_ = facingOf(target_.xfm);
    tolerance_ = std::max(target_.radius, target_.yRadius);
    if (!plan(mode, turnFlags, turnFilter, stopFilter)) {
        reset();
        return false;
    }
    next_ = 0;
    cursor_ = 0;
    speed_ = 0;
    setSegment(1);
    c_.setRegulate(nullptr);
    c_.setMoveSelf(true);
    state_ = 1;
    return true;
}

void CharWalk::reset() {
    // 0x8218BDD0. GH2 then drops a queued, unstarted clip of the plan, but
    // it searches the plan after clearing it: never [C].
    plan_.clear();
    points_.clear();
    state_ = 0;
}

bool CharWalk::planStart(uint32_t mode, uint32_t turnFlags, uint32_t turnFilter) {
    ClipDriver* d = driver();
    ClipSet* set = d ? d->clipSet() : nullptr;
    scene::CharClipGroup* walkGroup = set ? set->group("walk_walk") : nullptr;
    scene::CharClipSamples* walkClip = walkGroup ? set->nextMatching(*walkGroup, turnFlags) : nullptr;
    if (!walkClip || points_.size() < 2) return false;
    halfBlend_ = walkClip->blendWidth * 0.5f;
    // The context (0x82189D50).
    const int node = d->newestStarted();
    const ClipDriver::Node* cur = node >= 0 ? &d->chain()[size_t(node)] : nullptr;
    const P4 target = points_[1];
    const scene::Xfm& me = c_.xfm();
    const float charPos[4] = {me.pos[0], me.pos[1], me.pos[2], 1};
    const float facing = facingOf(me);
    float best = kNever;
    Entry turnEntry, walkEntry;
    P4 after{};
    // 0x8218A238: a turn clip from src (current clip) / dest (turn clip).
    auto evaluate = [&](float src, float dest, scene::CharClipSamples* turnClip) {
        float tsrc, tdest;
        if (!lastEdge(*turnClip, dest, *walkClip, tsrc, tdest)) return;
        RootMotion m;
        m.start(nullptr, charPos, facing);
        if (cur) {
            m.setClip(cur->clip);
            m.integrate(cur->frame, halfBlend_ + src);
        }
        const float before[3] = {m.pos[0], m.pos[1], m.pos[2]};
        m.setClip(turnClip);
        m.integrate((cur ? halfBlend_ : 0.0f) + dest, halfBlend_ + tsrc);
        const float ax = target[0] - m.pos[0], ay = target[1] - m.pos[1], az = target[2] - m.pos[2];
        const float bx = before[0] - target[0], by = before[1] - target[1], bz = before[2] - target[2];
        if (ax * ax + az * az + ay * ay > bx * bx + by * by + bz * bz) return;  // the turn takes him away
        // The walk clip's heading over its first beat from the turn.
        RootMotion w;
        const float zero[4] = {0, 0, 0, 1};
        w.start(walkClip, zero, 0);
        w.integrate(halfBlend_ + tdest, halfBlend_ + tdest + 1.0f);
        const float heading = std::atan2(w.pos[0], w.pos[1]) - w.angle;
        const float toTarget = std::atan2(ax, ay);
        const float err = std::fabs(wrapPi(m.angle - (heading - toTarget)));
        if (err < best) {
            best = err;
            walkEntry = {walkClip, tdest, tsrc};
            turnEntry = {turnClip, dest, src};
            after = {m.pos[0], m.pos[1], m.pos[2], m.pos[3]};
        }
    };
    if (!cur) {
        for (scene::CharClipSamples* c : set->clips())
            if ((c->flags & turnFilter) == turnFilter) evaluate(0, c->startFrame, c);
    } else {
        for (scene::CharClipSamples* c : set->groupClips("walk_turn")) {
            if ((c->flags & turnFilter) != turnFilter) continue;
            float src, dest;
            if (transition(*cur->clip, cur->frame, *c, mode, src, dest)) evaluate(src, dest, c);
        }
    }
    if (!turnEntry.clip) return false;
    plan_.push_back(turnEntry);
    plan_.push_back(walkEntry);
    points_[0] = after;
    return true;
}

bool CharWalk::plan(uint32_t mode, uint32_t turnFlags, uint32_t turnFilter, uint32_t stopFilter) {
    if (!planStart(mode, turnFlags, turnFilter)) return false;
    // The walk clip's loop (kPlayLast into itself).
    scene::CharClipSamples* walkClip = plan_[1].clip;
    float loopSrc, loopDest;
    if (!transition(*walkClip, plan_[1].dest, *walkClip, 4, loopSrc, loopDest)) return false;
    float pathLen = 0;
    for (size_t i = 1; i < points_.size(); ++i) {
        const float d[3] = {points_[i - 1][0] - points_[i][0], points_[i - 1][1] - points_[i][1], points_[i - 1][2] - points_[i][2]};
        pathLen += len3(d);
    }
    // One mark per beat of the loop: its plan entry, frame and the root
    // distance walked so far.
    RootMotion m;
    m.setClip(walkClip);
    const float f = plan_[1].dest;
    float frame = halfBlend_ + std::ceil(f);
    m.raw(halfBlend_ + f, frame);
    float dist = len3(m.pos);
    marks_.assign(1, Mark{1, frame, dist});
    const float limit = halfBlend_ + loopSrc;
    int entry = 1;
    while (dist <= pathLen && marks_.size() < kMaxMarks) {
        frame += 1;
        if (frame < limit) {
            m.rawStep(frame);
        } else {
            m.rawStep(limit);
            ++entry;
            dist += len3(m.pos);
            if (entry == int(plan_.size())) plan_.push_back({walkClip, loopDest, loopSrc});
            const float start = plan_[size_t(entry)].dest + halfBlend_;
            frame = (frame - limit) + start;
            m.raw(start, frame);
        }
        dist += len3(m.pos);
        marks_.push_back({entry, frame, dist});
    }
    cursor_ = int(marks_.size());
    return planStop(pathLen, stopFilter);
}

bool CharWalk::planStop(float pathLen, uint32_t stopFilter) {
    ClipDriver* d = driver();
    ClipSet* set = d ? d->clipSet() : nullptr;
    if (!set || points_.size() < 2 || marks_.empty()) return false;
    const int last = int(marks_.size()) - 1;  // +0x5c - 1
    const size_t n = points_.size();
    const float seg[3] = {points_[n - 1][0] - points_[n - 2][0], points_[n - 1][1] - points_[n - 2][1], points_[n - 1][2] - points_[n - 2][2]};
    const float before = pathLen - len3(seg);       // the path up to the last segment
    const float segLen2 = (pathLen - before) * (pathLen - before);
    float best = kNever, error = 0;
    scene::CharClipSamples* stop = nullptr;
    float stopFrame = 0;
    P4 stopPos{};
    for (scene::CharClipSamples* c : set->groupClips("walk_stop")) {
        if ((c->flags & stopFilter) != stopFilter) continue;
        // 0x82189E28: back from the clip's end at the target, the frame at
        // least 6 units of root motion (or end - 2.2 - halfBlend) before it.
        RootMotion m;
        m.start(c, points_[n - 1].data(), targetAngle_);
        float s = std::floor(c->endFrame - 2.2f - halfBlend_) + halfBlend_;
        m.integrate(c->endFrame, s);
        // [C] the 6-unit test is on the integrated position itself (world
        // space, starting at the target), as 0x82189E28 does.
        while (m.pos[0] * m.pos[0] + m.pos[1] * m.pos[1] + m.pos[2] * m.pos[2] < 36.0f && s > c->startFrame - 64) {
            s -= 1;
            m.step(s);
        }
        const float sRel = s - halfBlend_;
        const float d[3] = {m.pos[0] - points_[n - 2][0], m.pos[1] - points_[n - 2][1], m.pos[2] - points_[n - 2][2]};
        const float d2 = d[0] * d[0] + d[2] * d[2] + d[1] * d[1];
        if (d2 >= segLen2) continue;
        // The stop starts on the same foot as a mark of the loop.
        int i = last;
        const int a = int(sRel > 0 ? sRel + 0.5f : sRel - 0.5f);
        const float mf = marks_[size_t(last)].frame - halfBlend_;
        const int b = int(mf > 0 ? mf + 0.5f : mf - 0.5f);
        if ((a ^ b) & 1) --i;
        if (i < 0) continue;
        const float need = std::sqrt(d2) + before;
        while (i > 1 && marks_[size_t(i)].dist >= need) i -= 2;
        // The heading walked into the mark against the stop's start.
        RootMotion w;
        const float zero[4] = {0, 0, 0, 1};
        w.start(plan_.back().clip, zero, 0);
        w.integrate(marks_[size_t(i)].frame - 1.0f, marks_[size_t(i)].frame);
        const float walkHeading = -std::atan2(w.pos[0], w.pos[1]);
        const float toStop = -std::atan2(d[0], d[1]);
        const float err = std::fabs(wrapPi((w.angle - walkHeading) + toStop - m.angle));
        float here = marks_[size_t(i)].dist - need;
        const float there = (i + 2 <= last ? marks_[size_t(i + 2)].dist : kNever) - need;
        auto excess = [&](float x) { return x > tolerance_ ? x - tolerance_ : (x < -tolerance_ ? -tolerance_ - x : 0.0f); };
        float ex = excess(here);
        if (excess(there) < ex) ex = excess(there), here = there, i += 2;
        const float score = std::max(0.0f, ex - 3.0f) * 5.0f + err * 57.2957763671875f;
        if (score < best) {
            best = score;
            stop = c;
            stopFrame = sRel;
            error = here;
            chosen_ = i;
            stopPos = {m.pos[0], m.pos[1], m.pos[2], m.pos[3]};
        }
    }
    if (!stop) return false;
    saved_ = points_[n - 1];
    points_[n - 1] = stopPos;
    error = std::clamp(error, -tolerance_, tolerance_);
    setSegment(int(n) - 1);
    for (int k = 0; k < 3; ++k) points_[n - 1][size_t(k)] += dir_[k] * error, saved_[size_t(k)] += dir_[k] * error;
    const Entry e{stop, stopFrame, marks_[size_t(chosen_)].frame - halfBlend_};
    plan_.resize(std::min(plan_.size(), size_t(marks_[size_t(chosen_)].entry + 1)));
    plan_.push_back(e);
    return true;
}

void CharWalk::poll(float dBeats) {
    ClipDriver* d = driver();
    if (!d) return;
    if (state_ == 1) {
        const int dom = d->dominant();
        const bool newer = dom >= 0 && dom + 1 < int(d->chain().size());
        if (next_ == 0 || !newer) {
            if (next_ < int(plan_.size())) {
                const Entry& e = plan_[size_t(next_)];
                if (dom < 0) d->playClip(e.clip, kPlayNoBlend);
                else d->playAt(e.clip, 0, e.dest, e.src - d->chain()[size_t(dom)].frame);
            } else {
                state_ = 2;
            }
            ++next_;
        }
    } else if (state_ == 2) {
        if (plan_.empty() || d->firstPlaying() != plan_.back().clip) {
            const Spot t = target_;
            reset();
            c_.setRegulate(&t);
            return;
        }
    } else {
        return;
    }
    steer(dBeats);
}

bool CharWalk::actuallyWalking() const {
    ClipDriver* d = driver();
    if (state_ <= 0 || !d) return false;
    const int dom = d->dominant();
    if (dom < 0) return false;
    const int k = std::min(next_ - 2, int(plan_.size()) - 1);
    return k >= 0 && d->chain()[size_t(dom)].clip == plan_[size_t(k)].clip;
}

void CharWalk::predict(float ahead, RootMotion& m, int& index, float& frame) const {
    float f0 = frame;
    frame += ahead;
    while (index < int(plan_.size()) - 1) {
        const float f1 = plan_[size_t(index + 1)].src + halfBlend_;
        if (frame <= f1) break;
        m.setClip(plan_[size_t(index)].clip);
        ++index;
        m.integrate(f0, f1);
        f0 = plan_[size_t(index)].dest + halfBlend_;
        frame = (frame - f1) + f0;
    }
    m.setClip(plan_[size_t(index)].clip);
    m.integrate(f0, frame);
}

void CharWalk::goal(float frame, P4& out, int k) const {
    if (segment_ < int(points_.size()) - 1) {
        out = points_[size_t(segment_)];
        return;
    }
    // On the last segment: where he must be now so that the clips left end
    // on the target (integrated back from the stop's end).
    RootMotion m;
    m.start(nullptr, saved_.data(), targetAngle_);
    const int last = int(plan_.size()) - 1;
    float f = plan_[size_t(last)].clip->endFrame;
    for (int i = last; i > k; --i) {
        m.setClip(plan_[size_t(i)].clip);
        m.integrate(f, plan_[size_t(i)].dest + halfBlend_);
        f = plan_[size_t(i)].src + halfBlend_;
    }
    m.setClip(plan_[size_t(k)].clip);
    m.integrate(f, frame);
    out = {m.pos[0], m.pos[1], m.pos[2], m.pos[3]};
}

void CharWalk::steer(float dBeats) {
    if (!(dBeats > 0)) return;
    c_.setRegulate(nullptr);
    ClipDriver* d = driver();
    const int dom = d->dominant();
    if (dom < 0 || plan_.empty()) return;
    const ClipDriver::Node& node = d->chain()[size_t(dom)];
    const int k = std::min(next_ - 2, int(plan_.size()) - 1);
    if (k <= 0 || node.clip != plan_[size_t(k)].clip || k == int(plan_.size()) - 1) return;
    scene::Xfm me = c_.xfm();
    // Where the clips take him in a beat, and where he should be heading.
    RootMotion m;
    const float here[4] = {me.pos[0], me.pos[1], me.pos[2], 1};
    m.start(nullptr, here, facingOf(me));
    int index = k;
    float frame = node.frame;
    predict(1.0f, m, index, frame);
    P4 g;
    goal(node.frame, g, k);
    if (segment_ < int(points_.size()) - 1) {
        const float ahead = dir_[0] * (m.pos[0] - g[0]) + dir_[2] * (m.pos[2] - g[2]) + dir_[1] * (m.pos[1] - g[1]);
        if (ahead >= 0) setSegment(segment_ + 1);
    }
    // The goal kept within path_radius of the segment's line.
    const P4& base = points_[size_t(segment_ - 1)];
    const float t = dir_[0] * (g[0] - base[0]) + dir_[2] * (g[2] - base[2]) + dir_[1] * (g[1] - base[1]);
    const float proj[3] = {dir_[0] * t + base[0], dir_[1] * t + base[1], dir_[2] * t + base[2]};
    const float lat[3] = {g[0] - proj[0], g[1] - proj[1], g[2] - proj[2]};
    const float ll = len3(lat);
    if (pathRadius < ll) {
        const float over = ll - pathRadius, r = pathRadius / ll;
        for (int i = 0; i < 3; ++i) g[size_t(i)] = r * lat[i] + (dir_[i] * over + proj[i]);
    }
    // 0x8218A140: turn toward the goal (asin of the cross product of the
    // goal's and the predicted motion's directions, lengths at least 5).
    const float ax = g[0] - me.pos[0], ay = g[1] - me.pos[1], az = g[2] - me.pos[2];
    const float bx = m.pos[0] - me.pos[0], by = m.pos[1] - me.pos[1], bz = m.pos[2] - me.pos[2];
    const float la = std::max(5.0f, std::sqrt(ax * ax + az * az + ay * ay)), lb = std::max(5.0f, std::sqrt(bx * bx + bz * bz + by * by));
    const float sn = std::clamp((by * ax - ay * bx) / (la * lb), -1.0f, 1.0f);
    rotateZ(me, -std::asin(sn) * dBeats);
    // The speed: the path left against the plan's distance between marks.
    if (cursor_ < chosen_) {
        const Mark& mk = marks_[size_t(cursor_)];
        if (mk.entry < k || (mk.entry == k && mk.frame < node.frame)) {
            float left = 0;
            float p[3] = {me.pos[0], me.pos[1], me.pos[2]};
            for (size_t i = size_t(segment_); i < points_.size(); ++i) {
                const float dd[3] = {p[0] - points_[i][0], p[1] - points_[i][1], p[2] - points_[i][2]};
                left += len3(dd);
                p[0] = points_[i][0], p[1] = points_[i][1], p[2] = points_[i][2];
            }
            const float beats = std::max(2.0f, float(chosen_ - cursor_));
            speed_ = (left - (marks_[size_t(chosen_)].dist - marks_[size_t(cursor_)].dist)) / beats;
            ++cursor_;
        }
    }
    if (speed_ != 0) {
        const float dv[3] = {m.pos[0] - me.pos[0], m.pos[1] - me.pos[1], m.pos[2] - me.pos[2]};
        const float l = len3(dv);
        if (0 < l) {
            const float s = dBeats * (speed_ / l);
            for (int i = 0; i < 3; ++i) me.pos[size_t(i)] += dv[i] * s;
        }
    }
    c_.setXfm(me);
}

float CharWalk::walkTime(float& beatsPerUnit, uint32_t flags) const {
    beatsPerUnit = 0;
    ClipDriver* d = driver();
    ClipSet* set = d ? d->clipSet() : nullptr;
    if (!set) return 0;
    for (scene::CharClipSamples* c : set->groupClips("walk_walk")) {
        if (flags != ~0u && !(c->flags & flags)) continue;
        RootMotion m;
        const float zero[4] = {0, 0, 0, 1};
        m.start(c, zero, 0);
        m.raw(c->startFrame, c->endFrame);
        const float l = len3(m.pos);
        const float v = (c->endFrame - c->startFrame) / l;
        if (beatsPerUnit < v) beatsPerUnit = v;
    }
    float turnLen = 0, stopLen = 0;
    for (scene::CharClipSamples* c : set->groupClips("walk_turn")) turnLen = std::max(turnLen, c->endFrame - c->startFrame);
    for (scene::CharClipSamples* c : set->groupClips("walk_stop")) stopLen = std::max(stopLen, c->endFrame - c->startFrame);
    return stopLen + turnLen;
}

}  // namespace chr
