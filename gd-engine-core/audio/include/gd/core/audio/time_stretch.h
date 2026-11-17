// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

// Time stretch that keeps the pitch (Enhanced > Song Speed, GDALL-053): WSOLA
// (waveform-similarity overlap-add). Windows of ~46 ms are taken from the
// source every `tempo` x 23 ms and overlap-added every 23 ms with a periodic
// Hann window (the two halves sum to 1); each window's start is moved by up to
// +-12 ms to where the source looks most like the natural continuation of the
// previous window (normalised cross-correlation of a mono mix, every 4th
// frame), so the waveform keeps its phase and no echo or click comes in. Every
// channel uses the same positions, so the channels stay in step (the players'
// guitar tracks can still be muted or bent one by one after it). [I] An
// Enhancement: none of the original games stretches time without changing the
// pitch (GH2 / GH80s practice resample; docs/spikes/study_gh2dx.md).
//
// Header-only, no allocation once reset() ran: safe on the audio thread.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gd::core::audio {

class TimeStretch {
public:
    // `channels` source channels at `rate` Hz.
    void reset(int channels, int rate) {
        channels_ = std::max(1, channels);
        win_ = std::max(64, int(std::lround(rate * 0.046)) & ~1);
        hop_ = win_ / 2;
        search_ = std::max(8, int(std::lround(rate * 0.012)));
        window_.assign(size_t(win_), 0.0f);
        for (int i = 0; i < win_; ++i) window_[size_t(i)] = float(0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * i / win_));
        cap_ = win_ * 4 + search_ * 4 + 8;
        in_.assign(size_t(channels_), std::vector<float>(size_t(cap_), 0.0f));
        tail_.assign(size_t(channels_), std::vector<float>(size_t(hop_), 0.0f));
        out_.assign(size_t(channels_), std::vector<float>(size_t(hop_), 0.0f));
        mono_.assign(size_t(cap_), 0.0f);
        pullPtrs_.assign(size_t(channels_), nullptr);
        restart();
    }
    // Forget the source read so far (a seek, a new song); the tempo stays.
    void restart() {
        inLen_ = 0, inBase_ = 0, ana_ = 0, prev_ = -1, outAt_ = hop_, ended_ = false;
        for (auto& t : tail_) std::fill(t.begin(), t.end(), 0.0f);
    }
    // Source frames per output frame (1.5 = 150 %: the song ends at its length / 1.5).
    void setTempo(double tempo) { tempo_ = std::clamp(tempo, 0.25, 4.0); }
    double tempo() const { return tempo_; }
    int channels() const { return channels_; }

    // Writes `frames` output frames, out[c][i]. pull(float* const* dst, int n)
    // decodes up to n source frames into dst[c][0..n) and returns how many it
    // gave (fewer at the end: silence follows).
    template <class Pull>
    void process(float* const* out, int frames, Pull&& pull) {
        for (int i = 0; i < frames;) {
            if (outAt_ >= hop_) step(pull), outAt_ = 0;
            const int n = std::min(frames - i, hop_ - outAt_);
            for (int c = 0; c < channels_; ++c) std::copy_n(out_[size_t(c)].data() + outAt_, n, out[c] + i);
            outAt_ += n, i += n;
        }
    }

private:
    template <class Pull>
    void fillTo(int64_t absEnd, Pull& pull) {
        // Drop what no later window can use, then decode up to absEnd.
        const int64_t keep = std::min<int64_t>(int64_t(std::floor(ana_)) - search_, prev_ < 0 ? 0 : prev_ + hop_) - 2;
        if (keep > inBase_) {
            const int drop = int(std::min<int64_t>(keep - inBase_, inLen_));
            for (auto& ch : in_) std::copy(ch.begin() + drop, ch.begin() + inLen_, ch.begin());
            inLen_ -= drop, inBase_ += drop;
        }
        int want = int(std::min<int64_t>(absEnd - (inBase_ + inLen_), cap_ - inLen_));
        while (want > 0) {
            int got = 0;
            if (!ended_) {
                for (int c = 0; c < channels_; ++c) pullPtrs_[size_t(c)] = in_[size_t(c)].data() + inLen_;
                got = std::clamp(pull(pullPtrs_.data(), want), 0, want);
                if (got == 0) ended_ = true;
            }
            if (ended_) {  // silence after the end
                for (auto& ch : in_) std::fill_n(ch.begin() + inLen_, want, 0.0f);
                got = want;
            }
            inLen_ += got, want -= got;
        }
    }
    float at(int c, int64_t abs) const {
        const int64_t k = abs - inBase_;
        return k >= 0 && k < inLen_ ? in_[size_t(c)][size_t(k)] : 0.0f;
    }
    template <class Pull>
    void step(Pull& pull) {
        const int64_t nominal = int64_t(std::llround(ana_));
        int64_t pos = std::max<int64_t>(0, nominal);
        if (prev_ >= 0) {
            const int64_t target = prev_ + hop_;  // the previous window's natural continuation
            const int64_t lo = std::max<int64_t>(0, nominal - search_), hi = nominal + search_;
            fillTo(std::max(hi, target) + win_ + 1, pull);
            // Mono mix of the stretch the search reads.
            const int64_t from = std::min(lo, target);
            const int64_t to = std::max(hi, target) + hop_;
            for (int64_t a = from; a < to; ++a) {
                float s = 0;
                for (int c = 0; c < channels_; ++c) s += at(c, a);
                const int64_t k = a - inBase_;
                if (k >= 0 && k < cap_) mono_[size_t(k)] = s;
            }
            auto m = [&](int64_t a) {
                const int64_t k = a - inBase_;
                return k >= 0 && k < cap_ ? mono_[size_t(k)] : 0.0f;
            };
            double best = -2;
            for (int64_t p = lo; p <= hi; ++p) {
                double xy = 0, xx = 0, yy = 0;
                for (int j = 0; j < hop_; j += 4) {
                    const double x = m(p + j), y = m(target + j);
                    xy += x * y, xx += x * x, yy += y * y;
                }
                const double score = xx * yy > 1e-12 ? xy / std::sqrt(xx * yy) : (p == target ? 1.0 : 0.0) - std::abs(double(p - nominal)) * 1e-9;
                if (score > best) best = score, pos = p;
            }
        } else {
            fillTo(pos + win_ + 1, pull);
        }
        for (int c = 0; c < channels_; ++c) {
            auto& o = out_[size_t(c)];
            auto& t = tail_[size_t(c)];
            for (int j = 0; j < hop_; ++j) o[size_t(j)] = t[size_t(j)] + at(c, pos + j) * window_[size_t(j)];
            for (int j = 0; j < hop_; ++j) t[size_t(j)] = at(c, pos + hop_ + j) * window_[size_t(hop_ + j)];
        }
        prev_ = pos;
        ana_ += hop_ * tempo_;
    }

    int channels_ = 1, win_ = 2048, hop_ = 1024, search_ = 512, cap_ = 0;
    double tempo_ = 1;
    std::vector<float> window_, mono_;
    std::vector<std::vector<float>> in_, tail_, out_;
    std::vector<float*> pullPtrs_;
    int inLen_ = 0;
    int64_t inBase_ = 0, prev_ = -1;
    double ana_ = 0;
    int outAt_ = 0;
    bool ended_ = false;
};

}  // namespace gd::core::audio
