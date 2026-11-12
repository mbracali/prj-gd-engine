// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/anim_rate.h"

#include <SDL3/SDL.h>

#include <algorithm>

#include "data/midi.h"

namespace gfx {

double beatAt(const TempoMap* tempo, float ms) {
    // Before the song (the intro's negative times) and without a song: 120 a minute.
    if (!tempo || ms < 0 || tempo->division() <= 0) return double(ms) / 500.0;
    return tempo->msToTick(double(ms)) / double(tempo->division());
}

double animFrames(int rate, float fromMs, float toMs, const TempoMap* tempo) {
    switch (rate) {
    case k480Fpb: return (beatAt(tempo, toMs) - beatAt(tempo, fromMs)) * 480.0;
    case k1Fpb: return beatAt(tempo, toMs) - beatAt(tempo, fromMs);
    default: return double(toMs - fromMs) * 0.03;  // k30_fps, k30_fps_ui, k30_fps_tutorial
    }
}

bool highwayLegacy() {
    static const bool on = [] {
        const char* e = SDL_getenv("GD05_HWY_LEGACY");
        return e && *e == '1';
    }();
    return on;
}

}  // namespace gfx
