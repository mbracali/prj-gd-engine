// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include "data/bytes.h"

// Harmonix's Park-Miller XOR cipher, used by GH2 (Xbox 360) for gen/main.hdr
// and every .dtb [C] (tools/ark.py crypt(), docs/dtb_format.md): the first 4
// bytes are a little-endian seed stored in clear; each later byte is XORed
// with the low byte of the next key, key = 16807 * key mod (2^31 - 1)
// (Schrage's method), advanced before each byte. The result is 4 bytes
// shorter than the input. Throws on input shorter than the seed.
Bytes harmonixDecrypt(const Bytes& encrypted);
