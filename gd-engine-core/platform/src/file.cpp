// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "gd/core/platform/file.h"

#include <SDL3/SDL.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include "gd/core/platform/paths.h"
#include "utf8.h"

#ifdef _WIN32
#include <windows.h>
#endif
#ifdef __ANDROID__
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace gd::core::platform {

#ifdef _WIN32
namespace detail {
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}
std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}
}  // namespace detail
#endif

namespace {

void setError(std::string* error, const std::string& what) {
    if (error) *error = what;
}

int seek64(std::FILE* f, uint64_t offset, int whence) {
#ifdef _WIN32
    return _fseeki64(f, static_cast<long long>(offset), whence);
#else
    return fseeko(f, static_cast<off_t>(offset), whence);
#endif
}

int64_t tell64(std::FILE* f) {
#ifdef _WIN32
    return _ftelli64(f);
#else
    return ftello(f);
#endif
}

// A FILE; reads serialised (one file position).
class StdioFile final : public RandomAccessFile {
public:
    StdioFile(std::FILE* f, uint64_t size) : f_(f), size_(size) {}
    ~StdioFile() override { std::fclose(f_); }
    uint64_t size() const override { return size_; }
    bool read(uint64_t offset, void* dst, size_t n) override {
        if (n == 0) return offset <= size_;
        if (offset > size_ || n > size_ - offset) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        return seek64(f_, offset, SEEK_SET) == 0 && std::fread(dst, 1, n, f_) == n;
    }

private:
    std::FILE* f_;
    uint64_t size_;
    std::mutex mutex_;
};

// An SDL stream (Android content:// documents); reads serialised.
class SdlFile final : public RandomAccessFile {
public:
    SdlFile(SDL_IOStream* io, uint64_t size) : io_(io), size_(size) {}
    ~SdlFile() override { SDL_CloseIO(io_); }
    uint64_t size() const override { return size_; }
    bool read(uint64_t offset, void* dst, size_t n) override {
        if (n == 0) return offset <= size_;
        if (offset > size_ || n > size_ - offset) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        if (SDL_SeekIO(io_, Sint64(offset), SDL_IO_SEEK_SET) < 0) return false;
        size_t got = 0;
        while (got < n) {
            size_t r = SDL_ReadIO(io_, static_cast<uint8_t*>(dst) + got, n - got);
            if (r == 0) return false;
            got += r;
        }
        return true;
    }

private:
    SDL_IOStream* io_;
    uint64_t size_;
    std::mutex mutex_;
};

class MemoryFile final : public RandomAccessFile {
public:
    explicit MemoryFile(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}
    uint64_t size() const override { return bytes_.size(); }
    bool read(uint64_t offset, void* dst, size_t n) override {
        if (offset > bytes_.size() || n > bytes_.size() - offset) return false;
        if (n) std::memcpy(dst, bytes_.data() + offset, n);
        return true;
    }

private:
    std::vector<uint8_t> bytes_;
};

#ifdef __ANDROID__
// The launcher's /proc/self/fd/<n>: a duplicate of the descriptor, read with
// pread (no shared offset, so no lock).
class FdFile final : public RandomAccessFile {
public:
    FdFile(int fd, uint64_t size) : fd_(fd), size_(size) {}
    ~FdFile() override { ::close(fd_); }
    uint64_t size() const override { return size_; }
    bool read(uint64_t offset, void* dst, size_t n) override {
        if (offset > size_ || n > size_ - offset) return false;
        size_t got = 0;
        while (got < n) {
            ssize_t r = pread64(fd_, static_cast<uint8_t*>(dst) + got, n - got, off64_t(offset + got));
            if (r < 0 && errno == EINTR) continue;
            if (r <= 0) return false;
            got += size_t(r);
        }
        return true;
    }

private:
    int fd_;
    uint64_t size_;
};

std::unique_ptr<RandomAccessFile> openDescriptor(const std::string& path, std::string* error) {
    const long n = std::strtol(path.c_str() + 14, nullptr, 10);  // after "/proc/self/fd/"
    struct stat64 st {};
    if (n < 0 || fstat64(int(n), &st) != 0 || !S_ISREG(st.st_mode)) {
        setError(error, path + ": not an open file");
        return nullptr;
    }
    const int fd = fcntl(int(n), F_DUPFD_CLOEXEC, 0);
    if (fd < 0) {
        setError(error, path + ": " + std::strerror(errno));
        return nullptr;
    }
    return std::make_unique<FdFile>(fd, uint64_t(st.st_size));
}
#endif

std::unique_ptr<RandomAccessFile> openSdl(const std::string& path, std::string* error) {
    SDL_IOStream* io = SDL_IOFromFile(path.c_str(), "rb");
    if (!io) {
        setError(error, path + ": " + SDL_GetError());
        return nullptr;
    }
    const Sint64 size = SDL_GetIOSize(io);
    if (size < 0) {
        setError(error, path + ": size unknown (" + SDL_GetError() + ")");
        SDL_CloseIO(io);
        return nullptr;
    }
    return std::make_unique<SdlFile>(io, uint64_t(size));
}

}  // namespace

std::FILE* openFile(const std::string& path, const char* mode) {
#ifdef _WIN32
    return _wfopen(detail::widen(path).c_str(), detail::widen(mode).c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}

std::unique_ptr<RandomAccessFile> RandomAccessFile::open(const std::string& path, std::string* error) {
    if (isContentUri(path)) return openSdl(path, error);
#ifdef __ANDROID__
    if (std::FILE* f = openFile(path, "rb")) {
        const int64_t size = seek64(f, 0, SEEK_END) == 0 ? tell64(f) : -1;
        if (size >= 0) return std::make_unique<StdioFile>(f, uint64_t(size));
        std::fclose(f);
    }
    if (isDescriptorPath(path)) return openDescriptor(path, error);
    setError(error, path + ": " + std::strerror(errno));
    return nullptr;
#else
    std::FILE* f = openFile(path, "rb");
    if (!f) {
        setError(error, path + ": " + std::strerror(errno));
        return nullptr;
    }
    const int64_t size = seek64(f, 0, SEEK_END) == 0 ? tell64(f) : -1;
    if (size < 0) {
        setError(error, path + ": cannot find its size");
        std::fclose(f);
        return nullptr;
    }
    return std::make_unique<StdioFile>(f, uint64_t(size));
#endif
}

std::unique_ptr<RandomAccessFile> RandomAccessFile::fromMemory(std::vector<uint8_t> bytes) {
    return std::make_unique<MemoryFile>(std::move(bytes));
}

bool readFile(const std::string& path, std::vector<uint8_t>& out, std::string* error) {
    auto f = RandomAccessFile::open(path, error);
    if (!f) return false;
    if (f->size() > uint64_t(SIZE_MAX / 2)) {
        setError(error, path + ": too large to read at once");
        return false;
    }
    out.resize(size_t(f->size()));
    if (!f->read(0, out.data(), out.size())) {
        setError(error, path + ": read failed");
        out.clear();
        return false;
    }
    return true;
}

bool writeFileAtomic(const std::string& path, const void* data, size_t size, std::string* error) {
    const std::string tmp = path + ".tmp";
    std::FILE* f = openFile(tmp, "wb");
    if (!f) {
        setError(error, tmp + ": " + std::strerror(errno));
        return false;
    }
    const bool wrote = (size == 0 || std::fwrite(data, 1, size, f) == size);
    const bool closed = std::fclose(f) == 0;
    if (!wrote || !closed) {
        setError(error, tmp + ": write failed");
        std::remove(tmp.c_str());
        return false;
    }
#ifdef _WIN32
    if (!MoveFileExW(detail::widen(tmp).c_str(), detail::widen(path).c_str(), MOVEFILE_REPLACE_EXISTING)) {
        setError(error, path + ": cannot replace it (error " + std::to_string(GetLastError()) + ")");
        DeleteFileW(detail::widen(tmp).c_str());
        return false;
    }
#else
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        setError(error, path + ": " + std::strerror(errno));
        std::remove(tmp.c_str());
        return false;
    }
#endif
    return true;
}

}  // namespace gd::core::platform
