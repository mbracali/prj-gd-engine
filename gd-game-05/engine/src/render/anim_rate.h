// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

class TempoMap;

namespace gfx {

// RndAnimatable's rates (system/run/config/macros.dta RATE_ENUM) [C data]:
// an EventTrigger's anim entry carries one, and the frames it runs are
// counted in its units: k30_fps (0), k30_fps_ui (2) and k30_fps_tutorial
// thirty a second; k480_fpb (1) 480 a beat; k1_fpb (3) one a beat.
// The track panel's triggers use all of them (band_power_meter.milo's
// be_ready.trig: be_color_pulse.anim, loop 0..1 at k1_fpb, one pulse a
// beat; tracksystem.milo's star_deploy.trig: track_streak_glow.mnm, loop
// 1000..1480 at k480_fpb, one a beat).
enum AnimRate : int { k30Fps = 0, k480Fpb = 1, k30FpsUi = 2, k1Fpb = 3 };

// The song's beat at song time `ms` (its tempo map; none: 120 beats a minute).
double beatAt(const TempoMap* tempo, float ms);

// The frames an anim at `rate` runs from song time `fromMs` to `toMs`.
double animFrames(int rate, float fromMs, float toMs, const TempoMap* tempo);

// GD05_HWY_LEGACY=1: the highway and HUD as before GD05-070 (every rate 30
// a second, the HUD's directories in file order, the draw group's parts
// after it, star tails in their slot's colour), so the walks' checks can
// be seen failing (the before numbers).
bool highwayLegacy();

}  // namespace gfx
