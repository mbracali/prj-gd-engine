// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include "data/bytes.h"

// Decodes a raw deflate stream (RFC 1951, no zlib header).
// Throws std::runtime_error on malformed input.
Bytes inflateRaw(const uint8_t* data, size_t size);
