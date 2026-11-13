// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

namespace scene {
class Tex;
}

namespace chr {

// A Rock Band patch (a tattoo, face paint: the patch creator, PatchPanel,
// patchcreator.dta; GD05-084, DECISIONS D93): sticker layers drawn in
// order into a PatchRenderer's texture (patch_chest / _left_arm /
// _right_arm / _facepaint). A layer is a sticker of a category of ui.dta's
// (patch (stickers (<category> <milo> <preview> (sets ...))))  (the milo's
// Tex objects of type sticker, in order; each has sizex / sizey,
// palette_index, allow_color) [C data], its colour (the patch layer milo's
// sticker.pal, patch_warpmesh.milo) and its place [I: RB's layer record
// (RB 0x823CB348 loads the stickers) not read: centre, scale, rotation and
// flips in the patch's -1..1 square].
struct PatchLayer {
    std::string category;  // empty: no sticker
    int sticker = -1;
    int color = -1;        // sticker.pal index; -1: the sticker's own colours (white)
    float x = 0, y = 0;    // centre
    float sx = 1, sy = 1;  // scale (x the sticker's sizex ...)
    float rot = 0;         // radians, counter-clockwise
    bool flipX = false, flipY = false;
    bool operator==(const PatchLayer&) const = default;
};
using Patch = std::vector<PatchLayer>;

// A layer resolved for drawing (Character::patches).
struct PatchDraw {
    const scene::Tex* tex = nullptr;
    float halfW = 0.5f, halfH = 0.5f;  // half extents in the patch square
    float x = 0, y = 0, rot = 0;
    bool flipX = false, flipY = false;
    std::array<float, 4> color{1, 1, 1, 1};
};

}  // namespace chr
