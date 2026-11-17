// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/song_play.h"

#include <algorithm>
#include <cmath>

namespace gd24 {

namespace {
constexpr uint32_t kOdFlag = 0x02000000, kOdEndFlag = 0x04000000;  // [I] the "glowing white notes"
}

void SongPlay::start(const std::vector<Beatmatch::Track>& tracks, const std::vector<TimeSig>& sigs, int difficulty,
                     int initialTrack, int slopMs, const Rules& rules) {
    rules_ = rules;
    difficulty_ = std::clamp(difficulty, 0, 3);
    slop_ = slopMs > 0 ? slopMs : 100;
    tempo_.clear();
    uint32_t lastTick = 0;
    chartEndMs_ = 0;
    for (int t = 0; t < kTracks; ++t) {
        Track& tr = tracks_[size_t(t)];
        tr = Track{};
        if (t < int(tracks.size())) {
            tr.name = tracks[size_t(t)].name;
            if (difficulty_ < int(tracks[size_t(t)].difficulty.size())) tr.gems = tracks[size_t(t)].difficulty[size_t(difficulty_)].gems;
            // The tempo points from every difficulty's gems.
            for (const GemList& l : tracks[size_t(t)].difficulty)
                for (const Gem& g : l.gems) {
                    tempo_.push_back({double(g.tick), double(g.ms)});
                    lastTick = std::max(lastTick, g.tick);
                    chartEndMs_ = std::max(chartEndMs_, double(g.ms));
                }
        }
    }
    std::sort(tempo_.begin(), tempo_.end());
    tempo_.erase(std::unique(tempo_.begin(), tempo_.end(), [](const auto& a, const auto& b) { return a.first == b.first; }), tempo_.end());
    // The bars from the time signatures [C data], a few past the last gem.
    barTick_.clear();
    std::vector<TimeSig> ts = sigs.empty() ? std::vector<TimeSig>{TimeSig{}} : sigs;
    uint32_t tick = 0;
    for (int bar = 0; tick <= lastTick + 1920u * 16 && bar < 100000; ++bar) {
        const TimeSig* s = &ts[0];
        for (const TimeSig& x : ts)
            if (x.bar <= bar) s = &x;
        if (s->bar == bar) tick = s->tick;
        barTick_.push_back(tick);
        tick += uint32_t(s->num * 1920 / s->den);
    }
    for (int t = 0; t < kTracks; ++t) buildPhrases(tracks_[size_t(t)]);
    active_ = initialTrack >= 0 && initialTrack < kTracks ? initialTrack : 2;
    startAt(0);
}

void SongPlay::buildPhrases(Track& tr) {
    const size_t n = tr.gems.size();
    tr.state.assign(n, State::Pending);
    tr.pressed.assign(n, 0);
    tr.hitMs.assign(n, 0);
    tr.phraseOf.assign(n, -1);
    tr.odRunOf.assign(n, -1);
    tr.phrases.clear();
    tr.odRuns.clear();
    const int bars = std::max(1, rules_.phraseBars);
    size_t bi = 0;
    for (size_t i = 0; i < n; ++i) {
        while (bi + 1 < barTick_.size() && barTick_[bi + 1] <= tr.gems[i].tick) ++bi;
        const int window = int(bi) / bars;
        if (tr.phrases.empty() || tr.phrases.back().bar / bars != window) {
            Phrase p;
            p.first = i, p.bar = window * bars, p.startMs = tr.gems[i].ms;
            tr.phrases.push_back(p);
        }
        tr.phrases.back().end = i + 1, tr.phrases.back().endMs = tr.gems[i].ms;
        tr.phraseOf[i] = int(tr.phrases.size()) - 1;
        // Overdrive runs: consecutive gems flagged, the last one with the end flag.
        if (tr.gems[i].flags2 & kOdFlag) {
            const bool cont = i > 0 && tr.odRunOf[i - 1] >= 0 && !(tr.gems[i - 1].flags2 & kOdEndFlag);
            if (!cont) tr.odRuns.push_back(OdRun{i, i + 1, false});
            tr.odRuns.back().end = i + 1;
            tr.odRunOf[i] = int(tr.odRuns.size()) - 1;
        }
    }
}

void SongPlay::startAt(double songMs) {
    for (Track& t : tracks_) {
        for (size_t i = 0; i < t.gems.size(); ++i) {
            t.state[i] = t.gems[i].ms < songMs - slop_ ? State::Passed : State::Pending;
            t.pressed[i] = 0;
        }
        for (OdRun& r : t.odRuns) r.broken = t.gems[r.first].ms < songMs - slop_;
        t.passed = size_t(std::lower_bound(t.gems.begin(), t.gems.end(), songMs - slop_, [](const Gem& g, double ms) { return g.ms < ms; }) -
                          t.gems.begin());
        t.hits = t.misses = t.attempted = t.capturedCount = 0;
        t.captured = t.failed = false;
        t.captureBar = -1;
        t.health = rules_.healthInit;
        t.indicator = -1;
    }
    stats_ = Stats{};
    events_.clear();
    wrong_ = 0;
    multiplier_ = 1, energy_ = 0, od_ = false;
    graceUntil_ = -1e300;
    lastMs_ = songMs;
    odTick_ = msToTick(songMs);
    setIndicator(active_, songMs);
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

int SongPlay::multiplier() const {
    const int m = std::clamp(int(multiplier_), 1, rules_.maxMultiplier);
    if (!od_) return m;
    return m == 5 ? 11 : m * 2;  // [C] 0x08a0a204: doubled, 5 -> 11
}

float SongPlay::crowd() const {
    float sum = 0;
    int failed = 0;
    for (const Track& t : tracks_) sum += std::clamp(t.health, 0.0f, 1.0f), failed += t.failed;
    return sum * 0.25f - float(failed) * rules_.crowdFailedPenalty;
}

int SongPlay::barAt(double ms) const {
    const double tick = msToTick(ms);
    auto it = std::upper_bound(barTick_.begin(), barTick_.end(), uint32_t(std::max(0.0, tick)));
    return std::max(0, int(it - barTick_.begin()) - 1);
}

double SongPlay::barMs(int bar) const {
    if (barTick_.empty()) return 0;
    return tickToMs(double(barTick_[size_t(std::clamp(bar, 0, int(barTick_.size()) - 1))]));
}

int SongPlay::nextPhrase(int t, double ms) const {
    const Track& tr = tracks_[size_t(t)];
    for (size_t p = 0; p < tr.phrases.size(); ++p)
        if (tr.phrases[p].startMs >= ms) return int(p);
    return -1;
}

void SongPlay::setIndicator(int t, double ms) {
    Track& tr = tracks_[size_t(t)];
    tr.indicator = (t == active_ && playable(t)) ? nextPhrase(t, ms) : -1;
}

void SongPlay::heal(int t, float amount) {
    Track& tr = tracks_[size_t(t)];
    if (!tr.failed) tr.health = std::min(1.0f, tr.health + amount);
}

void SongPlay::damage(int t, float amount, double ms) {
    Track& tr = tracks_[size_t(t)];
    if (tr.failed || stats_.songFailed) return;
    tr.health -= amount;
    // [C] 0x08af608c: Easy keeps every health at 2 x the miss amount for the
    // first 50 % of the song, Medium for the first 30 %.
    const double frac = chartEndMs_ > 0 ? ms / chartEndMs_ : 1;
    if ((difficulty_ == 0 && frac < 0.5) || (difficulty_ == 1 && frac < 0.3))
        tr.health = std::max(tr.health, 2 * rules_.healthMiss[difficulty_]);
    if (tr.health <= 0) {
        tr.health = 0, tr.failed = true, tr.indicator = -1;
        ++stats_.failedTracks;
        events_.push_back({ms, 'f', t, 0, multiplier()});
    }
}

void SongPlay::onHit(int t, size_t i, double ms) {
    Track& tr = tracks_[size_t(t)];
    tr.state[i] = State::Hit, tr.hitMs[i] = ms, ++tr.hits;
    stats_.bestNoteStreak = std::max(stats_.bestNoteStreak, ++stats_.noteStreak);
    const int pts = rules_.gemPoints * multiplier();  // [C] 0x08a91240
    stats_.score += pts, stats_.gemScore += pts;
    events_.push_back({ms, 'h', t, pts, multiplier()});
    if (od_) {
        for (int k = 0; k < kTracks; ++k) heal(k, rules_.healthHit);  // [C] Overdrive heals every track
    } else {
        heal(t, rules_.healthHit);
    }
    // An Overdrive run hit to its last gem fills a quarter of the meter.
    const int run = tr.odRunOf[i];
    if (run >= 0 && !tr.odRuns[size_t(run)].broken && i + 1 == tr.odRuns[size_t(run)].end) {
        energy_ = std::min(1.0f, energy_ + rules_.odRunEnergy);
        ++stats_.odRunsCompleted;
        events_.push_back({ms, 'e', t, int(energy_ * 100 + 0.5f), multiplier()});
    }
    // The indicator's phrase complete: captured.
    if (tr.indicator >= 0 && tr.phraseOf[i] == tr.indicator) {
        const Phrase& p = tr.phrases[size_t(tr.indicator)];
        bool all = true;
        for (size_t k = p.first; k < p.end && all; ++k) all = tr.state[k] == State::Hit;
        if (all) capture(t, ms);
    }
}

void SongPlay::capture(int t, double ms) {
    Track& tr = tracks_[size_t(t)];
    ++tr.attempted, ++tr.capturedCount;
    // [C] 0x08a86368: int(multiplier) + 1, at most 5; bonus = points x multiplier.
    multiplier_ = float(std::min(rules_.maxMultiplier, int(multiplier_) + 1));
    stats_.maxMultiplier = std::max(stats_.maxMultiplier, multiplier());
    const int bonus = rules_.capturePoints * multiplier();
    stats_.score += bonus, stats_.captureScore += bonus;
    stats_.bestPhraseStreak = std::max(stats_.bestPhraseStreak, ++stats_.phraseStreak);
    graceUntil_ = ms + rules_.captureGraceMs;
    heal(t, rules_.healthCapture);
    tr.captured = true, tr.captureBar = barAt(ms), tr.capturedAtMs = ms, tr.indicator = -1;
    events_.push_back({ms, 'c', t, bonus, multiplier()});
}

void SongPlay::failPhrase(int t, double ms) {
    Track& tr = tracks_[size_t(t)];
    if (tr.indicator < 0) return;
    ++tr.attempted;
    // [C] 0x08a8de08: the multiplier back to 1 once the capture timer ran out.
    if (ms > graceUntil_) multiplier_ = 1, stats_.phraseStreak = 0;
    events_.push_back({ms, 'm', t, tr.indicator, multiplier()});
    tr.indicator = nextPhrase(t, ms + slop_);
}

void SongPlay::release(int t, double ms) {
    Track& tr = tracks_[size_t(t)];
    tr.captured = false, tr.captureBar = -1;
    events_.push_back({ms, 'r', t, barAt(ms), multiplier()});
    setIndicator(t, ms);
}

int SongPlay::update(double songMs) {
    if (stats_.songFailed) return 0;
    int missed = 0;
    // Releases [C 0x08a80ad8]: 8 bars after the capture, 11 with a multiplier of 2 or more.
    const int bar = barAt(songMs);
    int oldest = -1, playableCount = 0;
    for (int t = 0; t < kTracks; ++t) {
        Track& tr = tracks_[size_t(t)];
        playableCount += playable(t);
        if (!tr.captured) continue;
        if (oldest < 0 || tr.capturedAtMs < tracks_[size_t(oldest)].capturedAtMs) oldest = t;
        const int need = multiplier_ >= 2 ? rules_.releaseBarsHigh : rules_.releaseBars;
        if (bar - tr.captureBar >= need) release(t, songMs);
    }
    // [I] nothing left to play: the oldest capture is released at the next bar.
    if (playableCount == 0 && oldest >= 0 && bar > tracks_[size_t(oldest)].captureBar) release(oldest, songMs);

    for (int t = 0; t < kTracks; ++t) {
        Track& tr = tracks_[size_t(t)];
        const bool isActive = t == active_;
        while (tr.passed < tr.gems.size()) {
            const size_t i = tr.passed;
            const double ms = tr.gems[i].ms;
            if (tr.captured || tr.failed) {
                if (ms > songMs) break;  // played on its own when it reaches the strike line
                if (tr.state[i] == State::Pending) tr.state[i] = tr.captured ? State::Auto : State::Passed, tr.hitMs[i] = ms;
                if (tr.odRunOf[i] >= 0) tr.odRuns[size_t(tr.odRunOf[i])].broken = true;
                ++tr.passed;
                continue;
            }
            if (ms > songMs - slop_) break;
            ++tr.passed;
            if (tr.state[i] != State::Pending) continue;
            if (tr.odRunOf[i] >= 0) tr.odRuns[size_t(tr.odRunOf[i])].broken = true;
            if (isActive) {
                tr.state[i] = State::Missed, ++tr.misses, ++missed, stats_.noteStreak = 0;
                if (tr.indicator >= 0 && tr.phraseOf[i] == tr.indicator) {
                    damage(t, rules_.healthMiss[difficulty_], songMs);
                    failPhrase(t, songMs);
                }
            } else {
                // A phrase nobody played: the track's health goes down, once
                // per phrase at its first gem [C: the miss handler with its
                // off-track factor; I: once per phrase, the factor's value].
                tr.state[i] = State::Passed;
                if (tr.phraseOf[i] >= 0 && tr.phrases[size_t(tr.phraseOf[i])].first == i)
                    damage(t, rules_.healthMiss[difficulty_] * rules_.inactiveFactor, songMs);
            }
        }
        // The indicator's phrase already gone (e.g. the player came back late).
        if (isActive && playable(t) && tr.indicator < 0) setIndicator(t, songMs);
    }
    // Overdrive drains by beats [I].
    const double tick = msToTick(songMs);
    if (od_) {
        energy_ -= float((tick - odTick_) / 480.0 / rules_.odBeatsFull);
        if (energy_ <= 0) energy_ = 0, od_ = false, events_.push_back({songMs, 'o', active_, 0, multiplier()});
    }
    odTick_ = tick;
    // The song fails when the crowd meter is empty [I].
    if (crowd() <= 0) {
        stats_.songFailed = true, stats_.failMs = songMs;
        events_.push_back({songMs, 'F', active_, 0, multiplier()});
    }
    lastMs_ = songMs;
    return missed;
}

bool SongPlay::press(int lane, double songMs) {
    if (lane < 0 || lane > 3 || stats_.songFailed) return false;
    Track& t = tracks_[size_t(active_)];
    if (!playable(active_)) return false;  // [I] nothing to play on a captured / failed track
    const uint8_t bit = uint8_t(1u << lane);
    auto first = std::lower_bound(t.gems.begin(), t.gems.end(), songMs - slop_, [](const Gem& g, double ms) { return g.ms < ms; });
    for (size_t i = size_t(first - t.gems.begin()); i < t.gems.size() && t.gems[i].ms <= songMs + slop_; ++i) {
        if (t.state[i] != State::Pending || !(t.gems[i].lanes & bit) || (t.pressed[i] & bit)) continue;
        t.pressed[i] |= bit;
        if ((t.pressed[i] & t.gems[i].lanes) == t.gems[i].lanes) onHit(active_, i, songMs);
        return true;
    }
    ++wrong_;
    // [I] a wrong press while the indicator's phrase is under way fails it.
    if (t.indicator >= 0) {
        const Phrase& p = t.phrases[size_t(t.indicator)];
        if (songMs >= p.startMs - slop_ && songMs <= p.endMs + slop_) {
            damage(active_, rules_.healthMiss[difficulty_], songMs);
            failPhrase(active_, songMs);
            stats_.noteStreak = 0;
        }
    }
    return false;
}

void SongPlay::switchTrack(int dir, double songMs) {
    const int to = std::clamp(active_ + dir, 0, kTracks - 1);
    if (to == active_) return;
    tracks_[size_t(active_)].indicator = -1;
    active_ = to;
    Track& t = tracks_[size_t(active_)];
    for (size_t i = t.passed; i < t.gems.size() && t.gems[i].ms < songMs - slop_; ++i)
        if (t.state[i] == State::Pending) t.state[i] = State::Passed;
    setIndicator(active_, songMs);
}

bool SongPlay::overdrive(double songMs) {
    if (od_ || energy_ < rules_.odMinEnergy || stats_.songFailed) return false;
    od_ = true;
    odTick_ = msToTick(songMs);
    ++stats_.odActivations;
    // [C text] failed instruments are rescued.
    for (int t = 0; t < kTracks; ++t) {
        Track& tr = tracks_[size_t(t)];
        if (!tr.failed) continue;
        tr.failed = false, tr.health = rules_.healthInit, ++stats_.rescues;
        for (size_t i = tr.passed; i < tr.gems.size() && tr.gems[i].ms < songMs - slop_; ++i) tr.state[i] = State::Passed, tr.passed = i + 1;
        setIndicator(t, songMs);
    }
    stats_.maxMultiplier = std::max(stats_.maxMultiplier, multiplier());
    events_.push_back({songMs, 'o', active_, 1, multiplier()});
    return true;
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
    auto it = std::lower_bound(tempo_.begin(), tempo_.end(), ms, [](const auto& p, double v) { return p.second < v; });
    size_t i = std::clamp<size_t>(size_t(it - tempo_.begin()), 1, tempo_.size() - 1);
    const auto& a = tempo_[i - 1];
    const auto& b = tempo_[i];
    return a.first + (ms - a.second) * (b.first - a.first) / (b.second - a.second);
}

void autoplayStep(SongPlay& p, double songMs, bool useOverdrive) {
    const SongPlay::Track& t = p.tracks()[size_t(p.active())];
    if (!t.captured && !t.failed)
        for (size_t i = t.passed; i < t.gems.size() && t.gems[i].ms <= songMs; ++i)
            if (t.state[i] == SongPlay::State::Pending && t.gems[i].ms > songMs - p.slop())
                for (int l = 0; l < 4; ++l)
                    if ((t.gems[i].lanes & (1 << l)) && !(t.pressed[i] & (1 << l))) p.press(l, songMs);
    bool anyFailed = false;
    for (const SongPlay::Track& x : p.tracks()) anyFailed = anyFailed || x.failed;
    if (useOverdrive && !p.inOverdrive() && p.energy() >= p.rules().odMinEnergy && (p.multiplier() >= 5 || anyFailed))
        p.overdrive(songMs);
    // Stay while the indicator's phrase is under way.
    if (!t.captured && !t.failed && t.indicator >= 0 && t.phrases[size_t(t.indicator)].startMs <= songMs + 250) return;
    // Otherwise go to the playable track whose next phrase comes first (the
    // active one wins ties).
    int best = -1;
    double bestMs = 1e300;
    for (int k = 0; k < SongPlay::kTracks; ++k) {
        const SongPlay::Track& x = p.tracks()[size_t(k)];
        if (x.captured || x.failed) continue;
        const int ph = k == p.active() && x.indicator >= 0 ? x.indicator : p.nextPhrase(k, songMs + 250.0 * std::abs(k - p.active()));
        if (ph < 0) continue;
        const double ms = x.phrases[size_t(ph)].startMs + (k == p.active() ? 0.0 : 1.0);
        if (ms < bestMs) bestMs = ms, best = k;
    }
    if (best >= 0 && best != p.active()) p.switchTrack(best > p.active() ? 1 : -1, songMs);
}

}  // namespace gd24
