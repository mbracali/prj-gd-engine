// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

namespace scene {
class CharClipSamples;
}

// Rock Band's body deform: the creator's height and weight sliders
// (char/gen/char_objects.dta DEFORM_COMMON, GD05-084, DECISIONS D82) [C]:
// the character's deform_clips / deform clip holds five samples, the
// neutral body and four extremes at fixed (weight, height) points; the
// slider point's barycentric weights over a fixed triangle fan (Character
// constructor RB 0x822EA0C0 -> 0x823013A8, deform_frame RB 0x82300A00)
// blend them (char_utl_deform RB 0x82355918: CharBones ScaleAdd, then
// PoseMeshes 0x823630F0 writes the bones' local positions, rotations and
// scales). The bone_* positions stay: the limbs' lengths change.
namespace chr {

class Character;

// The weight of the deform clip's sample `frame` (0..4) at the slider
// point (RB 0x82300A00) [C]: points 0 (0.5, 0.5) neutral, 1 (1, 1) tall and
// fat, 2 (1, 0) short and fat, 3 (0, 1) tall and skinny, 4 (0, 0) short and
// skinny (x the weight, y the height); triangles (0 2 1) (0 1 3) (0 3 4)
// (0 4 2), the first containing the point (an edge counts as inside); a
// vertex not of it weighs 0, a point outside them all -1.
float deformFrameWeight(float weight, float height, int frame);

// The character's deform clip (deform_clips' "deform"), null without one.
const scene::CharClipSamples* deformClip(Character& c);

// Poses the character's bones from its deform clip at (weight, height)
// (clamped to 0..1 [I: RB's callers keep them there]); the number of
// bone channels set, 0 when the character has no deform clip; the posed
// bones' rest (Skeleton::bind) becomes the deformed pose. Not done [I]
// (GD05-092): the outfit meshes' MeshDeform bake (the exo_* bones'
// girth), the CharCuffs, CharIKScale (the animated pelvis height).
int applyDeform(Character& c, float weight, float height);

}  // namespace chr
