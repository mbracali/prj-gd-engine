// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// Reading files out of a disc image, whatever its file system. One interface,
// one implementation per file system:
//   iso9660  ISO 9660 (PlayStation 2 DVDs and CDs; PS2 DVDs also carry a UDF
//            bridge, and ISO 9660 is enough to read them) [C: gd01 / gd03
//            data/iso9660.cpp, the launcher's iso_probe.cpp]
//   xdvdfs   the Xbox DVD file system of Xbox and Xbox 360 discs (XGD1-3),
//            found at the game partition's known offsets [C: gd02 / gd04
//            data/xdvdfs.cpp, the launcher's iso_probe.cpp]
// More (UDF-only discs, GameCube / Wii, PSP UMD, CHD containers) are added as
// another DiscFileSystem and another try in openDisc(); no caller changes.
// The games keep their own readers for now; new code uses this one.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "gd/core/platform/file.h"

namespace gd::core::platform {

struct DiscEntry {
    std::string name;     // as stored, without ISO 9660's ";1" version
    bool isDir = false;
    uint64_t offset = 0;  // byte offset of its data in the image
    uint64_t size = 0;    // bytes
};

class DiscFileSystem {
public:
    virtual ~DiscFileSystem() = default;

    virtual std::string_view format() const = 0;  // "iso9660", "xdvdfs"
    virtual std::string volumeId() const { return {}; }  // ISO 9660 volume identifier, trimmed

    // A '/'-separated path, case-insensitive ("SYSTEM.CNF", "gen/main.hdr");
    // "" or "/" is the root folder.
    bool find(std::string_view path, DiscEntry& out) const;
    // The entries of a folder (any order); false if it is not a folder.
    bool list(std::string_view dir, std::vector<DiscEntry>& out) const;
    // Reads a file's bytes: all of them, or `n` from `offset` within it.
    bool readFile(const DiscEntry& file, std::vector<uint8_t>& out) const;
    bool read(const DiscEntry& file, uint64_t offset, void* dst, size_t n) const;

    RandomAccessFile& image() const { return *image_; }

protected:
    explicit DiscFileSystem(std::shared_ptr<RandomAccessFile> image) : image_(std::move(image)) {}
    virtual DiscEntry root() const = 0;
    virtual bool entries(const DiscEntry& dir, std::vector<DiscEntry>& out) const = 0;

    std::shared_ptr<RandomAccessFile> image_;
};

// The file system of a disc image: XDVDFS is tried first, then ISO 9660 (the
// launcher's probe order: an Xbox 360 image may carry an ISO 9660 video
// partition first). nullptr and `error` when neither is found.
std::unique_ptr<DiscFileSystem> openDisc(std::shared_ptr<RandomAccessFile> image, std::string* error = nullptr);
std::unique_ptr<DiscFileSystem> openIso9660(std::shared_ptr<RandomAccessFile> image);
std::unique_ptr<DiscFileSystem> openXdvdfs(std::shared_ptr<RandomAccessFile> image);

}  // namespace gd::core::platform
