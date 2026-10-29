// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/pak.h"

#include <stdexcept>

#include "data/disc.h"
#include "data/qbkey.h"

namespace {

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

constexpr uint32_t kLast = qbkey::key(".last");
constexpr uint32_t kLast2 = qbkey::key("last");

}  // namespace

Pak Pak::load(const Disc& disc, const std::string& pakPath) {
    Bytes header = disc.read(pakPath);
    size_t headerSize = header.size();
    const std::string suffix = ".pak.xen";
    if (pakPath.size() > suffix.size() && pakPath.compare(pakPath.size() - suffix.size(), suffix.size(), suffix) == 0) {
        std::string pab = pakPath.substr(0, pakPath.size() - suffix.size()) + ".pab.xen";
        if (disc.exists(pab)) {
            Bytes data = disc.read(pab);
            header.insert(header.end(), data.begin(), data.end());
        }
    }
    return fromBytes(std::move(header), headerSize, pakPath);
}

Pak Pak::fromBytes(Bytes buffer, size_t headerSize, std::string name) {
    Pak p;
    p.name_ = std::move(name);
    p.buf_ = std::move(buffer);
    for (size_t pos = 0; pos + 32 <= headerSize; pos += 32) {
        const uint8_t* e = p.buf_.data() + pos;
        PakEntry en;
        en.type = be32(e);
        if (en.type == kLast || en.type == kLast2) {
            p.ended_ = true;
            break;
        }
        en.offset = be32(e + 4);
        en.size = be32(e + 8);
        en.pakName = be32(e + 12);
        en.fullName = be32(e + 16);
        en.name = be32(e + 20);
        en.parent = be32(e + 24);
        en.flags = be32(e + 28);
        en.pos = pos;
        if (en.dataOffset() + en.size > p.buf_.size())
            throw std::runtime_error(p.name_ + ": entry " + std::to_string(p.entries_.size()) + " lies outside the data");
        p.entries_.push_back(en);
    }
    if (!p.ended_) throw std::runtime_error(p.name_ + ": no .last entry");
    return p;
}

const PakEntry* Pak::find(uint32_t key) const {
    for (const PakEntry& e : entries_)
        if (e.fullName == key) return &e;
    for (const PakEntry& e : entries_)
        if (e.name == key) return &e;
    return nullptr;
}

const PakEntry* Pak::findType(uint32_t key, uint32_t type) const {
    for (const PakEntry& e : entries_)
        if (e.type == type && (e.fullName == key || e.name == key)) return &e;
    return nullptr;
}
