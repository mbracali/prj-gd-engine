// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

using Bytes = std::vector<uint8_t>;

// The byte order of the milo payload being decoded. Rock Band's disc mixes
// both: most .milo_xbox directory streams are big-endian (version 25 stored
// 00 00 00 19), some older files little-endian as GH2's (docs/formats.md
// "Milo"). The parser sets it per file; later decodes of a record set it
// from the object (scene::Object::bigEndian) with a MiloOrder scope.
namespace milo_order {
inline thread_local bool big = true;
struct Scope {
    bool prev;
    explicit Scope(bool b) : prev(big) { big = b; }
    ~Scope() { big = prev; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};
}  // namespace milo_order
inline uint32_t miloU32(const uint8_t* p) {
    return milo_order::big ? uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3])
                           : uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline uint16_t miloU16(const uint8_t* p) {
    return milo_order::big ? uint16_t(p[0] << 8 | p[1]) : uint16_t(p[0] | p[1] << 8);
}

// Bounds-checked little/big-endian reader over a byte buffer.
class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}
    explicit ByteReader(const Bytes& b) : ByteReader(b.data(), b.size()) {}

    size_t pos() const { return pos_; }
    size_t size() const { return size_; }
    size_t remaining() const { return size_ - pos_; }
    void seek(size_t p) {
        if (p > size_) throw std::runtime_error("seek past end of data");
        pos_ = p;
    }
    void skip(size_t n) { seek(pos_ + n); }

    uint8_t u8() { need(1); return data_[pos_++]; }
    uint16_t u16le() { need(2); uint16_t v = data_[pos_] | data_[pos_ + 1] << 8; pos_ += 2; return v; }
    uint32_t u32le() {
        need(4);
        uint32_t v = uint32_t(data_[pos_]) | uint32_t(data_[pos_ + 1]) << 8 |
                     uint32_t(data_[pos_ + 2]) << 16 | uint32_t(data_[pos_ + 3]) << 24;
        pos_ += 4;
        return v;
    }
    uint16_t u16be() { need(2); uint16_t v = data_[pos_] << 8 | data_[pos_ + 1]; pos_ += 2; return v; }
    uint32_t u32be() {
        need(4);
        uint32_t v = uint32_t(data_[pos_]) << 24 | uint32_t(data_[pos_ + 1]) << 16 |
                     uint32_t(data_[pos_ + 2]) << 8 | uint32_t(data_[pos_ + 3]);
        pos_ += 4;
        return v;
    }
    float f32le() { uint32_t u = u32le(); float f; std::memcpy(&f, &u, 4); return f; }
    float f32be() { uint32_t u = u32be(); float f; std::memcpy(&f, &u, 4); return f; }
    // In the current milo payload order (milo_order).
    uint32_t u32m() { need(4); uint32_t v = miloU32(data_ + pos_); pos_ += 4; return v; }
    uint16_t u16m() { need(2); uint16_t v = miloU16(data_ + pos_); pos_ += 2; return v; }
    float f32m() { uint32_t u = u32m(); float f; std::memcpy(&f, &u, 4); return f; }
    std::string str(size_t n) {
        need(n);
        std::string s(reinterpret_cast<const char*>(data_ + pos_), n);
        pos_ += n;
        return s;
    }
    const uint8_t* ptr() const { return data_ + pos_; }

private:
    void need(size_t n) const {
        if (n > size_ - pos_) throw std::runtime_error("unexpected end of data");
    }
    const uint8_t* data_;
    size_t size_;
    size_t pos_ = 0;
};
