// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>
#include <vector>

namespace scene {
class Object;
class Directory;
}  // namespace scene

namespace gfx {

// RB's RndDir draw list (RB 0x821B3A48 sorts it with 0x821FF100) [C]: by
// draw order; at the same order, objects of the same class by name
// (strcmp). Classes compare by their Symbol's address, which the port cannot
// know: different classes keep the directory's order [I].
// tour_band_welcome.milo's all.grp (blackmask.mesh, 85% black) draws
// before art.grp and the texts' group, as in RB; in the file's order it
// covered the popup ("Congratulations!" at 12% of its colour). The track
// panel's streak meter (GD05-070): multi_meter_anim.grp (order 1: the
// multiplier's tag and "Nx") draws before streak_meter.grp (order 2: the
// dial, its black circle and glass), which covers the tag's end.
void sortDraws(std::vector<const scene::Object*>& roots);

// A directory's draw list: its drawables (meshes, groups, inline
// directories, texts) no Group of it holds, sorted as above.
std::vector<const scene::Object*> dirDrawList(const scene::Directory& dir);

// Pairs drawn out of draw order: in `drawn` (one directory's top-level
// drawables in the order they were drawn) how many come before one of a
// lower draw order. The walks' check: zero when a directory draws as RB's.
int drawOrderInversions(const std::vector<const scene::Object*>& drawn);

}  // namespace gfx
