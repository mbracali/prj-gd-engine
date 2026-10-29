// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

// QB keys: every name in GH3's data (scripts, files, structure fields) is a
// 32-bit checksum of the lower-cased name: CRC-32 with the reflected
// polynomial 0xEDB88320 and initial value 0xFFFFFFFF, *without* the final
// XOR [C: every pak entry's type key (".qb" = 0xa7f505c4) and every
// compress.toc path key match; docs/formats.md "QB keys"]. Paths use '\'.
namespace qbkey {

namespace detail {
constexpr std::array<uint32_t, 256> makeTable() {
    std::array<uint32_t, 256> t{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        t[i] = c;
    }
    return t;
}
inline constexpr std::array<uint32_t, 256> kTable = makeTable();
constexpr char lower(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }
}  // namespace detail

constexpr uint32_t key(std::string_view name) {
    uint32_t c = 0xFFFFFFFFu;
    for (char ch : name) c = detail::kTable[(c ^ uint8_t(detail::lower(ch))) & 0xFF] ^ (c >> 8);
    return c;
}

static_assert(key(".qb") == 0xa7f505c4u, "QB key of .qb");

// checksum -> name, from the disc's debug paks (dbg.pak: "[Checksums]"
// lines) and the file names (Disc::loadNames). Unknown keys print as hex.
class Names {
public:
    void add(uint32_t k, std::string name) { names_.emplace(k, std::move(name)); }
    void add(const std::string& name) { names_.emplace(key(name), name); }
    std::string get(uint32_t k) const;
    const std::string* find(uint32_t k) const {
        auto it = names_.find(k);
        return it == names_.end() ? nullptr : &it->second;
    }
    size_t size() const { return names_.size(); }
    // Adds the "[Checksums]" lines ("0x1234abcd name") of a debug file.
    void addDebugText(std::string_view text);

private:
    std::unordered_map<uint32_t, std::string> names_;
};

// The table the program uses (filled once the disc is open).
Names& names();
inline std::string name(uint32_t k) { return names().get(k); }

}  // namespace qbkey
