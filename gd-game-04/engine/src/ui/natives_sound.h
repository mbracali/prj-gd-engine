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
void addSoundNatives(qs::Vm& vm, Sfx& sfx);
// Called with each sound event's key as SoundEventFast plays it (the song
// hooks' counters); empty: none.
void setSoundEventListener(std::function<void(uint32_t)> f);
