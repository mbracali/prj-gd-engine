// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// PSP_GAME/PARAM.SFO: the PSP's standard "\0PSF" key / value table (version
// 1.1): a 20-byte header {magic, version, key table offset, data table
// offset, count}, then `count` 16-byte entries {u16 key offset, u16 format
// (0x0204 UTF-8 string, 0x0404 u32), u32 length, u32 max length, u32 data
// offset}. Keys used: TITLE, DISC_ID, DISC_VERSION.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace gd24 {

struct ParamSfo {
    std::map<std::string, std::string> strings;  // the string values
    std::map<std::string, uint32_t> numbers;     // the u32 values

    bool parse(const std::vector<uint8_t>& bytes, std::string* error);
    std::string get(const std::string& key) const {
        auto it = strings.find(key);
        return it == strings.end() ? std::string() : it->second;
    }
};

}  // namespace gd24
