// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "data/bytes.h"
#include "data/random_file.h"
#include "data/xdvdfs.h"

// The player's Guitar Hero III (Xbox 360) disc: a disc image (XDVDFS) or a
// folder with the disc's files (DATA/, default.xex). Paths are
// '/'-separated and case-insensitive, relative to the disc root
// ("DATA/COMPRESSED/PAK/qb.pak.xen").
//
// Files under DATA/COMPRESSED/ are stored as one raw deflate stream each
// (RFC 1951, no zlib header); read() inflates them [C: data, all 417 files
// of compress.toc.xen; docs/formats.md "Compressed files"].
class Disc {
public:
    // A disc image or a folder; nullptr and `error` when neither works.
    static std::unique_ptr<Disc> open(const std::string& path, std::string& error);

    bool exists(const std::string& path) const;
    // The file's bytes (inflated when compressed). Throws std::runtime_error
    // when the file is missing or broken.
    Bytes read(const std::string& path) const;
    // The stored bytes, never inflated.
    Bytes readRaw(const std::string& path) const;
    // Size of the stored file, or nullopt.
    std::optional<uint64_t> size(const std::string& path) const;
    // Every file on the disc ('/'-separated, as stored).
    std::vector<std::string> list() const;
    // Fills qbkey::names() from the debug paks and file names.
    void loadNames() const;
    const std::string& path() const { return path_; }

    // Reads part of a stored file (streams: the song audio).
    void readRange(const std::string& path, uint64_t offset, void* dst, size_t size) const;

private:
    std::string path_;
    std::string folder_;  // set when the disc is a folder
    std::unique_ptr<RandomFile> image_;
    std::optional<Xdvdfs> fs_;
    std::string resolve(const std::string& path) const;  // folder: the real path ("" if missing)
};
