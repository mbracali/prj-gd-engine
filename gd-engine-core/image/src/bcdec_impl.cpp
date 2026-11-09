// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// bcdec (third_party/bcdec), internal to this file; compiled without
// warnings (code we do not change). BC4 / BC5 in the precise mode (signed
// formats need it).
#include "third_party_impl.h"

#define BCDEC_STATIC
#define BCDEC_BC4BC5_PRECISE
#define BCDEC_IMPLEMENTATION
#include "bcdec.h"

namespace gd::core::image::detail {

void bcdecRgba8(int kind, const void* block, uint8_t* dst, int pitchBytes) {
    switch (kind) {
        case 1: bcdec_bc1(block, dst, pitchBytes); break;
        case 2: bcdec_bc2(block, dst, pitchBytes); break;
        case 3: bcdec_bc3(block, dst, pitchBytes); break;
        case 7: bcdec_bc7(block, dst, pitchBytes); break;
        default: break;
    }
}

void bcdecBc4(const void* block, uint8_t* dst, int pitchBytes) { bcdec_bc4(block, dst, pitchBytes, 0); }
void bcdecBc5(const void* block, uint8_t* dst, int pitchBytes) { bcdec_bc5(block, dst, pitchBytes, 0); }
void bcdecBc4Snorm(const void* block, float* dst, int pitchFloats) { bcdec_bc4_float(block, dst, pitchFloats, 1); }
void bcdecBc5Snorm(const void* block, float* dst, int pitchFloats) { bcdec_bc5_float(block, dst, pitchFloats, 1); }
void bcdecBc6h(const void* block, float* dst, int pitchFloats, bool isSigned) {
    bcdec_bc6h_float(block, dst, pitchFloats, isSigned ? 1 : 0);
}

}  // namespace gd::core::image::detail
