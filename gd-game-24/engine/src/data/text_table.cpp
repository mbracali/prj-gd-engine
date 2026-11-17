// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/text_table.h"

#include <cstdlib>

namespace gd24 {

bool TextTable::parse(const std::vector<uint8_t>& bytes, std::string* error) {
    std::vector<std::string> lines;
    std::string cur;
    for (uint8_t b : bytes) {
        if (b == '\n') {
            if (!cur.empty() && cur.back() == '\r') cur.pop_back();
            lines.push_back(cur), cur.clear();
        } else {
            cur.push_back(char(b));
        }
    }
    if (!cur.empty()) lines.push_back(cur);
    if (lines.size() < 2) {
        if (error) *error = "fewer than two lines";
        return false;
    }
    const long count = std::strtol(lines[0].c_str(), nullptr, 10);
    if (count < 1 || size_t(count) + 1 != lines.size()) {
        if (error) *error = "count " + std::to_string(count) + " but " + std::to_string(lines.size() - 1) + " lines";
        return false;
    }
    language = lines[1];
    strings.clear();
    for (long i = 0; i < count; ++i) {
        std::string s;
        const std::string& l = lines[size_t(i) + 1];
        for (size_t k = 0; k < l.size(); ++k) {
            if (l[k] == '\\' && k + 1 < l.size() && l[k + 1] == 'n') s.push_back('\n'), ++k;
            else s.push_back(l[k]);
        }
        strings.push_back(std::move(s));
    }
    return true;
}

}  // namespace gd24
