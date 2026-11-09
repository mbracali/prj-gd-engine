// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/vocal_player.h"

#include <algorithm>
#include <cmath>

namespace {
// Loudness that counts as singing a talky (unpitched) note [I].
constexpr float kVoiceLoudness = 0.02f;
}  // namespace

VocalPlayer::VocalPlayer(const Song& song, Difficulty difficulty, const Rules& rules)
    : track_(song.vocals), rules_(rules), difficulty_(int(difficulty)) {
    slopMs_ = vocal("slop", 120);
    marginSemis_ = vocal("pitch_margin", 1.5f);
    if (auto it = rules.rb.vocals.find("rating_thresholds"); it != rules.rb.vocals.end() && it->second.size() == 4) thresholds_ = it->second;
    sungMs_.assign(track_.notes.size(), 0.0f);
    fraction_.assign(track_.phrases.size(), 0.0f);
    rated_.assign(track_.phrases.size(), 0);
}

// (vocals (<key> easy medium hard expert)) [C data]; a single value applies to every difficulty.
float VocalPlayer::vocal(const char* key, float fallback) const {
    auto it = rules_.rb.vocals.find(key);
    if (it == rules_.rb.vocals.end() || it->second.empty()) return fallback;
    const auto& v = it->second;
    return v.size() >= 4 ? v[size_t(difficulty_)] : v[0];
}

float VocalPlayer::targetPitch(float t) const {
    size_t i = 0;
    // Binary search for the last note starting at or before t.
    size_t lo = 0, hi = track_.notes.size();
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (track_.notes[mid].ms <= t) lo = mid + 1;
        else hi = mid;
    }
    if (lo == 0) return -1;
    i = lo - 1;
    const VocalNote& n = track_.notes[i];
    if (t <= n.ms + n.durationMs) return n.unpitched ? -1.0f : float(n.pitch);
    return -1;
}

bool VocalPlayer::noteAt(float t) const {
    size_t lo = 0, hi = track_.notes.size();
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (track_.notes[mid].ms <= t) lo = mid + 1;
        else hi = mid;
    }
    return lo > 0 && t <= track_.notes[lo - 1].ms + track_.notes[lo - 1].durationMs;
}

void VocalPlayer::sing(float t, float midiPitch, float loudness) {
    const float dt = lastT_ < -1e8f ? 0.0f : std::clamp(t - lastT_, 0.0f, 100.0f);
    lastT_ = t;
    if (dt <= 0 || loudness <= 0) return;
    // Every note whose time (with the slop on each side) covers t gets the
    // frame when the pitch is within the margin, octave-free [I].
    while (cursor_ < track_.notes.size() && track_.notes[cursor_].ms + track_.notes[cursor_].durationMs + slopMs_ < t) ++cursor_;
    for (size_t i = cursor_; i < track_.notes.size(); ++i) {
        const VocalNote& n = track_.notes[i];
        if (n.ms - slopMs_ > t) break;
        if (t > n.ms + n.durationMs + slopMs_) continue;
        bool on;
        if (n.unpitched) {
            on = loudness >= kVoiceLoudness;
        } else {
            if (midiPitch < 0) continue;
            float d = std::fmod(midiPitch - float(n.pitch), 12.0f);
            if (d > 6) d -= 12;
            if (d < -6) d += 12;
            on = std::fabs(d) <= marginSemis_;
        }
        if (on) sungMs_[i] = std::min(std::max(n.durationMs, 1.0f), sungMs_[i] + dt);
    }
}

void VocalPlayer::percussion(float t) {
    for (const Percussion& p : track_.percussion) {
        if (!p.shown || std::fabs(p.ms - t) > slopMs_) continue;
        events_.push_back({Event::Kind::Percussion, -1});
        score_ += int(vocal("tambourine_points", 25));
        return;
    }
}

void VocalPlayer::finishPhrase(int phrase) {
    const VocalPhrase& ph = track_.phrases[size_t(phrase)];
    float total = 0, sung = 0;
    for (int k = ph.firstNote; k >= 0 && k <= ph.lastNote; ++k) {
        const VocalNote& n = track_.notes[size_t(k)];
        if (n.slide && n.durationMs <= 0) continue;
        total += std::max(n.durationMs, 1.0f);
        sung += sungMs_[size_t(k)];
    }
    rated_[size_t(phrase)] = 1;
    if (total <= 0) return;  // a phrase marker with no notes (a gap) is not rated
    const float f = std::min(1.0f, sung / total);
    fraction_[size_t(phrase)] = f;
    Rating r = Rating::Awful;
    for (size_t i = 0; i < thresholds_.size(); ++i)
        if (f >= thresholds_[i]) r = Rating(int(i) + 1);
    events_.push_back({Event::Kind::PhraseRated, phrase, r, f});
    if (r >= Rating::Okay) {
        ++streak_;
    } else {
        streak_ = 0;
    }
    // phrase_value per difficulty × the rated fraction × the streak multiplier [I].
    int mult = 1;
    if (auto it = rules_.rb.streakMultipliers.find("vocals"); it != rules_.rb.streakMultipliers.end())
        for (const auto& [count, m] : it->second)
            if (streak_ >= count) mult = m;
    score_ += int(std::lround(vocal("phrase_value", 400) * f)) * mult;
    if (ph.overdrive && r >= Rating::Okay) {
        meter_ = std::min(1.0f, meter_ + rules_.solo.starPower.phrase);
        events_.push_back({Event::Kind::PhraseCaptured, phrase, r, f});
    }
}

int VocalPlayer::phrasesRated(Rating atLeast) const {
    int n = 0;
    for (size_t i = 0; i < track_.phrases.size(); ++i) {
        const VocalPhrase& ph = track_.phrases[i];
        if (ph.firstNote < 0) continue;
        n += rated_[i] && fraction_[i] >= (atLeast == Rating::Awful ? 0.0f : thresholds_[size_t(int(atLeast) - 1)]);
    }
    return n;
}

void VocalPlayer::update(float t) {
    // A phrase is rated once it and its last note have ended (a note may start
    // a tick before the phrase marker ends: Maps at tick 80639) [I].
    auto doneAt = [&](const VocalPhrase& ph) {
        float end = ph.endMs;
        if (ph.lastNote >= 0) end = std::max(end, track_.notes[size_t(ph.lastNote)].ms + track_.notes[size_t(ph.lastNote)].durationMs);
        return end + slopMs_;
    };
    while (nextPhrase_ < track_.phrases.size() && doneAt(track_.phrases[nextPhrase_]) < t) finishPhrase(int(nextPhrase_++));
}

std::vector<VocalPlayer::Event> VocalPlayer::takeEvents() {
    std::vector<Event> out;
    out.swap(events_);
    return out;
}
