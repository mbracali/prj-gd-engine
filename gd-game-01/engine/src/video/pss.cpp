// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "video/pss.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace {

constexpr size_t kChunk = 64 * 1024;
constexpr size_t kAudioHeaderSize = 0x28;  // SShd (8 + 0x18) + SSbd (8)

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

}  // namespace

PssMovie::PssMovie(Reader reader) : reader_(std::move(reader)) {
    if (!reader_) throw std::runtime_error("PSS reader is empty");
}

PssMovie::Reader PssMovie::fileReader(const std::string& path) {
    auto file = std::make_shared<std::ifstream>(path, std::ios::binary);
    if (!*file) throw std::runtime_error("cannot open " + path);
    return [file](uint8_t* dst, size_t n) -> size_t {
        file->read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(n));
        return static_cast<size_t>(file->gcount());
    };
}

bool PssMovie::fill(size_t n) {
    while (in_.size() - pos_ < n) {
        if (eof_) return false;
        if (pos_ > kChunk) {
            in_.erase(in_.begin(), in_.begin() + static_cast<ptrdiff_t>(pos_));
            pos_ = 0;
        }
        size_t old = in_.size();
        in_.resize(old + kChunk);
        size_t got = reader_(in_.data() + old, kChunk);
        in_.resize(old + got);
        if (got == 0) eof_ = true;
    }
    return true;
}

bool PssMovie::demuxPacket() {
    for (;;) {
        if (!fill(4)) return false;
        const uint8_t* p = &in_[pos_];
        if (p[0] != 0 || p[1] != 0 || p[2] != 1) {  // resync
            ++pos_;
            continue;
        }
        int id = p[3];
        if (id == 0xBA) {  // pack header
            if (!fill(14)) return false;
            p = &in_[pos_];
            if ((p[4] & 0xC0) == 0x40) pos_ += 14 + (p[13] & 7);
            else pos_ += 12;  // MPEG-1 pack
            continue;
        }
        if (id == 0xB9) {  // program end
            pos_ += 4;
            continue;
        }
        if (id < 0xBB) {
            ++pos_;
            continue;
        }
        if (!fill(6)) return false;
        size_t len = 6 + (size_t(in_[pos_ + 4]) << 8 | in_[pos_ + 5]);
        if (!fill(len)) return false;
        p = &in_[pos_];
        pos_ += len;
        if (id != 0xE0 && id != 0xBD) continue;  // system header, padding, other streams

        size_t hdr = 6;
        if ((p[6] & 0xC0) == 0x80) {
            hdr = 9 + size_t(p[8]);
        } else {  // MPEG-1 PES header
            while (hdr < len && p[hdr] == 0xFF) ++hdr;
            if (hdr < len && (p[hdr] & 0xC0) == 0x40) hdr += 2;
            if (hdr < len) {
                if ((p[hdr] & 0xF0) == 0x20) hdr += 5;
                else if ((p[hdr] & 0xF0) == 0x30) hdr += 10;
                else ++hdr;
            }
        }
        if (hdr > len) throw std::runtime_error("PSS: PES header longer than its packet");
        const uint8_t* payload = p + hdr;
        size_t n = len - hdr;
        if (id == 0xE0) {
            video_.push(payload, n);
        } else if (n >= 4 && payload[0] == 0xFF) {  // sub-stream FF A0 00 00
            onAudio(payload + 4, n - 4);
        }
        return true;
    }
}

void PssMovie::onAudio(const uint8_t* p, size_t n) {
    if (audioHeader_.size() < kAudioHeaderSize) {
        size_t take = std::min(n, kAudioHeaderSize - audioHeader_.size());
        audioHeader_.insert(audioHeader_.end(), p, p + take);
        p += take;
        n -= take;
        if (audioHeader_.size() < kAudioHeaderSize) return;
        const uint8_t* h = audioHeader_.data();
        if (std::memcmp(h, "SShd", 4) != 0 || std::memcmp(h + 0x20, "SSbd", 4) != 0)
            throw std::runtime_error("PSS: audio stream without SShd/SSbd header");
        uint32_t type = le32(h + 8), channels = le32(h + 16), interleave = le32(h + 20);
        if (type != 1) throw std::runtime_error("PSS: audio is not 16-bit PCM");
        if (channels != 2) throw std::runtime_error("PSS: audio is not stereo");
        if (interleave == 0 || interleave % 2 || interleave > 0x10000)
            throw std::runtime_error("PSS: bad audio interleave");
        headerRate_ = static_cast<int>(le32(h + 12));
        interleave_ = interleave;
        audioLeft_ = le32(h + 0x24);
    }
    n = static_cast<size_t>(std::min<uint64_t>(n, audioLeft_));
    audioLeft_ -= n;
    // Data: interleave_ bytes of left samples, then interleave_ bytes of right.
    size_t pair = interleave_ * 2, half = interleave_ / 2;
    auto emit = [&](const uint8_t* b) {
        size_t base = audio_.size();
        audio_.resize(base + half * 2);
        for (size_t i = 0; i < half; ++i) {
            audio_[base + 2 * i] = static_cast<int16_t>(b[2 * i] | b[2 * i + 1] << 8);
            audio_[base + 2 * i + 1] = static_cast<int16_t>(b[interleave_ + 2 * i] | b[interleave_ + 2 * i + 1] << 8);
        }
    };
    if (!block_.empty()) {
        size_t take = std::min(n, pair - block_.size());
        block_.insert(block_.end(), p, p + take);
        p += take;
        n -= take;
        if (block_.size() < pair) return;
        emit(block_.data());
        block_.clear();
    }
    for (; n >= pair; p += pair, n -= pair) emit(p);
    block_.assign(p, p + n);
}

const Mpeg2Frame* PssMovie::nextFrame() {
    for (;;) {
        if (const Mpeg2Frame* f = video_.decode()) return f;
        if (finished_) {
            done_ = true;
            return nullptr;
        }
        if (!demuxPacket()) {
            video_.pushEnd();
            finished_ = true;
        }
    }
}

void PssMovie::prefetchAudio(size_t frames) {
    while (!finished_ && audio_.size() / 2 < frames) {
        if (!demuxPacket()) {
            video_.pushEnd();
            finished_ = true;
        }
    }
}

size_t PssMovie::takeAudio(std::vector<int16_t>& out) {
    size_t frames = audio_.size() / 2;
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
    return frames;
}
