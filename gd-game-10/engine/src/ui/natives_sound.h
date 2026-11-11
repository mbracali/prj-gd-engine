// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

class Sfx;
namespace qs {
class Vm;
}

#include <cstdint>
#include <functional>
#include <vector>

// SoundEventFast / PlaySound and the sound stubs (natives_sound.cpp).
class Disc;
void addSoundNatives(qs::Vm& vm, Sfx& sfx, const Disc& disc);
// Called with each sound event's key as SoundEventFast plays it (the song
// hooks' counters); empty: none.
void setSoundEventListener(std::function<void(uint32_t)> f);
// gh4's sound units: vol in dB -> gain, pitch in semitones -> ratio.
float dbGain(float db);
float semitoneRatio(float semis);
// The voices the scripts started (PlaySound, StartPreloadedStream's
// streamall streams) with the starting script's name: StopSoundEvent on a
// script event (Menu_Music_SE) stops those of the scripts named after it.
void addScriptVoice(Sfx& sfx, uint32_t voice, uint32_t script);
namespace qb {
struct Struct;
}
// SetSoundBussParams's parameters applied to the game's busses
// (audio/sound_buss.h): `{<BussSet>}` structs of `Buss = {vol pitch}` and
// named `Buss = {...}` items, `time = s` (the song hooks' native calls it too).
void applySoundBussParams(qs::Vm& vm, const qb::Struct& params);
