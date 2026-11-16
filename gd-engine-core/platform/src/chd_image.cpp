// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// CHD disc images through libchdr (gd/core/platform/chd.h, GDUL-030).
#include "gd/core/platform/chd.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <list>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "gd/core/platform/file.h"

// libchdr's names as chd_libchdr.c compiled them (gdchd_ prefixes).
#include "chd_libchdr_names.h"
#include <libchdr/chd.h>

namespace gd::core::platform {

namespace {

constexpr uint32_t kSector = 2048;
constexpr uint32_t kCdFrame = 2352 + 96;  // a CD CHD unit: sector data, then subcode
constexpr uint32_t kCdTrackPadding = 4;   // each track's frames rounded up to 4 in the CHD
constexpr size_t kCacheBytes = 4u << 20;  // decoded hunks kept (PS2 DVD hunks: 4 KB; CD: 19 KB)
constexpr size_t kReadAheadBytes = 1u << 20;  // libchdr's compressed read-ahead window

void setError(std::string* error, const std::string& why) {
    if (error) *error = why;
}

// libchdr reads the CHD file through these (chd_open_core_file_callbacks):
// the caller's read function and a file position.
struct Source {
    uint64_t size = 0;
    uint64_t pos = 0;
    ChdImage::ReadFn read;
};

uint64_t srcSize(void* p) { return static_cast<Source*>(p)->size; }

size_t srcRead(void* dst, size_t size, size_t count, void* p) {
    auto* s = static_cast<Source*>(p);
    if (size == 0 || count == 0 || s->pos >= s->size) return 0;
    const uint64_t want = std::min<uint64_t>(uint64_t(size) * count, s->size - s->pos);
    const size_t items = size_t(want / size);
    if (items == 0) return 0;
    const size_t bytes = items * size;
    if (!s->read(s->pos, dst, bytes)) return 0;
    s->pos += bytes;
    return items;
}

int srcClose(void* p) {
    delete static_cast<Source*>(p);
    return 0;
}

int srcSeek(void* p, int64_t offset, int whence) {
    auto* s = static_cast<Source*>(p);
    int64_t base = 0;
    if (whence == SEEK_CUR) base = int64_t(s->pos);
    else if (whence == SEEK_END) base = int64_t(s->size);
    else if (whence != SEEK_SET) return -1;
    const int64_t to = base + offset;
    if (to < 0) return -1;
    s->pos = uint64_t(to);
    return 0;
}

const core_file_callbacks kCallbacks = {srcSize, srcRead, srcClose, srcSeek};

// Where a CD track's 2048 user bytes sit in its frames, by the track type
// chdman writes; -1 for audio (and anything else that holds no data).
int cdDataOffset(const char* type) {
    if (!std::strcmp(type, "MODE1") || !std::strcmp(type, "MODE2_FORM1")) return 0;  // 2048 cooked
    if (!std::strcmp(type, "MODE1_RAW")) return 16;                                   // sync + header
    if (!std::strcmp(type, "MODE2_RAW") || !std::strcmp(type, "MODE2_FORM_MIX")) return 24;  // + XA subheader
    if (!std::strcmp(type, "MODE2")) return 8;  // 2336: the XA subheader first
    return -1;
}

}  // namespace

bool isChdSignature(const void* bytes, size_t n) {
    return n >= kChdSignatureBytes && std::memcmp(bytes, "MComprHD", kChdSignatureBytes) == 0;
}

struct ChdImage::Impl {
    chd_file* chd = nullptr;
    uint32_t hunkBytes = 0;
    uint32_t totalHunks = 0;
    uint64_t size = 0;
    bool cd = false;
    // CD: the data track's first frame (after a stored pregap) and where
    // its 2048 user bytes are in a frame.
    uint64_t cdFirstFrame = 0;
    uint32_t cdDataOffset = 0;
    uint32_t framesPerHunk = 0;

    std::mutex mutex;
    size_t cacheMax = 8;
    std::list<uint32_t> lru;  // most recent first
    struct Hunk {
        std::vector<uint8_t> bytes;
        std::list<uint32_t>::iterator at;
    };
    std::unordered_map<uint32_t, Hunk> cache;

    ~Impl() {
        if (chd) chd_close(chd);
    }

    // The decoded hunk (cached); nullptr if it does not decode.
    const uint8_t* hunk(uint32_t n) {
        auto it = cache.find(n);
        if (it != cache.end()) {
            lru.splice(lru.begin(), lru, it->second.at);
            return it->second.bytes.data();
        }
        if (n >= totalHunks) return nullptr;
        std::vector<uint8_t> bytes;
        if (cache.size() >= cacheMax) {  // reuse the oldest one's buffer
            const uint32_t old = lru.back();
            lru.pop_back();
            auto o = cache.find(old);
            bytes.swap(o->second.bytes);
            cache.erase(o);
        }
        bytes.resize(hunkBytes);
        if (chd_read(chd, n, bytes.data()) != CHDERR_NONE) return nullptr;
        lru.push_front(n);
        Hunk& h = cache[n];
        h.bytes.swap(bytes);
        h.at = lru.begin();
        return h.bytes.data();
    }

    bool readDvd(uint64_t offset, uint8_t* dst, size_t n) {
        while (n > 0) {
            const uint32_t h = uint32_t(offset / hunkBytes);
            const uint32_t in = uint32_t(offset % hunkBytes);
            const size_t take = std::min<size_t>(n, hunkBytes - in);
            const uint8_t* src = hunk(h);
            if (!src) return false;
            std::memcpy(dst, src + in, take);
            dst += take, offset += take, n -= take;
        }
        return true;
    }

    bool readCd(uint64_t offset, uint8_t* dst, size_t n) {
        while (n > 0) {
            const uint64_t frame = cdFirstFrame + offset / kSector;
            const uint32_t in = uint32_t(offset % kSector);
            const size_t take = std::min<size_t>(n, kSector - in);
            const uint8_t* src = hunk(uint32_t(frame / framesPerHunk));
            if (!src) return false;
            std::memcpy(dst, src + size_t(frame % framesPerHunk) * kCdFrame + cdDataOffset + in, take);
            dst += take, offset += take, n -= take;
        }
        return true;
    }
};

std::unique_ptr<ChdImage> ChdImage::open(uint64_t fileSize, ReadFn read, std::string* error) {
    auto* src = new Source{fileSize, 0, std::move(read)};
    chd_file* chd = nullptr;
    // The source is libchdr's from here: it closes (deletes) it, on failure too.
    const chd_error err = chd_open_core_file_callbacks(&kCallbacks, src, CHD_OPEN_READ, nullptr, &chd);
    if (err != CHDERR_NONE) {
        setError(error, std::string("CHD: ") + chd_error_string(err) +
                            (err == CHDERR_REQUIRES_PARENT ? " (a child CHD: merge it with its parent first, chdman copy)" : ""));
        return nullptr;
    }
    auto impl = std::make_unique<Impl>();
    impl->chd = chd;
    const chd_header* h = chd_get_header(chd);
    impl->hunkBytes = h->hunkbytes;
    impl->totalHunks = h->totalhunks;
    if (impl->hunkBytes == 0) {
        setError(error, "CHD: no hunks");
        return nullptr;
    }

    // A CD: its track list (CHT2, or the older CHTR) names the data track.
    char meta[256];
    bool sawTrack = false, found = false;
    uint64_t frameAt = 0;
    for (uint32_t i = 0; !found; ++i) {
        uint32_t len = 0;
        int track = 0, frames = 0, pregap = 0, postgap = 0;
        char type[32] = {}, subtype[32] = {}, pgtype[32] = {}, pgsub[32] = {};
        std::memset(meta, 0, sizeof(meta));
        if (chd_get_metadata(chd, CDROM_TRACK_METADATA2_TAG, i, meta, sizeof(meta) - 1, &len, nullptr, nullptr) == CHDERR_NONE) {
            if (std::sscanf(meta, CDROM_TRACK_METADATA2_FORMAT, &track, type, subtype, &frames, &pregap, pgtype, pgsub, &postgap) < 4)
                break;
        } else if (chd_get_metadata(chd, CDROM_TRACK_METADATA_TAG, i, meta, sizeof(meta) - 1, &len, nullptr, nullptr) == CHDERR_NONE) {
            if (std::sscanf(meta, CDROM_TRACK_METADATA_FORMAT, &track, type, subtype, &frames) < 4) break;
        } else {
            break;
        }
        sawTrack = true;
        // A pregap stored in the CHD ("V..." types) comes before the track's own frames.
        const uint64_t stored = pgtype[0] == 'V' ? uint64_t(std::max(pregap, 0)) : 0;
        const int at = cdDataOffset(type);
        if (at >= 0 && frames > 0 && uint64_t(frames) > stored) {
            impl->cd = true;
            impl->cdFirstFrame = frameAt + stored;
            impl->cdDataOffset = uint32_t(at);
            impl->size = (uint64_t(frames) - stored) * kSector;
            found = true;
        }
        frameAt += (uint64_t(std::max(frames, 0)) + kCdTrackPadding - 1) / kCdTrackPadding * kCdTrackPadding;
    }
    if (sawTrack && !found) {
        setError(error, "CHD: a CD image without a data track");
        return nullptr;
    }
    if (impl->cd) {
        if (impl->hunkBytes % kCdFrame != 0) {
            setError(error, "CHD: a CD image with an unexpected hunk size");
            return nullptr;
        }
        impl->framesPerHunk = impl->hunkBytes / kCdFrame;
        const uint64_t lastFrame = impl->cdFirstFrame + impl->size / kSector;
        if (lastFrame > uint64_t(impl->totalHunks) * impl->framesPerHunk) {
            setError(error, "CHD: the CD's track list runs past its data");
            return nullptr;
        }
    } else {
        // A DVD (and a raw or hard-disk image): the logical bytes as they are.
        impl->size = std::min<uint64_t>(h->logicalbytes, uint64_t(impl->totalHunks) * impl->hunkBytes);
    }
    impl->cacheMax = std::max<size_t>(8, kCacheBytes / impl->hunkBytes);
    chd_set_cache_budget(chd, kReadAheadBytes);  // fine without it if it fails
    return std::unique_ptr<ChdImage>(new ChdImage(std::move(impl)));
}

ChdImage::ChdImage(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
ChdImage::~ChdImage() = default;

uint64_t ChdImage::size() const { return impl_->size; }

const char* ChdImage::kind() const { return impl_->cd ? "cd" : "dvd"; }

bool ChdImage::read(uint64_t offset, void* dst, size_t n) {
    if (offset > impl_->size || n > impl_->size - offset) return false;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto* out = static_cast<uint8_t*>(dst);
    return impl_->cd ? impl_->readCd(offset, out, n) : impl_->readDvd(offset, out, n);
}

namespace {

class ChdFile final : public RandomAccessFile {
public:
    ChdFile(std::shared_ptr<RandomAccessFile> raw, std::unique_ptr<ChdImage> image)
        : raw_(std::move(raw)), image_(std::move(image)) {}
    uint64_t size() const override { return image_->size(); }
    bool read(uint64_t offset, void* dst, size_t n) override { return image_->read(offset, dst, n); }

private:
    std::shared_ptr<RandomAccessFile> raw_;
    std::unique_ptr<ChdImage> image_;
};

}  // namespace

std::unique_ptr<RandomAccessFile> openChd(std::shared_ptr<RandomAccessFile> raw, std::string* error) {
    if (!raw) {
        setError(error, "CHD: no file");
        return nullptr;
    }
    RandomAccessFile* f = raw.get();
    auto image = ChdImage::open(
        f->size(), [f](uint64_t offset, void* dst, size_t n) { return f->read(offset, dst, n); }, error);
    if (!image) return nullptr;
    return std::make_unique<ChdFile>(std::move(raw), std::move(image));
}

}  // namespace gd::core::platform
