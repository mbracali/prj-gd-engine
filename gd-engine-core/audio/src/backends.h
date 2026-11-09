// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

// The built-in backends (internal to gdcore_audio). Each file defines its
// entry; output.cpp lists those compiled in (GDCORE_AUDIO_HAS_* below).

#include "gd/core/audio/output.h"

namespace gd::core::audio::detail {

#if GDCORE_AUDIO_HAS_MINIAUDIO
Backend miniaudioBackend();
#endif
#if GDCORE_AUDIO_HAS_OPENAL
Backend openalBackend();
#endif
#if GDCORE_AUDIO_HAS_FAUDIO
Backend faudioBackend();
#endif

// The folder holding the library or program this code is linked into (the
// game module, or the executable), with a trailing separator, or "" when
// unknown. Shared libraries the core loads at run time are looked up there
// first (they ship next to the module).
std::string thisModuleDir();

}  // namespace gd::core::audio::detail
