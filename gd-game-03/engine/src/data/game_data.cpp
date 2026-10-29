// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/game_data.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <cstring>
#include <optional>
#include <stdexcept>

#include "data/iso9660.h"

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Case-insensitive child lookup, since extracted copies may change the case.
std::optional<fs::path> findChild(const fs::path& dir, const std::string& name) {
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (lower(e.path().filename().string()) == lower(name)) return e.path();
    }
    return std::nullopt;
}

std::string partName(size_t i) { return "main_" + std::to_string(i) + ".ark"; }

// The archive count is the second u32 of the header.
size_t partCount(const Bytes& plainHdr) {
    ByteReader r(plainHdr);
    r.skip(4);
    return r.u32le();
}

}  // namespace

GameData::GameData(const std::string& path) {
    std::error_code ec;
    Bytes hdr;

    if (fs::is_directory(path, ec)) {
        auto gen = findChild(path, "gen");
        auto hdrPath = gen ? findChild(*gen, "main.hdr") : std::nullopt;
        if (!hdrPath) throw std::runtime_error("No gen/main.hdr found in folder: " + path);
        auto f = RandomFile::open(hdrPath->string());
        if (!f) throw std::runtime_error("Cannot open " + hdrPath->string());
        Bytes raw(f->size());
        f->read(0, raw.data(), raw.size());
        hdr = std::move(raw);
        for (size_t i = 0, n = partCount(hdr); i < n; ++i) {
            auto p = findChild(*gen, partName(i));
            auto pf = p ? RandomFile::open(p->string()) : nullptr;
            if (!pf) throw std::runtime_error("Missing archive part gen/" + partName(i));
            parts_.push_back({pf.get(), 0, pf->size()});
            files_.push_back(std::move(pf));
        }
        description_ = "folder " + path;
        folder_ = path;
    } else {
        auto image = RandomFile::open(path);
        if (!image) throw std::runtime_error("Cannot open " + path);
        auto disc = Iso9660::open(*image);
        if (!disc) throw std::runtime_error("Not a folder or a PlayStation 2 disc image: " + path);
        auto hdrExtent = disc->find("gen/main.hdr");
        if (!hdrExtent) throw std::runtime_error("Disc image has no GEN/MAIN.HDR (not Guitar Hero Encore: Rocks the 80s?): " + path);
        hdr.resize(hdrExtent->size);
        image->read(hdrExtent->offset, hdr.data(), hdr.size());
        for (size_t i = 0, n = partCount(hdr); i < n; ++i) {
            auto e = disc->find("gen/" + partName(i));
            if (!e) throw std::runtime_error("Disc image is missing gen/" + partName(i));
            parts_.push_back({image.get(), e->offset, e->size});
        }
        image_ = image.get();
        files_.push_back(std::move(image));
        description_ = "disc image " + path;
    }
    loadHeader(hdr);
}

// The index, version 3, stored plain on this disc [C data, tools/ark.py]:
// u32 version, u32 archive count, u32 size count + that many archive sizes,
// u32 string table size + the table (NUL-terminated names), u32 name count +
// offsets into the table, u32 entry count + entries of five u32: offset across
// the archive set, file name index, directory name index, size, inflated size
// (0 = stored). The same layout as GH1's (gd-game-01) and, encrypted, GH2
// Xbox 360's.
void GameData::loadHeader(const Bytes& hdr) {
    ByteReader r(hdr);
    if (r.u32le() != 3) throw std::runtime_error("Unsupported ARK version (expected 3)");
    r.u32le();  // archive count, repeated below as the size list length
    uint32_t sizeCount = r.u32le();
    for (uint32_t i = 0; i < sizeCount; ++i) r.u32le();
    uint32_t tableSize = r.u32le();
    const char* table = reinterpret_cast<const char*>(r.ptr());
    r.skip(tableSize);
    std::vector<uint32_t> offsets(r.u32le());
    for (auto& o : offsets) o = r.u32le();
    auto name = [&](uint32_t idx) {
        if (idx >= offsets.size() || offsets[idx] >= tableSize) throw std::runtime_error("Corrupt ARK string index");
        const char* s = table + offsets[idx];
        return std::string(s, strnlen(s, tableSize - offsets[idx]));
    };
    uint32_t count = r.u32le();
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t offset = r.u32le(), nameIdx = r.u32le(), dirIdx = r.u32le(), size = r.u32le(), inflated = r.u32le();
        std::string dir = name(dirIdx);
        std::string full = dir.empty() || dir == "." ? name(nameIdx) : dir + "/" + name(nameIdx);
        entries_[lower(full)] = {offset, size, inflated};
    }
}

const GameData::Entry& GameData::entry(const std::string& arkPath) const {
    auto it = entries_.find(lower(arkPath));
    if (it == entries_.end()) throw std::runtime_error("File not in archive: " + arkPath);
    return it->second;
}

bool GameData::exists(const std::string& arkPath) const { return entries_.count(lower(arkPath)) != 0; }

uint64_t GameData::fileSize(const std::string& arkPath) const { return entry(arkPath).size; }

void GameData::readSpan(uint64_t offset, uint8_t* dst, size_t n) const {
    size_t done = 0;
    for (const Part& p : parts_) {
        if (done == n) break;
        if (offset >= p.size) {
            offset -= p.size;
            continue;
        }
        size_t k = static_cast<size_t>(std::min<uint64_t>(p.size - offset, n - done));
        p.file->read(p.base + offset, dst + done, k);
        done += k;
        offset = 0;
    }
    if (done != n) throw std::runtime_error("Archive entry runs past the end of the archives");
}

Bytes GameData::read(const std::string& arkPath) const {
    const Entry& e = entry(arkPath);
    // No entry of this disc's archive is compressed (every inflated size is 0
    // [C data], all 1761 entries), so no decoder for them.
    if (e.inflated != 0) throw std::runtime_error("Compressed archive entries are not supported yet: " + arkPath);
    Bytes out(e.size);
    readSpan(e.offset, out.data(), out.size());
    return out;
}

size_t GameData::readRange(const std::string& arkPath, uint64_t offset, uint8_t* dst, size_t n) const {
    const Entry& e = entry(arkPath);
    if (offset >= e.size) return 0;
    n = static_cast<size_t>(std::min<uint64_t>(n, e.size - offset));
    readSpan(e.offset + offset, dst, n);
    return n;
}

std::vector<std::string> GameData::paths() const {
    std::vector<std::string> out;
    out.reserve(entries_.size());
    for (const auto& [path, entry] : entries_) out.push_back(path);
    return out;
}

size_t DiscFile::read(uint64_t offset, void* dst, size_t n) {
    if (offset >= size_) return 0;
    n = static_cast<size_t>(std::min<uint64_t>(n, size_ - offset));
    if (n) file_->read(base_ + offset, dst, n);
    return n;
}

std::unique_ptr<DiscFile> GameData::openDiscFile(const std::string& discPath) const {
    if (image_) {
        auto disc = Iso9660::open(*image_);
        auto e = disc ? disc->find(discPath) : std::nullopt;
        if (!e) return nullptr;
        return std::make_unique<DiscFile>(image_, nullptr, e->offset, e->size);
    }
    fs::path p = folder_;
    for (size_t at = 0; at <= discPath.size();) {
        size_t slash = discPath.find('/', at);
        if (slash == std::string::npos) slash = discPath.size();
        if (slash > at) {
            auto child = findChild(p, discPath.substr(at, slash - at));
            if (!child) return nullptr;
            p = *child;
        }
        at = slash + 1;
    }
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) return nullptr;
    auto f = RandomFile::open(p.string());
    if (!f) return nullptr;
    RandomFile* raw = f.get();
    const uint64_t size = raw->size();
    return std::make_unique<DiscFile>(raw, std::move(f), 0, size);
}
