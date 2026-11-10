// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <map>
#include <memory>
#include <vector>

#include "char/rig.h"
#include "char/skeleton.h"
#include "render/scene_gpu.h"

// RB's swaying cloth banners (qp_selsong.milo, the shop's and tour's
// RenderDirs; docs/frontend.md "The cloth"): a RenderDir's own pollables,
// polled as its directory polls them every frame [C data: the objects]:
// - CharTransCopy (Poll RB 0x8233F6F8) [C]: the dest's local transform
//   becomes the source's (controller.trans, rocked by controller.tnm on
//   loop.trg, onto Box01.mesh, the cloth's root);
// - CharHair (the hair strand of the character code, rig.cpp): the chain
//   bone_01..bone_10 hangs from Box01 and trails it;
// - the cloth mesh (popup_bg.mesh) skinned to those bones (its vertices
//   index any number of bones: skinned on the CPU, as BandView does).
namespace gfx {

class UiCloth {
public:
    // A cloth for `dir` when it holds a CharHair; nullptr otherwise.
    static std::unique_ptr<UiCloth> make(const scene::Directory& dir, RenderDevice& device);
    ~UiCloth();

    // One frame: the locals from `pose` (its anims), CharTransCopy, the
    // hair (stepped at 60 Hz on `seconds`), the bones back into `pose`.
    void poll(ScenePose& pose, double seconds);
    // The geometry of a mesh skinned to the posed bones (in the directory's
    // space), or nullptr when `mesh` has no bones.
    const GpuMesh* skinned(const scene::Mesh& mesh, const ScenePose& pose);
    int polls() const { return polls_; }
    // The skinned vertices (16 floats each, position first) of the last
    // skinned() of `mesh`; nullptr before.
    const std::vector<float>* vertices(const scene::Mesh& mesh) const {
        auto it = skins_.find(&mesh);
        return it == skins_.end() ? nullptr : &it->second.work;
    }

private:
    explicit UiCloth(const scene::Directory& dir, RenderDevice& device);
    const scene::Directory& dir_;
    RenderDevice& device_;
    chr::Skeleton sk_;
    std::unique_ptr<chr::Rig> rig_;
    struct Copy {
        int src = -1, dest = -1;
    };
    std::vector<Copy> copies_;
    std::vector<int> simulated_;  // the hair's bones (their locals go back to the pose)
    struct Skin {
        std::vector<float> packed, work;
        std::vector<uint16_t> indices;
        std::unique_ptr<DynamicMesh> mesh;
        GpuMesh gpu;
    };
    std::map<const scene::Mesh*, Skin> skins_;
    int polls_ = 0;
};

}  // namespace gfx
