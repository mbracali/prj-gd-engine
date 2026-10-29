// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/random_file.h"

#include <SDL3/SDL_iostream.h>

#include <stdexcept>

namespace {

int seek64(std::FILE* f, uint64_t offset) {
#if defined(_WIN32)
    return _fseeki64(f, static_cast<long long>(offset), SEEK_SET);
#else
    return fseeko(f, static_cast<off_t>(offset), SEEK_SET);
#endif
}

uint64_t size64(std::FILE* f) {
#if defined(_WIN32)
    _fseeki64(f, 0, SEEK_END);
    return static_cast<uint64_t>(_ftelli64(f));
#else
    fseeko(f, 0, SEEK_END);
    return static_cast<uint64_t>(ftello(f));
#endif
}

}  // namespace

std::unique_ptr<RandomFile> RandomFile::open(const std::string& path) {
    // A content:// document (Android's file dialog): SDL opens it through
    // its file descriptor, seekable, so the disc is read where it is
    // (GD04-028). The launcher passes /proc/self/fd/<n>, which its shim's
    // fopen reads the same way.
    if (path.rfind("content://", 0) == 0) {
        SDL_IOStream* io = SDL_IOFromFile(path.c_str(), "rb");
        if (!io) return nullptr;
        const Sint64 n = SDL_GetIOSize(io);
        if (n <= 0) {
            SDL_CloseIO(io);
            return nullptr;
        }
        std::unique_ptr<RandomFile> r(new RandomFile(nullptr, uint64_t(n)));
        r->io_ = io;
        return r;
    }
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return nullptr;
    uint64_t size = size64(f);
    return std::unique_ptr<RandomFile>(new RandomFile(f, size));
}

RandomFile::~RandomFile() {
    if (io_) SDL_CloseIO(io_);
    if (f_) std::fclose(f_);
}

void RandomFile::read(uint64_t offset, void* dst, size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (io_) {
        if (offset + size > size_ || SDL_SeekIO(io_, Sint64(offset), SDL_IO_SEEK_SET) < 0) throw std::runtime_error("read beyond end of file");
        size_t got = 0;
        while (got < size) {
            const size_t n = SDL_ReadIO(io_, static_cast<char*>(dst) + got, size - got);
            if (n == 0) throw std::runtime_error("read beyond end of file");
            got += n;
        }
        return;
    }
    if (offset + size > size_ || seek64(f_, offset) != 0 || std::fread(dst, 1, size, f_) != size)
        throw std::runtime_error("read beyond end of file");
}
