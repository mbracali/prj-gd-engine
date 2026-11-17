// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/song_play.h"

#include <algorithm>
#include <cmath>

namespace gd24 {

void SongPlay::start(const std::vector<Beatmatch::Track>& tracks, int difficulty, int initialTrack, int slopMs) {
    difficulty_ = std::clamp(difficulty, 0, 3);
    slop_ = slopMs > 0 ? slopMs : 100;
    wrong_ = streak_ = best_ = 0;
    tempo_.clear();
    for (int t = 0; t < kTracks; ++t) {
        Track& tr = tracks_[size_t(t)];
        tr = Track{};
        if (t < int(tracks.size())) {
            tr.name = tracks[size_t(t)].name;
            if (difficulty_ < int(tracks[size_t(t)].difficulty.size())) tr.gems = tracks[size_t(t)].difficulty[size_t(difficulty_)].gems;
            // The tempo points from every difficulty's gems.
            for (const GemList& l : tracks[size_t(t)].difficulty)
                for (const Gem& g : l.gems) tempo_.push_back({double(g.tick), double(g.ms)});
        }
        tr.state.assign(tr.gems.size(), State::Pending);
        tr.pressed.assign(tr.gems.size(), 0);
        tr.hitMs.assign(tr.gems.size(), 0);
    }
    std::sort(tempo_.begin(), tempo_.end());
    tempo_.erase(std::unique(tempo_.begin(), tempo_.end(), [](const auto& a, const auto& b) { return a.first == b.first; }), tempo_.end());
    active_ = initialTrack >= 0 && initialTrack < kTracks ? initialTrack : 2;
}

int SongPlay::hits() const {
    int n = 0;
    for (const Track& t : tracks_) n += t.hits;
    return n;
}

int SongPlay::misses() const {
    int n = 0;
    for (const Track& t : tracks_) n += t.misses;
    return n;
}

int SongPlay::update(double songMs) {
    int missed = 0;
    Track& t = tracks_[size_t(active_)];
    for (size_t i = 0; i < t.gems.size(); ++i) {
        if (t.gems[i].ms > songMs - slop_) break;
        if (t.state[i] == State::Pending) t.state[i] = State::Missed, ++t.misses, ++missed, streak_ = 0;
    }
    return missed;
}

bool SongPlay::press(int lane, double songMs) {
    if (lane < 0 || lane > 3) return false;
    Track& t = tracks_[size_t(active_)];
    const uint8_t bit = uint8_t(1u << lane);
    auto first = std::lower_bound(t.gems.begin(), t.gems.end(), songMs - slop_, [](const Gem& g, double ms) { return g.ms < ms; });
    for (size_t i = size_t(first - t.gems.begin()); i < t.gems.size() && t.gems[i].ms <= songMs + slop_; ++i) {
        if (t.state[i] != State::Pending || !(t.gems[i].lanes & bit) || (t.pressed[i] & bit)) continue;
        t.pressed[i] |= bit;
        if ((t.pressed[i] & t.gems[i].lanes) == t.gems[i].lanes) {
            t.state[i] = State::Hit, t.hitMs[i] = songMs, ++t.hits;
            best_ = std::max(best_, ++streak_);
        }
        return true;
    }
    ++wrong_, streak_ = 0;
    return false;
}

void SongPlay::switchTrack(int dir, double songMs) {
    active_ = std::clamp(active_ + dir, 0, kTracks - 1);
    Track& t = tracks_[size_t(active_)];
    for (size_t i = 0; i < t.gems.size() && t.gems[i].ms < songMs - slop_; ++i)
        if (t.state[i] == State::Pending) t.state[i] = State::Passed;
}

void SongPlay::rewind(double songMs) {
    for (Track& t : tracks_)
        for (size_t i = 0; i < t.gems.size(); ++i)
            if (t.gems[i].ms >= songMs - slop_) {
                if (t.state[i] == State::Hit) --t.hits;
                if (t.state[i] == State::Missed) --t.misses;
                t.state[i] = State::Pending, t.pressed[i] = 0;
            } else if (t.state[i] == State::Pending) {
                t.state[i] = State::Passed;
            }
    streak_ = 0;
}

double SongPlay::tickToMs(double tick) const {
    if (tempo_.size() < 2) return tick / 480.0 * 500.0;  // 120 bpm
    auto it = std::lower_bound(tempo_.begin(), tempo_.end(), std::make_pair(tick, -1e300));
    size_t i = size_t(it - tempo_.begin());
    i = std::clamp<size_t>(i, 1, tempo_.size() - 1);
    const auto& a = tempo_[i - 1];
    const auto& b = tempo_[i];
    return a.second + (tick - a.first) * (b.second - a.second) / (b.first - a.first);
}

double SongPlay::msToTick(double ms) const {
    if (tempo_.size() < 2) return ms / 500.0 * 480.0;
    size_t i = 1;
    while (i + 1 < tempo_.size() && tempo_[i].second < ms) ++i;
    const auto& a = tempo_[i - 1];
    const auto& b = tempo_[i];
    return a.first + (ms - a.second) * (b.first - a.first) / (b.second - a.second);
}

}  // namespace gd24
