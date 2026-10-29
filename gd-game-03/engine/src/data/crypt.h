// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include "data/bytes.h"

// Decrypts a .dtb file: a u32 seed (stored plain), then every byte XORed with
// the low byte of the next word of the game's table cipher (docs/formats.md
// "dtb"). The table is 256 words seeded with the C rand() LCG
// (x * 0x41C64E6D + 0x3039, two steps per word) at 0x002d95b0 [C]; the read
// indices start at 0 and 0x67 [C 0x002d95b0] and wrap after 0xF8.
// The same cipher as gd-game-01 (GH1 PS2).
Bytes dtbDecrypt(const Bytes& encrypted);
