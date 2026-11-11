// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/fsb.h"

#include <cstring>
#include <stdexcept>

#include "data/bytes.h"
#include "data/disc.h"

namespace {

constexpr char kKey[] = "5atu6w4zaw";
constexpr size_t kKeyLen = sizeof kKey - 1;

uint8_t reverseBits(uint8_t b) {
    b = uint8_t((b & 0xF0) >> 4 | (b & 0x0F) << 4);
    b = uint8_t((b & 0xCC) >> 2 | (b & 0x33) << 2);
    return uint8_t((b & 0xAA) >> 1 | (b & 0x55) << 1);
}

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

}  // namespace

void fsbDecrypt(uint8_t* data, size_t n, uint64_t fileOffset) {
    size_t k = size_t(fileOffset % kKeyLen);
    for (size_t i = 0; i < n; ++i) {
        data[i] = reverseBits(uint8_t(data[i] ^ uint8_t(kKey[k])));
        if (++k == kKeyLen) k = 0;
    }
}

const FsbSample* FsbBank::stem(uint32_t nameKey) const {
    for (auto& [key, index] : stems)
        if (key == nameKey && index < samples.size()) return &samples[index];
    return nullptr;
}

FsbBank loadFsb(const Disc& disc, const std::string& fsbPath, const std::string& datPath) {
    FsbBank bank;
    bank.path = fsbPath;
    auto fileSize = disc.size(fsbPath);
    if (!fileSize || *fileSize < 24) throw std::runtime_error("no bank " + fsbPath);
    uint8_t head[24];
    disc.readRange(fsbPath, 0, head, sizeof head);
    if (std::memcmp(head, "FSB3", 4) != 0) {
        fsbDecrypt(head, sizeof head, 0);
        bank.encrypted = true;
    }
    if (std::memcmp(head, "FSB3", 4) != 0) throw std::runtime_error(fsbPath + ": not an FSB3 bank");
    const uint32_t n = le32(head + 4), shdr = le32(head + 8), dsize = le32(head + 12);
    if (24 + uint64_t(shdr) + dsize != *fileSize) throw std::runtime_error(fsbPath + ": sizes do not add up");
    Bytes h(shdr);
    disc.readRange(fsbPath, 24, h.data(), h.size());
    if (bank.encrypted) fsbDecrypt(h.data(), h.size(), 24);
    size_t o = 0;
    uint64_t pos = 24 + uint64_t(shdr);
    for (uint32_t i = 0; i < n; ++i) {
        if (o + 80 > h.size()) throw std::runtime_error(fsbPath + ": sample header past the end");
        const uint8_t* s = h.data() + o;
        FsbSample sm;
        const uint16_t size = le16(s);
        sm.name.assign(reinterpret_cast<const char*>(s + 2), strnlen(reinterpret_cast<const char*>(s + 2), 30));
        sm.length = le32(s + 32);
        sm.stored = le32(s + 36);
        sm.mode = le32(s + 48);
        sm.rate = int(le32(s + 52));
        sm.channels = le16(s + 62);
        sm.offset = pos;
        pos += sm.stored;
        if (size < 80) throw std::runtime_error(fsbPath + ": short sample header");
        if (size >= 80 + 24 && o + size <= h.size())
            for (size_t k = 80 + 24; k + 4 <= size; k += 4) sm.seek.push_back(le32(s + k));
        o += size;
        bank.samples.push_back(std::move(sm));
    }
    if (pos != *fileSize) throw std::runtime_error(fsbPath + ": sample data does not end the file");
    if (!datPath.empty() && disc.exists(datPath)) {
        Bytes d = disc.read(datPath);
        if (d.size() < 8) throw std::runtime_error(datPath + ": too short");
        const uint32_t count = be32(d.data());
        if (8 + size_t(count) * 20 != d.size()) throw std::runtime_error(datPath + ": size");
        if (be32(d.data() + 4) != *fileSize) throw std::runtime_error(datPath + ": bank size differs");
        for (uint32_t i = 0; i < count; ++i)
            bank.stems.emplace_back(be32(d.data() + 8 + 20 * i), be32(d.data() + 12 + 20 * i));
    }
    return bank;
}
