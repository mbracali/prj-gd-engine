// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include "scene/scene.h"

// PsParticleSys SetFrame (PsParticleSys_v1 @ 0x1cffc0): dt = frame - last
// frame; update(dt), then emit(dt). A frame earlier than the particles'
// birth kills them, which is how a restarted effect clears (docs/hit_fx.md §5).
// `emitter` is the system's world transform (the spawn space when
// relative_motion < 1).
void particleSetFrame(ParticleSysObj& p, float frame, const Xfm& emitter);

// [I] Enhanced Settings > Enable Particles FX: off, no system simulates or
// draws (low-end hardware); gameplay is unaffected.
void setParticlesEnabled(bool on);
bool particlesEnabled();
