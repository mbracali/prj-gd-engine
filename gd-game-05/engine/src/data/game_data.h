// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "data/bytes.h"
#include "gd/core/platform/disc.h"
#include "gd/core/platform/file.h"

// A file of the disc's own file system, outside the archive (GH2's videos:
// "videos/intro.wmv"), in the disc image or the extracted folder.
// Reads are thread-safe (gd-engine-core's RandomAccessFile).
class DiscFile {
public:
    DiscFile(std::shared_ptr<gd::core::platform::RandomAccessFile> file, uint64_t base, uint64_t size)
        : file_(std::move(file)), base_(base), size_(size) {}
    uint64_t size() const { return size_; }
    // Reads bytes [offset, offset + n), clamped to the file's end; returns the
    // count. Throws std::runtime_error when the file cannot be read.
    size_t read(uint64_t offset, void* dst, size_t n);

private:
    std::shared_ptr<gd::core::platform::RandomAccessFile> file_;
    uint64_t base_, size_;
};

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
    size_t fileCount() const { return entries_.size(); }
    size_t archivePartCount() const { return parts_.size(); }
    // All archive paths (lower case), in no particular order.
    std::vector<std::string> paths() const;
    // Opens a file of the disc's file system (not the archive), matched
    // case-insensitively, e.g. "videos/intro.wmv"; nullptr if missing.
    std::unique_ptr<DiscFile> openDiscFile(const std::string& discPath) const;

private:
    struct Part {
        gd::core::platform::RandomAccessFile* file;
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

    std::vector<std::shared_ptr<gd::core::platform::RandomAccessFile>> files_;
    std::vector<Part> parts_;
    std::unordered_map<std::string, Entry> entries_;
    std::string description_;
    std::shared_ptr<gd::core::platform::RandomAccessFile> image_;  // the disc image and
    std::unique_ptr<gd::core::platform::DiscFileSystem> disc_;      // its file system, or
    std::string folder_;                                            // the extracted folder
};
