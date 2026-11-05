// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

class Sfx;
namespace qs {
class Vm;
}

#include <cstdint>
#include <functional>

// SoundEventFast / PlaySound and the sound stubs (natives_sound.cpp).
class Disc;
void addSoundNatives(qs::Vm& vm, Sfx& sfx, const Disc& disc);
// Called with each sound event's key as SoundEventFast plays it (the song
// hooks' counters); empty: none.
void setSoundEventListener(std::function<void(uint32_t)> f);
namespace qb {
struct Struct;
}
// SetSoundBussParams's parameters applied to the game's busses
// (audio/sound_buss.h): `{<BussSet>}` structs of `Buss = {vol pitch}` and
// named `Buss = {...}` items, `time = s` (the song hooks' native calls it too).
void applySoundBussParams(qs::Vm& vm, const qb::Struct& params);
