// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// TEXT/STR_<LANG>_*.TXT (docs/FORMATS.md "TEXT"): UTF-8; line 1 "<count>\t1",
// then `count` strings, one per line, the first being the language name
// ("English"); a line break is written as the two characters "\n".
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gd24 {

struct TextTable {
    std::string language;
    std::vector<std::string> strings;  // strings[0] is the language name

    // false and `error` when the count does not match the lines.
    bool parse(const std::vector<uint8_t>& bytes, std::string* error);
};

}  // namespace gd24
