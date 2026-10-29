// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstddef>
#include <cstdint>
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
    size_t volumes = 0, volumeSlices = 0;      // volume textures decoded slice by slice
    uint32_t volumeHash = 0x811C9DC5u;         // FNV-1a of their PC-order blocks (img.py check prints the same)
    size_t havokBodies = 0, havokAccessoryBodies = 0, havokConstraints = 0, havokTriangles = 0;
    std::map<std::string, size_t> decoded;     // per type: decoded to the end
    std::map<std::string, size_t> notDecoded;  // per type: no decoder yet
};

bool checkPakFile(const Pak& pak, const PakEntry& e, FormatStats& stats, std::string& what);
void printFormatStats(const FormatStats& stats);
