// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>
#include <vector>

#include "script/script.h"

// The optional (volume dB) (pan p) (transpose semitones) arrays a sound
// play takes: `synth play_sequence` (0x822E8A90 looks the three up in its
// arguments) and an Sfx's / sequence's `play` ({$sfx play (volume {options
// get_fx_volume})}, world_objects_worldbase.dta, system_script.dta) [C].
struct SfxArgs {
    float volume = 0, pan = 0, transpose = 0;
};

inline SfxArgs sfxArgs(Interp& in, const std::vector<Value>& args, size_t first) {
    SfxArgs a;
    for (size_t k = first; k < args.size(); ++k) {
        const Value v = in.evalValue(args[k]);
        if (v.type != Value::Type::Array || !v.array || v.array->size() < 2) continue;
        const Value key = in.evalValue((*v.array)[0]), val = in.evalValue((*v.array)[1]);
        if (!key.isText() || !val.isNumber()) continue;
        if (key.s == "volume") a.volume = val.number();
        else if (key.s == "pan") a.pan = val.number();
        else if (key.s == "transpose") a.transpose = val.number();
    }
    return a;
}
