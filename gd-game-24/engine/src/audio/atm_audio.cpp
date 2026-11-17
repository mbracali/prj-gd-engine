// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/atm_audio.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace gd24 {
namespace {

constexpr size_t kHeaderSize = 0x38;
constexpr uint32_t kTrackCount = 5;
constexpr uint32_t kKeyXor[4] = {0x5156B456, 0x28CBD70B, 0x4B5AEFFC, 0x6165E84B};

uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

uint32_t load32(const std::array<uint8_t, 16>& bytes, size_t offset) {
    return le32(bytes.data() + offset);
}

uint32_t rotateRight(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
uint32_t rotateLeft(uint32_t x, unsigned n) { return (x << n) | (x >> (32 - n)); }

uint32_t f1(uint32_t x) { return rotateRight(x, 7) ^ rotateRight(x, 18) ^ (x >> 3); }
uint32_t f2(uint32_t x) { return rotateRight(x, 17) ^ rotateRight(x, 19) ^ (x >> 10); }
uint32_t expand(const std::array<uint32_t, 1280>& w, size_t i) {
    return f2(w[i - 2]) + w[i - 7] + f1(w[i - 15]) + w[i - 16] + uint32_t(i);
}

class Hc128 {
public:
    Hc128(const std::array<uint8_t, 16>& key, const std::array<uint8_t, 16>& iv) {
        std::array<uint32_t, 1280> w{};
        for (size_t i = 0; i < 4; ++i) {
            w[i] = w[i + 4] = load32(key, i * 4);
            w[i + 8] = w[i + 12] = load32(iv, i * 4);
        }
        for (size_t i = 16; i < w.size(); ++i) w[i] = expand(w, i);
        for (size_t i = 0; i < 512; ++i) p_[i] = w[i + 256], q_[i] = w[i + 768];

        // HC-128 warms its two tables for 1024 steps before emitting bytes.
        for (uint32_t i = 0; i < 1024; ++i) (void)nextWord();
    }

    void xorBytes(uint8_t* bytes, size_t size) {
        size_t pos = 0;
        while (pos < size) {
            const uint32_t word = nextWord();
            for (unsigned b = 0; b < 4 && pos < size; ++b, ++pos)
                bytes[pos] ^= uint8_t(word >> (8 * b));
        }
    }

private:
    uint32_t h1(uint32_t x) const {
        return q_[x & 0xff] + q_[256 + ((x >> 16) & 0xff)];
    }
    uint32_t h2(uint32_t x) const {
        return p_[x & 0xff] + p_[256 + ((x >> 16) & 0xff)];
    }
    uint32_t nextWord() {
        const uint32_t i = counter_ & 0x1ff;
        const uint32_t i1 = (i + 1) & 0x1ff;
        const uint32_t im3 = (i - 3) & 0x1ff;
        const uint32_t im10 = (i - 10) & 0x1ff;
        const uint32_t im12 = (i - 12) & 0x1ff;
        uint32_t out;
        if (counter_ < 512) {
            p_[i] += rotateRight(p_[im10], 8) + (rotateRight(p_[im3], 10) ^ rotateRight(p_[i1], 23));
            out = h1(p_[im12]) ^ p_[i];
        } else {
            q_[i] += rotateLeft(q_[im10], 8) + (rotateLeft(q_[im3], 10) ^ rotateLeft(q_[i1], 23));
            out = h2(q_[im12]) ^ q_[i];
        }
        counter_ = (counter_ + 1) & 0x3ff;
        return out;
    }

    std::array<uint32_t, 512> p_{};
    std::array<uint32_t, 512> q_{};
    uint32_t counter_ = 0;
};

std::array<uint8_t, 16> makeIv(const uint8_t* header, uint32_t track) {
    std::array<uint8_t, 16> iv{};
    constexpr uint32_t stride = 3;  // ((8 - 5) * 4) / 5 + 1
    for (uint32_t i = 0; i < iv.size(); ++i) {
        const uint32_t index = 0x28 + track * (stride - 1) + i % stride;
        const uint8_t byte = header[index];
        const uint8_t previous = i ? iv[i - 1] : 0;
        const uint8_t sum = uint8_t(previous + uint8_t(~byte) + uint8_t(i) + uint8_t(track));
        iv[i] = byte ^ sum;
    }
    return iv;
}

void xorTotalBytes(std::vector<uint8_t>& track, uint32_t total) {
    for (size_t i = 0; i < 8; ++i) track[i] ^= uint8_t(total >> (8 * (i & 3)));
}

}  // namespace

bool decodeAtm(const std::vector<uint8_t>& atm,
               std::array<std::vector<uint8_t>, 5>& tracks,
               std::string& error) {
    tracks = {};
    error.clear();
    if (atm.size() < kHeaderSize) {
        error = "ATM file is shorter than its 0x38-byte header";
        return false;
    }
    const uint32_t count = le32(atm.data() + 4);
    const uint32_t chunkSize = le32(atm.data() + 0x0c);
    if (count != kTrackCount || !chunkSize || chunkSize > (1u << 20)) {
        error = "unsupported ATM track count or chunk size";
        return false;
    }

    std::array<uint32_t, kTrackCount> lengths{};
    uint32_t maxLength = 0;
    uint64_t total64 = 0;
    for (uint32_t i = 0; i < count; ++i) {
        lengths[i] = le32(atm.data() + 0x14 + i * 4);
        maxLength = std::max(maxLength, lengths[i]);
        total64 += lengths[i];
    }
    if (!maxLength || total64 > std::numeric_limits<uint32_t>::max()) {
        error = "invalid ATM stream lengths";
        return false;
    }
    const uint64_t fullRounds = maxLength / chunkSize;
    const uint64_t remainder = maxLength % chunkSize;
    const uint64_t payloadSize = uint64_t(maxLength) * count;
    if (payloadSize > atm.size() - kHeaderSize || payloadSize > std::numeric_limits<size_t>::max()) {
        error = "ATM interleaved stream data is truncated";
        return false;
    }

    const uint8_t* header = atm.data();
    const uint32_t keyWords[4] = {
        le32(header + 0x10) ^ kKeyXor[0],
        le32(header + 0x08) ^ kKeyXor[1],
        le32(header + 0x34) ^ kKeyXor[2],
        le32(header + 0x00) ^ kKeyXor[3]
    };
    std::array<uint8_t, 16> key{};
    for (size_t i = 0; i < 4; ++i)
        for (unsigned b = 0; b < 4; ++b) key[i * 4 + b] = uint8_t(keyWords[i] >> (8 * b));

    for (uint32_t track = 0; track < count; ++track) {
        tracks[track].resize(lengths[track]);
        size_t written = 0;
        for (uint64_t round = 0; written < tracks[track].size(); ++round) {
            const bool finalPartialRound = round >= fullRounds;
            const uint64_t stride = finalPartialRound ? remainder : chunkSize;
            if (!stride) {
                tracks = {};
                error = "ATM track length exceeds the interleaved rounds";
                return false;
            }
            const size_t amount = std::min<size_t>(size_t(stride), tracks[track].size() - written);
            const uint64_t offset = finalPartialRound
                ? kHeaderSize + fullRounds * count * chunkSize + track * remainder
                : kHeaderSize + (round * count + track) * chunkSize;
            if (offset + amount > atm.size()) {
                tracks = {};
                error = "ATM interleaved stream offset is outside the file";
                return false;
            }
            std::memcpy(tracks[track].data() + written, atm.data() + size_t(offset), amount);
            written += amount;
        }
        Hc128 cipher(key, makeIv(header, track));
        cipher.xorBytes(tracks[track].data(), tracks[track].size());
        if (tracks[track].size() < 8) {
            tracks = {};
            error = "ATM stream is too short for its length checksum";
            return false;
        }
        xorTotalBytes(tracks[track], uint32_t(total64));
    }
    return true;
}

}  // namespace gd24
