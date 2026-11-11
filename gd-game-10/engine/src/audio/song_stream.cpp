// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/song_stream.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cstring>
#include <exception>

#include "data/disc.h"
#include "data/qbkey.h"

namespace {
constexpr size_t kPacket = 2048;
}

std::unique_ptr<SongStream> SongStream::open(const Disc& disc, const std::string& song, std::string& error) {
    auto s = std::unique_ptr<SongStream>(new SongStream());
    s->disc_ = &disc;
    try {
        s->bank_ = loadFsb(disc, "DATA/MUSIC/" + song + ".fsb.xen", "DATA/MUSIC/" + song + ".dat.xen");
    } catch (const std::exception& e) {
        error = e.what();
        return nullptr;
    }
    static const char* const kStem[StemCount] = {"_guitar", "_rhythm", "_song"};
    for (int i = 0; i < StemCount; ++i) {
        StemState& st = s->stems_[size_t(i)];
        st.sample = s->bank_.stem(qbkey::key(song + kStem[i]));
        if (!st.sample) {
            st.ended = true;
            continue;
        }
        if (st.sample->channels != 2) {
            error = song + kStem[i] + ": not a stereo stem";
            return nullptr;
        }
        s->rate_ = st.sample->rate;
        s->length_ = std::max(s->length_, st.sample->length);
        if (!st.xma.open(1, st.sample->rate, 2, error)) return nullptr;
    }
    return s;
}

bool SongStream::seek(double ms) {
    const uint64_t target = uint64_t(std::max(0.0, ms) * rate_ / 1000.0);
    for (StemState& s : stems_) {
        if (!s.sample || s.ended) continue;
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

bool SongStream::refill(StemState& s, size_t frames) {
    std::vector<uint8_t> packet(kPacket);
    while (!s.ended && (s.pending.size() - s.pendingAt) / 2 < frames) {
        if (s.pendingAt > 0 && s.pendingAt == s.pending.size()) s.pending.clear(), s.pendingAt = 0;
        if (s.next >= s.sample->stored) {
            if (!s.xma.flush(s.pending, error_)) SDL_Log("song audio: %s", error_.c_str());
            s.ended = true;
            break;
        }
        const size_t n = size_t(std::min<uint64_t>(kPacket, s.sample->stored - s.next));
        const uint64_t at = s.sample->offset + s.next;
        try {
            disc_->readRange(bank_.path, at, packet.data(), n);
        } catch (const std::exception& e) {
            error_ = e.what();
            s.ended = true;
            return false;
        }
        if (bank_.encrypted) fsbDecrypt(packet.data(), n, at);
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
    bool any = false;
    for (int i = 0; i < StemCount; ++i) {
        StemState& s = stems_[size_t(i)];
        float* l = out[2 * i];
        float* r = out[2 * i + 1];
        int got = 0;
        if (s.sample) {
            refill(s, size_t(frames) + s.skip);
            if (s.skip) {  // a seek's samples before the start
                const size_t drop = std::min<size_t>(s.skip, (s.pending.size() - s.pendingAt) / 2);
                s.pendingAt += drop * 2, s.skip -= uint32_t(drop), s.produced += uint32_t(drop);
                refill(s, size_t(frames));
            }
            const size_t avail = (s.pending.size() - s.pendingAt) / 2;
            // Never past the stem's length (the decoder's last packet pads).
            const size_t left = s.produced < s.sample->length ? size_t(s.sample->length - s.produced) : 0;
            got = int(std::min({avail, size_t(frames), left}));
            for (int f = 0; f < got; ++f) {
                l[f] = s.pending[s.pendingAt + 2 * size_t(f)];
                r[f] = s.pending[s.pendingAt + 2 * size_t(f) + 1];
            }
            s.pendingAt += size_t(got) * 2;
            s.produced += uint32_t(got);
        }
        for (int f = got; f < frames; ++f) l[f] = r[f] = 0.0f;
        if (got > 0 || (s.sample && s.produced < s.sample->length && !s.ended)) any = true;
    }
    return any ? frames : 0;
}
