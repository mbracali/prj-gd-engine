// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "data/havok.h"
#include "render/band_anim.h"

// The band's accessory bones (hair, coat tails, chains: Bone_ACC_01..15 of
// Guitarist_accessory_bones) as GH3's Havok ragdoll moves them
// (docs/band.md "Accessory bones"). GH3 [C 821AFC08, 823FE8C8, 823FFC28]:
// the member's .rag bodies in a world with gravity (0, -9.8, 0) and the
// 4-iteration solver (tau 0.6, damping 1); the accessory bodies
// MOTION_STABILIZED_SPHERE_INERTIA with their constraint to the parent body
// in the world, every other body MOTION_KEYFRAMED (its constraint removed)
// and hard-keyframed each frame to the animated pose with 1 / frame time;
// the ragdoll's pose read back before the step (one frame behind), then
// hkConstraintUtils::checkAndFixConstraint(0.05, 0.8) over the constraints
// from the last until one is fixed; the accessory bones take the ragdoll's
// local transforms. gd10 re-implements the Havok parts it needs: rigid
// bodies (the .rag masses, centres of mass, sphere inertia, damping,
// velocity caps), the ball-socket / ragdoll (twist, cone, planes, angular
// friction) / limited hinge / prismatic constraints as velocity rows solved
// Gauss-Seidel in 4 sub-steps, at a fixed 60 Hz of song time (GH3's frame
// time on the 360 [I]) so the result does not depend on the frame rate.
class AccessorySim {
public:
    // False when the rig has none of `bones` with a .rag body.
    bool init(const bandanim::Rig& rig, const havok::Physics& rag, const std::vector<uint32_t>& bones);
    bool empty() const { return dynamic_ == 0; }
    // True when the bone is an accessory (a simulated body).
    bool isDynamic(int bone) const {
        const int b = bone >= 0 && size_t(bone) < bodyOf_.size() ? bodyOf_[size_t(bone)] : -1;
        return b >= 0 && bodies_[size_t(b)].dynamic;
    }
    // The animated model-space bone matrices at `ms` in `world`, replaced by
    // the simulated ones; `animate(t, world)` gives the animated matrices at
    // a step time. `gravity`: the world's (0, -9.8, 0) in model space.
    // `resets`: song times (ascending) of ragdoll_markforreset: the step
    // that reaches one starts over at rest on the animated pose (null: none).
    void apply(double ms, std::vector<bandanim::Mat34>& world, const std::function<void(double, std::vector<bandanim::Mat34>&)>& animate,
               const bandanim::Vec& gravity, const std::vector<double>* resets = nullptr);

    // A hair segment hanging from a keyframed head by a ragdoll constraint
    // (cone 0.3 rad) under gravity for 3 s: "" when the cone holds and the
    // joint stays together, else what went wrong (chartest).
    static std::string selfTest();

    struct Rt {  // a rigid transform: rotation then translation
        bandanim::Quat q;
        bandanim::Vec t;
    };

private:
    struct Body {
        int bone = -1;
        bool dynamic = false;
        float invMass = 0, invInertia = 0;  // keyframed: 0
        bandanim::Vec com;                  // centre of mass in the body frame
        float maxLin = 0, maxAng = 0, linDamp = 0, angDamp = 0;
        Rt boneFromBody;                    // bone = body * boneFromBody (the bind poses)
    };
    struct Con {
        int a = -1, b = -1;  // child, parent (indices into bodies_)
        enum Kind { Ragdoll, Hinge, Prismatic, Other } kind = Other;
        bool active = false;  // in the world: the child is an accessory body
        Rt fa, fb;            // the constraint frames in the bodies
        havok::Constraint d;
    };
    struct BodyState {
        Rt x;
        bandanim::Vec v, w;  // the centre of mass's velocity, the angular velocity (model space)
    };
    struct State {
        int64_t step = 0;
        std::vector<BodyState> b;
    };
    void targets(const std::vector<bandanim::Mat34>& world, std::vector<Rt>& out) const;
    void step(State& s, const std::vector<bandanim::Mat34>& world, const bandanim::Vec& gravity) const;
    struct SolverRow;
    // The constraint's velocity rows at the step's start (h: the step's time).
    void rows(const std::vector<BodyState>& s, const Con& c, float h, std::vector<SolverRow>& out) const;
    void pose(const State& s, std::vector<bandanim::Mat34>& world) const;

    std::vector<Body> bodies_;
    std::vector<Con> cons_;
    std::vector<int> bodyOf_;  // bone -> body (-1)
    std::vector<int> parent_;
    int dynamic_ = 0;
    State cur_;
    bool started_ = false;
    std::deque<State> history_;  // the last steps (a query earlier than the newest step)
};
