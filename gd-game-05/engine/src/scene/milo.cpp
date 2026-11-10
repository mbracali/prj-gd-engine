// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "scene/milo.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <stdexcept>

#include "data/game_data.h"
#include "scene/inflate.h"

namespace scene {

namespace {

constexpr uint32_t kMagicDeflate = 0xCBBEDEAF;  // the only kind in GH2's data
constexpr uint32_t kMagicStored = 0xCABEDEAF;   // same table, blocks stored as is
constexpr uint32_t kDirVersion = 25;
const uint8_t kMarker[4] = {0xAD, 0xDE, 0xAD, 0xDE};

// The directory stream and records are in the file's payload order (most RB
// files big-endian, some little-endian; milo_order); the container header
// (magic, block table) is always little-endian (docs/formats.md "Milo").
uint32_t be32(const uint8_t* p) { return miloU32(p); }  // the payload order of the file being parsed
uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

struct Header {
    std::string cls, name;
    uint32_t stringCount = 0, stringSize = 0;
    std::vector<std::pair<std::string, std::string>> entries;
};

bool classLike(const std::string& s) {
    if (s.size() < 2 || s.size() > 41 || !std::isalpha(static_cast<unsigned char>(s[0]))) return false;
    return std::all_of(s.begin(), s.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
}

// Directory header (ObjectDir stream v25) at `pos`; returns the position
// after the object table. Throws on malformed data.
size_t readHeader(const Bytes& d, size_t pos, Header& h) {
    ByteReader r(d.data(), d.size());
    r.seek(pos);
    uint32_t v = r.u32m();
    if (v != kDirVersion) throw std::runtime_error("directory version " + std::to_string(v) + ", expected 25");
    auto str = [&](size_t limit) {
        uint32_t n = r.u32m();
        if (n > limit || n > r.remaining()) throw std::runtime_error("bad string in directory header");
        return r.str(n);
    };
    h.cls = str(256);
    h.name = str(1024);
    h.stringCount = r.u32m();
    h.stringSize = r.u32m();
    uint32_t n = r.u32m();
    if (n > 100000) throw std::runtime_error("implausible object count");
    h.entries.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        std::string c = str(256);
        std::string nm = str(1024);
        h.entries.emplace_back(std::move(c), std::move(nm));
    }
    return r.pos();
}

// A complete directory header that starts at q and whose table ends by `end`
// (the first end marker): how inline sub-directories are recognised.
bool inlineHeaderAt(const Bytes& d, size_t q, size_t end) {
    if (q + 8 > end) return false;
    uint32_t n = be32(&d[q + 4]);
    if (n < 3 || n > 40 || q + 8 + n > end) return false;
    if (!classLike(std::string(reinterpret_cast<const char*>(&d[q + 8]), n))) return false;
    try {
        Header h;
        size_t after = readHeader(d, q, h);
        if (after > end) return false;
        return std::all_of(h.entries.begin(), h.entries.end(), [](const auto& e) { return classLike(e.first); });
    } catch (const std::exception&) {
        return false;
    }
}

size_t findMarker(const Bytes& d, size_t from) {
    auto it = std::search(d.begin() + std::ptrdiff_t(from), d.end(), kMarker, kMarker + 4);
    return it == d.end() ? std::string::npos : size_t(it - d.begin());
}

class Parser {
public:
    explicit Parser(const Bytes& d) : d_(d) {}

    std::unique_ptr<Directory> dir(size_t& pos, Directory* parent, int depth) {
        if (depth > 8) throw std::runtime_error("inline directories nested too deep");
        Header h;
        pos = readHeader(d_, pos, h);
        auto dir = std::make_unique<Directory>();
        dir->cls = h.cls;
        dir->name = h.name;
        dir->stringCount = h.stringCount;
        dir->stringSize = h.stringSize;
        dir->parent = parent;
        dir->self = object(pos, h.cls, h.name, *dir, depth, true);
        for (const auto& [cls, name] : h.entries) dir->objects.push_back(object(pos, cls, name, *dir, depth, false));
        return dir;
    }

private:
    // One record from `pos` through its end marker. Inline sub-directories
    // found inside it are parsed and attached to `owner`.
    std::unique_ptr<Object> object(size_t& pos, const std::string& cls, const std::string& name, Directory& owner,
                                   int depth, bool isSelf) {
        size_t start = pos;
        size_t end;
        if (isDirClass(cls)) {
            // Directory classes are decoded field by field, which also tells
            // where an inline proxied directory sits.
            auto inlineDir = [&](size_t p) {
                auto d = dir(p, &owner, depth + 1);
                owner.subdirs.emplace_back(name, std::move(d));
                return p;
            };
            std::unique_ptr<Object> o;
            try {
                o = decodeDir(cls, name, d_, start, isSelf, inlineDir, end);
                o->record.assign(d_.begin() + std::ptrdiff_t(start), d_.begin() + std::ptrdiff_t(end));
            } catch (const std::exception& e) {
                // Keep going with the marker split so one bad record does not
                // lose the file; the object stays undecoded.
                end = findMarker(d_, start);
                if (end == std::string::npos) throw std::runtime_error(cls + " " + name + ": no end marker");
                o = std::make_unique<Object>();
                o->cls = cls;
                o->name = name;
                o->typed = true;
                o->error = e.what();
                o->record.assign(d_.begin() + std::ptrdiff_t(start), d_.begin() + std::ptrdiff_t(end));
                if (o->record.size() >= 4) o->version = be32(o->record.data());
            }
            o->dir = &owner;
            pos = end + 4;
            attachFollowing(pos, name, owner, depth);
            return o;
        }
        // Rock Band writes an object's inline sub-directory after the
        // object's own end marker (attached below), not inside its record
        // [C data: every file parses to its end; agents/scratch m1 split.py].
        end = findMarker(d_, pos);
        if (end == std::string::npos) throw std::runtime_error(cls + " " + name + ": no end marker");
        std::unique_ptr<Object> o;
        if (isSelf) {
            o = std::make_unique<Object>();
            o->cls = cls;
            o->name = name;
            o->record.assign(d_.begin() + std::ptrdiff_t(start), d_.begin() + std::ptrdiff_t(end));
            if (o->record.size() >= 4) o->version = be32(o->record.data());
        } else {
            o = decodeObject(cls, name, d_.data() + start, end - start);
        }
        o->dir = &owner;
        pos = end + 4;
        attachFollowing(pos, name, owner, depth);
        return o;
    }

    // The inline directories that follow an object's end marker belong to it.
    void attachFollowing(size_t& pos, const std::string& name, Directory& owner, int depth) {
        while (pos + 8 <= d_.size() && be32(&d_[pos]) == kDirVersion && inlineHeaderAt(d_, pos, d_.size())) {
            auto d = dir(pos, &owner, depth + 1);
            owner.subdirs.emplace_back(name, std::move(d));
        }
    }

    const Bytes& d_;
};

}  // namespace

Object* Directory::get(const std::string& name) const {
    if (index_.empty() && !objects.empty())
        for (const auto& o : objects) index_.emplace(o->name, o.get());
    auto it = index_.find(name);
    return it == index_.end() ? nullptr : it->second;
}

Object* Directory::find(const std::string& name) const {
    if (Object* o = get(name)) return o;
    for (const auto& [owner, d] : subdirs)
        if (Object* o = d->find(name)) return o;
    for (const Directory* d : linked)
        if (Object* o = d != this ? d->get(name) : nullptr) return o;
    return nullptr;
}

Object* Directory::resolve(const std::string& name) const {
    for (const Directory* d = this; d; d = d->parent)
        if (Object* o = d->find(name)) return o;
    return nullptr;
}

std::vector<Object*> Directory::byClass(const std::string& cls) const {
    std::vector<Object*> v;
    forEach([&](Object& o) {
        if (o.cls == cls) v.push_back(&o);
    });
    return v;
}

Bytes decompressMilo(const Bytes& file, MiloInfo* info) {
    MiloInfo local;
    MiloInfo& in = info ? *info : local;
    if (file.size() < 16) throw std::runtime_error("milo file too small");
    in.magic = le32(file.data());
    if (in.magic != kMagicDeflate && in.magic != kMagicStored) {
        in.streamSize = file.size();
        return file;
    }
    in.headerSize = le32(&file[4]);
    in.blocks = le32(&file[8]);
    in.largestBlock = le32(&file[12]);
    if (in.blocks > 100000 || 16 + size_t(in.blocks) * 4 > file.size() || in.headerSize > file.size())
        throw std::runtime_error("bad milo block table");
    Bytes out;
    size_t pos = in.headerSize;
    for (uint32_t i = 0; i < in.blocks; ++i) {
        uint32_t n = le32(&file[16 + size_t(i) * 4]);
        if (n > file.size() - pos) throw std::runtime_error("milo block runs past the file");
        if (in.magic == kMagicDeflate) {
            Bytes b = inflateRaw(file.data() + pos, n);
            out.insert(out.end(), b.begin(), b.end());
        } else {
            out.insert(out.end(), file.begin() + std::ptrdiff_t(pos), file.begin() + std::ptrdiff_t(pos + n));
        }
        pos += n;
    }
    if (pos != file.size()) throw std::runtime_error("milo blocks end before the file does");
    in.streamSize = out.size();
    return out;
}

Milo::Milo(const Bytes& file, std::string path) : path_(std::move(path)) {
    Bytes stream = decompressMilo(file, &info_);
    // The payload order: version 25 read big-endian means a big-endian file.
    bigEndian_ = stream.size() >= 4 && stream[0] == 0 && stream[1] == 0 && stream[2] == 0 && stream[3] == kDirVersion;
    milo_order::Scope order(bigEndian_);
    size_t pos = 0;
    root_ = Parser(stream).dir(pos, nullptr, 0);
    if (pos != stream.size())
        throw std::runtime_error(std::to_string(stream.size() - pos) + " bytes after the last object");
}

std::string miloArkPath(const std::string& path) {
    std::string p = path;
    std::transform(p.begin(), p.end(), p.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (p.size() >= 10 && p.compare(p.size() - 10, 10, ".milo_xbox") == 0) return p;
    if (p.size() >= 5 && p.compare(p.size() - 5, 5, ".milo") == 0) {
        size_t slash = p.rfind('/');
        std::string dir = slash == std::string::npos ? "" : p.substr(0, slash + 1);
        std::string file = slash == std::string::npos ? p : p.substr(slash + 1);
        return dir + "gen/" + file + "_xbox";
    }
    return p;
}

std::string resolveMiloRelative(const std::string& fromArkPath, const std::string& rel) {
    std::vector<std::string> parts;
    auto push = [&](const std::string& path) {
        size_t i = 0;
        while (i <= path.size()) {
            size_t j = path.find('/', i);
            if (j == std::string::npos) j = path.size();
            std::string seg = path.substr(i, j - i);
            if (seg == "..") {
                if (!parts.empty()) parts.pop_back();
            } else if (!seg.empty() && seg != ".") {
                parts.push_back(seg);
            }
            i = j + 1;
        }
    };
    size_t slash = fromArkPath.rfind('/');
    std::string base = slash != std::string::npos ? fromArkPath.substr(0, slash) : std::string();
    // RB's paths are relative to the milo's source folder, the parent of the
    // gen/ folder it is compiled into [C data: every proxy on the disc
    // resolves this way] (GH2's data wrote "../" for it).
    if (base == "gen") base.clear();
    else if (base.size() > 4 && base.compare(base.size() - 4, 4, "/gen") == 0) base.resize(base.size() - 4);
    if (!base.empty()) push(base);
    push(rel);
    std::string out;
    for (const auto& p : parts) out += (out.empty() ? "" : "/") + p;
    return miloArkPath(out);
}

std::string findMiloRelative(const GameData& data, const std::string& fromArkPath, const std::string& rel) {
    std::string p = resolveMiloRelative(fromArkPath, rel);
    if (data.exists(p)) return p;
    std::string q = p;
    for (size_t i = q.find("/og/"); i != std::string::npos; i = q.find("/og/", i + 4)) q.replace(i, 4, "/ng/");
    if (q.rfind("og/", 0) == 0) q.replace(0, 3, "ng/");
    return data.exists(q) ? q : p;
}

namespace {
// A UI milo's inline proxied directories (seldiff_solo.milo's slot0..3:
// PanelDir records naming seldiffpart_player.milo) carry a cached copy of
// the proxied file as it was when the panel was saved; the disc ships the
// file too, newer: the slots' cache lacks the file's seldiff.grp,
// selpart.grp, bass.grp and border.mesh that seldiff.dta shows and hides.
// The file's objects replace the cache [I: RB's ObjectDir inline_proxy
// rule (PreLoad RB 0x8247FB78) is not read; the scripts need the file's].
void useProxyFiles(const GameData& data, const std::string& path, Directory& dir, int depth) {
    if (depth > 4) return;
    for (auto& [owner, sub] : dir.subdirs) {
        if (!sub) continue;
        useProxyFiles(data, path, *sub, depth + 1);
        const auto* d = sub->self ? sub->self->as<DirObj>() : nullptr;
        if (!d || d->objectDir.proxyFile.empty()) continue;
        const std::string file = findMiloRelative(data, path, d->objectDir.proxyFile);
        if (!data.exists(miloArkPath(file))) continue;
        std::unique_ptr<Directory> fresh;
        try {
            Milo m(data.read(miloArkPath(file)), miloArkPath(file));
            fresh = m.releaseRoot();
        } catch (const std::exception&) {
            continue;
        }
        if (!fresh || fresh->cls != sub->cls) continue;
        sub->objects = std::move(fresh->objects);
        for (auto& o : sub->objects) o->dir = sub.get();
        for (auto& [n, ss] : fresh->subdirs) {
            if (ss) ss->parent = sub.get();
            sub->subdirs.emplace_back(n, std::move(ss));
        }
        sub->clearIndex();
        if (SDL_getenv("GD05_UILOG")) SDL_Log("milo %s: inline %s from %s (%llu objects)", path.c_str(), sub->name.c_str(), file.c_str(), static_cast<unsigned long long>(sub->objects.size()));
    }
}
}  // namespace

Milo& MiloLibrary::load(const std::string& path) {
    std::string p = miloArkPath(path);
    auto it = milos_.find(p);
    if (it != milos_.end()) return *it->second;
    auto m = loadMilo(data_, p);
    if (p.rfind("ui/", 0) == 0) useProxyFiles(data_, p, m->root(), 0);
    Milo& ref = *m;
    milos_.emplace(p, std::move(m));
    return ref;
}

Milo* MiloLibrary::proxy(const Milo& owner, const Object& dirObject) {
    const auto* d = dirObject.as<DirObj>();
    if (!d || d->own || d->objectDir.proxyFile.empty()) return nullptr;
    if (d->objectDir.inlineProxy == 1 || d->objectDir.inlineProxy == 2) return nullptr;  // stored inline
    return &load(findMiloRelative(data_, owner.path(), d->objectDir.proxyFile));
}

std::unique_ptr<Milo> loadMilo(const GameData& data, const std::string& arkPath) {
    std::string p = miloArkPath(arkPath);
    try {
        return std::make_unique<Milo>(data.read(p), p);
    } catch (const std::exception& e) {
        throw std::runtime_error(p + ": " + e.what());
    }
}

}  // namespace scene
