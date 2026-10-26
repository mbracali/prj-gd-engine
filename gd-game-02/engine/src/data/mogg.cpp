// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/mogg.h"

#include <cstring>
#include <stdexcept>
#include <string>

MoggHeader parseMoggHeader(const uint8_t* data, size_t size) {
    ByteReader r(data, size);
    if (size < 20) throw std::runtime_error("too short for a mogg header");
    MoggHeader h;
    h.version = r.u32le();
    h.oggOffset = r.u32le();
    r.u32le();  // map version (14)
    r.u32le();  // map interval (20000)
    h.mapEntries = r.u32le();
    if (h.version != 10)
        throw std::runtime_error("mogg version " + std::to_string(h.version) +
                                 (h.version > 10 ? ": an encrypted mogg (not used by Guitar Hero II)" : ": not a plain mogg"));
    if (uint64_t(20) + 8 * uint64_t(h.mapEntries) != h.oggOffset) throw std::runtime_error("mogg map size does not match its Ogg offset");
    if (size >= uint64_t(h.oggOffset) + 4 && std::memcmp(data + h.oggOffset, "OggS", 4) != 0)
        throw std::runtime_error("no Ogg stream at the mogg's Ogg offset");
    return h;
}
