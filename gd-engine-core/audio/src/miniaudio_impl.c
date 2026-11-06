// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// miniaudio's implementation (third_party/miniaudio/miniaudio.h, pinned in
// third_party/miniaudio/README.md), compiled once, as C. The options are the
// target's compile definitions (audio/CMakeLists.txt): device IO and the
// low-level API only for now; decoding, the node graph and the engine can be
// switched on when a game needs them.
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
