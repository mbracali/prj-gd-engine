// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>

// A file opened for 64-bit random access reads (from gd01's data/iso9660).
// Reads are serialised, so the audio worker and the main thread can share it.
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
    std::FILE* f_ = nullptr;
    // Android: a content:// document read in place through SDL's IO (no copy).
    struct SDL_IOStream* io_ = nullptr;
    uint64_t size_;
    std::mutex mutex_;
};
