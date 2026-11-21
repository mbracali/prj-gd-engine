// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/qs.h"

#include <SDL3/SDL_log.h>

#include <exception>
#include <unordered_map>

#include "data/disc.h"
#include "data/pak.h"
#include "data/qbkey.h"
#include "script/pc_strings.h"

namespace qstr {
namespace {

std::unordered_map<uint32_t, std::string>& table() {
    static std::unordered_map<uint32_t, std::string> t;
    return t;
}

void appendUtf8(std::string& out, uint32_t c) {
    if (c < 0x80) {
        out += char(c);
    } else if (c < 0x800) {
        out += char(0xC0 | (c >> 6));
        out += char(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += char(0xE0 | (c >> 12));
        out += char(0x80 | ((c >> 6) & 0x3F));
        out += char(0x80 | (c & 0x3F));
    } else {
        out += char(0xF0 | (c >> 18));
        out += char(0x80 | ((c >> 12) & 0x3F));
        out += char(0x80 | ((c >> 6) & 0x3F));
        out += char(0x80 | (c & 0x3F));
    }
}

int hexDigit(uint32_t c) {
    if (c >= '0' && c <= '9') return int(c - '0');
    if (c >= 'a' && c <= 'f') return int(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return int(c - 'A' + 10);
    return -1;
}

// One .qs file: UTF-16LE (BOM), lines `xxxxxxxx "text"`.
void parse(const uint8_t* d, size_t n) {
    std::u32string line;
    auto flush = [&] {
        if (line.size() >= 11) {
            uint32_t key = 0;
            bool ok = true;
            for (size_t i = 0; i < 8 && ok; ++i) {
                const int h = hexDigit(line[i]);
                ok = h >= 0;
                key = key << 4 | uint32_t(h < 0 ? 0 : h);
            }
            const size_t q0 = line.find(U'"'), q1 = line.rfind(U'"');
            if (ok && q0 != std::u32string::npos && q1 > q0) {
                std::string s;
                for (size_t i = q0 + 1; i < q1; ++i) appendUtf8(s, line[i]);
                table()[key] = pcstrings::pcQsText(s);  // PC wording (GD10-035)
            }
        }
        line.clear();
    };
    for (size_t i = 0; i + 1 < n; i += 2) {
        uint32_t c = uint32_t(d[i] | d[i + 1] << 8);
        if (c >= 0xD800 && c < 0xDC00 && i + 3 < n) {
            const uint32_t lo = uint32_t(d[i + 2] | d[i + 3] << 8);
            c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
            i += 2;
        }
        if (c == 0xFEFF) continue;
        if (c == '\n') flush();
        else if (c != '\r') line += char32_t(c);
    }
    flush();
}

}  // namespace

void load(const Disc& disc) {
    if (!table().empty()) return;
    try {
        Pak pak = Pak::load(disc, "DATA/COMPRESSED/PAK/qs.pak.xen");
        for (const PakEntry& e : pak.entries()) {
            auto s = pak.data(e);
            parse(s.first, s.second);
        }
    } catch (const std::exception& ex) {
        SDL_Log("qs: %s", ex.what());
    }
}

const std::string* find(uint32_t key) {
    auto it = table().find(key);
    return it == table().end() ? nullptr : &it->second;
}

std::string text(uint32_t key) {
    const std::string* s = find(key);
    if (!s) return {};
    if (s->size() >= 2 && (*s)[0] == '\\' && (*s)[1] == 'L') return s->substr(2);
    return *s;
}

size_t size() { return table().size(); }

void each(const std::function<void(uint32_t, const std::string&)>& fn) {
    for (const auto& [k, v] : table()) fn(k, v);
}

void resolve(qb::Value& v) {
    if (v.type == qb::Type::QsKey) {
        const uint32_t k = v.key;
        v.type = qb::Type::String;
        v.str = text(k);
        if (v.str.empty() && !find(k)) v.str = qbkey::name(k);
        return;
    }
    if (v.type == qb::Type::Struct && v.st)
        for (qb::Item& it : v.st->items) resolve(it.value);
    if (v.type == qb::Type::Array && v.arr) {
        for (qb::Value& x : v.arr->values) resolve(x);
        if (v.arr->type == qb::Type::QsKey) v.arr->type = qb::Type::String;
    }
}

}  // namespace qstr
