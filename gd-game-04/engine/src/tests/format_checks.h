// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstddef>
#include <map>
#include <string>

#include "data/pak.h"

// Per-format checks of one pak entry for --formattest. Each format's decoder
// adds its check here as it is written (M1: QB first, then textures,
// scenes, ...). A type without a decoder yet is counted as "not decoded"
// (printed, not a failure, until its decoder lands; docs/formats.md lists
// them).
struct FormatStats {
    size_t qbFiles = 0, qbSections = 0, qbScripts = 0, qbUnused = 0;
    std::map<std::string, size_t> decoded;     // per type: decoded to the end
    std::map<std::string, size_t> notDecoded;  // per type: no decoder yet
};

bool checkPakFile(const Pak& pak, const PakEntry& e, FormatStats& stats, std::string& what);
void printFormatStats(const FormatStats& stats);
