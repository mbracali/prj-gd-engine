// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "data/random_file.h"

// Read-only lookup in the game partition of an Xbox 360 disc image (XGD2 /
// XGD3, the Xbox DVD file system "XDVDFS"); a port of tools/xdvdfs.py.
//
// 2048-byte sectors. The game partition starts at one of a few fixed offsets
// in the image, depending on the disc type [C] (GH2's Rev1 image: 0xFD90000,
// XGD2). At sector 32 of the partition, the volume descriptor:
// "MICROSOFT*XBOX*MEDIA", u32 root directory sector, u32 root directory size
// (little-endian). A directory is a binary search tree of entries
// (u16 left, u16 right subtree offsets in dwords; u32 first sector; u32 size;
// u8 attributes, 0x10 = directory; u8 name length; name), 4-byte aligned,
// never crossing a sector; 0xFFFF fills the rest of a sector.
class Xdvdfs {
public:
    struct Extent {
        uint64_t offset;  // byte offset in the image
        uint64_t size;
    };

    // Returns nullopt if no game partition is found at any known offset.
    static std::optional<Xdvdfs> open(RandomFile& image);

    // Case-insensitive lookup of a '/'-separated path, e.g. "gen/main.hdr".
    std::optional<Extent> find(const std::string& path) const;
    uint64_t partitionOffset() const { return base_; }

private:
    Xdvdfs(RandomFile& image, uint64_t base, Extent root) : image_(&image), base_(base), root_(root) {}
    std::optional<Extent> findIn(Extent dir, const std::string& name, bool wantDir) const;

    RandomFile* image_;
    uint64_t base_;
    Extent root_;
};
