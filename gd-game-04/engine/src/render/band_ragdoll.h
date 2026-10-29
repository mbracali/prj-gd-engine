// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

#include "data/havok.h"
#include "render/band_anim.h"

// The band's accessory bones (hair, coat tails, chains: Bone_ACC_01..15 of
// Guitarist_accessory_bones) swinging as GH3's Havok ragdoll makes them
// (ragdoll_setaccessorybones: those bodies simulated, the body bones
// keyframed by the animation) [C scripts; docs/band.md "Accessory bones"].
// gd04's model [I]: each accessory body is a point mass at its bone's tip
// (the .rag shape's far end) on a rod of fixed length from the bone's
// joint, under gravity with the body's linear damping, its swing limited
// by its ragdoll constraint's cone around the animated direction (the
// parent's simulated frame); the bone turns to point at the mass, and its
// descendants follow. Stepped at a fixed 60 Hz of song time from the
// animated pose at each step, so the result does not depend on the frame
// rate.
class AccessorySim {
public:
    // False when the rig has none of `bones` with a .rag body.
    bool init(const bandanim::Rig& rig, const havok::Physics& rag, const std::vector<uint32_t>& bones);
    bool empty() const { return links_.empty(); }
    // The animated model-space bone matrices at `ms` in `world`, replaced by
    // the simulated ones; `animate(t, world)` gives the animated matrices at
    // a step time. `gravity`: (0, -9.81, 0) in model space.
    void apply(double ms, std::vector<bandanim::Mat34>& world, const std::function<void(double, std::vector<bandanim::Mat34>&)>& animate,
               const bandanim::Vec& gravity);

private:
    struct Link {
        int bone = -1;
        bandanim::Vec tip;  // the tip in the bone's frame
        float len = 0, cone = 0, damping = 0;
    };
    struct State {
        int64_t step = 0;
        std::vector<bandanim::Vec> x, xp;  // tip positions (model space) now and at the step before
    };
    // Turns the accessory bones of `world` to the tips `x`; when `x` is
    // null, the tips the pose itself gives (initialisation).
    void pose(std::vector<bandanim::Mat34>& world, const std::vector<bandanim::Vec>* x, std::vector<bandanim::Vec>* restTips,
              std::vector<bandanim::Vec>* pivots) const;
    std::vector<Link> links_;
    std::vector<int> linkOf_;    // bone -> link (-1)
    std::vector<int> parent_;
    State cur_;
    bool started_ = false;
    std::deque<State> history_;  // the last steps (a query earlier than the newest step)
};
