// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "render/device.h"
#include "render/material.h"
#include "render/scene_gpu.h"

namespace chr {
class Character;
}
namespace scene {
class Directory;
class Mat;
class Object;
class Tex;
class TexRenderer;
}  // namespace scene

namespace gfx {

// Rock Band's character skins (GD05-022 / GD05-084, DECISIONS D84): the
// heads' and torsos' diffuse textures are placeholders (head_head1_diffuse.tex
// in head_<n>.milo, torso_naked.tex in <gender>_<instrument>.milo) that a
// TexRenderer (head_skin.rndtex, torso_skin.rndtex) fills at run time from
// its skin proxy, an inline RndDir (head_skin_proxy, torso_skin_proxy) [C
// data]. RndTexRenderer::DrawShowing (RB 0x821ED620) [C]: when dirty, the
// proxy's camera (its first Cam: Cam.cam, y_fov 0, a parallel projection)
// targets the output texture, the proxy's draw list draws into it, and the
// renderer is clean until something changes. The proxies draw ScreenMasks
// (the skin texture tinted by skin.mat's colour, the make-up overlays: the
// eyes' / lips' outfit textures in their screen rects) and meshes laid out
// in uv space (the patch meshes, the arms' overlays) whose materials sample
// the PatchRenderers' outputs (patch_chest / _left_arm / _right_arm /
// _facepaint: the tattoos and face paint, rendered from their patch
// directory, RB 0x822D2C70; transparent without one [I]).
//
// The character's materials then sample the rendered targets in place of
// the placeholders (apply). One CharSkin per view; the targets of
// characters no longer drawn are freed after a while.
class CharSkin {
public:
    explicit CharSkin(SceneCache& cache);
    ~CharSkin();
    CharSkin(const CharSkin&) = delete;
    CharSkin& operator=(const CharSkin&) = delete;

    // Renders c's TexRenderers whose output is out of date (new, or the
    // character's skinSerial moved). Restores the device's target, viewport
    // and `mr`'s frame afterwards.
    void update(MaterialRenderer& mr, chr::Character& c);
    // `gm` (the material made from `mat`) samples the rendered texture its
    // diffuse names, if one is: true then.
    bool apply(const scene::Mat& mat, Material& gm) const;
    // The rendered pixels of c's texture `tex` (RGBA, top row first), for
    // the walks' checks; null when it was not rendered. SDL_DestroySurface it.
    SDL_Surface* read(chr::Character& c, const std::string& tex);
    // TexRenderers rendered so far (all views).
    static int renders() { return renders_; }

private:
    struct Target {
        TargetId id;
        int w = 0, h = 0;
        uint64_t texUid = 0;      // the output Tex (an address may be reused)
        uint32_t serial = ~0u;    // the character's skinSerial when rendered
        uint64_t lastUse = 0;     // SDL ticks
    };
    Target* target(const scene::Tex& out);
    void render(MaterialRenderer& mr, chr::Character& c, const scene::TexRenderer& t, const scene::Directory* proxy, Target& tg);
    void drawList(MaterialRenderer& mr, const scene::Directory& dir);
    void drawObject(MaterialRenderer& mr, const scene::Object& o, int depth);
    void collect(uint64_t now);

    SceneCache& cache_;
    std::unique_ptr<DynamicMesh> quad_;
    ScenePose pose_;  // the patch outputs on the proxies' materials
    std::map<const scene::Tex*, Target> targets_;
    float aspect_ = 1;
    static int renders_;
};

}  // namespace gfx
