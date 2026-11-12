// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <map>
#include <memory>
#include <string>

#include "scene/objects.h"

class BandView;
namespace gfx {
class UiView;
}
namespace chr {
class Character;
class ClipLibrary;
}

namespace ui {

class UiManager;

// A profile character drawn in a Rock Band menu (GD05-083, DECISIONS D83):
// RB's CharPanel (the Rock Shop's store.milo, its char0 CompositeCharacter)
// and the practice space's creator.trans (charcreator.dta's
// practice_space_panel) show the character being dressed or made in its
// look [C data: the milos' slots]. The port builds it as the band does
// (game/band.h rbOutfitMilos, rbSkin, char/deform.h) and plays its main
// driver's stand (a drummer: sit) group, the attitude's clip [I: RB's
// shell clips and the shots' cameras (select_cam) are not read: the
// panel's own camera].
class RbCharPreview {
public:
    struct Look {
        std::string gender = "male", instrument = "guitar";
        std::map<std::string, std::string> outfit;
        int skin = 0;
        uint32_t attitude = 0;
        float weight = 0.5f, height = 0.5f;
        bool operator==(const Look&) const = default;
    };
    RbCharPreview();
    ~RbCharPreview();
    // Rebuilds the character when the look changed (false: it could not be built).
    bool set(UiManager& ui, const Look& look);
    void clear();
    // One poll of its clips (menu time: 120 beats a minute [I], as the GH2 char select).
    void poll(double dtMs);
    // Draws it at `at` through the view's camera and environment; the
    // meshes drawn (0 without a character).
    int draw(UiManager& ui, gfx::UiView& view, const scene::Xfm& at);
    chr::Character* character() { return ch_.get(); }
    // Every preview's meshes drawn so far (the walks' check).
    static int totalMeshes() { return total_; }
    // The walks' pixel check: every preview hidden while set.
    static bool hidden;

private:
    Look look_;
    bool has_ = false;
    std::unique_ptr<chr::ClipLibrary> clips_;
    std::unique_ptr<chr::Character> ch_;
    std::unique_ptr<BandView> view_;
    double clock_ = 0;
    static int total_;
};

}  // namespace ui
