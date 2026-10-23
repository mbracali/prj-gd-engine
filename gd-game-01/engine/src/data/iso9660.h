// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>

// A file opened for 64-bit random access reads.
class RandomFile {
public:
    static std::unique_ptr<RandomFile> open(const std::string& path);
    ~RandomFile();
    RandomFile(const RandomFile&) = delete;
    RandomFile& operator=(const RandomFile&) = delete;

    // Reads exactly `size` bytes at `offset`; throws on short read.
    void read(uint64_t offset, void* dst, size_t size);
    uint64_t size() const { return size_; }

private:
    RandomFile(std::FILE* f, uint64_t size) : f_(f), size_(size) {}
    std::FILE* f_;
    uint64_t size_;
};

// Minimal read-only ISO 9660 directory lookup (enough for PS2 DVD images).
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

private:
    Iso9660(RandomFile& image, Extent root) : image_(&image), root_(root) {}
    std::optional<Extent> findIn(Extent dir, const std::string& name, bool wantDir) const;

    RandomFile* image_;
    Extent root_;
};
