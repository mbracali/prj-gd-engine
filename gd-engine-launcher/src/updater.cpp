// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Update from inside the launcher (GDUL-031): see updater.h.
#include "updater.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "platform.h"

#ifdef __ANDROID__
#include <jni.h>
#endif

namespace fs = std::filesystem;

namespace update {

namespace {

constexpr const char* kApiLatest = "https://api.github.com/repos/mbracali/prj-gd-engine/releases/latest";
constexpr const char* kApiList = "https://api.github.com/repos/mbracali/prj-gd-engine/releases?per_page=20";
constexpr const char* kReleasesPage = "https://github.com/mbracali/prj-gd-engine/releases/latest";
constexpr const char* kTagPrefix = "gdul/v";
// The new launcher counts as started when it says so (the state file), or,
// for a version that does not know the state file, when it still runs after this long.
constexpr double kAliveSeconds = 15;

fs::path P(const std::string& s) { return fs::u8path(s); }
std::string U(const fs::path& p) { return p.u8string(); }

// ---- A small JSON reader (GitHub's release JSON, the package manifest) ----

struct Json {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::vector<std::pair<std::string, Json>> o;
    const Json* get(const char* key) const {
        for (const auto& [k, v] : o)
            if (k == key) return &v;
        return nullptr;
    }
    std::string str(const char* key) const {
        const Json* v = get(key);
        return v && v->type == Str ? v->s : "";
    }
    bool flag(const char* key) const {
        const Json* v = get(key);
        return v && v->type == Bool && v->b;
    }
};

class JsonReader {
public:
    explicit JsonReader(const std::string& t) : t_(t) {}
    bool parse(Json& out) {
        if (!value(out, 0)) return false;
        ws();
        return i_ == t_.size();
    }

private:
    const std::string& t_;
    size_t i_ = 0;
    void ws() {
        while (i_ < t_.size() && (t_[i_] == ' ' || t_[i_] == '\n' || t_[i_] == '\r' || t_[i_] == '\t')) ++i_;
    }
    bool lit(const char* w) {
        size_t n = std::strlen(w);
        if (t_.compare(i_, n, w) != 0) return false;
        i_ += n;
        return true;
    }
    static void utf8(std::string& s, unsigned cp) {
        if (cp < 0x80) s += char(cp);
        else if (cp < 0x800) s += char(0xC0 | (cp >> 6)), s += char(0x80 | (cp & 0x3F));
        else if (cp < 0x10000) s += char(0xE0 | (cp >> 12)), s += char(0x80 | ((cp >> 6) & 0x3F)), s += char(0x80 | (cp & 0x3F));
        else
            s += char(0xF0 | (cp >> 18)), s += char(0x80 | ((cp >> 12) & 0x3F)), s += char(0x80 | ((cp >> 6) & 0x3F)),
                s += char(0x80 | (cp & 0x3F));
    }
    bool hex4(unsigned& v) {
        if (i_ + 4 > t_.size()) return false;
        v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = t_[i_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= unsigned(c - '0');
            else if (c >= 'a' && c <= 'f') v |= unsigned(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= unsigned(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool string(std::string& s) {
        if (i_ >= t_.size() || t_[i_] != '"') return false;
        ++i_;
        while (i_ < t_.size()) {
            char c = t_[i_++];
            if (c == '"') return true;
            if (c != '\\') {
                s += c;
                continue;
            }
            if (i_ >= t_.size()) return false;
            char e = t_[i_++];
            switch (e) {
            case 'n': s += '\n'; break;
            case 'r': s += '\r'; break;
            case 't': s += '\t'; break;
            case 'b': s += '\b'; break;
            case 'f': s += '\f'; break;
            case 'u': {
                unsigned cp = 0;
                if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp < 0xDC00 && i_ + 6 <= t_.size() && t_[i_] == '\\' && t_[i_ + 1] == 'u') {
                    i_ += 2;
                    unsigned lo = 0;
                    if (!hex4(lo)) return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                utf8(s, cp);
                break;
            }
            default: s += e; break;  // \" \\ \/
            }
        }
        return false;
    }
    bool value(Json& v, int depth) {
        if (depth > 64) return false;
        ws();
        if (i_ >= t_.size()) return false;
        char c = t_[i_];
        if (c == '{') {
            v.type = Json::Obj;
            ++i_;
            ws();
            if (i_ < t_.size() && t_[i_] == '}') return ++i_, true;
            for (;;) {
                ws();
                std::string k;
                if (!string(k)) return false;
                ws();
                if (i_ >= t_.size() || t_[i_++] != ':') return false;
                Json item;
                if (!value(item, depth + 1)) return false;
                v.o.emplace_back(std::move(k), std::move(item));
                ws();
                if (i_ >= t_.size()) return false;
                if (t_[i_] == ',') ++i_;
                else if (t_[i_] == '}') return ++i_, true;
                else return false;
            }
        }
        if (c == '[') {
            v.type = Json::Arr;
            ++i_;
            ws();
            if (i_ < t_.size() && t_[i_] == ']') return ++i_, true;
            for (;;) {
                Json item;
                if (!value(item, depth + 1)) return false;
                v.a.push_back(std::move(item));
                ws();
                if (i_ >= t_.size()) return false;
                if (t_[i_] == ',') ++i_;
                else if (t_[i_] == ']') return ++i_, true;
                else return false;
            }
        }
        if (c == '"') return v.type = Json::Str, string(v.s);
        if (lit("true")) return v.type = Json::Bool, v.b = true, true;
        if (lit("false")) return v.type = Json::Bool, true;
        if (lit("null")) return true;
        size_t start = i_;
        while (i_ < t_.size() && (std::isdigit(static_cast<unsigned char>(t_[i_])) || std::strchr("+-.eE", t_[i_]))) ++i_;
        if (i_ == start) return false;
        v.type = Json::Num;
        v.n = std::strtod(t_.substr(start, i_ - start).c_str(), nullptr);
        return true;
    }
};

bool releaseFrom(const Json& r, Release& out) {
    if (r.type != Json::Obj) return false;
    out = Release{};
    out.tag = r.str("tag_name");
    out.htmlUrl = r.str("html_url");
    out.draft = r.flag("draft");
    out.prerelease = r.flag("prerelease");
    out.highlights = highlights(r.str("body"));
    if (out.tag.rfind(kTagPrefix, 0) == 0) out.version = out.tag.substr(std::strlen(kTagPrefix));
    if (const Json* as = r.get("assets"); as && as->type == Json::Arr)
        for (const Json& a : as->a) {
            Asset x;
            x.name = a.str("name");
            x.url = a.str("browser_download_url");
            if (const Json* sz = a.get("size"); sz && sz->type == Json::Num) x.size = (long long)sz->n;
            if (!x.name.empty() && !x.url.empty()) out.assets.push_back(x);
        }
    return true;
}

bool usable(const Release& r) {
    int v[3];
    return !r.draft && !r.prerelease && r.tag.rfind(kTagPrefix, 0) == 0 && parseVersion(r.version, v);
}

// ---- SHA-256 (FIPS 180-4) ----

struct Sha256 {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t buf[64];
    size_t used = 0;
    uint64_t bits = 0;
    static uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void block(const uint8_t* p) {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
            0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
            0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
            0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
            0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
            0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = uint32_t(p[4 * i]) << 24 | uint32_t(p[4 * i + 1]) << 16 | uint32_t(p[4 * i + 2]) << 8 | uint32_t(p[4 * i + 3]);
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t t1 = hh + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
        }
        h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += hh;
    }
    void add(const void* data, size_t n) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        bits += uint64_t(n) * 8;
        while (n) {
            size_t take = std::min(n, 64 - used);
            std::memcpy(buf + used, p, take);
            used += take, p += take, n -= take;
            if (used == 64) block(buf), used = 0;
        }
    }
    std::string hex() {
        uint64_t total = bits;
        uint8_t pad = 0x80;
        add(&pad, 1);
        uint8_t zero = 0;
        while (used != 56) add(&zero, 1);
        uint8_t len[8];
        for (int i = 0; i < 8; ++i) len[i] = uint8_t(total >> (56 - 8 * i));
        add(len, 8);
        char out[65];
        for (int i = 0; i < 8; ++i) std::snprintf(out + 8 * i, 9, "%08x", h[i]);
        return std::string(out, 64);
    }
};

std::string readFile(const std::string& path) {
    std::ifstream f(P(path), std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

bool writeFile(const std::string& path, const std::string& text) {
    std::ofstream f(P(path), std::ios::binary | std::ios::trunc);
    f << text;
    return bool(f);
}

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}

// key=value lines.
std::string field(const std::string& text, const std::string& key) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line))
        if (line.rfind(key + "=", 0) == 0) return trim(line.substr(key.size() + 1));
    return "";
}

std::string firstLine(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    std::getline(in, line);
    return trim(line);
}

bool skippedPart(const fs::path& rel) {
    for (const fs::path& part : rel)
        if (part == "original_isos" || part == ".gdul-update") return true;
    return false;
}

}  // namespace

// ---- Versions, assets, release JSON ----

bool parseVersion(const std::string& s0, int out[3]) {
    std::string s = s0;
    if (s.rfind(kTagPrefix, 0) == 0) s = s.substr(std::strlen(kTagPrefix));
    if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s = s.substr(1);
    int v[3] = {0, 0, 0};
    size_t i = 0;
    for (int k = 0; k < 3; ++k) {
        if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) return false;
        long n = 0;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])) && n < 100000) n = n * 10 + (s[i++] - '0');
        v[k] = int(n);
        if (k < 2) {
            if (i >= s.size() || s[i] != '.') return false;
            ++i;
        }
    }
    if (i < s.size() && s[i] != '-' && s[i] != '+') return false;
    out[0] = v[0], out[1] = v[1], out[2] = v[2];
    return true;
}

int compareVersions(const std::string& a, const std::string& b) {
    int x[3], y[3];
    const bool okA = parseVersion(a, x), okB = parseVersion(b, y);
    if (!okA || !okB) return okA ? 1 : okB ? -1 : 0;
    for (int k = 0; k < 3; ++k)
        if (x[k] != y[k]) return x[k] < y[k] ? -1 : 1;
    return 0;
}

const char* kindName(Kind k) {
    switch (k) {
    case Kind::Windows: return "windows";
    case Kind::Portable: return "portable linux";
    case Kind::SteamDeck: return "steam deck";
    case Kind::MacOS: return "macos";
    case Kind::AppImage: return "appimage";
    case Kind::Android: return "android";
    default: return "none";
    }
}

std::vector<std::string> assetNames(Kind k, const std::string& v) {
    const std::string p = "gdul-" + v + "-";
    switch (k) {
    case Kind::Windows: return {p + "windows-x86_64.zip"};
    case Kind::Portable: return {p + "portable-x86_64.tar.gz", p + "linux-x86_64.tar.gz"};
    case Kind::SteamDeck: return {p + "steamdeck-x86_64.tar.gz"};
    case Kind::MacOS: return {p + "macos-arm64.zip"};
    case Kind::AppImage: return {p + "linux-x86_64.AppImage"};
    case Kind::Android: return {p + "android-arm64.apk"};
    default: return {};
    }
}

const Asset* Release::find(const std::string& name) const {
    for (const Asset& a : assets)
        if (a.name == name) return &a;
    return nullptr;
}

bool parseRelease(const std::string& json, Release& out, std::string& error) {
    Json root;
    if (!JsonReader(json).parse(root)) return error = "not JSON", false;
    if (root.type == Json::Arr) {  // the release list: the newest usable gdul release
        for (const Json& r : root.a) {
            Release x;
            if (releaseFrom(r, x) && usable(x)) return out = x, true;
        }
        return error = "no gdul release in the list", false;
    }
    if (!releaseFrom(root, out)) return error = "not a release", false;
    if (out.tag.empty()) return error = root.str("message").empty() ? "no tag_name" : root.str("message"), false;
    return true;
}

std::string highlights(const std::string& body, int maxLines) {
    std::istringstream in(body);
    std::string line, out;
    bool inside = false;
    int n = 0;
    while (std::getline(in, line)) {
        line = trim(line);
        if (!inside) {
            std::string bare;
            for (char c : line)
                if (std::isalpha(static_cast<unsigned char>(c))) bare += char(std::tolower(static_cast<unsigned char>(c)));
            inside = bare == "highlights";
            continue;
        }
        if (line.empty() && out.empty()) continue;
        if (line.rfind("- ", 0) != 0 && line.rfind("* ", 0) != 0) break;
        if (n == maxLines) break;
        std::string item = line.substr(2);
        if (item.size() > 240) {
            size_t cut = 237;
            while (cut > 0 && (static_cast<unsigned char>(item[cut]) & 0xC0) == 0x80) --cut;
            item = item.substr(0, cut) + "...";
        }
        out += (out.empty() ? "" : "\n") + std::string("- ") + item;
        ++n;
    }
    return out;
}

// ---- Checksums ----

std::string sha256Hex(const void* data, size_t size) {
    Sha256 s;
    s.add(data, size);
    return s.hex();
}

bool sha256File(const std::string& path, std::string& hex) {
    std::ifstream f(P(path), std::ios::binary);
    if (!f) return false;
    Sha256 s;
    std::vector<char> buf(1 << 20);
    while (f) {
        f.read(buf.data(), std::streamsize(buf.size()));
        if (f.gcount() > 0) s.add(buf.data(), size_t(f.gcount()));
    }
    hex = s.hex();
    return true;
}

std::string expectedSum(const std::string& sums, const std::string& name) {
    std::istringstream in(sums);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.size() < 66) continue;
        std::string hex = line.substr(0, 64), rest = trim(line.substr(64));
        if (!rest.empty() && rest[0] == '*') rest = rest.substr(1);  // binary mode mark
        if (rest != name) continue;
        for (char& c : hex) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (hex.find_first_not_of("0123456789abcdef") == std::string::npos) return hex;
    }
    return "";
}

// ---- The package and the swap ----

bool checkPackage(const std::string& root, const std::string& exe, std::string& error) {
    std::error_code ec;
    const fs::path r = P(root);
    if (!fs::is_regular_file(r / P(exe), ec) || fs::file_size(r / P(exe), ec) == 0)
        return error = "the package has no " + exe, false;
    fs::path games = r / "gd-engine-games";
    if (fs::is_directory(r / "Contents" / "Resources", ec)) games = r / "Contents" / "Resources" / "gd-engine-games";
    const std::string manifest = readFile(U(games / "manifest.json"));
    Json m;
    if (manifest.empty() || !JsonReader(manifest).parse(m) || m.type != Json::Obj)
        return error = "the package has no gd-engine-games/manifest.json", false;
    const Json* list = m.get("games");
    if (!list || list->type != Json::Obj || list->o.empty()) return error = "the package's manifest lists no game", false;
    for (const auto& [id, g] : list->o) {
        const std::string mod = g.str("module");
        const fs::path f = games / P(id) / P(mod);
        if (mod.empty() || !fs::is_regular_file(f, ec) || fs::file_size(f, ec) == 0)
            return error = "the package lacks " + id + "'s module (" + (mod.empty() ? "none listed" : mod) + ")", false;
    }
    return true;
}

bool rollback(const std::string& journal, std::string& error) {
    const std::string text = readFile(journal);
    if (text.empty()) return error = "no journal", false;
    const fs::path src = P(field(text, "src")), dst = P(field(text, "dst")), backup = P(field(text, "backup"));
    std::vector<std::string> steps;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line))
        if (line.size() > 2 && line[1] == ' ' && (line[0] == 'B' || line[0] == 'N' || line[0] == 'D')) steps.push_back(line);
    bool ok = true;
    std::error_code ec;
    for (auto it = steps.rbegin(); it != steps.rend(); ++it) {
        const fs::path rel = P(it->substr(2));
        ec.clear();
        if ((*it)[0] == 'N') {  // the new file goes back where it came from
            fs::create_directories((src / rel).parent_path(), ec);
            fs::rename(dst / rel, src / rel, ec);
            if (ec) fs::remove(dst / rel, ec);
        } else if ((*it)[0] == 'B') {  // the old file comes back
            fs::rename(backup / rel, dst / rel, ec);
        } else {
            fs::remove(dst / rel, ec);  // a folder the update made: only when empty
            ec.clear();
        }
        if (ec) {
            ok = false;
            error = "cannot put back " + U(rel) + ": " + ec.message();
        }
    }
    return ok;
}

bool swapIn(const std::string& srcS, const std::string& dstS, const std::string& backupS, const std::string& journalS,
            std::string& error) {
    const fs::path src = P(srcS), dst = P(dstS), backup = P(backupS);
    std::error_code ec;
    std::vector<fs::path> files;
    for (fs::recursive_directory_iterator it(src, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path rel = it->path().lexically_relative(src);
        if (skippedPart(rel)) {
            if (it->is_directory(ec) && !it->is_symlink(ec)) it.disable_recursion_pending();
            continue;
        }
        if (it->is_symlink(ec) || !it->is_directory(ec)) files.push_back(rel);
    }
    if (ec) return error = "cannot read the new files: " + ec.message(), false;
    if (files.empty()) return error = "the package is empty", false;

    std::ofstream journal(P(journalS), std::ios::binary | std::ios::trunc);
    if (!journal) return error = "cannot write " + journalS, false;
    journal << "src=" << U(src) << "\ndst=" << U(dst) << "\nbackup=" << U(backup) << "\n";
    journal.flush();
    auto fail = [&](const std::string& why) {
        journal.close();
        std::string undo;
        if (!rollback(journalS, undo)) error = why + " (and putting the old files back: " + undo + ")";
        else error = why;
        return false;
    };
    for (const fs::path& rel : files) {
        // The folders on the way, made when missing (and removed again on a rollback).
        fs::path part;
        for (const fs::path& p : rel.parent_path()) {
            part /= p;
            ec.clear();
            const fs::file_status st = fs::symlink_status(dst / part, ec);
            if (fs::exists(st)) {
                if (!fs::is_directory(fs::status(dst / part, ec)))
                    return fail(U(part) + " is in the way (a file where the update has a folder)");
                continue;
            }
            if (!fs::create_directory(dst / part, ec) || ec) return fail("cannot make " + U(dst / part) + ": " + ec.message());
            journal << "D " << part.generic_u8string() << "\n";
            journal.flush();
        }
        ec.clear();
        const fs::file_status st = fs::symlink_status(dst / rel, ec);
        if (fs::exists(st)) {
            if (fs::is_directory(st)) return fail(U(rel) + " is in the way (a folder where the update has a file)");
            fs::create_directories((backup / rel).parent_path(), ec);
            ec.clear();
            fs::rename(dst / rel, backup / rel, ec);
            if (ec) return fail("cannot move " + U(rel) + " aside: " + ec.message());
            journal << "B " << rel.generic_u8string() << "\n";
            journal.flush();
        }
        fs::rename(src / rel, dst / rel, ec);
        if (ec) return fail("cannot move the new " + U(rel) + " in: " + ec.message());
        journal << "N " << rel.generic_u8string() << "\n";
        journal.flush();
    }
    journal << "done\n";
    return true;
}

// ---- Self-test (gdul --update-selftest) ----

int selfTest() {
    int failed = 0;
    auto check = [&](bool ok, const std::string& what) {
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
        if (!ok) ++failed;
    };
    // Versions.
    check(compareVersions("gdul/v1.1.16", "1.1.15") > 0, "1.1.16 is newer than 1.1.15");
    check(compareVersions("1.1.9", "1.1.10") < 0, "1.1.9 is older than 1.1.10 (numbers, not text)");
    check(compareVersions("v1.2.0", "1.1.99") > 0, "1.2.0 is newer than 1.1.99");
    check(compareVersions("1.1.16", "gdul/v1.1.16") == 0, "1.1.16 equals gdul/v1.1.16");
    check(compareVersions("junk", "0.0.1") < 0, "an unparsable version is the oldest");
    int v[3];
    check(!parseVersion("1.1", v) && !parseVersion("1.1.x", v) && parseVersion("2.0.1-rc1", v), "version parsing");
    // Asset names.
    check(assetNames(Kind::SteamDeck, "1.1.16")[0] == "gdul-1.1.16-steamdeck-x86_64.tar.gz", "Steam Deck takes the steamdeck tar.gz");
    check(assetNames(Kind::Portable, "1.1.16")[0] == "gdul-1.1.16-portable-x86_64.tar.gz" &&
              assetNames(Kind::Portable, "1.1.15")[1] == "gdul-1.1.15-linux-x86_64.tar.gz",
          "portable takes portable-x86_64.tar.gz (linux-x86_64.tar.gz read for old releases)");
    check(assetNames(Kind::Windows, "1.1.16")[0] == "gdul-1.1.16-windows-x86_64.zip", "Windows takes the zip");
    check(assetNames(Kind::MacOS, "1.1.16")[0] == "gdul-1.1.16-macos-arm64.zip", "macOS takes the zip");
    check(assetNames(Kind::Android, "1.1.16")[0] == "gdul-1.1.16-android-arm64.apk", "Android takes the APK");
    // Release JSON and highlights.
    const std::string json =
        "{\"tag_name\":\"gdul/v1.1.16\",\"html_url\":\"https://x/r\",\"draft\":false,\"prerelease\":false,"
        "\"body\":\"## Title\\r\\n\\r\\n**Highlights**\\r\\n- One \\u00e9.\\r\\n- Two.\\r\\n\\r\\n**Details**\\r\\n- No.\","
        "\"assets\":[{\"name\":\"gdul-1.1.16-steamdeck-x86_64.tar.gz\",\"size\":81211609,\"browser_download_url\":\"https://x/d\"},"
        "{\"name\":\"SHA256SUMS.txt\",\"size\":782,\"browser_download_url\":\"https://x/s\"}]}";
    Release r;
    std::string err;
    check(parseRelease(json, r, err) && r.version == "1.1.16" && r.assets.size() == 2 &&
              r.find("gdul-1.1.16-steamdeck-x86_64.tar.gz") && r.find("gdul-1.1.16-steamdeck-x86_64.tar.gz")->size == 81211609,
          "release JSON: tag, version, assets");
    check(r.highlights == "- One \xC3\xA9.\n- Two.", "highlights: the list under **Highlights** only");
    Release l;
    check(parseRelease("[{\"tag_name\":\"gd01/v1.0.0\"},{\"tag_name\":\"gdul/v1.2.0\",\"draft\":true},{\"tag_name\":\"gdul/v1.1.17\"}]", l, err) &&
              l.version == "1.1.17",
          "release list: the newest non-draft gdul release");
    check(!parseRelease("{\"message\":\"API rate limit exceeded\"}", l, err) && err == "API rate limit exceeded", "an API error is an error");
    // SHA-256.
    check(sha256Hex("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 of abc");
    check(sha256Hex("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256 of nothing");
    const std::string big(1000000, 'a');
    check(sha256Hex(big.data(), big.size()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "SHA-256 of a million a");
    const std::string sums = "1111111111111111111111111111111111111111111111111111111111111111  gdul-1.1.16-windows-x86_64.zip\n"
                             "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD *gdul-1.1.16-steamdeck-x86_64.tar.gz\n";
    check(expectedSum(sums, "gdul-1.1.16-steamdeck-x86_64.tar.gz") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" &&
              expectedSum(sums, "gdul-1.1.16-portable-x86_64.tar.gz").empty(),
          "SHA256SUMS.txt: the asset's line, none for another");

    // The swap, in a temporary folder.
    std::error_code ec;
    const fs::path t = fs::temp_directory_path(ec) / ("gdul-update-selftest-" + std::to_string(SDL_GetTicksNS()));
    const fs::path install = t / "install", pkg = t / "pkg", up = t / "install" / ".gdul-update";
    auto put = [](const fs::path& f, const std::string& text) {
        std::error_code e;
        fs::create_directories(f.parent_path(), e);
        writeFile(U(f), text);
    };
    put(install / "Launcher", "old exe");
    put(install / "gd-engine-games/manifest.json", "{\"games\":{\"gd-game-01\":{\"module\":\"gd-game-01.so\"}}}");
    put(install / "gd-engine-games/gd-game-01/gd-game-01.so", "old module");
    put(install / "gd-engine-games/gd-game-01/old-only.txt", "old only");
    put(install / "original_isos/My Game.iso", "player's disc");
    put(install / "mine/notes.txt", "player's file");
    put(install / "player.cfg", "player's settings");
    put(pkg / "Launcher", "new exe");
    put(pkg / "gd-engine-games/manifest.json", "{\"games\":{\"gd-game-01\":{\"module\":\"gd-game-01.so\"},\"gd-game-02\":{\"module\":\"gd-game-02.so\"}}}");
    put(pkg / "gd-engine-games/gd-game-01/gd-game-01.so", "new module 1");
    put(pkg / "gd-engine-games/gd-game-02/gd-game-02.so", "new module 2");
    put(pkg / "original_isos/README.md", "new readme");
    put(pkg / "original_isos/My Game.iso", "must never land");
    check(checkPackage(U(pkg), "Launcher", err), "package check: the program and every module of the manifest");
    fs::remove(pkg / "gd-engine-games/gd-game-02/gd-game-02.so", ec);
    const bool caught = !checkPackage(U(pkg), "Launcher", err);
    check(caught && err.find("gd-game-02") != std::string::npos, "package check: a missing module fails (" + err + ")");
    put(pkg / "gd-engine-games/gd-game-02/gd-game-02.so", "new module 2");
    fs::create_directories(up, ec);
    const std::string journal = U(up / "journal.txt");
    const bool swapped = swapIn(U(pkg), U(install), U(up / "previous-1.0.0"), journal, err);
    check(swapped, swapped ? "swap: done" : "swap: " + err);
    check(readFile(U(install / "Launcher")) == "new exe" && readFile(U(install / "gd-engine-games/gd-game-02/gd-game-02.so")) == "new module 2" &&
              readFile(U(install / "gd-engine-games/gd-game-01/gd-game-01.so")) == "new module 1",
          "swap: the program and the modules are the new ones (gd-engine-games carried over)");
    check(readFile(U(install / "original_isos/My Game.iso")) == "player's disc" && !fs::exists(install / "original_isos/README.md") &&
              readFile(U(install / "mine/notes.txt")) == "player's file" && readFile(U(install / "player.cfg")) == "player's settings" &&
              readFile(U(install / "gd-engine-games/gd-game-01/old-only.txt")) == "old only",
          "swap: original_isos and the player's files untouched, nothing deleted");
    check(readFile(U(up / "previous-1.0.0/Launcher")) == "old exe" &&
              readFile(U(up / "previous-1.0.0/gd-engine-games/gd-game-01/gd-game-01.so")) == "old module",
          "swap: the old files kept in .gdul-update/previous-<version>/");
    const bool back = rollback(journal, err);
    check(back, back ? "rollback: done" : "rollback: " + err);
    check(readFile(U(install / "Launcher")) == "old exe" && readFile(U(install / "gd-engine-games/gd-game-01/gd-game-01.so")) == "old module" &&
              !fs::exists(install / "gd-engine-games/gd-game-02") && readFile(U(install / "original_isos/My Game.iso")) == "player's disc" &&
              readFile(U(install / "mine/notes.txt")) == "player's file",
          "rollback: the old version back as it was, the player's files untouched");
    // A swap that fails half-way undoes itself: a folder where the package has a file.
    fs::remove_all(up / "previous-1.0.0", ec);
    put(install / "zzz/dummy", "player's");
    put(pkg / "zzz", "a file where the install has a folder");
    err.clear();
    const bool refused = !swapIn(U(pkg), U(install), U(up / "previous-1.0.0"), journal, err);
    check(refused && readFile(U(install / "Launcher")) == "old exe" &&
              readFile(U(install / "zzz/dummy")) == "player's",
          "a failed swap puts everything back (" + err + ")");
    fs::remove_all(t, ec);
    std::printf("%s: %d failed\n", failed ? "update self-test FAILED" : "update self-test passed", failed);
    return failed ? 1 : 0;
}

// ---- Updater ----

void Updater::init(const std::string& launcherVersion) {
    version_ = launcherVersion;
    // Development aid: GDUL_UPDATE_AS_VERSION=<x.y.z> checks as if this were that version.
    if (const char* as = SDL_getenv("GDUL_UPDATE_AS_VERSION"); as && *as) version_ = as;
    const char* rl = SDL_getenv("RECOMPLAUNCHER_MODE");
    recomp_ = rl && *rl;
    const std::string settings = readFile(platform::join(platform::saveDir(), "update.txt"));
    checkAtStart_ = field(settings, "check_at_start") != "0";
    skip_ = field(settings, "skip");

#if defined(__ANDROID__)
    kind_ = Kind::Android;
#elif defined(_WIN32)
    kind_ = Kind::Windows;
    exeRel_ = "Guitar Dream Universal Launcher.exe";
#elif defined(__APPLE__)
    kind_ = Kind::MacOS;
    exeRel_ = "Contents/MacOS/Guitar Dream Universal Launcher";
#else
    const std::string base = platform::baseDir();
    const char* appImage = SDL_getenv("APPIMAGE");
    const char* appDir = SDL_getenv("APPDIR");
    if (appImage && *appImage && appDir && *appDir && base.rfind(appDir, 0) == 0) kind_ = Kind::AppImage;
#if defined(GDUL_STEAM_DECK)
    else kind_ = Kind::SteamDeck;
#else
    else kind_ = Kind::Portable;
#endif
    exeRel_ = "Guitar Dream Universal Launcher";
#endif

    std::error_code ec;
    if (kind_ == Kind::Windows || kind_ == Kind::Portable || kind_ == Kind::SteamDeck) {
        installRoot_ = platform::baseDir();
    } else if (kind_ == Kind::MacOS) {
        installRoot_ = platform::appBundle();
        if (installRoot_.empty()) whyNot_ = "the launcher does not run from its .app";
        else if (platform::isTranslocated(installRoot_)) whyNot_ = "macOS runs the app from a read-only copy (move it out of Downloads first)";
    } else if (kind_ == Kind::AppImage) {
        whyNot_ = "the AppImage updates with AppImageUpdate, AppManager or Gear Lever";
    } else if (kind_ == Kind::Android) {
        whyNot_ = "Android installs the new APK itself";
    }
    if (!installRoot_.empty() && whyNot_.empty()) {
        const fs::path root = P(installRoot_);
        const fs::path manifest = kind_ == Kind::MacOS ? root / "Contents/Resources/gd-engine-games/manifest.json"
                                                       : root / "gd-engine-games/manifest.json";
        if (!fs::is_regular_file(root / P(exeRel_), ec) || !fs::is_regular_file(manifest, ec)) {
            whyNot_ = "this is not a release download (a development build)";
        } else {
            // Writable? (Program Files, a read-only folder: no.)
            const fs::path probe = P(updateDir()).parent_path().parent_path() / ".gdul-write-test";
            if (!writeFile(U(probe), "ok")) whyNot_ = "the launcher's folder is read-only (" + installRoot_ + ")";
            fs::remove(probe, ec);
        }
    }
    selfUpdate_ = whyNot_.empty() && !installRoot_.empty();
    SDL_Log("gdul: update: %s build, %s", kindName(kind_),
            recomp_ ? "RecompLauncher updates it" : selfUpdate_ ? "updates itself" : whyNot_.c_str());
}

std::string Updater::updateDir() const {
    if (kind_ == Kind::MacOS) return platform::join(platform::parentDir(installRoot_), ".gdul-update") + "/";
    return platform::join(installRoot_, ".gdul-update") + "/";
}

std::string Updater::statePath() const { return updateDir() + "state.txt"; }

std::string Updater::releasesPage() const {
    std::lock_guard<std::mutex> g(lock_);
    return release_.htmlUrl.empty() ? kReleasesPage : release_.htmlUrl;
}

void Updater::saveSettings() {
    writeFile(platform::join(platform::saveDir(), "update.txt"),
              "# Guitar Dream Universal Launcher updates\ncheck_at_start=" + std::string(checkAtStart_ ? "1" : "0") + "\nskip=" + skip_ + "\n");
}

void Updater::setCheckAtStart(bool on) {
    checkAtStart_ = on;
    saveSettings();
}

void Updater::skip(const std::string& v) {
    skip_ = v;
    saveSettings();
}

Release Updater::release() const {
    std::lock_guard<std::mutex> g(lock_);
    return release_;
}

std::string Updater::error() const {
    std::lock_guard<std::mutex> g(lock_);
    return error_;
}

std::string Updater::assetName() const {
    std::lock_guard<std::mutex> g(lock_);
    return asset_;
}

void Updater::progress(long long& done, long long& total) const {
    total = total_.load();
    std::string path;
    {
        std::lock_guard<std::mutex> g(lock_);
        path = downloadPath_;
    }
    std::error_code ec;
    done = path.empty() ? 0 : (long long)fs::file_size(P(path), ec);
    if (ec) done = 0;
}

void Updater::fail(const std::string& why) {
    {
        std::lock_guard<std::mutex> g(lock_);
        error_ = why;
    }
    SDL_Log("gdul: update: %s", why.c_str());
    state_ = State::Failed;
}

void Updater::startCheck(bool manual) {
    const State s = state_.load();
    if (recomp_ || s == State::Checking || s == State::Downloading || s == State::Applying) return;
    if (thread_) SDL_WaitThread(thread_, nullptr), thread_ = nullptr;
    manual_ = manual;
    cancel_ = false;
    job_ = 1;
    state_ = State::Checking;
    thread_ = SDL_CreateThread(&Updater::threadMain, "gdul-update", this);
    if (!thread_) fail(std::string("no thread: ") + SDL_GetError());
}

void Updater::startDownload() {
    if (state_.load() != State::Available || !selfUpdate_) return;
    if (thread_) SDL_WaitThread(thread_, nullptr), thread_ = nullptr;
    cancel_ = false;
    job_ = 2;
    state_ = State::Downloading;
    thread_ = SDL_CreateThread(&Updater::threadMain, "gdul-update", this);
    if (!thread_) fail(std::string("no thread: ") + SDL_GetError());
}

void Updater::cancel() { cancel_ = true; }

void Updater::shutdown() {
    cancel_ = true;
    if (thread_) SDL_WaitThread(thread_, nullptr), thread_ = nullptr;
    if (child_) SDL_DestroyProcess(child_), child_ = nullptr;  // the new launcher keeps running
}

int Updater::threadMain(void* self) {
    auto* u = static_cast<Updater*>(self);
    if (u->job_ == 1) u->runCheck();
    else u->runDownload();
    return 0;
}

// A helper program (curl, tar, ditto), without a console window on Windows.
bool Updater::run(const std::vector<std::string>& args, std::string& error) {
    std::vector<const char*> argv;
    for (const std::string& a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
#ifdef _WIN32
    // No console window flashing up (its exit code is then always 0: the
    // results are checked by what they leave on disk).
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
#endif
    SDL_Process* p = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (!p) return error = args[0] + " did not start: " + SDL_GetError(), false;
    int code = 0;
    while (!SDL_WaitProcess(p, false, &code)) {
        if (cancel_) {
            SDL_KillProcess(p, true);
            SDL_WaitProcess(p, true, &code);
            SDL_DestroyProcess(p);
            return error = "cancelled", false;
        }
        SDL_Delay(50);
    }
    SDL_DestroyProcess(p);
    if (code != 0) return error = platform::fileName(args[0]) + " failed (exit " + std::to_string(code) + ")", false;
    return true;
}

namespace {
#ifdef _WIN32
std::string system32(const char* exe) {
    const char* root = SDL_getenv("SystemRoot");
    return std::string(root && *root ? root : "C:\\Windows") + "\\System32\\" + exe;
}
#endif
std::string curlPath() {
#if defined(_WIN32)
    return system32("curl.exe");  // Windows 10 1803 and later
#elif defined(__APPLE__)
    return "/usr/bin/curl";
#else
    return "curl";
#endif
}
}  // namespace

bool Updater::httpGet(const std::string& url, std::string& body, std::string& error) {
    const std::string ua = "gdul/" + std::string(GDUL_VERSION);
#ifdef __ANDROID__
    JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) return error = "no Java", false;
    jclass cls = env->GetObjectClass(activity);
    env->DeleteLocalRef(activity);
    jmethodID m = env->GetStaticMethodID(cls, "httpGet", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    if (!m) {
        env->ExceptionClear();
        env->DeleteLocalRef(cls);
        return error = "the launcher's Java side is missing (GdulActivity.httpGet)", false;
    }
    jstring ju = env->NewStringUTF(url.c_str()), ja = env->NewStringUTF(ua.c_str());
    auto r = static_cast<jstring>(env->CallStaticObjectMethod(cls, m, ju, ja));
    if (env->ExceptionCheck()) env->ExceptionClear(), r = nullptr;
    env->DeleteLocalRef(ju), env->DeleteLocalRef(ja), env->DeleteLocalRef(cls);
    if (!r) return error = "no answer from GitHub (no network?)", false;
    const char* c = env->GetStringUTFChars(r, nullptr);
    body = c ? c : "";
    if (c) env->ReleaseStringUTFChars(r, c);
    env->DeleteLocalRef(r);
    if (body.rfind("\x01", 0) == 0) return error = body.substr(1), false;
    return true;
#else
    const std::string tmp = platform::join(platform::saveDir(), "update-answer.tmp");
    std::error_code ec;
    fs::remove(P(tmp), ec);
    if (!run({curlPath(), "-fsSL", "--max-time", "30", "--connect-timeout", "10", "-A", ua, "-H", "Accept: application/vnd.github+json",
              "-o", tmp, url},
             error))
        return false;
    body = readFile(tmp);
    fs::remove(P(tmp), ec);
    if (body.empty()) return error = "no answer from GitHub (no network?)", false;
    return true;
#endif
}

void Updater::runCheck() {
    std::string body, err;
    Release r;
    if (!httpGet(kApiLatest, body, err)) return fail("check: " + err);
    if (!parseRelease(body, r, err) || !usable(r)) {
        // "latest" is another kind of release, or not readable: the list.
        if (!httpGet(kApiList, body, err)) return fail("check: " + err);
        if (!parseRelease(body, r, err)) return fail("check: " + err);
    }
    std::string asset;
    for (const std::string& name : assetNames(kind_, r.version))
        if (r.find(name)) {
            asset = name;
            break;
        }
    {
        std::lock_guard<std::mutex> g(lock_);
        release_ = r;
        asset_ = asset;
        error_.clear();
    }
    const bool newer = compareVersions(r.version, version_) > 0;
    SDL_Log("gdul: update: latest is %s, this is %s%s", r.version.c_str(), version_.c_str(),
            newer ? (asset.empty() ? " (newer, but no download for this build)" : " (newer)") : "");
    if (newer && !asset.empty() && (manual_ || !skipped(r.version))) state_ = State::Available;
    else state_ = State::UpToDate;
}

void Updater::runDownload() {
    Release r;
    std::string asset;
    {
        std::lock_guard<std::mutex> g(lock_);
        r = release_;
        asset = asset_;
    }
    const Asset* a = r.find(asset);
    const Asset* sumsAsset = r.find("SHA256SUMS.txt");
    if (!a) return fail("the release has no " + asset);
    if (!sumsAsset) return fail("the release has no SHA256SUMS.txt: not updating without it");
    std::error_code ec;
    const std::string dir = updateDir();
    const fs::path dl = P(dir) / "download", staging = P(dir) / "staging";
    fs::remove_all(dl, ec);
    fs::remove_all(staging, ec);
    fs::create_directories(dl, ec);
    fs::create_directories(staging, ec);
    if (ec) return fail("cannot make " + U(staging) + ": " + ec.message());

    std::string sums, err;
    if (!httpGet(sumsAsset->url, sums, err)) return fail("SHA256SUMS.txt: " + err);
    const std::string want = expectedSum(sums, asset);
    if (want.empty()) return fail("SHA256SUMS.txt has no line for " + asset);

    const std::string file = U(dl / P(asset));
    total_ = a->size;
    {
        std::lock_guard<std::mutex> g(lock_);
        downloadPath_ = file;
    }
    const std::string ua = "gdul/" + std::string(GDUL_VERSION);
    if (!run({curlPath(), "-fsSL", "--connect-timeout", "20", "--retry", "2", "-A", ua, "-o", file, a->url}, err))
        return fail(err == "cancelled" ? err : "download: " + err);
    if (cancel_) return fail("cancelled");
    std::string got;
    if (!sha256File(file, got)) return fail("download: no file (no network?)");
    if (got != want) {
        fs::remove(P(file), ec);
        return fail("the download's SHA-256 does not match SHA256SUMS.txt (" + got.substr(0, 12) + "... instead of " + want.substr(0, 12) +
                    "...): nothing was changed");
    }
    SDL_Log("gdul: update: %s downloaded, SHA-256 %s matches", asset.c_str(), got.c_str());

    // Extract next to the install (the same disk: the swap only renames).
#if defined(_WIN32)
    const std::vector<std::string> extract = {system32("tar.exe"), "-xf", file, "-C", U(staging)};
#elif defined(__APPLE__)
    const std::vector<std::string> extract = {"/usr/bin/ditto", "-x", "-k", file, U(staging)};
#else
    const std::vector<std::string> extract = {"tar", "-xzf", file, "-C", U(staging)};
#endif
    if (!run(extract, err)) return fail("extract: " + err);
    fs::remove(P(file), ec);
    // The archive has one folder (gdul-<v>-<system>/); macOS: the .app in it.
    fs::path root = staging;
    std::vector<fs::path> top;
    for (const auto& e : fs::directory_iterator(staging, ec)) top.push_back(e.path());
    if (top.size() == 1 && fs::is_directory(top[0], ec)) root = top[0];
    if (kind_ == Kind::MacOS) {
        fs::path app;
        for (const auto& e : fs::directory_iterator(root, ec))
            if (e.path().extension() == ".app") app = e.path();
        if (app.empty()) return fail("the package has no .app");
        root = app;
    }
    if (!checkPackage(U(root), exeRel_, err)) return fail(err + ": nothing was changed");
    // Its manifest says the version it is.
    fs::path games = kind_ == Kind::MacOS ? root / "Contents/Resources/gd-engine-games" : root / "gd-engine-games";
    Json m;
    const std::string manifest = readFile(U(games / "manifest.json"));
    if (JsonReader(manifest).parse(m) && !m.str("launcher_version").empty() && m.str("launcher_version") != r.version)
        return fail("the package says it is " + m.str("launcher_version") + ", not " + r.version + ": nothing was changed");
    {
        std::lock_guard<std::mutex> g(lock_);
        packageRoot_ = U(root);
        downloadPath_.clear();
    }
    state_ = State::Ready;
}

bool Updater::apply() {
    if (state_.load() != State::Ready) return false;
    if (thread_) SDL_WaitThread(thread_, nullptr), thread_ = nullptr;
    const Release r = release();
    const std::string dir = updateDir();
    const std::string backup = dir + "previous-" + version_;
    journal_ = dir + "journal.txt";
    std::error_code ec;
    fs::remove_all(P(backup), ec);
    std::string err;
    if (!swapIn(packageRoot_, installRoot_, backup, journal_, err)) {
        fail("the swap failed, the old version is kept: " + err);
        return false;
    }
    writeFile(statePath(), "swapped\nfrom=" + version_ + "\nto=" + r.version + "\n");
    SDL_Log("gdul: update: %s -> %s swapped in, starting the new launcher", version_.c_str(), r.version.c_str());

    // The new launcher, with this environment minus the development aids.
    const std::string exe = U(P(installRoot_) / P(exeRel_));
    const char* argv[] = {exe.c_str(), nullptr};
    SDL_Environment* env = SDL_CreateEnvironment(true);
    for (const char* v : {"GDUL_SCRIPT", "GDUL_UPDATE_AS_VERSION"}) SDL_UnsetEnvironmentVariable(env, v);
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv);
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, env);
    child_ = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    SDL_DestroyEnvironment(env);
    if (!child_) {
        const std::string why = std::string("the new launcher did not start (") + SDL_GetError() + ")";
        std::string undo;
        rollback(journal_, undo);
        writeFile(statePath(), "rolledback\n");
        fail(why + "; the old version is back" + (undo.empty() ? "" : " (" + undo + ")"));
        return false;
    }
    childStartNs_ = SDL_GetTicksNS();
    state_ = State::Applying;
    return true;
}

int Updater::pollStarted() {
    if (state_.load() != State::Applying || !child_) return -1;
    if (firstLine(readFile(statePath())) == "ok") return 1;
    int code = 0;
    if (SDL_WaitProcess(child_, false, &code)) {
        SDL_DestroyProcess(child_);
        child_ = nullptr;
        std::string undo;
        const bool back = rollback(journal_, undo);
        writeFile(statePath(), "rolledback\n");
        fail("the new launcher stopped at once (exit " + std::to_string(code) + "); " +
             (back ? "the old version is back" : "putting the old version back failed: " + undo));
        return -1;
    }
    if (double(SDL_GetTicksNS() - childStartNs_) / 1e9 > kAliveSeconds) return 1;  // a version without the state file
    return 0;
}

std::string Updater::finishPending() {
    if (!selfUpdate_) return "";
    const std::string state = readFile(statePath());
    const std::string what = firstLine(state);
    std::error_code ec;
    if (what == "swapped" && field(state, "to") == version_) return field(state, "from");
    if (what == "ok" || what == "rolledback") {
        // A finished update: the previous version's files and the leftovers go.
        fs::remove_all(P(updateDir()), ec);
        SDL_Log("gdul: update: cleaned %s%s", updateDir().c_str(), ec ? (" (" + ec.message() + ")").c_str() : "");
    }
    return "";
}

void Updater::confirmStarted() {
    const std::string state = readFile(statePath());
    if (firstLine(state) == "swapped") writeFile(statePath(), "ok\nfrom=" + field(state, "from") + "\nto=" + field(state, "to") + "\n");
}

}  // namespace update
