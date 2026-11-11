// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "data/anim.h"
#include "data/qb.h"
#include "data/venue.h"
#include "render/band_anim.h"

class Globals;

// gh4's mocap cameras: the camera lock targets (create_all_camera_lock_targets:
// moment_cam_lock_target_01..10, <member>_mocap_lock_target_01 / 02,
// venue_cam_lock_target), ghosts with the GH4_Camera skeleton
// (Profile_Ven_Camera_Obj) that play a camera animation at a venue node
// (momentcamera_playanim: obj_setposition / obj_setorientation at
// <zone>_<node> or the start waypoint, then the anim on the body) [C scripts
// guitar_character.qb, guitar_transitions.qb]. A camera entry with
// lockto = <target> locktobone = bone_camera sits on that bone.
namespace venue {

class MocapCameras {
public:
    using ClipLookup = std::function<const anim::Clip*(uint32_t key)>;
    // The song's <song>_performance entries (time / scr / params), the clip
    // structs in `g`, the camera clips from `clips`. `fromMs`: the venue's first ms.
    void init(const Venue& v, const Globals& g, const std::vector<qb::File>* songFiles, const std::vector<const qb::Struct*>& performance,
              const anim::Skeleton* skeleton, const ClipLookup& clips, double fromMs);
    // The lock target's bone in world space at a song time; false when the
    // target plays nothing then (or is not a camera lock target).
    bool frame(uint32_t target, uint32_t bone, double ms, Vec3& pos, Quat& rot) const;
    bool animated(uint32_t target, double ms) const;
    size_t segments() const { return segs_.size(); }

private:
    struct Seg {
        uint32_t target = 0;
        double t = 0;        // song ms the anim starts
        float offset = 0;    // s into the clip at t
        bool cycle = false;  // momentcamera_playanim cycle = 1
        const anim::Clip* clip = nullptr;
        Vec3 pos{};
        Quat rot{0, 0, 0, 1};
    };
    const Seg* at(uint32_t target, double ms) const;
    std::vector<Seg> segs_;  // by start time
    bandanim::Rig rig_;
    bool haveRig_ = false;
};

}  // namespace venue
