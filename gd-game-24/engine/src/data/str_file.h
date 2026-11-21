// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The .STR container (docs/FORMATS.md "STR: heap images"): u32 version 1,
// u32 header data size, u32 N, then at 0x40 N + 1 entries of 0x40 bytes
// {u32 file offset, u32 size, u32 end, ...}; the chunks are memory images.
// (GLOBAL_BINARY.STR and the PRESET files have other headers.)
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gd24 {

struct StrChunk {
    uint32_t index = 0;   // entry number
    uint32_t offset = 0;  // in the file
    uint32_t size = 0;
};

struct StrFile {
    uint32_t version = 0, headerData = 0, count = 0;
    std::vector<StrChunk> chunks;  // the entries with a size, in order

    // Parses the header from the first bytes of a file of `fileSize` bytes;
    // false and `error` when it is not this container or an entry runs past
    // the end of the file.
    bool parse(const uint8_t* head, size_t headSize, uint64_t fileSize, std::string* error);
};

}  // namespace gd24
