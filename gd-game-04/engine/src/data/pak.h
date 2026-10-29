// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "data/bytes.h"

class Disc;

// GH3's archives (port of tools/pak.py; docs/formats.md "PAK"). A .pak.xen
// (inflated) is a table of 32-byte big-endian entries: type key (extension),
// offset from the entry to its data, size, pak name key (0 on the disc),
// full name key, short name key, parent full name key, flags. The ".last"
// entry ends the table. With a .pab.xen next to it, the data lies past the
// end of the .pak: offsets count through pak + pab as one buffer [C: data,
// every pak on the disc; the reader in the XEX: TODO address].
struct PakEntry {
    uint32_t type = 0, offset = 0, size = 0, pakName = 0, fullName = 0, name = 0, parent = 0, flags = 0;
    size_t pos = 0;  // of the entry in the table
    size_t dataOffset() const { return pos + offset; }
};

class Pak {
public:
    // Throws std::runtime_error when broken.
    static Pak load(const Disc& disc, const std::string& pakPath);
    static Pak fromBytes(Bytes buffer, size_t headerSize, std::string name);

    const std::vector<PakEntry>& entries() const { return entries_; }
    std::pair<const uint8_t*, size_t> data(const PakEntry& e) const {
        return {buf_.data() + e.dataOffset(), e.size};
    }
    Bytes copy(const PakEntry& e) const {
        auto s = data(e);
        return Bytes(s.first, s.first + s.second);
    }
    // The first entry with this full name key (or short name key), or null.
    const PakEntry* find(uint32_t fullOrShortName) const;
    const PakEntry* findType(uint32_t fullOrShortName, uint32_t type) const;
    const std::string& name() const { return name_; }
    bool ended() const { return ended_; }

private:
    std::string name_;
    Bytes buf_;
    std::vector<PakEntry> entries_;
    bool ended_ = false;
};
