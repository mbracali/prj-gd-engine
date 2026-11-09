// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// File IO: whole files, atomic writes, and random access reads of large files
// (disc images of several GB) that work with every path a game gets:
//   - plain paths, UTF-8 on every system (Windows: wide-character open, so a
//     folder with accents or CJK characters works);
//   - Android "content://" documents (read through SDL, which opens them with
//     the document provider [C: gd01's android_data.cpp relies on it]);
//   - Android "/proc/self/fd/<n>": the launcher's disc descriptor, read with
//     pread on a duplicate (reopening that path is refused on Android 11+ for
//     shared storage the app does not own [C: launcher sdk/gd_module_shim.cpp]).
// No exceptions: failures return false / nullptr and say why in `error`.
#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace gd::core::platform {

// A read-only file read at any offset. Thread-safe: several threads may read
// one file at once (the audio worker and the loader do).
class RandomAccessFile {
public:
    virtual ~RandomAccessFile() = default;
    virtual uint64_t size() const = 0;
    // Exactly `n` bytes at `offset`; false on a short read or an error.
    virtual bool read(uint64_t offset, void* dst, size_t n) = 0;

    // Opens `path` (any of the kinds above); nullptr and `error` on failure.
    static std::unique_ptr<RandomAccessFile> open(const std::string& path, std::string* error = nullptr);
    // A file held in memory (tests, unpacked archives).
    static std::unique_ptr<RandomAccessFile> fromMemory(std::vector<uint8_t> bytes);
};

// fopen with a UTF-8 path on every system.
std::FILE* openFile(const std::string& path, const char* mode);

bool readFile(const std::string& path, std::vector<uint8_t>& out, std::string* error = nullptr);
// Writes to "<path>.tmp" and renames it over `path`: a crash never leaves a
// half-written settings file behind.
bool writeFileAtomic(const std::string& path, const void* data, size_t size, std::string* error = nullptr);

}  // namespace gd::core::platform
