// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "data/bytes.h"
#include "data/iso9660.h"

// Read access to the files inside the game's ARK archive (GEN/MAIN.HDR +
// GEN/MAIN_<n>.ARK), loaded from either a disc image or an extracted folder.
class GameData {
public:
    // `path` is a .iso image or a folder containing GEN/MAIN.HDR. Throws
    // std::runtime_error with a user-facing message on failure.
    explicit GameData(const std::string& path);

    bool exists(const std::string& arkPath) const;
    // Throws std::runtime_error if the file is missing.
    Bytes read(const std::string& arkPath) const;
    // Reads bytes [offset, offset + n) of a file (clamped to its end); returns the count.
    // For streaming large files (the bonus videos are hundreds of MB).
    size_t readRange(const std::string& arkPath, uint64_t offset, uint8_t* dst, size_t n) const;
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
    };

    void loadHeader(const Bytes& hdr);

    std::vector<std::unique_ptr<RandomFile>> files_;
    std::vector<Part> parts_;
    std::unordered_map<std::string, Entry> entries_;
    std::string description_;
};
