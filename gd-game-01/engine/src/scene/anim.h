// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "scene/scene.h"

// Plays scene animations the way RndAnimatable does (see FINDINGS.md
// "Animation"): SetFrame(frame, blend) runs the frame through the anim's
// filters, applies the class's keys to its target (mixed with the target's
// current value by `blend`), and forwards the filtered frame to the child
// anims. Objects are looked up across all the scenes given, in order, so a
// lighting section can animate the venue's environments and lights.
class AnimPlayer {
public:
    explicit AnimPlayer(std::vector<Scene*> scenes);

    SceneObject* find(const std::string& name);
    bool exists(const std::string& name) { return find(name) != nullptr; }

    // FUN_001abab8: filtered frame, class keys, then the children.
    void setFrame(const std::string& anim, float frame, float blend = 1.0f);
    // The class SetFrame alone (vtable +0x14) with the children; used by the
    // arena's anim slots, which pass frames past the top-level filters.
    void setFrameUnfiltered(const std::string& anim, float frame, float blend);
    // The anim's filters applied to `frame` (FUN_001ac468).
    float filter(const std::string& anim, float frame);

    // A TransAnim's transform at `frame` (parts without keys are identity).
    bool transAt(const std::string& anim, float frame, Xfm& out);
    // The anim's last key frame (RndAnimatable EndFrame, no filters).
    float endFrame(const std::string& anim);

    // Drops `anim` from every group's anim list (the script's unhook_anim_parents).
    void unhook(const std::string& anim);

    // Makes `child` follow `parent` (the script's {parent add_trans child}).
    void attach(const std::string& child, const std::string& parent);
    // Recomputes world transforms below every object a TransAnim moved,
    // then those of attached objects.
    void updateTransforms();
    // Marks an object whose transform the caller changed, for updateTransforms.
    void markMoved(const std::string& name);
    // Recomputes only the attached objects (after their parents moved).
    void updateAttached();

private:
    struct Node {
        SceneObject* obj;
        Scene* scene;
        std::string parent;
        Xfm parentWorld0;  // the parent's world transform at load, for parents in no scene
    };
    void apply(SceneObject& o, float frame, float blend);
    void evalTrans(const AnimObj& keys, float frame, float blend, Xfm& x, bool reset);
    void children(SceneObject& o, float frame, float blend, int depth);
    void setFrameAt(SceneObject& o, float frame, float blend, int depth, bool filtered);

    std::vector<Scene*> scenes_;
    std::map<std::string, SceneObject*> cache_;
    std::vector<Node> order_;  // transformables, parents before children
    std::map<std::string, size_t> nodeIndex_;
    std::vector<char> dirty_;
    std::vector<std::pair<SceneObject*, SceneObject*>> attached_;  // child, parent
};
