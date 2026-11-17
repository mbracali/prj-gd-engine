// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/umd.h"

#include <cstring>

#include "gd/core/platform/file.h"

namespace gd24 {
namespace {

constexpr uint64_t kSector = 2048;

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

}  // namespace

std::string upperPath(std::string s) {
    for (char& c : s) {
        if (c >= 'a' && c <= 'z') c = char(c - 32);
        if (c == '\\') c = '/';
    }
    while (!s.empty() && s.front() == '/') s.erase(s.begin());
    return s;
}

Umd::~Umd() = default;

std::unique_ptr<Umd> Umd::open(const std::string& imagePath, std::string* error) {
    std::unique_ptr<Umd> u(new Umd());
    u->file_ = gd::core::platform::RandomAccessFile::open(imagePath, error);
    if (!u->file_) return nullptr;
    uint8_t pvd[kSector];
    if (!u->file_->read(16 * kSector, pvd, sizeof pvd) || pvd[0] != 1 || std::memcmp(&pvd[1], "CD001", 5) != 0) {
        if (error) *error = "not an ISO 9660 disc image (no primary volume descriptor at sector 16)";
        return nullptr;
    }
    u->volumeId_ = trim(std::string(reinterpret_cast<const char*>(&pvd[40]), 32));
    const uint8_t* root = &pvd[156];
    if (!u->walk(le32(root + 2), le32(root + 10), "", 0, error)) return nullptr;
    std::vector<uint8_t> umd;
    if (u->read("UMD_DATA.BIN", umd)) {
        std::string text(umd.begin(), umd.end());
        u->discId_ = trim(text.substr(0, text.find('|')));
    }
    return u;
}

bool Umd::walk(uint32_t lba, uint32_t size, const std::string& prefix, int depth, std::string* error) {
    if (depth > 16 || size > (16u << 20)) {
        if (error) *error = "ISO 9660 directory too deep or too large";
        return false;
    }
    std::vector<uint8_t> dir(size);
    if (!file_->read(uint64_t(lba) * kSector, dir.data(), dir.size())) {
        if (error) *error = "cannot read the directory at sector " + std::to_string(lba);
        return false;
    }
    for (size_t i = 0; i < dir.size();) {
        const uint8_t len = dir[i];
        if (len == 0) {  // records never cross a sector: on to the next one
            i = (i / kSector + 1) * kSector;
            continue;
        }
        if (len < 34 || i + len > dir.size()) break;
        const uint8_t nameLen = dir[i + 32];
        const uint8_t flags = dir[i + 25];
        std::string name(reinterpret_cast<const char*>(&dir[i + 33]), std::min<size_t>(nameLen, len - 33u));
        const uint32_t elba = le32(&dir[i + 2]), esize = le32(&dir[i + 10]);
        i += len;
        if (nameLen == 1 && (name[0] == 0 || name[0] == 1)) continue;  // "." and ".."
        if (size_t semi = name.find(';'); semi != std::string::npos) name.erase(semi);
        const std::string path = prefix + name;
        if (flags & 2) {
            if (!walk(elba, esize, path + "/", depth + 1, error)) return false;
        } else {
            index_[upperPath(path)] = files_.size();
            files_.push_back({path, elba, esize});
        }
    }
    return true;
}

const Umd::Entry* Umd::find(const std::string& path) const {
    auto it = index_.find(upperPath(path));
    return it == index_.end() ? nullptr : &files_[it->second];
}

bool Umd::readAt(const Entry& e, uint64_t offset, void* dst, size_t n) const {
    if (offset + n > e.size) return false;
    return file_->read(uint64_t(e.lba) * kSector + offset, dst, n);
}

bool Umd::read(const std::string& path, std::vector<uint8_t>& out) const {
    const Entry* e = find(path);
    if (!e) return false;
    out.resize(e->size);
    return e->size == 0 || readAt(*e, 0, out.data(), out.size());
}

std::vector<const Umd::Entry*> Umd::list(const std::string& prefix) const {
    const std::string p = upperPath(prefix);
    std::vector<const Entry*> out;
    for (const Entry& e : files_)
        if (upperPath(e.path).rfind(p, 0) == 0) out.push_back(&e);
    return out;
}

}  // namespace gd24
