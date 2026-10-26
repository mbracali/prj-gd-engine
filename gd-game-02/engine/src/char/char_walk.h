// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "char/char_objects.h"
#include "scene/objects.h"

// GH2's walking guitarist (docs/band.md "Walks") [C unless noted]:
// RootMotion (0x82192670..0x82192D60) integrates a clip's root channels
// (bone_facing.pos / .rotz) the way the servo moves a "move self"
// character; CharWalk (vtables 0x820145EC / 0x82014604) plans a walk along
// a waypoint path as walk_turn -> walk_walk (looped) -> walk_stop clips and
// steers the character along it while they play.

namespace chr {

class Character;
class ClipDriver;

// A Waypoint at run time (+0xd8 flags, +0xdc radius, +0xe0 y radius (a box
// when > 0, else a circle), +0xe4 angle tolerance; the world transform).
struct Spot {
    const void* id = nullptr;  // the venue object
    std::string name;
    scene::Xfm xfm;
    uint32_t flags = 0;
    float radius = 0, yRadius = 0, angle = 0;
};

// The facing of a transform (0x822699E8): -atan2(y axis x, y axis y).
float facingOf(const scene::Xfm& x);
// 0x82161C70: an angle into [-pi, pi).
float wrapPi(float a);
// 0x82194A90: the move that brings `p` into the spot (circle of `radius`
// in x y, or the box radius x yRadius along the spot's x and y axes).
std::array<float, 3> spotCorrection(const Spot& s, const float p[3]);
// 0x82194C58: the turn that brings facing `a` within the spot's tolerance.
float spotTurn(const Spot& s, float a);

// A clip's root motion (the integrator of 0x82192670 / 0x82192720 /
// 0x821927B8 / 0x82192858 / 0x82192980 / 0x82192D60). Samples are taken at
// clip frames (interpolated, clamped to the clip).
class RootMotion {
public:
    float pos[4] = {0, 0, 0, 1};  // [3..6]
    float angle = 0;              // [7]

    // 0x82192670: the clip whose channels are sampled (no-op when the same).
    void setClip(const scene::CharClipSamples* clip);
    // 0x82192D60: a clip, a start position and facing.
    void start(const scene::CharClipSamples* clip, const float p[4], float a);
    // 0x82192720: pos = the raw displacement from f0 to f1 (clip space).
    void raw(float f0, float f1);
    // 0x821927B8: pos = the raw displacement from the last sample to f.
    void rawStep(float f);
    // 0x82192980: moves pos / angle by the clip's motion from f0 to f1 (the
    // displacement turned into the current facing).
    void integrate(float f0, float f1);
    // 0x82192858: the same from the last sample to f.
    void step(float f);

private:
    struct Channel {
        const scene::CharBonesSamples* set = nullptr;
        int bone = -1;
    };
    void samplePos(float frame, float out[4]) const;
    float sampleRot(float frame) const;
    const scene::CharClipSamples* clip_ = nullptr;
    Channel pos_, rot_;
    float last_[4] = {0, 0, 0, 1};  // [8..11]
    float lastRot_ = 0;             // [12]
};

// CharWalk (the guitarist's `walk`).
class CharWalk {
public:
    explicit CharWalk(Character& c) : c_(c) {}
    float pathRadius = 12;  // char_objects.dta CharWalk types guitarist (path_radius 12)

    // 0x8218CA90: walk through `path` (waypoints, the last the target).
    // mode: the play mode of the turn's transition (3, kPlayFirst);
    // turnFlags: the walk clip's flags (direction | kWalkNormal/Extreme);
    // turnFilter: flags the turn clip must have; stopFilter: flags the stop
    // clip must have. False (and reset) when no plan was found.
    bool walk(const std::vector<Spot>& path, uint32_t mode, uint32_t turnFlags, uint32_t turnFilter, uint32_t stopFilter);
    // vtable +4 (0x8218BDD0): plan and path dropped, idle.
    void reset();
    // Poll (0x8218B1A0): queues the plan's clips, steers (0x8218ABC8);
    // when the stop clip has played, the servo regulates to the target.
    void poll(float dBeats);
    // 0x8218A568: the walk's turn / walk clips are what shows.
    bool actuallyWalking() const;
    // 0x8218A8F0: the longest walk_turn + walk_stop clip (beats);
    // beatsPerUnit = the slowest walk_walk clip's beats per unit of root
    // motion (clips with any of `flags`; ~0: all).
    float walkTime(float& beatsPerUnit, uint32_t flags = ~0u) const;
    int state() const { return state_; }  // +0x44: 0 idle, 1 walking, 2 the stop clip queued
    const Spot& target() const { return target_; }

    struct Entry {  // a planned clip: {clip, frame it starts at, frame of the previous clip it starts from}
        scene::CharClipSamples* clip = nullptr;
        float dest = 0, src = 0;
    };
    const std::vector<Entry>& plan() const { return plan_; }

private:
    struct Mark {  // +0x64: one per beat of the walk loop
        int entry = 0;
        float frame = 0, dist = 0;
    };
    using P4 = std::array<float, 4>;
    ClipDriver* driver() const;
    bool plan(uint32_t mode, uint32_t turnFlags, uint32_t turnFilter, uint32_t stopFilter);  // 0x8218C800
    bool planStart(uint32_t mode, uint32_t turnFlags, uint32_t turnFilter);                   // 0x8218BFC8
    bool planStop(float pathLen, uint32_t stopFilter);                                        // 0x8218C1F8
    void setSegment(int i);                                                                    // 0x8218AB18
    void steer(float dBeats);                                                                  // 0x8218ABC8
    void predict(float ahead, RootMotion& m, int& index, float& frame) const;                  // 0x8218A618
    void goal(float frame, P4& out, int k) const;                                              // 0x8218A768

    Character& c_;
    std::vector<P4> points_;      // +0xc: the character, then the path
    Spot target_;                 // +0x18
    float targetAngle_ = 0;       // +0x1c
    float tolerance_ = 0;         // +0x20: max(radius, y radius)
    P4 saved_{};                  // +0x24: the target before the stop moved it
    float dir_[3] = {0, 0, 0};    // +0x34: the segment's direction
    int state_ = 0;               // +0x44
    int next_ = 0;                // +0x48: the next plan entry to queue
    int segment_ = 0;             // +0x4c
    std::vector<Entry> plan_;     // +0x50
    int cursor_ = 0;              // +0x5c (the mark count while planning)
    int chosen_ = 0;              // +0x60
    std::vector<Mark> marks_;     // +0x64
    float speed_ = 0;             // +0xc64
    float halfBlend_ = 0;         // +0xc68
};

}  // namespace chr
