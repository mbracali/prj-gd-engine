// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include "data/bitmap.h"

// Enhanced Settings "Sharper Text & Sprites": a menu / font texture upscaled
// (x4 up to 256 px, x2 up to 512 px) with a Catmull-Rom filter on
// premultiplied colour; mostly hard-edged alpha (glyphs, cut-outs) is
// tightened back to a crisp edge. Larger images are returned unchanged.
Image enhanceImage(const Image& in);
