// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/random_file.h"

#include <stdexcept>

#include "gd/core/platform/chd.h"
#include <string>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif


namespace gd::core::gh2 {

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

// Windows: paths are UTF-8 (the launcher, SDL, the file dialogs), but fopen
// reads its argument in the ANSI code page, so a disc in a folder or with a
// name outside it (a user folder with an accent, Música, Japanese) would not
// open while the launcher had recognised it (docs/guidelines/usual-bugs.md:
// the launcher cannot load ISOs on Windows). Open it as a wide path there;
// everywhere else (and if the text is not valid UTF-8) fopen as before.
static std::FILE* openForReading(const std::string& path) {
#ifdef _WIN32
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1, nullptr, 0);
    if (n > 0) {
        std::wstring wide(size_t(n), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1, wide.data(), n);
        if (std::FILE* f = _wfopen(wide.c_str(), L"rb")) return f;
    }
#endif
    return std::fopen(path.c_str(), "rb");
}

// A CHD disc image (GDUL-030): its signature at the start of the file.
static bool startsWithChd(std::FILE* f) {
    unsigned char sig[gd::core::platform::kChdSignatureBytes];
    return seek64(f, 0) == 0 && std::fread(sig, 1, sizeof(sig), f) == sizeof(sig) &&
           gd::core::platform::isChdSignature(sig, sizeof(sig));
}

// The CHD read by gd-engine-core as the plain image (.chd next to .iso, #112):
// every path kind the game opens (UTF-8 on Windows, Android content:// and
// /proc/self/fd/<n>) goes through its RandomAccessFile::open.
static std::unique_ptr<gd::core::platform::RandomAccessFile> openChdImage(const std::string& path) {
    std::string why;
    auto f = gd::core::platform::RandomAccessFile::open(path, &why);
    if (!f) std::fprintf(stderr, "disc image %s: %s\n", path.c_str(), why.c_str());
    return f;
}

std::unique_ptr<RandomFile> RandomFile::open(const std::string& path) {
    std::FILE* f = openForReading(path);
    if (!f) return nullptr;
    uint64_t size = size64(f);
    if (size >= 124 && startsWithChd(f)) {
        std::fclose(f);
        auto chd = openChdImage(path);
        if (!chd) return nullptr;
        std::unique_ptr<RandomFile> r(new RandomFile(nullptr, chd->size()));
        r->core_ = std::move(chd);
        return r;
    }
    return std::unique_ptr<RandomFile>(new RandomFile(f, size));
}

RandomFile::~RandomFile() {
    if (f_) std::fclose(f_);
}

void RandomFile::read(uint64_t offset, void* dst, size_t size) {
    if (core_) {
        if (offset + size > size_ || !core_->read(offset, dst, size)) throw std::runtime_error("read beyond end of file");
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (offset + size > size_ || seek64(f_, offset) != 0 || std::fread(dst, 1, size, f_) != size)
        throw std::runtime_error("read beyond end of file");
}

}  // namespace gd::core::gh2
