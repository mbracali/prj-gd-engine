// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/game_data.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Case-insensitive child lookup, since extracted discs keep upper-case names.
std::optional<fs::path> findChild(const fs::path& dir, const std::string& name) {
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (lower(e.path().filename().string()) == lower(name)) return e.path();
    }
    return std::nullopt;
}

std::string partName(size_t i) { return "MAIN_" + std::to_string(i) + ".ARK"; }

}  // namespace

GameData::GameData(const std::string& path) {
    std::error_code ec;
    Bytes hdr;
    size_t partCount = 0;

    if (fs::is_directory(path, ec)) {
        auto gen = findChild(path, "GEN");
        auto hdrPath = gen ? findChild(*gen, "MAIN.HDR") : std::nullopt;
        if (!hdrPath) throw std::runtime_error("No GEN/MAIN.HDR found in folder: " + path);
        auto f = RandomFile::open(hdrPath->string());
        if (!f) throw std::runtime_error("Cannot open " + hdrPath->string());
        hdr.resize(f->size());
        f->read(0, hdr.data(), hdr.size());
        ByteReader r(hdr);
        r.skip(4);
        partCount = r.u32le();
        for (size_t i = 0; i < partCount; ++i) {
            auto p = findChild(*gen, partName(i));
            auto pf = p ? RandomFile::open(p->string()) : nullptr;
            if (!pf) throw std::runtime_error("Missing archive part GEN/" + partName(i));
            parts_.push_back({pf.get(), 0, pf->size()});
            files_.push_back(std::move(pf));
        }
        description_ = "folder " + path;
    } else {
        auto image = RandomFile::open(path);
        if (!image) throw std::runtime_error("Cannot open " + path);
        auto iso = Iso9660::open(*image);
        if (!iso) throw std::runtime_error("Not a folder or an ISO 9660 disc image: " + path);
        auto hdrExtent = iso->find("GEN/MAIN.HDR");
        if (!hdrExtent) throw std::runtime_error("Disc image has no GEN/MAIN.HDR: " + path);
        hdr.resize(hdrExtent->size);
        image->read(hdrExtent->offset, hdr.data(), hdr.size());
        ByteReader r(hdr);
        r.skip(4);
        partCount = r.u32le();
        for (size_t i = 0; i < partCount; ++i) {
            auto e = iso->find("GEN/" + partName(i));
            if (!e) throw std::runtime_error("Disc image is missing GEN/" + partName(i));
            parts_.push_back({image.get(), e->offset, e->size});
        }
        files_.push_back(std::move(image));
        description_ = "disc image " + path;
    }
    loadHeader(hdr);
}

// Layout recovered from the game (see FINDINGS.md, "Data archive").
void GameData::loadHeader(const Bytes& hdr) {
    ByteReader r(hdr);
    if (r.u32le() != 3) throw std::runtime_error("Unsupported ARK version (expected 3)");
    r.u32le();  // part count, repeated below as the size list length
    uint32_t sizeCount = r.u32le();
    for (uint32_t i = 0; i < sizeCount; ++i) r.u32le();
    uint32_t tableSize = r.u32le();
    const char* table = reinterpret_cast<const char*>(r.ptr());
    r.skip(tableSize);
    std::vector<uint32_t> offsets(r.u32le());
    for (auto& o : offsets) o = r.u32le();
    auto name = [&](uint32_t idx) {
        if (idx >= offsets.size() || offsets[idx] >= tableSize) throw std::runtime_error("Corrupt ARK string index");
        return std::string(table + offsets[idx]);
    };
    uint32_t count = r.u32le();
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t offset = r.u32le(), nameIdx = r.u32le(), dirIdx = r.u32le(), size = r.u32le();
        r.u32le();
        entries_[lower(name(dirIdx) + "/" + name(nameIdx))] = {offset, size};
    }
}

bool GameData::exists(const std::string& arkPath) const { return entries_.count(lower(arkPath)) != 0; }

Bytes GameData::read(const std::string& arkPath) const {
    auto it = entries_.find(lower(arkPath));
    if (it == entries_.end()) throw std::runtime_error("File not in archive: " + arkPath);
    Bytes out(it->second.size);
    uint64_t offset = it->second.offset;
    size_t done = 0;
    for (const Part& p : parts_) {
        if (done == out.size()) break;
        if (offset >= p.size) {
            offset -= p.size;
            continue;
        }
        size_t n = static_cast<size_t>(std::min<uint64_t>(p.size - offset, out.size() - done));
        p.file->read(p.base + offset, out.data() + done, n);
        done += n;
        offset = 0;
    }
    if (done != out.size()) throw std::runtime_error("Archive entry runs past the end: " + arkPath);
    return out;
}

size_t GameData::readRange(const std::string& arkPath, uint64_t offset, uint8_t* dst, size_t n) const {
    auto it = entries_.find(lower(arkPath));
    if (it == entries_.end() || offset >= it->second.size) return 0;
    n = static_cast<size_t>(std::min<uint64_t>(n, it->second.size - offset));
    uint64_t at = it->second.offset + offset;
    size_t done = 0;
    for (const Part& p : parts_) {
        if (done == n) break;
        if (at >= p.size) {
            at -= p.size;
            continue;
        }
        size_t k = static_cast<size_t>(std::min<uint64_t>(p.size - at, n - done));
        p.file->read(p.base + at, dst + done, k);
        done += k;
        at = 0;
    }
    return done;
}

std::vector<std::string> GameData::paths() const {
    std::vector<std::string> out;
    out.reserve(entries_.size());
    for (const auto& [path, entry] : entries_) out.push_back(path);
    return out;
}
