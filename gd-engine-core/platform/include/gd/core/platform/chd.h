// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// CHD disc images (MAME's "Compressed Hunks of Data", what emulators keep
// collections in) read as the plain disc image they hold, 2048-byte sectors
// from offset 0, the bytes an .iso of the same disc has (GDUL-030, #112):
//   - DVD CHDs (`chdman createdvd`: PS2 DVDs, Xbox 360 discs; also raw /
//     hard-disk CHDs): the logical bytes as they are;
//   - CD CHDs (`chdman createcd`: PS2 CDs): the first data track's user data,
//     2048 bytes per frame whatever the track stores (MODE1 / MODE1_RAW /
//     MODE2_RAW / MODE2_FORM1).
// Decoded with libchdr (cmake/gdcore_chd.cmake: the library `gdcore_chd`).
// Every game gets it through RandomAccessFile::open (file.h), which opens a
// .chd as its image; the launcher, which has no gdcore, uses ChdImage itself.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace gd::core::platform {

class RandomAccessFile;

// The first bytes of a CHD file ("MComprHD"); `n` is how many were read.
constexpr size_t kChdSignatureBytes = 8;
bool isChdSignature(const void* bytes, size_t n);

class ChdImage {
public:
    // Reads the CHD file's own (compressed) bytes: exactly `n` at `offset`,
    // false on a short read. Called from one thread at a time.
    using ReadFn = std::function<bool(uint64_t offset, void* dst, size_t n)>;

    // nullptr and `error` when the file is not a CHD this reader handles
    // (a CD CHD without a data track, a CHD that needs a parent, damage).
    static std::unique_ptr<ChdImage> open(uint64_t fileSize, ReadFn read, std::string* error = nullptr);
    ~ChdImage();
    ChdImage(const ChdImage&) = delete;
    ChdImage& operator=(const ChdImage&) = delete;

    // The disc image's size in bytes (a multiple of 2048 on CD CHDs).
    uint64_t size() const;
    // Exactly `n` image bytes at `offset`; false past the end or on a hunk
    // that does not decode. Thread-safe (one lock; decoded hunks are cached).
    bool read(uint64_t offset, void* dst, size_t n);
    // "dvd" (a DVD / raw CHD) or "cd".
    const char* kind() const;

    struct Impl;

private:
    explicit ChdImage(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

// `raw` (a CHD file) as the image it holds; nullptr and `error` otherwise.
std::unique_ptr<RandomAccessFile> openChd(std::shared_ptr<RandomAccessFile> raw, std::string* error = nullptr);

}  // namespace gd::core::platform
