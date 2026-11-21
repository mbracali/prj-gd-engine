// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The player's Rock Band Unplugged UMD image (.iso, or a .chd through
// gd-engine-core's reader): an ISO 9660 file system with 2048-byte sectors.
// Files are looked up by their disc path, case-insensitive, with '/'
// ("PSP_GAME/USRDIR/MUSIC/ABC/BEATMATCH.STR"). See docs/FORMATS.md.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace gd::core::platform {
class RandomAccessFile;
}

namespace gd24 {

class Umd {
public:
    struct Entry {
        std::string path;  // as on the disc, '/' separated
        uint32_t lba = 0;  // first sector
        uint32_t size = 0; // bytes
    };

    // nullptr and `error` when the image cannot be read or is not ISO 9660.
    static std::unique_ptr<Umd> open(const std::string& imagePath, std::string* error);
    ~Umd();

    // "ULUS-10418" (UMD_DATA.BIN, the text before the first '|'); "" if absent.
    const std::string& discId() const { return discId_; }
    const std::string& volumeId() const { return volumeId_; }
    const std::vector<Entry>& files() const { return files_; }
    const Entry* find(const std::string& path) const;
    bool exists(const std::string& path) const { return find(path) != nullptr; }
    // The whole file; false when it is missing or a read fails.
    bool read(const std::string& path, std::vector<uint8_t>& out) const;
    // `n` bytes at `offset` inside a file.
    bool readAt(const Entry& e, uint64_t offset, void* dst, size_t n) const;
    // The files whose path starts with `prefix` (case-insensitive).
    std::vector<const Entry*> list(const std::string& prefix) const;

private:
    Umd() = default;
    bool walk(uint32_t lba, uint32_t size, const std::string& prefix, int depth, std::string* error);

    std::unique_ptr<gd::core::platform::RandomAccessFile> file_;
    std::vector<Entry> files_;
    std::unordered_map<std::string, size_t> index_;  // upper-case path -> files_
    std::string discId_, volumeId_;
};

std::string upperPath(std::string s);

}  // namespace gd24
