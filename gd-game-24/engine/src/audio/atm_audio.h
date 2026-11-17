// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Rock Band Unplugged's encrypted, five-stream song audio container.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace gd24 {

// Decode one complete .ATM file from a player's disc. Track order is the
// container's order: drums, bass, lead guitar, vocals, fill/backing.
// On failure, `tracks` is cleared and `error` describes the malformed input.
bool decodeAtm(const std::vector<uint8_t>& atm,
               std::array<std::vector<uint8_t>, 5>& tracks,
               std::string& error);

}  // namespace gd24
