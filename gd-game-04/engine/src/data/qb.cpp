// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/qb.h"

#include <cstring>
#include <stdexcept>

namespace qb {

namespace {

const uint8_t kHeaderMagic[20] = {0x1c, 0x08, 0x02, 0x04, 0x10, 0x04, 0x08, 0x0c, 0x0c, 0x08,
                                  0x02, 0x04, 0x14, 0x02, 0x04, 0x0c, 0x10, 0x10, 0x0c, 0x00};

bool isSimple(Type t) {
    return t == Type::Int || t == Type::Float || t == Type::Key || t == Type::KeyPtr || t == Type::StrPtr;
}

std::string hex(size_t v) {
    char b[24];
    std::snprintf(b, sizeof b, "%#zx", v);
    return b;
}

void appendUtf8(std::string& s, uint32_t c) {
    if (c < 0x80) s += char(c);
    else if (c < 0x800) s += char(0xC0 | c >> 6), s += char(0x80 | (c & 0x3F));
    else s += char(0xE0 | c >> 12), s += char(0x80 | ((c >> 6) & 0x3F)), s += char(0x80 | (c & 0x3F));
}

class Reader {
public:
    Reader(const uint8_t* d, size_t n) : d_(d), n_(n), used_(n, 0) {}

    uint32_t u32(size_t o) {
        if (o + 4 > n_) throw std::runtime_error("qb: read past the end at " + hex(o));
        mark(o, 4);
        return uint32_t(d_[o]) << 24 | uint32_t(d_[o + 1]) << 16 | uint32_t(d_[o + 2]) << 8 | d_[o + 3];
    }
    float f32(size_t o) {
        uint32_t v = u32(o);
        float f;
        std::memcpy(&f, &v, 4);
        return f;
    }
    void mark(size_t o, size_t len) {
        if (o + len > n_) throw std::runtime_error("qb: data past the end at " + hex(o));
        std::memset(used_.data() + o, 1, len);
    }
    void pad(size_t o) {
        while (o % 4 && o < n_ && d_[o] == 0) used_[o++] = 1;
    }
    bool used(size_t o) const { return used_[o] != 0; }
    size_t unused() const {
        size_t c = 0;
        for (uint8_t u : used_) c += !u;
        return c;
    }

    Value value(Type t, uint32_t v);
    std::shared_ptr<Struct> structAt(size_t o);
    std::shared_ptr<Array> arrayAt(size_t o);

private:
    const uint8_t* d_;
    size_t n_;
    std::vector<uint8_t> used_;
};

Value Reader::value(Type t, uint32_t v) {
    Value out;
    out.type = t;
    switch (t) {
    case Type::Int: out.i = int32_t(v); break;
    case Type::Float: std::memcpy(&out.f[0], &v, 4); break;
    case Type::Key:
    case Type::KeyPtr:
    case Type::StrPtr: out.key = v; break;
    case Type::String: {
        size_t e = v;
        while (e < n_ && d_[e]) ++e;
        if (e >= n_) throw std::runtime_error("qb: unterminated string at " + hex(v));
        mark(v, e + 1 - v);
        pad(e + 1);
        out.str.assign(reinterpret_cast<const char*>(d_ + v), e - v);
        break;
    }
    case Type::WString: {
        size_t e = v;
        while (e + 1 < n_ && (d_[e] || d_[e + 1])) {
            appendUtf8(out.str, uint32_t(d_[e]) << 8 | d_[e + 1]);
            e += 2;
        }
        if (e + 1 >= n_) throw std::runtime_error("qb: unterminated wide string at " + hex(v));
        mark(v, e + 2 - v);
        pad(e + 2);
        break;
    }
    case Type::Pair:
    case Type::Vector: {
        if (u32(v) != 0x00010000) throw std::runtime_error("qb: pair/vector header at " + hex(v));
        int n = t == Type::Pair ? 2 : 3;
        for (int k = 0; k < n; ++k) out.f[k] = f32(v + 4 + 4 * size_t(k));
        break;
    }
    case Type::Struct: out.st = structAt(v); break;
    case Type::Array: out.arr = arrayAt(v); break;
    case Type::Script: {
        auto s = std::make_shared<ScriptCode>();
        s->checksum = u32(v);
        uint32_t size = u32(v + 4), packed = u32(v + 8);
        mark(v + 12, packed);
        pad(v + 12 + size_t(packed));
        if (packed == size) s->code.assign(d_ + v + 12, d_ + v + 12 + packed);
        else s->code = lzss(d_ + v + 12, packed, size);
        if (s->code.size() != size) throw std::runtime_error("qb: script at " + hex(v) + " unpacks short");
        out.script = std::move(s);
        break;
    }
    default: throw std::runtime_error("qb: unknown type " + hex(size_t(t)));
    }
    return out;
}

std::shared_ptr<Struct> Reader::structAt(size_t o) {
    if (u32(o) != 0x100) throw std::runtime_error("qb: struct header at " + hex(o));
    auto st = std::make_shared<Struct>();
    uint32_t next = u32(o + 4);
    size_t guard = 0;
    while (next) {
        uint32_t t = u32(next);
        if ((t & 0xFF00FFFF) || !(t & 0x00800000)) throw std::runtime_error("qb: struct item type at " + hex(next));
        Type tt = Type((t >> 16) & 0x7F);
        Item it;
        it.name = u32(next + 4);
        uint32_t v = u32(next + 8);
        uint32_t following = u32(next + 12);
        it.value = value(tt, v);
        st->items.push_back(std::move(it));
        next = following;
        if (++guard > 1000000) throw std::runtime_error("qb: struct loop at " + hex(o));
    }
    return st;
}

std::shared_ptr<Array> Reader::arrayAt(size_t o) {
    uint32_t hdr = u32(o);
    if ((hdr & 0xFFFF00FF) != 0x00010000) throw std::runtime_error("qb: array header at " + hex(o));
    auto a = std::make_shared<Array>();
    a->type = Type((hdr >> 8) & 0xFF);
    uint32_t count = u32(o + 4);
    if (a->type == Type::None || count == 0) {
        u32(o + 8);
        return a;
    }
    a->values.reserve(count);
    // One value (or, for complex types, its offset) is stored inline; more
    // sit in a list the third word points at.
    if (count == 1) {
        a->values.push_back(value(a->type, u32(o + 8)));
        return a;
    }
    uint32_t base = u32(o + 8);
    for (uint32_t k = 0; k < count; ++k) a->values.push_back(value(a->type, u32(base + 4 * size_t(k))));
    return a;
}

}  // namespace

const char* typeName(Type t) {
    switch (t) {
    case Type::None: return "none";
    case Type::Int: return "int";
    case Type::Float: return "float";
    case Type::String: return "string";
    case Type::WString: return "wstring";
    case Type::Pair: return "pair";
    case Type::Vector: return "vector";
    case Type::Script: return "script";
    case Type::Struct: return "struct";
    case Type::Array: return "array";
    case Type::Key: return "key";
    case Type::KeyPtr: return "keyptr";
    case Type::StrPtr: return "strptr";
    }
    return "?";
}

Bytes lzss(const uint8_t* src, size_t n, size_t outSize) {
    uint8_t ring[4096];
    std::memset(ring, 0x20, sizeof ring);  // spaces, as the XEX's decoders 0x823071B0 / 0x823072D0 [C]
    unsigned r = 4078;
    Bytes out;
    out.reserve(outSize);
    size_t i = 0;
    unsigned flags = 0;
    while (out.size() < outSize && i < n) {
        flags >>= 1;
        if (!(flags & 0x100)) {
            flags = src[i++] | 0xFF00u;
            if (i >= n) break;
        }
        if (flags & 1) {
            uint8_t c = src[i++];
            out.push_back(c);
            ring[r] = c;
            r = (r + 1) & 4095;
        } else {
            if (i + 1 >= n) break;
            unsigned b0 = src[i], b1 = src[i + 1];
            i += 2;
            unsigned pos = b0 | ((b1 & 0xF0u) << 4);
            unsigned len = (b1 & 0x0Fu) + 3;
            for (unsigned k = 0; k < len; ++k) {
                uint8_t c = ring[(pos + k) & 4095];
                out.push_back(c);
                ring[r] = c;
                r = (r + 1) & 4095;
            }
        }
    }
    if (out.size() > outSize) out.resize(outSize);
    return out;
}

std::shared_ptr<Struct> decodeStruct(const uint8_t* data, size_t size) {
    Reader r(data, size);
    return r.structAt(0);
}

File decode(const uint8_t* data, size_t size, size_t* unusedBytes) {
    if (size < 28) throw std::runtime_error("qb: too short");
    Reader r(data, size);
    r.mark(0, 28);
    if (std::memcmp(data + 8, kHeaderMagic, 20) != 0) throw std::runtime_error("qb: unknown header");
    File f;
    if (r.u32(4) != size) throw std::runtime_error("qb: size field differs from the file");
    size_t o = 28;
    while (o < size) {
        uint32_t t = r.u32(o);
        if ((t & 0xFF0000FF) || ((t >> 16) & 0xFF) != 0x20) throw std::runtime_error("qb: section type at " + hex(o));
        Type tt = Type((t >> 8) & 0xFF);
        Section s;
        s.name = r.u32(o + 4);
        uint32_t fk = r.u32(o + 8);
        if (f.fileKey == 0) f.fileKey = fk;
        uint32_t v = r.u32(o + 12);
        if (r.u32(o + 16) != 0) throw std::runtime_error("qb: reserved word at " + hex(o + 16));
        s.value = r.value(tt, v);
        f.sections.push_back(std::move(s));
        o += 20;
        if (!isSimple(tt)) {
            if (v != o) throw std::runtime_error("qb: section data not after its header at " + hex(o));
            while (o < size && r.used(o)) ++o;
        }
    }
    if (unusedBytes) *unusedBytes = r.unused();
    return f;
}

}  // namespace qb
