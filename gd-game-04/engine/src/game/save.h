// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "data/qb.h"

// The save: GH3 keeps its progress and options in global tags
// (GetGlobalTags / SetGlobalTags, saved by SaveToMemoryCard); the port
// writes them as versioned JSON (lesson L12) in the save folder,
// atomically with a .bak of the previous file.
namespace save {

constexpr const char* kFormat = "gd04-save";
constexpr int kVersion = 1;

std::string path();  // <save folder>/save.json ("" if no save folder)
bool exists();
// Writes the tags; false and `error` on failure.
bool write(const std::unordered_map<uint32_t, qb::Struct>& tags, std::string& error);
// Reads them; false and `error` when missing or malformed (type and range checked).
bool read(std::unordered_map<uint32_t, qb::Struct>& tags, std::string& error);

}  // namespace save
