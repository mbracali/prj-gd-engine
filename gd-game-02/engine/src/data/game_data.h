// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "data/bytes.h"
#include "data/random_file.h"

// Read access to the files inside GH2's ARK archive (gen/main.hdr +
// gen/main_<n>.ark), loaded from either an Xbox 360 disc image or an
// extracted folder (the game partition's files: gen/, default.xex, ...).
// The interface is gd01's GameData; the header format is GH2's.
class GameData {
public:
    // `path` is a disc image or a folder containing gen/main.hdr. Throws
    // std::runtime_error with a user-facing message on failure.
    explicit GameData(const std::string& path);

    // Archive paths are matched case-insensitively, as stored in the index
    // ("config/gen/songs.dtb"; shared engine files keep their
    // "../../system/run/..." prefix).
    bool exists(const std::string& arkPath) const;
    // Throws std::runtime_error if the file is missing.
    Bytes read(const std::string& arkPath) const;
    // Reads bytes [offset, offset + n) of a file (clamped to its end); returns the count.
    size_t readRange(const std::string& arkPath, uint64_t offset, uint8_t* dst, size_t n) const;
    uint64_t fileSize(const std::string& arkPath) const;
    const std::string& description() const { return description_; }
    // All archive paths (lower case), in no particular order.
    std::vector<std::string> paths() const;

private:
    struct Part {
        RandomFile* file;
        uint64_t base;  // offset of this part inside `file`
        uint64_t size;
    };
    struct Entry {
        uint64_t offset;  // offset across all parts
        uint32_t size;
        uint32_t inflated;  // 0: stored as is
    };

    void loadHeader(const Bytes& hdr);
    const Entry& entry(const std::string& arkPath) const;
    void readSpan(uint64_t offset, uint8_t* dst, size_t n) const;

    std::vector<std::unique_ptr<RandomFile>> files_;
    std::vector<Part> parts_;
    std::unordered_map<std::string, Entry> entries_;
    std::string description_;
};
