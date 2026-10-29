// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/qbkey.h"

#include <cstdio>
#include <cstdlib>

namespace qbkey {

std::string Names::get(uint32_t k) const {
    if (const std::string* n = find(k)) return *n;
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%08x", k);
    return buf;
}

void Names::addDebugText(std::string_view text) {
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
        if (line.size() < 12 || line[0] != '0' || (line[1] != 'x' && line[1] != 'X')) continue;
        size_t sp = line.find(' ');
        if (sp == std::string_view::npos || sp < 3) continue;
        std::string hex(line.substr(2, sp - 2));
        char* stop = nullptr;
        unsigned long v = std::strtoul(hex.c_str(), &stop, 16);
        if (!stop || *stop) continue;
        std::string_view n = line.substr(sp + 1);
        while (!n.empty() && n.front() == ' ') n.remove_prefix(1);
        if (!n.empty()) names_.emplace(uint32_t(v), std::string(n));
    }
}

Names& names() {
    static Names table;
    return table;
}

}  // namespace qbkey
