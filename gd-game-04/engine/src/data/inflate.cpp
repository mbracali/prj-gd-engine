// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Compact raw-deflate decoder, following the structure of Mark Adler's puff.c.

#include "data/inflate.h"

#include <stdexcept>

namespace {

constexpr int kMaxBits = 15;

struct Huffman {
    short count[kMaxBits + 1];
    short symbol[288];
};

class Inflater {
public:
    Inflater(const uint8_t* d, size_t n) : in_(d), size_(n) {}

    Bytes run() {
        int last;
        do {
            last = bits(1);
            switch (bits(2)) {
            case 0: stored(); break;
            case 1: fixed(); break;
            case 2: dynamic(); break;
            default: fail();
            }
        } while (!last);
        return std::move(out_);
    }

private:
    [[noreturn]] static void fail() { throw std::runtime_error("corrupt deflate stream"); }

    int bits(int need) {
        long val = bitbuf_;
        while (bitcnt_ < need) {
            if (pos_ >= size_) fail();
            val |= long(in_[pos_++]) << bitcnt_;
            bitcnt_ += 8;
        }
        bitbuf_ = int(val >> need);
        bitcnt_ -= need;
        return int(val & ((1L << need) - 1));
    }

    void stored() {
        bitbuf_ = 0;
        bitcnt_ = 0;
        if (pos_ + 4 > size_) fail();
        unsigned len = in_[pos_] | in_[pos_ + 1] << 8;
        unsigned nlen = in_[pos_ + 2] | in_[pos_ + 3] << 8;
        pos_ += 4;
        if (len != (~nlen & 0xFFFF) || pos_ + len > size_) fail();
        out_.insert(out_.end(), in_ + pos_, in_ + pos_ + len);
        pos_ += len;
    }

    int decode(const Huffman& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= kMaxBits; ++len) {
            code |= bits(1);
            int count = h.count[len];
            if (code - count < first) return h.symbol[index + (code - first)];
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
                out_.push_back(uint8_t(symbol));
            } else if (symbol == 256) {
                return;
            } else {
                symbol -= 257;
                if (symbol >= 29) fail();
                int len = base[symbol] + bits(extra[symbol]);
                int ds = decode(distcode);
                if (ds >= 30) fail();
                size_t dist = size_t(dbase[ds] + bits(dextra[ds]));
                if (dist > out_.size()) fail();
                size_t from = out_.size() - dist;
                for (int i = 0; i < len; ++i) out_.push_back(out_[from + i]);
            }
        }
    }

    void fixed() {
        static Huffman lencode, distcode;
        static bool built = false;
        if (!built) {
            short lengths[288];
            int s = 0;
            for (; s < 144; ++s) lengths[s] = 8;
            for (; s < 256; ++s) lengths[s] = 9;
            for (; s < 280; ++s) lengths[s] = 7;
            for (; s < 288; ++s) lengths[s] = 8;
            construct(lencode, lengths, 288);
            for (s = 0; s < 30; ++s) lengths[s] = 5;
            construct(distcode, lengths, 30);
            built = true;
        }
        codes(lencode, distcode);
    }

    void dynamic() {
        static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        int nlen = bits(5) + 257, ndist = bits(5) + 1, ncode = bits(4) + 4;
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
                rep = 3 + bits(2);
            } else if (symbol == 17) {
                rep = 3 + bits(3);
            } else {
                rep = 11 + bits(7);
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
    int bitbuf_ = 0, bitcnt_ = 0;
    Bytes out_;
};

}  // namespace

Bytes inflateRaw(const uint8_t* data, size_t size) { return Inflater(data, size).run(); }
