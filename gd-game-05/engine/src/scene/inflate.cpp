// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Raw-deflate decoder (RFC 1951). The structure is Mark Adler's puff.c; the
// codes are read through a 10-bit lookup table (puff's bit-at-a-time walk
// only for the rare longer codes) from a 64-bit bit buffer, and the output
// grows in place (GD05-073: the band's clip milos took 1.4..2 s to inflate
// at every song start and at boot; the same bytes come out).

#include "scene/inflate.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace {

constexpr int kMaxBits = 15;
constexpr int kFastBits = 10;

struct Huffman {
    short count[kMaxBits + 1];
    short symbol[288];
    // Bit-reversed code -> (symbol << 4) | length for codes up to kFastBits; 0: longer.
    uint16_t fast[1 << kFastBits];
};

class Inflater {
public:
    Inflater(const uint8_t* d, size_t n) : in_(d), size_(n) { out_.resize(n * 4 + 1024); }

    Bytes run() {
        int last;
        do {
            last = int(bits(1));
            switch (bits(2)) {
            case 0: stored(); break;
            case 1: fixed(); break;
            case 2: dynamic(); break;
            default: fail();
            }
        } while (!last);
        out_.resize(n_);
        return std::move(out_);
    }

private:
    [[noreturn]] static void fail() { throw std::runtime_error("corrupt deflate stream"); }

    // At least `need` (<= 32) bits in the buffer; past the end only zero bits
    // come in (a code that uses them fails in consume).
    void refill(int need) {
        while (bitcnt_ < need) {
            if (pos_ < size_) bitbuf_ |= uint64_t(in_[pos_++]) << bitcnt_;
            else ++over_;
            bitcnt_ += 8;
        }
    }
    void consume(int n) {
        bitbuf_ >>= n;
        bitcnt_ -= n;
        if (over_ && bitcnt_ < 8 * over_) fail();  // a bit past the stream's end was used
    }
    uint32_t bits(int need) {
        if (need == 0) return 0;
        refill(need);
        const uint32_t v = uint32_t(bitbuf_ & ((uint64_t(1) << need) - 1));
        consume(need);
        return v;
    }

    void reserve(size_t more) {
        if (n_ + more > out_.size()) out_.resize(std::max(out_.size() * 2, n_ + more));
    }

    void stored() {
        // Drop to the byte boundary; whole bytes still in the buffer go back.
        consume(bitcnt_ & 7);
        while (bitcnt_ >= 8) {
            if (over_) --over_;
            else --pos_;
            bitbuf_ = 0;
            bitcnt_ -= 8;
        }
        bitbuf_ = 0, bitcnt_ = 0;
        if (pos_ + 4 > size_) fail();
        unsigned len = in_[pos_] | in_[pos_ + 1] << 8;
        unsigned nlen = in_[pos_ + 2] | in_[pos_ + 3] << 8;
        pos_ += 4;
        if (len != (~nlen & 0xFFFF) || pos_ + len > size_) fail();
        reserve(len);
        std::memcpy(out_.data() + n_, in_ + pos_, len);
        n_ += len;
        pos_ += len;
    }

    int decode(const Huffman& h) {
        refill(kMaxBits);
        if (const uint16_t e = h.fast[bitbuf_ & ((1u << kFastBits) - 1)]) {
            consume(e & 15);
            return e >> 4;
        }
        // puff's canonical walk, one bit at a time, for codes past kFastBits.
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= kMaxBits; ++len) {
            code |= int((bitbuf_ >> (len - 1)) & 1);
            const int count = h.count[len];
            if (code - count < first) {
                consume(len);
                return h.symbol[index + (code - first)];
            }
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        fail();
    }

    static void construct(Huffman& h, const short* length, int n) {
        for (auto& c : h.count) c = 0;
        for (int s = 0; s < n; ++s) h.count[length[s]]++;
        short offs[kMaxBits + 1];
        offs[1] = 0;
        for (int len = 1; len < kMaxBits; ++len) offs[len + 1] = short(offs[len] + h.count[len]);
        for (int s = 0; s < n; ++s)
            if (length[s] != 0) h.symbol[offs[length[s]]++] = short(s);
        // The lookup table: canonical codes in (length, symbol) order, bit-reversed.
        std::memset(h.fast, 0, sizeof h.fast);
        int code = 0;
        h.count[0] = 0;
        int next[kMaxBits + 2];
        for (int len = 1; len <= kMaxBits; ++len) {
            code = (code + h.count[len - 1]) << 1;
            next[len] = code;
        }
        for (int s = 0; s < n; ++s) {
            const int len = length[s];
            if (len == 0) continue;
            const int c = next[len]++;
            if (len > kFastBits) continue;
            int rev = 0;
            for (int b = 0; b < len; ++b) rev |= ((c >> b) & 1) << (len - 1 - b);
            for (int f = rev; f < (1 << kFastBits); f += 1 << len) h.fast[f] = uint16_t((s << 4) | len);
        }
    }

    void codes(const Huffman& lencode, const Huffman& distcode) {
        static const short base[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                       35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static const short extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                        3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static const short dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                        193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
                                        4097, 6145, 8193, 12289, 16385, 24577};
        static const short dextra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                         7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            int symbol = decode(lencode);
            if (symbol < 256) {
                reserve(1);
                out_[n_++] = uint8_t(symbol);
            } else if (symbol == 256) {
                return;
            } else {
                symbol -= 257;
                if (symbol >= 29) fail();
                const size_t len = size_t(base[symbol]) + bits(extra[symbol]);
                const int ds = decode(distcode);
                if (ds >= 30) fail();
                const size_t dist = size_t(dbase[ds]) + bits(dextra[ds]);
                if (dist > n_) fail();
                reserve(len);
                uint8_t* o = out_.data() + n_;
                const uint8_t* from = o - dist;
                if (dist >= len) std::memcpy(o, from, len);
                else
                    for (size_t i = 0; i < len; ++i) o[i] = from[i];  // overlapping: byte by byte
                n_ += len;
            }
        }
    }

    void fixed() {
        static Huffman lencode, distcode;
        static const bool built = [] {
            short lengths[288];
            int s = 0;
            for (; s < 144; ++s) lengths[s] = 8;
            for (; s < 256; ++s) lengths[s] = 9;
            for (; s < 280; ++s) lengths[s] = 7;
            for (; s < 288; ++s) lengths[s] = 8;
            construct(lencode, lengths, 288);
            for (s = 0; s < 30; ++s) lengths[s] = 5;
            construct(distcode, lengths, 30);
            return true;
        }();
        (void)built;
        codes(lencode, distcode);
    }

    void dynamic() {
        static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        int nlen = int(bits(5)) + 257, ndist = int(bits(5)) + 1, ncode = int(bits(4)) + 4;
        if (nlen > 286 || ndist > 30) fail();
        short lengths[320] = {};
        for (int i = 0; i < ncode; ++i) lengths[order[i]] = short(bits(3));
        Huffman lencode, distcode;
        construct(lencode, lengths, 19);
        int index = 0;
        while (index < nlen + ndist) {
            int symbol = decode(lencode);
            if (symbol < 16) {
                lengths[index++] = short(symbol);
                continue;
            }
            int len = 0, rep;
            if (symbol == 16) {
                if (index == 0) fail();
                len = lengths[index - 1];
                rep = 3 + int(bits(2));
            } else if (symbol == 17) {
                rep = 3 + int(bits(3));
            } else {
                rep = 11 + int(bits(7));
            }
            if (index + rep > nlen + ndist) fail();
            while (rep--) lengths[index++] = short(len);
        }
        if (lengths[256] == 0) fail();
        construct(lencode, lengths, nlen);
        construct(distcode, lengths + nlen, ndist);
        codes(lencode, distcode);
    }

    const uint8_t* in_;
    size_t size_, pos_ = 0;
    uint64_t bitbuf_ = 0;
    int bitcnt_ = 0;
    int over_ = 0;  // bytes of zeros fed past the input's end
    Bytes out_;
    size_t n_ = 0;
};

}  // namespace

Bytes inflateRaw(const uint8_t* data, size_t size) { return Inflater(data, size).run(); }
