// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/disc.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <system_error>

#include "data/inflate.h"
#include "data/pak.h"
#include "data/qbkey.h"

namespace fs = std::filesystem;

namespace {

// UTF-8 strings <-> paths on every system (Windows' narrow paths are not UTF-8).
fs::path toPath(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string fromPath(const fs::path& p) {
    std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}
std::string fromPathGeneric(const fs::path& p) {
    std::u8string u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}

bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

bool isCompressed(const std::string& path) {
    std::string p = path;
    std::transform(p.begin(), p.end(), p.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return p.rfind("data/compressed/", 0) == 0 && p.find(".lst") == std::string::npos;
}

}  // namespace

std::unique_ptr<Disc> Disc::open(const std::string& path, std::string& error) {
    auto d = std::unique_ptr<Disc>(new Disc());
    d->path_ = path;
    std::error_code ec;
    if (fs::is_directory(toPath(path), ec)) {
        d->folder_ = path;
        if (d->resolve("DATA/COMPRESSED/PAK/qb.pak.xen").empty()) {
            error = path + ": not a Guitar Hero III (Xbox 360) disc folder (no DATA/COMPRESSED/PAK/qb.pak.xen)";
            return nullptr;
        }
        return d;
    }
    try {
        d->image_ = RandomFile::open(path);
    } catch (const std::exception& e) {
        error = e.what();
        return nullptr;
    }
    if (!d->image_) {
        error = path + ": cannot open";
        return nullptr;
    }
    d->fs_ = Xdvdfs::open(*d->image_);
    if (!d->fs_) {
        error = path + ": no Xbox 360 file system in this image";
        return nullptr;
    }
    if (!d->exists("DATA/COMPRESSED/PAK/qb.pak.xen")) {
        error = path + ": not a Guitar Hero III (Xbox 360) disc (no DATA/COMPRESSED/PAK/qb.pak.xen)";
        return nullptr;
    }
    return d;
}

std::string Disc::resolve(const std::string& path) const {
    fs::path cur = toPath(folder_);
    size_t start = 0;
    while (start <= path.size()) {
        size_t slash = path.find('/', start);
        std::string part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        start = slash == std::string::npos ? path.size() + 1 : slash + 1;
        if (part.empty()) continue;
        fs::path direct = cur / toPath(part);
        std::error_code ec;
        if (fs::exists(direct, ec)) {
            cur = direct;
            continue;
        }
        bool found = false;
        for (const auto& e : fs::directory_iterator(cur, ec)) {
            if (iequals(fromPath(e.path().filename()), part)) {
                cur = e.path();
                found = true;
                break;
            }
        }
        if (!found) return "";
    }
    return fromPath(cur);
}

std::optional<uint64_t> Disc::size(const std::string& path) const {
    if (fs_) {
        auto e = fs_->find(path);
        if (!e) return std::nullopt;
        return e->size;
    }
    std::string p = resolve(path);
    if (p.empty()) return std::nullopt;
    std::error_code ec;
    auto s = fs::file_size(toPath(p), ec);
    if (ec) return std::nullopt;
    return s;
}

bool Disc::exists(const std::string& path) const { return size(path).has_value(); }

void Disc::readRange(const std::string& path, uint64_t offset, void* dst, size_t size) const {
    if (fs_) {
        auto e = fs_->find(path);
        if (!e) throw std::runtime_error("not on the disc: " + path);
        if (offset + size > e->size) throw std::runtime_error("read past the end of " + path);
        image_->read(e->offset + offset, dst, size);
        return;
    }
    std::string p = resolve(path);
    if (p.empty()) throw std::runtime_error("not on the disc: " + path);
    std::ifstream f(toPath(p), std::ios::binary);
    f.seekg(std::streamoff(offset));
    f.read(static_cast<char*>(dst), std::streamsize(size));
    if (!f) throw std::runtime_error("read failed: " + path);
}

Bytes Disc::readRaw(const std::string& path) const {
    auto s = size(path);
    if (!s) throw std::runtime_error("not on the disc: " + path);
    const size_t n = size_t(*s);
    Bytes b(n);
    if (!b.empty()) readRange(path, 0, b.data(), b.size());
    return b;
}

Bytes Disc::read(const std::string& path) const {
    Bytes raw = readRaw(path);
    if (!isCompressed(path)) return raw;
    return inflateRaw(raw.data(), raw.size());
}

std::vector<std::string> Disc::list() const {
    std::vector<std::string> out;
    if (fs_) {
        for (auto& f : fs_->list()) out.push_back(f.path);
    } else {
        std::error_code ec;
        fs::path root = toPath(folder_);
        for (auto it = fs::recursive_directory_iterator(root, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            std::string rel = fromPathGeneric(fs::relative(it->path(), root, ec));
            if (rel == "names.tsv" || rel == "README.md") continue;  // the rip's own notes
            out.push_back(rel);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

void Disc::loadNames() const {
    qbkey::Names& n = qbkey::names();
    for (const std::string& f : list()) {
        n.add(f);
        std::string p = f;
        std::replace(p.begin(), p.end(), '/', '\\');
        n.add(p);
        size_t slash = f.rfind('/');
        std::string base = slash == std::string::npos ? f : f.substr(slash + 1);
        while (true) {
            n.add(base);
            size_t dot = base.rfind('.');
            if (dot == std::string::npos || dot == 0) break;
            n.add(base.substr(dot));
            base = base.substr(0, dot);
        }
    }
    for (const char* e : {".qb", ".img", ".tex", ".scn", ".col", ".ska", ".ske", ".fnt", ".pak", ".pab", ".last", ".table",
                          ".pfx", ".hkc", ".nqb", ".mqb", ".sqb", ".dbg", ".qs", ".qs.en", ".qs.fr", ".qs.de", ".qs.it",
                          ".qs.es", ".mdl", ".skin", ".fam", ".rag", ".cif", ".dat", ".fsb"})
        n.add(e);
    // The debug paks: one text file per script file with its checksums [C: data].
    try {
        Pak dbg = Pak::load(*this, "DATA/COMPRESSED/PAK/dbg.pak.xen");
        for (const PakEntry& e : dbg.entries()) {
            auto span = dbg.data(e);
            n.addDebugText(std::string_view(reinterpret_cast<const char*>(span.first), span.second));
        }
    } catch (const std::exception&) {
        // A disc without the debug pak still plays; names then print as hex.
    }
}
