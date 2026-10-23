// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "video/mpeg2.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <stdexcept>
#include <string>

// Section and table numbers below refer to ISO/IEC 13818-2.

namespace {

// ---------------------------------------------------------------------------
// Bit reader over a buffer followed by kPadding zero bytes.

constexpr size_t kPadding = 256;

class BitReader {
public:
    BitReader(const uint8_t* data, size_t size) : data_(data), bits_(size * 8) {}

    uint32_t peek(int n) const {
        const uint8_t* p = data_ + (pos_ >> 3);
        uint64_t v = uint64_t(p[0]) << 56 | uint64_t(p[1]) << 48 | uint64_t(p[2]) << 40 | uint64_t(p[3]) << 32 |
                     uint64_t(p[4]) << 24 | uint64_t(p[5]) << 16 | uint64_t(p[6]) << 8 | uint64_t(p[7]);
        return static_cast<uint32_t>((v << (pos_ & 7)) >> (64 - n));
    }
    void skip(int n) { pos_ += n; }
    uint32_t read(int n) {
        uint32_t v = peek(n);
        pos_ += n;
        return v;
    }
    bool bit() { return read(1) != 0; }
    // Throws once the reader went far enough past the end to risk leaving the padding.
    void check() const {
        if (pos_ > bits_ + 64) throw std::runtime_error("MPEG-2 bitstream overrun");
    }
    bool atEnd() const { return pos_ >= bits_; }

private:
    const uint8_t* data_;
    size_t bits_;
    size_t pos_ = 0;
};

// ---------------------------------------------------------------------------
// VLC tables, built at startup from the code strings of Annex B.

struct VlcCode {
    const char* bits;  // '0'/'1', spaces ignored
    int value;
};

struct VlcEntry {
    int16_t value = 0;
    uint8_t len = 0;  // 0 = invalid code
};

template <int Bits>
struct Vlc {
    std::vector<VlcEntry> table;

    template <size_t N>
    explicit Vlc(const VlcCode (&codes)[N]) : table(size_t(1) << Bits) {
        for (const VlcCode& c : codes) {
            uint32_t code = 0;
            int len = 0;
            for (const char* p = c.bits; *p; ++p) {
                if (*p == ' ') continue;
                code = code << 1 | uint32_t(*p == '1');
                ++len;
            }
            if (len > Bits) throw std::logic_error("VLC code longer than table");
            uint32_t first = code << (Bits - len), count = 1u << (Bits - len);
            for (uint32_t i = 0; i < count; ++i) {
                VlcEntry& e = table[first + i];
                if (e.len) throw std::logic_error(std::string("VLC codes overlap at ") + c.bits);
                e.value = static_cast<int16_t>(c.value);
                e.len = static_cast<uint8_t>(len);
            }
        }
    }

    // Returns the value, or throws on an invalid code.
    int decode(BitReader& br) const {
        const VlcEntry& e = table[br.peek(Bits)];
        if (!e.len) throw std::runtime_error("invalid MPEG-2 VLC code");
        br.skip(e.len);
        return e.value;
    }
};

// Table B.1, macroblock_address_increment (escape and stuffing are handled by the caller).
const VlcCode kMbaCodes[] = {
    {"1", 1},              {"011", 2},            {"010", 3},            {"0011", 4},
    {"0010", 5},           {"0001 1", 6},         {"0001 0", 7},         {"0000 111", 8},
    {"0000 110", 9},       {"0000 1011", 10},     {"0000 1010", 11},     {"0000 1001", 12},
    {"0000 1000", 13},     {"0000 0111", 14},     {"0000 0110", 15},     {"0000 0101 11", 16},
    {"0000 0101 10", 17},  {"0000 0101 01", 18},  {"0000 0101 00", 19},  {"0000 0100 11", 20},
    {"0000 0100 10", 21},  {"0000 0100 011", 22}, {"0000 0100 010", 23}, {"0000 0100 001", 24},
    {"0000 0100 000", 25}, {"0000 0011 111", 26}, {"0000 0011 110", 27}, {"0000 0011 101", 28},
    {"0000 0011 100", 29}, {"0000 0011 011", 30}, {"0000 0011 010", 31}, {"0000 0011 001", 32},
    {"0000 0011 000", 33}, {"0000 0001 000", 34}, {"0000 0001 111", 35},  // 34 = escape, 35 = stuffing
};

enum MbFlags { kQuant = 1, kFwd = 2, kBwd = 4, kPattern = 8, kIntra = 16 };

// Tables B.2, B.3, B.4, macroblock_type for I, P and B pictures.
const VlcCode kMbTypeI[] = {{"1", kIntra}, {"01", kIntra | kQuant}};
const VlcCode kMbTypeP[] = {
    {"1", kFwd | kPattern},          {"01", kPattern},          {"001", kFwd},
    {"0001 1", kIntra},              {"0001 0", kQuant | kFwd | kPattern},
    {"0000 1", kQuant | kPattern},   {"0000 01", kQuant | kIntra},
};
const VlcCode kMbTypeB[] = {
    {"10", kFwd | kBwd},
    {"11", kFwd | kBwd | kPattern},
    {"010", kBwd},
    {"011", kBwd | kPattern},
    {"0010", kFwd},
    {"0011", kFwd | kPattern},
    {"0001 1", kIntra},
    {"0001 0", kQuant | kFwd | kBwd | kPattern},
    {"0000 11", kQuant | kFwd | kPattern},
    {"0000 10", kQuant | kBwd | kPattern},
    {"0000 01", kQuant | kIntra},
};

// Table B.9, coded_block_pattern.
const VlcCode kCbpCodes[] = {
    {"111", 60},        {"1101", 4},        {"1100", 8},        {"1011", 16},       {"1010", 32},
    {"1001 1", 12},     {"1001 0", 48},     {"1000 1", 20},     {"1000 0", 40},     {"0111 1", 28},
    {"0111 0", 44},     {"0110 1", 52},     {"0110 0", 56},     {"0101 1", 1},      {"0101 0", 61},
    {"0100 1", 2},      {"0100 0", 62},     {"0011 11", 24},    {"0011 10", 36},    {"0011 01", 3},
    {"0011 00", 63},    {"0010 111", 5},    {"0010 110", 9},    {"0010 101", 17},   {"0010 100", 33},
    {"0010 011", 6},    {"0010 010", 10},   {"0010 001", 18},   {"0010 000", 34},   {"0001 1111", 7},
    {"0001 1110", 11},  {"0001 1101", 19},  {"0001 1100", 35},  {"0001 1011", 13},  {"0001 1010", 49},
    {"0001 1001", 21},  {"0001 1000", 41},  {"0001 0111", 14},  {"0001 0110", 50},  {"0001 0101", 22},
    {"0001 0100", 42},  {"0001 0011", 15},  {"0001 0010", 51},  {"0001 0001", 23},  {"0001 0000", 43},
    {"0000 1111", 25},  {"0000 1110", 37},  {"0000 1101", 26},  {"0000 1100", 38},  {"0000 1011", 29},
    {"0000 1010", 45},  {"0000 1001", 53},  {"0000 1000", 57},  {"0000 0111", 30},  {"0000 0110", 46},
    {"0000 0101", 54},  {"0000 0100", 58},  {"0000 0011 1", 31}, {"0000 0011 0", 47}, {"0000 0010 1", 55},
    {"0000 0010 0", 59}, {"0000 0001 1", 27}, {"0000 0001 0", 39}, {"0000 0000 1", 0},
};

// Table B.10, motion_code magnitude (the sign bit follows non-zero codes).
const VlcCode kMotionCodes[] = {
    {"1", 0},             {"01", 1},            {"001", 2},           {"0001", 3},
    {"0000 11", 4},       {"0000 101", 5},      {"0000 100", 6},      {"0000 011", 7},
    {"0000 0101 1", 8},   {"0000 0101 0", 9},   {"0000 0100 1", 10},  {"0000 0100 01", 11},
    {"0000 0100 00", 12}, {"0000 0011 11", 13}, {"0000 0011 10", 14}, {"0000 0011 01", 15},
    {"0000 0011 00", 16},
};

// Tables B.12 and B.13, dct_dc_size_luminance / chrominance.
const VlcCode kDcLumaCodes[] = {
    {"100", 0},    {"00", 1},      {"01", 2},       {"101", 3},        {"110", 4},         {"1110", 5},
    {"1111 0", 6}, {"1111 10", 7}, {"1111 110", 8}, {"1111 1110", 9}, {"1111 1111 0", 10}, {"1111 1111 1", 11},
};
const VlcCode kDcChromaCodes[] = {
    {"00", 0},       {"01", 1},        {"10", 2},          {"110", 3},          {"1110", 4},          {"1111 0", 5},
    {"1111 10", 6},  {"1111 110", 7},  {"1111 1110", 8},   {"1111 1111 0", 9},  {"1111 1111 10", 10}, {"1111 1111 11", 11},
};

// DCT coefficient codes without the trailing sign bit; value = run << 8 | level.
constexpr int kEob = -1, kEsc = -2;
#define RL(r, l) ((r) << 8 | (l))

// Codes shared by tables B.14 and B.15 (13 bits and longer).
#define DCT_LONG_CODES                                                                                         \
    {"0000 0000 1011 0", RL(1, 6)}, {"0000 0000 1010 1", RL(1, 7)}, {"0000 0000 1010 0", RL(2, 5)},            \
    {"0000 0000 1001 1", RL(3, 4)}, {"0000 0000 1001 0", RL(5, 3)}, {"0000 0000 1000 1", RL(9, 2)},            \
    {"0000 0000 1000 0", RL(10, 2)}, {"0000 0000 1111 1", RL(22, 1)}, {"0000 0000 1111 0", RL(23, 1)},         \
    {"0000 0000 1110 1", RL(24, 1)}, {"0000 0000 1110 0", RL(25, 1)}, {"0000 0000 1101 1", RL(26, 1)},         \
    {"0000 0000 0111 11", RL(0, 16)}, {"0000 0000 0111 10", RL(0, 17)}, {"0000 0000 0111 01", RL(0, 18)},      \
    {"0000 0000 0111 00", RL(0, 19)}, {"0000 0000 0110 11", RL(0, 20)}, {"0000 0000 0110 10", RL(0, 21)},      \
    {"0000 0000 0110 01", RL(0, 22)}, {"0000 0000 0110 00", RL(0, 23)}, {"0000 0000 0101 11", RL(0, 24)},      \
    {"0000 0000 0101 10", RL(0, 25)}, {"0000 0000 0101 01", RL(0, 26)}, {"0000 0000 0101 00", RL(0, 27)},      \
    {"0000 0000 0100 11", RL(0, 28)}, {"0000 0000 0100 10", RL(0, 29)}, {"0000 0000 0100 01", RL(0, 30)},      \
    {"0000 0000 0100 00", RL(0, 31)}, {"0000 0000 0011 000", RL(0, 32)}, {"0000 0000 0010 111", RL(0, 33)},    \
    {"0000 0000 0010 110", RL(0, 34)}, {"0000 0000 0010 101", RL(0, 35)}, {"0000 0000 0010 100", RL(0, 36)},   \
    {"0000 0000 0010 011", RL(0, 37)}, {"0000 0000 0010 010", RL(0, 38)}, {"0000 0000 0010 001", RL(0, 39)},   \
    {"0000 0000 0010 000", RL(0, 40)}, {"0000 0000 0011 111", RL(1, 8)}, {"0000 0000 0011 110", RL(1, 9)},     \
    {"0000 0000 0011 101", RL(1, 10)}, {"0000 0000 0011 100", RL(1, 11)}, {"0000 0000 0011 011", RL(1, 12)},   \
    {"0000 0000 0011 010", RL(1, 13)}, {"0000 0000 0011 001", RL(1, 14)}, {"0000 0000 0001 0011", RL(1, 15)},  \
    {"0000 0000 0001 0010", RL(1, 16)}, {"0000 0000 0001 0001", RL(1, 17)}, {"0000 0000 0001 0000", RL(1, 18)}, \
    {"0000 0000 0001 0100", RL(6, 3)}, {"0000 0000 0001 1010", RL(11, 2)}, {"0000 0000 0001 1001", RL(12, 2)}, \
    {"0000 0000 0001 1000", RL(13, 2)}, {"0000 0000 0001 0111", RL(14, 2)}, {"0000 0000 0001 0110", RL(15, 2)}, \
    {"0000 0000 0001 0101", RL(16, 2)}, {"0000 0000 0001 1111", RL(27, 1)}, {"0000 0000 0001 1110", RL(28, 1)}, \
    {"0000 0000 0001 1101", RL(29, 1)}, {"0000 0000 0001 1100", RL(30, 1)}, {"0000 0000 0001 1011", RL(31, 1)}

// Table B.14 (DCT coefficients table zero), for coefficients after the first.
const VlcCode kDctZero[] = {
    {"10", kEob},             {"11", RL(0, 1)},         {"011", RL(1, 1)},        {"0100", RL(0, 2)},
    {"0101", RL(2, 1)},       {"0010 1", RL(0, 3)},     {"0011 1", RL(3, 1)},     {"0011 0", RL(4, 1)},
    {"0001 10", RL(1, 2)},    {"0001 11", RL(5, 1)},    {"0001 01", RL(6, 1)},    {"0001 00", RL(7, 1)},
    {"0000 110", RL(0, 4)},   {"0000 100", RL(2, 2)},   {"0000 111", RL(8, 1)},   {"0000 101", RL(9, 1)},
    {"0000 01", kEsc},        {"0010 0110", RL(0, 5)},  {"0010 0001", RL(0, 6)},  {"0010 0101", RL(1, 3)},
    {"0010 0100", RL(3, 2)},  {"0010 0111", RL(10, 1)}, {"0010 0011", RL(11, 1)}, {"0010 0010", RL(12, 1)},
    {"0010 0000", RL(13, 1)}, {"0000 0010 10", RL(0, 7)}, {"0000 0011 00", RL(1, 4)}, {"0000 0010 11", RL(2, 3)},
    {"0000 0011 11", RL(4, 2)}, {"0000 0010 01", RL(5, 2)}, {"0000 0011 10", RL(14, 1)},
    {"0000 0011 01", RL(15, 1)}, {"0000 0010 00", RL(16, 1)}, {"0000 0001 1101", RL(0, 8)},
    {"0000 0001 1000", RL(0, 9)}, {"0000 0001 0011", RL(0, 10)}, {"0000 0001 0000", RL(0, 11)},
    {"0000 0001 1011", RL(1, 5)}, {"0000 0001 0100", RL(2, 4)}, {"0000 0001 1100", RL(3, 3)},
    {"0000 0001 0010", RL(4, 3)}, {"0000 0001 1110", RL(6, 2)}, {"0000 0001 0101", RL(7, 2)},
    {"0000 0001 0001", RL(8, 2)}, {"0000 0001 1111", RL(17, 1)}, {"0000 0001 1010", RL(18, 1)},
    {"0000 0001 1001", RL(19, 1)}, {"0000 0001 0111", RL(20, 1)}, {"0000 0001 0110", RL(21, 1)},
    {"0000 0000 1101 0", RL(0, 12)}, {"0000 0000 1100 1", RL(0, 13)}, {"0000 0000 1100 0", RL(0, 14)},
    {"0000 0000 1011 1", RL(0, 15)}, DCT_LONG_CODES,
};

// Table B.15 (DCT coefficients table one), intra blocks with intra_vlc_format = 1.
const VlcCode kDctOne[] = {
    {"0110", kEob},           {"10", RL(0, 1)},         {"010", RL(1, 1)},        {"110", RL(0, 2)},
    {"0010 1", RL(2, 1)},     {"0111", RL(0, 3)},       {"0011 1", RL(3, 1)},     {"0001 10", RL(4, 1)},
    {"0011 0", RL(1, 2)},     {"0001 11", RL(5, 1)},    {"0000 110", RL(6, 1)},   {"0000 100", RL(7, 1)},
    {"1110 0", RL(0, 4)},     {"0000 111", RL(2, 2)},   {"0000 101", RL(8, 1)},   {"1111 000", RL(9, 1)},
    {"0000 01", kEsc},        {"1110 1", RL(0, 5)},     {"0001 01", RL(0, 6)},    {"1111 001", RL(1, 3)},
    {"0010 0110", RL(3, 2)},  {"1111 010", RL(10, 1)},  {"0010 0001", RL(11, 1)}, {"0010 0101", RL(12, 1)},
    {"0010 0100", RL(13, 1)}, {"0001 00", RL(0, 7)},    {"0010 0111", RL(1, 4)},  {"1111 1100", RL(2, 3)},
    {"1111 1101", RL(4, 2)},  {"0000 0010 0", RL(5, 2)}, {"0000 0010 1", RL(14, 1)}, {"0000 0011 1", RL(15, 1)},
    {"0000 0011 01", RL(16, 1)}, {"1111 011", RL(0, 8)}, {"1111 100", RL(0, 9)},  {"0010 0011", RL(0, 10)},
    {"0010 0010", RL(0, 11)}, {"0010 0000", RL(1, 5)},  {"0000 0011 00", RL(2, 4)}, {"0000 0001 1100", RL(3, 3)},
    {"0000 0001 0010", RL(4, 3)}, {"0000 0001 1110", RL(6, 2)}, {"0000 0001 0101", RL(7, 2)},
    {"0000 0001 0001", RL(8, 2)}, {"0000 0001 1111", RL(17, 1)}, {"0000 0001 1010", RL(18, 1)},
    {"0000 0001 1001", RL(19, 1)}, {"0000 0001 0111", RL(20, 1)}, {"0000 0001 0110", RL(21, 1)},
    {"1111 1010", RL(0, 12)}, {"1111 1011", RL(0, 13)}, {"1111 1110", RL(0, 14)}, {"1111 1111", RL(0, 15)},
    DCT_LONG_CODES,
};
#undef DCT_LONG_CODES
#undef RL

struct Tables {
    Vlc<11> mba{kMbaCodes};
    Vlc<2> mbTypeI{kMbTypeI};
    Vlc<6> mbTypeP{kMbTypeP};
    Vlc<6> mbTypeB{kMbTypeB};
    Vlc<9> cbp{kCbpCodes};
    Vlc<11> motion{kMotionCodes};
    Vlc<9> dcLuma{kDcLumaCodes};
    Vlc<10> dcChroma{kDcChromaCodes};
    Vlc<16> dctZero{kDctZero};
    Vlc<16> dctOne{kDctOne};
    float idct[8][8];  // idct[x][u] = C(u)/2 * cos((2x+1)u*pi/16)

    Tables() {
        for (int x = 0; x < 8; ++x)
            for (int u = 0; u < 8; ++u)
                idct[x][u] = static_cast<float>((u ? 0.5 : 0.5 / std::sqrt(2.0)) *
                                                std::cos((2 * x + 1) * u * 3.14159265358979323846 / 16));
    }
};

const Tables& tables() {
    static const Tables t;
    return t;
}

// Zig-zag and alternate scans (figure 7-2, 7-3): scan position -> raster index.
constexpr uint8_t kScan[2][64] = {
    {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
     41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
     30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63},
    {0,  8,  16, 24, 1,  9,  2,  10, 17, 25, 32, 40, 48, 56, 57, 49, 41, 33, 26, 18, 3,  11,
     4,  12, 19, 27, 34, 42, 50, 58, 35, 43, 51, 59, 20, 28, 5,  13, 6,  14, 21, 29, 36, 44,
     52, 60, 37, 45, 53, 61, 22, 30, 7,  15, 23, 31, 38, 46, 54, 62, 39, 47, 55, 63},
};

// Default intra matrix (§6.3.11), raster order.
constexpr uint8_t kDefaultIntra[64] = {
    8,  16, 19, 22, 26, 27, 29, 34, 16, 16, 22, 24, 27, 29, 34, 37, 19, 22, 26, 27, 29, 34,
    34, 38, 22, 22, 26, 27, 29, 34, 37, 40, 22, 26, 27, 29, 32, 35, 40, 48, 26, 27, 29, 32,
    35, 40, 48, 58, 26, 27, 29, 34, 38, 46, 56, 69, 27, 29, 35, 38, 46, 56, 69, 83,
};

// Table 7-6, quantiser_scale for q_scale_type = 1.
constexpr uint8_t kNonLinearQ[32] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  10, 12, 14, 16, 18, 20,  22,
                                     24, 28, 32, 36, 40, 44, 48, 52, 56, 64, 72, 80, 88, 96, 104, 112};

constexpr double kFrameRates[16] = {0, 24000.0 / 1001, 24, 25, 30000.0 / 1001, 30, 50, 60000.0 / 1001, 60,
                                    0, 0, 0, 0, 0, 0, 0};

inline uint8_t clamp255(int v) { return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v); }

// ---------------------------------------------------------------------------
// Inverse DCT (separable float, §A) and block output.

void idct8x8(const int16_t* in, int* out, uint8_t rowMask) {
    const auto& c = tables().idct;
    float tmp[64];
    for (int y = 0; y < 8; ++y) {
        float* t = tmp + y * 8;
        if (!(rowMask >> y & 1)) {
            std::fill(t, t + 8, 0.0f);
            continue;
        }
        const int16_t* r = in + y * 8;
        for (int x = 0; x < 8; ++x) {
            float s = 0;
            for (int u = 0; u < 8; ++u) s += c[x][u] * r[u];
            t[x] = s;
        }
    }
    for (int x = 0; x < 8; ++x) {
        float col[8];
        for (int v = 0; v < 8; ++v) col[v] = tmp[v * 8 + x];
        for (int y = 0; y < 8; ++y) {
            float s = 0;
            for (int v = 0; v < 8; ++v) s += c[y][v] * col[v];
            int i = static_cast<int>(std::floor(s + 0.5f));
            out[y * 8 + x] = std::clamp(i, -256, 255);
        }
    }
}

// ---------------------------------------------------------------------------
// Motion compensation: predict a w x h block at half-pel position (x2, y2) of
// a plane (refW x refH addressable through refStride) into dst, or average
// into dst when `avg` (bidirectional / dual prime).

template <bool Avg>
void predictBlockT(uint8_t* dst, int dstStride, const uint8_t* src, int srcStride, int w, int h, int hx, int hy) {
    for (int y = 0; y < h; ++y) {
        const uint8_t* s0 = src + y * srcStride;
        const uint8_t* s1 = s0 + srcStride;
        uint8_t* d = dst + y * dstStride;
        for (int x = 0; x < w; ++x) {
            int p;
            if (!hx && !hy) p = s0[x];
            else if (hx && !hy) p = (s0[x] + s0[x + 1] + 1) >> 1;
            else if (!hx) p = (s0[x] + s1[x] + 1) >> 1;
            else p = (s0[x] + s0[x + 1] + s1[x] + s1[x + 1] + 2) >> 2;
            if constexpr (Avg) d[x] = static_cast<uint8_t>((d[x] + p + 1) >> 1);
            else d[x] = static_cast<uint8_t>(p);
        }
    }
}

void predictBlock(uint8_t* dst, int dstStride, const uint8_t* ref, int refStride, int refW, int refH, int x2, int y2,
                  int w, int h, bool avg) {
    int ix = x2 >> 1, iy = y2 >> 1, hx = x2 & 1, hy = y2 & 1;
    uint8_t tmp[17 * 17];
    if (ix < 0 || iy < 0 || ix + w + hx > refW || iy + h + hy > refH) {
        // Vectors pointing outside the picture are illegal; clamp to the edge.
        for (int y = 0; y < h + hy; ++y) {
            int sy = std::clamp(iy + y, 0, refH - 1);
            for (int x = 0; x < w + hx; ++x) tmp[y * 17 + x] = ref[sy * refStride + std::clamp(ix + x, 0, refW - 1)];
        }
        ref = tmp;
        refStride = 17;
        ix = iy = 0;
    }
    const uint8_t* src = ref + iy * refStride + ix;
    if (avg) predictBlockT<true>(dst, dstStride, src, refStride, w, h, hx, hy);
    else predictBlockT<false>(dst, dstStride, src, refStride, w, h, hx, hy);
}

}  // namespace

// ---------------------------------------------------------------------------

struct Mpeg2Decoder::Impl {
    // Elementary stream buffer and start code scanning.
    std::vector<uint8_t> buf;
    size_t unitStart = SIZE_MAX;  // offset of the current unit's 00 00 01 xx
    size_t scanFrom = 0;
    bool ended = false, finished = false;
    std::vector<uint8_t> unit;  // copy of the current unit's payload + padding

    // Sequence state.
    bool haveSequence = false, haveExtension = false;
    int width = 0, height = 0, mbWidth = 0, mbHeight = 0;
    int frameRateCode = 0;
    uint8_t intraQ[64], nonIntraQ[64];  // raster order

    // Picture state.
    bool closedGop = false;
    int codingType = 0;  // 1 I, 2 P, 3 B
    int temporalRef = 0;
    int fcode[2][2] = {{15, 15}, {15, 15}};
    int dcPrecision = 0, structure = 3;
    bool topFieldFirst = true, framePredFrameDct = true, concealment = false, qScaleType = false;
    bool intraVlc = false, altScan = false, progressiveFrame = true;
    bool inPicture = false, haveSlices = false, skipPicture = false;

    // Frame store: the two references and a buffer for B pictures.
    Mpeg2Frame frames[3];
    int fwdRef = -1, bwdRef = -1, cur = -1;
    bool bwdPending = false;  // newest reference not output yet
    std::deque<Mpeg2Frame*> ready;
    uint64_t nextIndex = 0;

    uint64_t sliceErrors = 0, dualPrime = 0;

    // Slice / macroblock state.
    int qscale = 0;
    int dcPred[3] = {};
    int pmv[2][2][2] = {};  // [r][s][t]
    int prevFlags = 0;      // B pictures: direction of the previous macroblock
    alignas(16) int16_t coef[64];
    int resid[64];

    Impl() { reset(); }

    void reset() {
        buf.clear();
        unitStart = SIZE_MAX;
        scanFrom = 0;
        ended = finished = false;
        haveSequence = haveExtension = false;
        inPicture = haveSlices = false;
        fwdRef = bwdRef = cur = -1;
        bwdPending = false;
        ready.clear();
        nextIndex = 0;
        std::memcpy(intraQ, kDefaultIntra, 64);
        std::memset(nonIntraQ, 16, 64);
    }

    // ---- stream framing ----

    const Mpeg2Frame* decode() {
        for (;;) {
            if (!ready.empty()) {
                Mpeg2Frame* f = ready.front();
                ready.pop_front();
                f->index = nextIndex++;
                return f;
            }
            if (finished) return nullptr;

            if (unitStart == SIZE_MAX) {
                size_t sc = findStartCode(scanFrom);
                if (sc == SIZE_MAX) {
                    scanFrom = buf.size() > 3 ? buf.size() - 3 : 0;
                    if (ended) finishStream();
                    else return nullptr;
                    continue;
                }
                unitStart = sc;
            }
            size_t next = findStartCode(std::max(scanFrom, unitStart + 4));
            if (next == SIZE_MAX) {
                if (!ended) {
                    scanFrom = std::max(unitStart + 4, buf.size() > 3 ? buf.size() - 3 : 0);
                    return nullptr;
                }
                next = buf.size();
            }
            int code = unitStart + 3 < buf.size() ? buf[unitStart + 3] : 0xB7;
            bool isSlice = code >= 0x01 && code <= 0xAF;
            if (inPicture && haveSlices && !isSlice) {
                finishPicture();
                continue;  // reprocess this unit once the picture is out
            }
            processUnit(code, unitStart + 4, next);
            unitStart = next >= buf.size() ? SIZE_MAX : next;
            scanFrom = next;
            if (next >= buf.size()) {
                if (ended) finishStream();
                buf.clear();
                unitStart = SIZE_MAX;
                scanFrom = 0;
            } else if (unitStart > (1u << 20)) {
                buf.erase(buf.begin(), buf.begin() + static_cast<ptrdiff_t>(unitStart));
                scanFrom -= unitStart;
                unitStart = 0;
            }
        }
    }

    size_t findStartCode(size_t from) const {
        const uint8_t* d = buf.data();
        size_t n = buf.size();
        for (size_t i = from; i + 3 < n;) {
            if (d[i + 2] > 1) i += 3;
            else if (d[i + 2] == 1 && d[i] == 0 && d[i + 1] == 0) return i;
            else ++i;
        }
        return SIZE_MAX;
    }

    void finishStream() {
        if (inPicture && haveSlices) finishPicture();
        inPicture = false;
        flushReference();
        finished = true;
    }

    void flushReference() {
        if (bwdPending && bwdRef >= 0) ready.push_back(&frames[bwdRef]);
        bwdPending = false;
    }

    void processUnit(int code, size_t begin, size_t end) {
        if (code == 0xB7) {  // sequence_end_code
            flushReference();
            return;
        }
        unit.assign(buf.begin() + static_cast<ptrdiff_t>(begin), buf.begin() + static_cast<ptrdiff_t>(end));
        unit.resize(unit.size() + kPadding, 0);
        BitReader br(unit.data(), end - begin);
        if (code >= 0x01 && code <= 0xAF) {
            if (!inPicture || skipPicture) return;
            if (!haveSlices) startPicture();
            haveSlices = true;
            try {
                decodeSlice(br, code);
            } catch (const std::runtime_error&) {
                ++sliceErrors;
            }
            return;
        }
        switch (code) {
        case 0xB3: parseSequenceHeader(br); break;
        case 0xB5: parseExtension(br); break;
        case 0xB8: parseGop(br); break;
        case 0x00: parsePictureHeader(br); break;
        default: break;  // user data, reserved, system codes
        }
    }

    // ---- headers ----

    void parseSequenceHeader(BitReader& br) {
        int w = static_cast<int>(br.read(12)), h = static_cast<int>(br.read(12));
        br.skip(4);  // aspect_ratio_information
        frameRateCode = static_cast<int>(br.read(4));
        br.skip(18 + 1 + 10 + 1);  // bit_rate, marker, vbv_buffer_size, constrained_parameters_flag
        if (br.bit()) for (int i = 0; i < 64; ++i) intraQ[kScan[0][i]] = static_cast<uint8_t>(br.read(8));
        else std::memcpy(intraQ, kDefaultIntra, 64);
        if (br.bit()) for (int i = 0; i < 64; ++i) nonIntraQ[kScan[0][i]] = static_cast<uint8_t>(br.read(8));
        else std::memset(nonIntraQ, 16, 64);
        if (w <= 0 || h <= 0) throw std::runtime_error("MPEG-2 sequence header with empty picture size");
        if (w != width || h != height) {
            width = w;
            height = h;
            mbWidth = (w + 15) / 16;
            mbHeight = (h + 15) / 16;
            for (Mpeg2Frame& f : frames) {
                f.width = w;
                f.height = h;
                f.lumaStride = mbWidth * 16;
                f.chromaStride = mbWidth * 8;
                f.y.assign(size_t(mbWidth) * 16 * mbHeight * 16, 16);
                f.cb.assign(size_t(mbWidth) * 8 * mbHeight * 8, 128);
                f.cr.assign(size_t(mbWidth) * 8 * mbHeight * 8, 128);
            }
            fwdRef = bwdRef = -1;
            bwdPending = false;
        }
        haveSequence = true;
        haveExtension = false;
    }

    void parseExtension(BitReader& br) {
        int id = static_cast<int>(br.read(4));
        if (id == 1) {  // sequence_extension
            br.skip(8);  // profile_and_level_indication
            br.skip(1);  // progressive_sequence
            int chroma = static_cast<int>(br.read(2));
            int hExt = static_cast<int>(br.read(2)), vExt = static_cast<int>(br.read(2));
            if (chroma != 1) throw std::runtime_error("MPEG-2 chroma format other than 4:2:0 is not supported");
            if (hExt || vExt) throw std::runtime_error("MPEG-2 picture size extensions are not supported");
            haveExtension = true;
        } else if (id == 3) {  // quant_matrix_extension
            if (br.bit()) for (int i = 0; i < 64; ++i) intraQ[kScan[0][i]] = static_cast<uint8_t>(br.read(8));
            if (br.bit()) for (int i = 0; i < 64; ++i) nonIntraQ[kScan[0][i]] = static_cast<uint8_t>(br.read(8));
            // Chroma matrices only apply to 4:2:2 / 4:4:4.
        } else if (id == 8) {  // picture_coding_extension
            for (auto& s : fcode)
                for (int& f : s) f = static_cast<int>(br.read(4));
            dcPrecision = static_cast<int>(br.read(2));
            structure = static_cast<int>(br.read(2));
            topFieldFirst = br.bit();
            framePredFrameDct = br.bit();
            concealment = br.bit();
            qScaleType = br.bit();
            intraVlc = br.bit();
            altScan = br.bit();
            br.skip(1);  // repeat_first_field
            br.skip(1);  // chroma_420_type
            progressiveFrame = br.bit();
            if (structure != 3) throw std::runtime_error("MPEG-2 field pictures are not supported");
            if (dcPrecision == 3) throw std::runtime_error("MPEG-2 11-bit DC precision needs High profile");
        }
    }

    void parseGop(BitReader& br) {
        br.skip(25);  // time_code
        closedGop = br.bit();
        bool brokenLink = br.bit();
        if (brokenLink) fwdRef = -1;  // the B pictures that follow lost their forward reference
    }

    void parsePictureHeader(BitReader& br) {
        if (!haveSequence) return;  // wait for a sequence header
        if (!haveExtension) throw std::runtime_error("MPEG-1 video is not supported");
        temporalRef = static_cast<int>(br.read(10));
        codingType = static_cast<int>(br.read(3));
        inPicture = true;
        haveSlices = false;
        skipPicture = false;
        if (codingType == 2 && bwdRef < 0) skipPicture = true;  // P without a reference
        else if (codingType == 3 && bwdRef < 0) skipPicture = true;
        else if (codingType < 1 || codingType > 3) skipPicture = true;  // D pictures
        // Defaults if the picture coding extension were missing.
        structure = 3;
    }

    void startPicture() {
        if (codingType == 3) {
            cur = 0;
            while (cur == fwdRef || cur == bwdRef) ++cur;
        } else {
            // A new reference replaces the old forward one, which no B picture needs any more.
            cur = fwdRef >= 0 ? fwdRef : bwdRef >= 0 ? (bwdRef + 1) % 3 : 0;
        }
        Mpeg2Frame& f = frames[cur];
        f.type = "?IPB"[codingType];
        f.progressive = progressiveFrame;
        f.topFieldFirst = topFieldFirst;
    }

    void finishPicture() {
        inPicture = false;
        if (!haveSlices || skipPicture) return;
        haveSlices = false;
        if (codingType == 3) {
            ready.push_back(&frames[cur]);
        } else {
            flushReference();
            fwdRef = bwdRef;
            bwdRef = cur;
            bwdPending = true;
        }
    }

    // ---- slices and macroblocks (§6.2.4, §6.2.5) ----

    int qscaleFromCode(int code) const { return qScaleType ? kNonLinearQ[code] : code * 2; }

    void resetDc() { dcPred[0] = dcPred[1] = dcPred[2] = 128 << dcPrecision; }

    void decodeSlice(BitReader& br, int vpos) {
        const Tables& t = tables();
        if (vpos > mbHeight) throw std::runtime_error("MPEG-2 slice below the picture");
        qscale = qscaleFromCode(static_cast<int>(br.read(5)));
        if (br.bit()) {  // intra_slice_flag, intra_slice, reserved, extra_information_slice
            br.skip(8);
            while (br.bit()) br.skip(8);
        }
        resetDc();
        std::memset(pmv, 0, sizeof(pmv));
        prevFlags = 0;
        int mbCount = mbWidth * mbHeight;
        int addr = (vpos - 1) * mbWidth - 1;
        bool first = true;
        for (;;) {
            int inc = 0;
            for (;;) {
                int v = t.mba.decode(br);
                if (v == 34) inc += 33;
                else if (v != 35) {
                    inc += v;
                    break;
                }
            }
            if (!first) {
                for (int k = 1; k < inc; ++k) {
                    if (addr + k >= mbCount) throw std::runtime_error("MPEG-2 skipped macroblocks past the picture");
                    skippedMacroblock(addr + k);
                }
            }
            first = false;
            addr += inc;
            if (addr >= mbCount) throw std::runtime_error("MPEG-2 macroblock address past the picture");
            macroblock(br, addr % mbWidth, addr / mbWidth);
            br.check();
            if (br.peek(23) == 0 || br.atEnd()) break;
        }
    }

    const Mpeg2Frame& forwardRef() const { return frames[codingType == 3 && fwdRef >= 0 ? fwdRef : bwdRef]; }
    const Mpeg2Frame& backwardRef() const { return frames[bwdRef]; }

    void skippedMacroblock(int addr) {
        int mx = addr % mbWidth, my = addr / mbWidth;
        resetDc();
        if (codingType == 2) {
            std::memset(pmv, 0, sizeof(pmv));
            int mv[2] = {0, 0};
            frameMc(forwardRef(), mx, my, mv, false);
        } else if (codingType == 3) {
            bool avg = false;
            if (prevFlags & kFwd) {
                frameMc(forwardRef(), mx, my, pmv[0][0], false);
                avg = true;
            }
            if (prevFlags & kBwd) frameMc(backwardRef(), mx, my, pmv[0][1], avg);
            if (!(prevFlags & (kFwd | kBwd))) throw std::runtime_error("MPEG-2 skipped macroblock after intra");
        } else {
            throw std::runtime_error("MPEG-2 skipped macroblock in an I picture");
        }
    }

    // Frame prediction of the 16x16 macroblock (and its chroma) with vector mv.
    void frameMc(const Mpeg2Frame& ref, int mx, int my, const int* mv, bool avg) {
        Mpeg2Frame& f = frames[cur];
        int ls = f.lumaStride, cs = f.chromaStride;
        int lh = mbHeight * 16, ch = mbHeight * 8;
        predictBlock(&f.y[size_t(my) * 16 * ls + mx * 16], ls, ref.y.data(), ls, ls, lh, mx * 32 + mv[0],
                     my * 32 + mv[1], 16, 16, avg);
        int cx = mv[0] / 2, cy = mv[1] / 2;
        size_t co = size_t(my) * 8 * cs + mx * 8;
        predictBlock(&f.cb[co], cs, ref.cb.data(), cs, cs, ch, mx * 16 + cx, my * 16 + cy, 8, 8, avg);
        predictBlock(&f.cr[co], cs, ref.cr.data(), cs, cs, ch, mx * 16 + cx, my * 16 + cy, 8, 8, avg);
    }

    // Field prediction of one field (parity 0 = top lines) of the macroblock
    // from field `sel` of ref, mv in field units.
    void fieldMc(const Mpeg2Frame& ref, int mx, int my, int parity, int sel, const int* mv, bool avg) {
        Mpeg2Frame& f = frames[cur];
        int ls = f.lumaStride, cs = f.chromaStride;
        int lh = mbHeight * 8, ch = mbHeight * 4;  // rows per field
        predictBlock(&f.y[(size_t(my) * 16 + parity) * ls + mx * 16], ls * 2, ref.y.data() + sel * ls, ls * 2, ls, lh,
                     mx * 32 + mv[0], my * 16 + mv[1], 16, 8, avg);
        int cx = mv[0] / 2, cy = mv[1] / 2;
        size_t co = (size_t(my) * 8 + parity) * cs + mx * 8;
        predictBlock(&f.cb[co], cs * 2, ref.cb.data() + sel * cs, cs * 2, cs, ch, mx * 16 + cx, my * 8 + cy, 8, 4, avg);
        predictBlock(&f.cr[co], cs * 2, ref.cr.data() + sel * cs, cs * 2, cs, ch, mx * 16 + cx, my * 8 + cy, 8, 4, avg);
    }

    int motionDelta(BitReader& br, int fc) {
        int code = tables().motion.decode(br);
        if (code && br.bit()) code = -code;
        int rsize = fc - 1;
        if (rsize == 0 || code == 0) return code;
        int residual = static_cast<int>(br.read(rsize));
        int delta = ((std::abs(code) - 1) << rsize) + residual + 1;
        return code < 0 ? -delta : delta;
    }

    // motion_vector(r, s) with reconstruction (§7.6.3.1); field vectors of a
    // frame picture predict from and store PMV vertical in frame units.
    void motionVector(BitReader& br, int r, int s, bool field, int* out, int* dmv) {
        for (int t = 0; t < 2; ++t) {
            int fc = fcode[s][t];
            if (fc < 1 || fc > 9) throw std::runtime_error("MPEG-2 invalid f_code");
            int delta = motionDelta(br, fc);
            if (dmv) {
                int d = 0;
                if (br.bit()) d = br.bit() ? -1 : 1;
                dmv[t] = d;
            }
            int f = 1 << (fc - 1);
            int low = -16 * f, high = 16 * f - 1, range = 32 * f;
            int pred = pmv[r][s][t];
            if (field && t == 1) pred >>= 1;
            int v = pred + delta;
            if (v < low) v += range;
            if (v > high) v -= range;
            out[t] = v;
            pmv[r][s][t] = field && t == 1 ? v * 2 : v;
        }
    }

    void macroblock(BitReader& br, int mx, int my) {
        const Tables& t = tables();
        int flags = codingType == 1 ? t.mbTypeI.decode(br) : codingType == 2 ? t.mbTypeP.decode(br) : t.mbTypeB.decode(br);
        bool intra = flags & kIntra;
        int motionType = 2;  // frame
        if ((flags & (kFwd | kBwd)) && !framePredFrameDct) {
            motionType = static_cast<int>(br.read(2));
            if (motionType == 0) throw std::runtime_error("MPEG-2 reserved frame_motion_type");
        }
        bool fieldDct = false;
        if (!framePredFrameDct && (intra || (flags & kPattern))) fieldDct = br.bit();
        if (flags & kQuant) qscale = qscaleFromCode(static_cast<int>(br.read(5)));

        // Motion vectors (§6.2.5.2).
        int mv[2][2][2] = {};  // [r][s][t]
        int fieldSel[2][2] = {};
        int dmv[2] = {};
        if (intra && concealment) {
            int tmp[2];
            motionVector(br, 0, 0, false, tmp, nullptr);
            br.skip(1);  // marker_bit
        }
        for (int s = 0; s < 2; ++s) {
            if (!(flags & (s ? kBwd : kFwd))) continue;
            if (motionType == 1) {  // field
                for (int r = 0; r < 2; ++r) {
                    fieldSel[r][s] = br.bit();
                    motionVector(br, r, s, true, mv[r][s], nullptr);
                }
            } else if (motionType == 2) {  // frame
                motionVector(br, 0, s, false, mv[0][s], nullptr);
                pmv[1][s][0] = pmv[0][s][0];
                pmv[1][s][1] = pmv[0][s][1];
            } else {  // dual prime
                motionVector(br, 0, s, true, mv[0][s], dmv);
                pmv[1][s][0] = pmv[0][s][0];
                pmv[1][s][1] = pmv[0][s][1];
            }
        }
        int cbp = intra ? 63 : 0;
        if (flags & kPattern) cbp = t.cbp.decode(br);

        if (intra) {
            if (!concealment) std::memset(pmv, 0, sizeof(pmv));
        } else {
            resetDc();
            if (codingType == 2 && !(flags & kFwd)) std::memset(pmv, 0, sizeof(pmv));
            predict(mx, my, flags, motionType, mv, fieldSel, dmv);
        }
        prevFlags = flags;

        Mpeg2Frame& f = frames[cur];
        int ls = f.lumaStride, cs = f.chromaStride;
        for (int b = 0; b < 6; ++b) {
            if (!(cbp & (32 >> b))) continue;
            uint8_t rowMask = decodeBlock(br, b, intra);
            uint8_t* dst;
            int stride;
            if (b < 4) {
                int x = mx * 16 + (b & 1) * 8;
                if (fieldDct) {
                    dst = &f.y[(size_t(my) * 16 + (b >> 1)) * ls + x];
                    stride = ls * 2;
                } else {
                    dst = &f.y[(size_t(my) * 16 + (b >> 1) * 8) * ls + x];
                    stride = ls;
                }
            } else {
                dst = &(b == 4 ? f.cb : f.cr)[size_t(my) * 8 * cs + mx * 8];
                stride = cs;
            }
            idct8x8(coef, resid, rowMask);
            for (int y = 0; y < 8; ++y) {
                uint8_t* d = dst + y * stride;
                const int* r = resid + y * 8;
                if (intra) for (int x = 0; x < 8; ++x) d[x] = clamp255(r[x]);
                else for (int x = 0; x < 8; ++x) d[x] = clamp255(d[x] + r[x]);
            }
        }
    }

    void predict(int mx, int my, int flags, int motionType, int mv[2][2][2], int fieldSel[2][2], const int* dmv) {
        if (!(flags & (kFwd | kBwd))) {  // P picture, no motion compensation: zero vector
            int zero[2] = {0, 0};
            frameMc(forwardRef(), mx, my, zero, false);
            return;
        }
        bool avg = false;
        for (int s = 0; s < 2; ++s) {
            if (!(flags & (s ? kBwd : kFwd))) continue;
            const Mpeg2Frame& ref = s ? backwardRef() : forwardRef();
            if (motionType == 2) {
                frameMc(ref, mx, my, mv[0][s], avg);
            } else if (motionType == 1) {
                fieldMc(ref, mx, my, 0, fieldSel[0][s], mv[0][s], avg);
                fieldMc(ref, mx, my, 1, fieldSel[1][s], mv[1][s], avg);
            } else {
                // Dual prime (§7.6.3.6): each field averages the same-parity
                // prediction with one from the opposite field.
                ++dualPrime;
                const int* v = mv[0][s];
                for (int parity = 0; parity < 2; ++parity) {
                    int m = (parity == 0) == topFieldFirst ? 1 : 3;
                    int e = parity == 0 ? -1 : 1;
                    int dv[2] = {((v[0] * m + (v[0] > 0)) >> 1) + dmv[0], ((v[1] * m + (v[1] > 0)) >> 1) + e + dmv[1]};
                    fieldMc(ref, mx, my, parity, parity, v, avg);
                    fieldMc(ref, mx, my, parity, parity ^ 1, dv, true);
                }
            }
            avg = true;
        }
    }

    // Decodes block b into coef (raster order, dequantised, §7.2-7.4); returns
    // a mask of the rows holding non-zero coefficients.
    uint8_t decodeBlock(BitReader& br, int b, bool intra) {
        const Tables& t = tables();
        std::memset(coef, 0, sizeof(coef));
        const uint8_t* scan = kScan[altScan ? 1 : 0];
        const uint8_t* w = intra ? intraQ : nonIntraQ;
        int sum = 0;
        uint8_t rowMask = 0;
        int idx = 0;
        const Vlc<16>* table = &t.dctZero;
        if (intra) {
            int cc = b < 4 ? 0 : b - 3;
            int size = cc ? t.dcChroma.decode(br) : t.dcLuma.decode(br);
            int diff = 0;
            if (size) {
                diff = static_cast<int>(br.read(size));
                if (diff < (1 << (size - 1))) diff -= (1 << size) - 1;
            }
            dcPred[cc] += diff;
            int v = dcPred[cc] << (3 - dcPrecision);
            coef[0] = static_cast<int16_t>(v);
            sum = v;
            rowMask = v ? 1 : 0;
            idx = 1;
            if (intraVlc) table = &t.dctOne;
        } else if (br.peek(1)) {  // first coefficient of a non-intra block: '1s' = run 0, level 1
            br.skip(1);
            int level = br.bit() ? -1 : 1;
            int v = ((2 + 1) * w[0] * qscale) >> 5;
            if (level < 0) v = -v;
            coef[0] = static_cast<int16_t>(v);
            sum = v;
            rowMask = v ? 1 : 0;
            idx = 1;
        }
        for (;;) {
            const VlcEntry& e = table->table[br.peek(16)];
            if (!e.len) throw std::runtime_error("invalid MPEG-2 DCT coefficient code");
            br.skip(e.len);
            int run, level;
            if (e.value == kEob) break;
            if (e.value == kEsc) {
                run = static_cast<int>(br.read(6));
                level = static_cast<int>(br.read(12));
                if (level >= 2048) level -= 4096;
                if (level == 0) throw std::runtime_error("invalid MPEG-2 escape level");
            } else {
                run = e.value >> 8;
                level = e.value & 0xFF;
                if (br.bit()) level = -level;
            }
            idx += run;
            if (idx > 63) throw std::runtime_error("MPEG-2 DCT coefficient index past 63");
            int pos = scan[idx++];
            int a = std::abs(level);
            int v = intra ? (a * w[pos] * qscale) >> 4 : ((2 * a + 1) * w[pos] * qscale) >> 5;
            if (level < 0) v = -v;
            v = std::clamp(v, -2048, 2047);
            coef[pos] = static_cast<int16_t>(v);
            sum += v;
            if (v) rowMask |= static_cast<uint8_t>(1 << (pos >> 3));
        }
        br.check();
        if (!(sum & 1)) {  // mismatch control (§7.4.4)
            coef[63] = static_cast<int16_t>(coef[63] & 1 ? coef[63] - 1 : coef[63] + 1);
            rowMask |= 0x80;
        }
        return rowMask;
    }
};

// ---------------------------------------------------------------------------

Mpeg2Decoder::Mpeg2Decoder() : impl_(std::make_unique<Impl>()) { tables(); }
Mpeg2Decoder::~Mpeg2Decoder() = default;

void Mpeg2Decoder::push(const uint8_t* data, size_t size) { impl_->buf.insert(impl_->buf.end(), data, data + size); }
void Mpeg2Decoder::pushEnd() { impl_->ended = true; }
const Mpeg2Frame* Mpeg2Decoder::decode() { return impl_->decode(); }
void Mpeg2Decoder::reset() { impl_->reset(); }
int Mpeg2Decoder::width() const { return impl_->width; }
int Mpeg2Decoder::height() const { return impl_->height; }
double Mpeg2Decoder::frameRate() const { return kFrameRates[impl_->frameRateCode & 15]; }
uint64_t Mpeg2Decoder::sliceErrors() const { return impl_->sliceErrors; }
uint64_t Mpeg2Decoder::dualPrimeMacroblocks() const { return impl_->dualPrime; }

void mpeg2ToRgba(const Mpeg2Frame& f, uint8_t* dst, size_t dstStride, bool deinterlace) {
    // BT.601 limited range, 16.16 fixed point.
    constexpr int kY = 76309, kRv = 104597, kGu = 25675, kGv = 53279, kBu = 132201;
    bool blend = deinterlace && !f.progressive;
    for (int y = 0; y < f.height; ++y) {
        // Interlaced 4:2:0 keeps each field's chroma on its own lines.
        int cy = f.progressive ? y >> 1 : ((y >> 2) << 1) | (y & 1);
        int y2 = std::min(y + 1, f.height - 1);
        int cy2 = f.progressive ? y2 >> 1 : ((y2 >> 2) << 1) | (y2 & 1);
        const uint8_t* ly = &f.y[size_t(y) * f.lumaStride];
        const uint8_t* ly2 = &f.y[size_t(y2) * f.lumaStride];
        const uint8_t* lu = &f.cb[size_t(cy) * f.chromaStride];
        const uint8_t* lv = &f.cr[size_t(cy) * f.chromaStride];
        const uint8_t* lu2 = &f.cb[size_t(cy2) * f.chromaStride];
        const uint8_t* lv2 = &f.cr[size_t(cy2) * f.chromaStride];
        uint8_t* d = dst + size_t(y) * dstStride;
        for (int x = 0; x < f.width; ++x) {
            int Y = ly[x], U = lu[x >> 1], V = lv[x >> 1];
            if (blend) {
                Y = (Y + ly2[x] + 1) >> 1;
                U = (U + lu2[x >> 1] + 1) >> 1;
                V = (V + lv2[x >> 1] + 1) >> 1;
            }
            int c = (Y - 16) * kY + 32768;
            U -= 128;
            V -= 128;
            d[0] = clamp255((c + kRv * V) >> 16);
            d[1] = clamp255((c - kGu * U - kGv * V) >> 16);
            d[2] = clamp255((c + kBu * U) >> 16);
            d[3] = 255;
            d += 4;
        }
    }
}
