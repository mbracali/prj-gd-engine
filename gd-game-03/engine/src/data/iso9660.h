// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "data/random_file.h"

// Minimal read-only ISO 9660 directory lookup (the PS2 DVD image), copied from
// gd-game-01's data/iso9660 (RandomFile lives in data/random_file.h here).
class Iso9660 {
public:
    struct Extent {
        uint64_t offset;  // byte offset in the image
        uint64_t size;
    };

    // Returns nullopt if the image has no ISO 9660 primary volume descriptor.
    static std::optional<Iso9660> open(RandomFile& image);

    // Case-insensitive lookup of a '/'-separated path, e.g. "GEN/MAIN.HDR".
    std::optional<Extent> find(const std::string& path) const;
    // The primary volume descriptor's volume identifier, trailing spaces removed
    // ("GH2" on this game's disc).
    const std::string& volumeId() const { return volumeId_; }

private:
    Iso9660(RandomFile& image, Extent root, std::string volumeId)
        : image_(&image), root_(root), volumeId_(std::move(volumeId)) {}
    std::optional<Extent> findIn(Extent dir, const std::string& name, bool wantDir) const;

    RandomFile* image_;
    Extent root_;
    std::string volumeId_;
};
