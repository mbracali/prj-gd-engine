// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

// The built-in capture backends (internal to gdcore_audio).

#include "gd/core/audio/input.h"

namespace gd::core::audio::detail {

InputBackend sdlInputBackend();  // always: SDL3 is a dependency of the core
#if GDCORE_AUDIO_HAS_MINIAUDIO
InputBackend miniaudioInputBackend();
#endif

}  // namespace gd::core::audio::detail
