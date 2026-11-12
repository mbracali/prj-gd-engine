// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/song_stream.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>

#include "data/disc.h"
#include "data/qbkey.h"

namespace {
constexpr size_t kPacket = 2048;

// gh4's pairs per bank, routed to the mixer's stems: bank 1 the drums (4
// pairs), bank 2 guitar, bass, vocals, bank 3 the rest (2 pairs) [I: D8,
// from the pairs' levels against the charts; the XEX's table to find].
constexpr int kGh4Pairs[3][4] = {
    {SongStream::Song, SongStream::Song, SongStream::Song, SongStream::Song},
    {SongStream::Guitar, SongStream::Rhythm, SongStream::Song, -1},
    {SongStream::Song, SongStream::Song, -1, -1},
};
}  // namespace

std::unique_ptr<SongStream> SongStream::open(const Disc& disc, const std::string& song, std::string& error) {
    auto s = std::unique_ptr<SongStream>(new SongStream());
    s->disc_ = &disc;
    const std::string base = "DATA/MUSIC/" + song;
    s->gh4_ = disc.exists(base + "_1.fsb.xen");
    try {
        if (s->gh4_) {
            for (int b = 0; b < 3; ++b) {
                auto src = std::make_unique<Source>();
                const std::string path = base + "_" + std::to_string(b + 1) + ".fsb.xen";
                if (!disc.exists(path)) continue;
                src->bank = loadFsb(disc, path, "");
                if (src->bank.samples.empty()) continue;
                src->sample = &src->bank.samples[0];
                src->channels = src->sample->channels;
                if (src->channels < 2 || src->channels % 2 || src->channels > 8) {
                    error = path + ": " + std::to_string(src->channels) + " channels";
                    return nullptr;
                }
                for (int p = 0; p < src->channels / 2; ++p) src->pairStem.push_back(kGh4Pairs[b][p]);
                s->sources_.push_back(std::move(src));
            }
        } else {
            auto bank = loadFsb(disc, base + ".fsb.xen", base + ".dat.xen");
            static const char* const kStem[StemCount] = {"_guitar", "_rhythm", "_song"};
            for (int i = 0; i < StemCount; ++i) {
                auto src = std::make_unique<Source>();
                src->bank = bank;
                src->sample = src->bank.stem(qbkey::key(song + kStem[i]));
                if (!src->sample) continue;
                if (src->sample->channels != 2) {
                    error = song + kStem[i] + ": not a stereo stem";
                    return nullptr;
                }
                src->pairStem = {i};
                s->sources_.push_back(std::move(src));
            }
        }
    } catch (const std::exception& e) {
        error = e.what();
        return nullptr;
    }
    if (s->sources_.empty()) {
        error = song + ": no song banks";
        return nullptr;
    }
    for (auto& src : s->sources_) {
        s->rate_ = src->sample->rate;
        s->length_ = std::max(s->length_, src->sample->length);
        // gh4's interleaved streams decode as one multichannel XMA2 [I: the
        // packet headers' skip counts route the packets].
        if (!src->xma.open(s->gh4_ ? 2 : 1, src->sample->rate, src->channels, error)) return nullptr;
    }
    return s;
}

std::unique_ptr<SongStream> SongStream::openPreview(const Disc& disc, const std::string& song, std::string& error) {
    auto s = std::unique_ptr<SongStream>(new SongStream());
    s->disc_ = &disc;
    s->gh4_ = true;
    const std::string path = "DATA/MUSIC/" + song + "_preview.fsb.xen";
    if (!disc.exists(path)) {
        error = "no " + path;
        return nullptr;
    }
    try {
        auto src = std::make_unique<Source>();
        src->bank = loadFsb(disc, path, "");
        if (src->bank.samples.empty()) {
            error = path + ": no sample";
            return nullptr;
        }
        src->sample = &src->bank.samples[0];
        src->channels = src->sample->channels;
        if (src->channels < 2 || src->channels % 2 || src->channels > 8) {
            error = path + ": " + std::to_string(src->channels) + " channels";
            return nullptr;
        }
        for (int p = 0; p < src->channels / 2; ++p) src->pairStem.push_back(Song);
        s->sources_.push_back(std::move(src));
    } catch (const std::exception& e) {
        error = e.what();
        return nullptr;
    }
    Source& src = *s->sources_[0];
    s->rate_ = src.sample->rate;
    s->length_ = src.sample->length;
    if (!src.xma.open(2, src.sample->rate, src.channels, error)) return nullptr;
    return s;
}

bool SongStream::seek(double ms) {
    const uint64_t target = uint64_t(std::max(0.0, ms) * rate_ / 1000.0);
    for (auto& sp : sources_) {
        Source& s = *sp;
        if (s.ended) continue;
        if (gh4_) {
            // gh4: decode from the start and drop the frames before the
            // target (the bank's seek table's unit is not known yet) [I].
            s.next = 0, s.produced = 0, s.skip = uint32_t(target);
            continue;
        }
        // The bank's XMA seek table: the frames before each packet; start at
        // the last packet at or before the target, drop the frames up to it.
        const std::vector<uint32_t>& t = s.sample->seek;
        size_t k = 0;
        while (k + 1 < t.size() && t[k + 1] <= target && uint64_t(k + 1) * kPacket < s.sample->stored) ++k;
        const uint64_t at = t.empty() ? 0 : t[k];
        s.next = uint64_t(k) * kPacket;
        s.produced = uint32_t(std::min<uint64_t>(at, s.sample->length));
        s.skip = uint32_t(target > at ? target - at : 0);
    }
    return true;
}

bool SongStream::refill(Source& s, size_t frames) {
    std::vector<uint8_t> packet(kPacket);
    while (!s.ended && avail(s) < frames) {
        if (s.pendingAt > 0 && s.pendingAt == s.pending.size()) s.pending.clear(), s.pendingAt = 0;
        if (s.next >= s.sample->stored) {
            if (!s.xma.flush(s.pending, error_)) SDL_Log("song audio: %s", error_.c_str());
            s.ended = true;
            break;
        }
        const size_t n = size_t(std::min<uint64_t>(kPacket, s.sample->stored - s.next));
        const uint64_t at = s.sample->offset + s.next;
        try {
            disc_->readRange(s.bank.path, at, packet.data(), n);
        } catch (const std::exception& e) {
            error_ = e.what();
            s.ended = true;
            return false;
        }
        if (s.bank.encrypted) s.bank.decrypt(packet.data(), n, at);
        s.next += n;
        if (!s.xma.feed(packet.data(), n, s.pending, error_)) {
            SDL_Log("song audio: %s at %llu", error_.c_str(), static_cast<unsigned long long>(at));
            s.ended = true;
            return false;
        }
    }
    return true;
}

int SongStream::decode(float* const* out, int frames) {
    for (int c = 0; c < 2 * StemCount; ++c) std::fill(out[c], out[c] + frames, 0.0f);
    bool any = false;
    for (auto& sp : sources_) {
        Source& s = *sp;
        refill(s, size_t(frames) + s.skip);
        if (s.skip) {  // a seek's samples before the start
            const size_t drop = std::min<size_t>(s.skip, avail(s));
            s.pendingAt += drop * size_t(s.channels), s.skip -= uint32_t(drop), s.produced += uint32_t(drop);
            refill(s, size_t(frames));
        }
        // Never past the stem's length (the decoder's last packet pads).
        const size_t left = s.produced < s.sample->length ? size_t(s.sample->length - s.produced) : 0;
        const int got = int(std::min({avail(s), size_t(frames), left}));
        const size_t ch = size_t(s.channels);
        for (size_t p = 0; p < s.pairStem.size(); ++p) {
            const int stem = s.pairStem[p];
            if (stem < 0) continue;
            float* l = out[2 * stem];
            float* r = out[2 * stem + 1];
            for (int f = 0; f < got; ++f) {
                l[f] += s.pending[s.pendingAt + ch * size_t(f) + 2 * p];
                r[f] += s.pending[s.pendingAt + ch * size_t(f) + 2 * p + 1];
            }
        }
        s.pendingAt += size_t(got) * ch;
        s.produced += uint32_t(got);
        if (got > 0 || (s.produced < s.sample->length && !s.ended)) any = true;
    }
    return any ? frames : 0;
}

std::vector<float> SongStream::pairRms(int frames) {
    std::vector<float> rms;
    for (auto& sp : sources_) {
        Source& s = *sp;
        refill(s, size_t(frames));
        const int got = int(std::min(avail(s), size_t(frames)));
        const size_t ch = size_t(s.channels);
        for (size_t p = 0; p < ch / 2; ++p) {
            double sum = 0;
            for (int f = 0; f < got; ++f) {
                const float a = s.pending[s.pendingAt + ch * size_t(f) + 2 * p], b = s.pending[s.pendingAt + ch * size_t(f) + 2 * p + 1];
                sum += double(a) * a + double(b) * b;
            }
            rms.push_back(got ? float(std::sqrt(sum / (2.0 * got))) : 0.0f);
        }
        s.pendingAt += size_t(got) * ch;
        s.produced += uint32_t(got);
    }
    return rms;
}
