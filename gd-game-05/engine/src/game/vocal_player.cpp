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
    : track_(song.vocals), tempo_(&song.tempo), rules_(rules), difficulty_(int(difficulty)) {
    slopMs_ = vocal("slop", 120);
    marginSemis_ = vocal("pitch_margin", 1.5f);
    if (auto it = rules.rb.vocals.find("rating_thresholds"); it != rules.rb.vocals.end() && it->second.size() == 4) thresholds_ = it->second;
    sungMs_.assign(track_.notes.size(), 0.0f);
    fraction_.assign(track_.phrases.size(), 0.0f);
    rated_.assign(track_.phrases.size(), 0);
    credit_.assign(track_.phrases.size(), 0.0f);
    bonus_.assign(track_.phrases.size(), 0.0f);
    maxW_.assign(track_.phrases.size(), 0.0f);
    // A phrase's maximum: its notes' weights, duration x note_length_factor,
    // a note under 40 ms weighing 0 (RB 0x82452800, 0x824530D8) [C].
    for (size_t p = 0; p < track_.phrases.size(); ++p)
        for (int k = track_.phrases[p].firstNote; k >= 0 && k <= track_.phrases[p].lastNote; ++k) maxW_[p] += noteWeight(track_.notes[size_t(k)]);
}

float VocalPlayer::noteWeight(const VocalNote& n) const {
    return n.durationMs < 40.0f ? 0.0f : n.durationMs * vocal("note_length_factor", 1.0f);
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

// RB's vocal poll (RB 0x8245A1C8) [C]: each frame credits the one
// best-matching note active over [start - slop, end + slop] (0x82453B68,
// 0x82455E18) with dt x hit multiplier x accuracy (0x82454208). Accuracy is
// 10^-(d / pitch_margin)^2, 0 under 0.01 (0x82455D78); the sung pitch moves
// by whole octaves toward the note only when within 2.5 semitones of it
// modulo 12 (0x82453A20). The hit multiplier is pitch_hit_multiplier, or
// nonpitch_hit_multiplier for a talky (x nonpitch_easy_multiplier for '^'),
// x short_note_multiplier under short_note_threshold_ms. The phrase's
// credit is capped by its maximum and by vocal_cap_growth x the weights
// of the notes reached so far. A talky's accuracy is RB's speech matcher's
// (Fonix, not read): any voice here [I]; the dropped credit's feedback ring
// (0x824531F0) is not done [I]. While Overdrive is on, (band multiplier -
// 1) x the credit builds the phrase's bonus (band +0x68).
void VocalPlayer::sing(float t, float midiPitch, float loudness) {
    const float dt = lastT_ < -1e8f ? 0.0f : std::clamp(t - lastT_, 0.0f, 100.0f);
    lastT_ = t;
    screamPoll(t, loudness, dt);
    if (!track_.percussion.empty()) tambourinePoll(t, loudness);
    if (dt <= 0 || loudness <= 0) return;
    while (cursor_ < track_.notes.size() && track_.notes[cursor_].ms + track_.notes[cursor_].durationMs + slopMs_ < t) ++cursor_;
    int best = -1;
    float bestAcc = 0;
    for (size_t i = cursor_; i < track_.notes.size(); ++i) {
        const VocalNote& n = track_.notes[i];
        if (n.ms - slopMs_ > t) break;
        if (t > n.ms + n.durationMs + slopMs_) continue;
        float acc = 0;
        if (n.unpitched) {
            acc = loudness >= kVoiceLoudness ? 1.0f : 0.0f;
        } else if (midiPitch >= 0) {
            const float diff = midiPitch - float(n.pitch);
            float m = std::fmod(diff, 12.0f);
            if (m > 6) m -= 12;
            if (m < -6) m += 12;
            const float d = std::fabs(m) <= 2.5f ? std::fabs(m) : std::fabs(diff);
            acc = std::pow(10.0f, -(d / marginSemis_) * (d / marginSemis_));
            if (acc < 0.01f) acc = 0;
        }
        if (acc > bestAcc) bestAcc = acc, best = int(i);
    }
    if (best < 0) return;
    const VocalNote& n = track_.notes[size_t(best)];
    float hitMult = n.unpitched ? vocal("nonpitch_hit_multiplier", 2.0f) : vocal("pitch_hit_multiplier", 1.5f);
    if (n.unpitched && !n.lyric.empty() && n.lyric.back() == '^') hitMult *= vocal("nonpitch_easy_multiplier", 2.5f);
    if (n.durationMs < vocal("short_note_threshold_ms", 150.0f)) hitMult *= vocal("short_note_multiplier", 1.35f);
    float credit = dt * hitMult * bestAcc;
    sungMs_[size_t(best)] += dt * bestAcc;
    const int ph = n.phrase;
    if (ph < 0 || size_t(ph) >= credit_.size()) return;
    float reached = 0;
    for (int k = track_.phrases[size_t(ph)].firstNote; k >= 0 && k <= track_.phrases[size_t(ph)].lastNote; ++k)
        if (track_.notes[size_t(k)].ms <= t) reached += noteWeight(track_.notes[size_t(k)]);
    const float cap = std::min(maxW_[size_t(ph)], vocal("vocal_cap_growth", 1.15f) * reached);
    credit = std::max(0.0f, std::min(credit, cap - credit_[size_t(ph)]));
    credit_[size_t(ph)] += credit;
    if (odActive_) bonus_[size_t(ph)] += float(bandBonus_ - 1) * credit;
}

// The singer's Overdrive deploy (RB 0x824AD9D0, 0x82458F70) [C]: inside a
// freestyle section (from a phrase's start + freestyle_pad to its first note
// - freestyle_pad, when that lead-in is at least freestyle_min_duration, or
// a whole phrase without notes) the band's energy at the deploy threshold
// and the voice at 0.1 or more for over 390 ms (ui vocals.dta
// set_scream_thresholds 0.1 0.39) send scream -> deploy_if_possible.
bool VocalPlayer::inFreestyle(float t) const {
    const float pad = vocal("freestyle_pad", 100.0f), minDur = vocal("freestyle_min_duration", 600.0f);
    for (const VocalPhrase& p : track_.phrases) {
        if (p.startMs > t) break;
        if (t > p.endMs) continue;
        if (p.firstNote < 0) return true;
        const float first = track_.notes[size_t(p.firstNote)].ms;
        return first - p.startMs >= minDur && t >= p.startMs + pad && t <= first - pad;
    }
    // Between phrases (RB re-cuts the phrases end to end: a gap is an empty phrase) [C].
    return true;
}

void VocalPlayer::screamPoll(float t, float loudness, float dt) {
    if (odActive_ || meter_ < vocal("deploy_threshold", 0.5f) || !inFreestyle(t) || loudness < 0.1f) {
        loudMs_ = 0;
        return;
    }
    loudMs_ += dt;
    if (loudMs_ > 390.0f) {
        loudMs_ = 0;
        if (deployOverdrive(t, drainMs_)) events_.push_back({Event::Kind::Deployed, -1});
    }
}

int VocalPlayer::phraseAt(float t) const {
    for (size_t i = 0; i < track_.phrases.size(); ++i) {
        const VocalPhrase& p = track_.phrases[i];
        if (t >= p.startMs && t <= p.endMs) return int(i);
        if (p.startMs > t) break;
    }
    return -1;
}

float VocalPlayer::sungShare(int phrase) const {
    if (phrase < 0 || size_t(phrase) >= credit_.size() || maxW_[size_t(phrase)] <= 0) return 0;
    return std::min(1.0f, credit_[size_t(phrase)] / maxW_[size_t(phrase)]);
}

bool VocalPlayer::deployOverdrive(float t, float drainMs) {
    (void)t;
    if (odActive_ || meter_ < 0.5f) return false;
    odActive_ = true;
    odDrainPerMs_ = drainMs > 0 ? 1.0f / drainMs : 1.0f / 16000.0f;
    return true;
}

const char* VocalPlayer::ratingName(Rating r) {
    switch (r) {
    case Rating::Awful: return "AWFUL";
    case Rating::Meh: return "MESSY";
    case Rating::Okay: return "OK";
    case Rating::Great: return "STRONG";
    default: return "AWESOME";
    }
}

// The tambourine (RB 0x82456080, 0x82455340, 0x82454AC0) [C]: a tap is a
// rise of the mic's level 0.06 over its running minimum, re-armed once it
// falls 0.02 under its running peak; a tap within +/-tambourine_window_ticks
// (120) of the next shown gem hits it (tambourine_points 25, no
// multiplier); a tap earlier than that is a stray miss (the gem stays); a
// gem whose window passes is missed.
void VocalPlayer::percussion(float t) {
    const double tick = tempo_->msToTick(t);
    const double win = vocal("tambourine_window_ticks", 120.0f);
    while (nextPerc_ < track_.percussion.size() && !track_.percussion[nextPerc_].shown) ++nextPerc_;
    if (nextPerc_ >= track_.percussion.size()) return;
    const Percussion& p = track_.percussion[nextPerc_];
    if (tick < double(p.tick) - win) {
        events_.push_back({Event::Kind::PercussionMiss, -1});
        return;
    }
    if (tick <= double(p.tick) + win) {
        score_ += int(vocal("tambourine_points", 25));
        ++percHits_;
        events_.push_back({Event::Kind::Percussion, -1});
        ++nextPerc_;
    }
}

void VocalPlayer::tambourinePoll(float t, float loudness) {
    // Missed gems: their window passed.
    const double tick = tempo_->msToTick(t);
    const double win = vocal("tambourine_window_ticks", 120.0f);
    while (nextPerc_ < track_.percussion.size() &&
           (!track_.percussion[nextPerc_].shown || double(track_.percussion[nextPerc_].tick) + win < tick)) {
        if (track_.percussion[nextPerc_].shown) events_.push_back({Event::Kind::PercussionMiss, -1});
        ++nextPerc_;
    }
    // Onsets of the mic's level.
    if (tapArmed_) {
        tapMin_ = std::min(tapMin_, loudness);
        if (loudness >= tapMin_ + 0.06f) tapArmed_ = false, tapPeak_ = loudness, percussion(t);
    } else {
        tapPeak_ = std::max(tapPeak_, loudness);
        if (loudness <= tapPeak_ - 0.02f) tapArmed_ = true, tapMin_ = loudness;
    }
}

bool VocalPlayer::percussionDue(float t) const {
    for (size_t i = nextPerc_; i < track_.percussion.size(); ++i) {
        const Percussion& p = track_.percussion[i];
        if (p.ms > t + 30) break;
        if (p.shown && std::fabs(p.ms - t) < 30) return true;
    }
    return false;
}

// Phrase end (RB 0x82458698) [C]: fraction = credit / maximum; the
// rating the highest threshold strictly passed (0x824587A0); points
// round(phrase_value x credit / max) x the streak multiplier, plus the
// Overdrive bonus the same way (0x824587EC), the multiplier read before
// the streak changes; only Awesome extends the streak, anything else
// breaks it (0x824537E8); an Overdrive phrase pays only when Awesome
// (0x824661B8), spotlight_phrase of energy.
void VocalPlayer::finishPhrase(int phrase) {
    const VocalPhrase& ph = track_.phrases[size_t(phrase)];
    rated_[size_t(phrase)] = 1;
    const float total = maxW_[size_t(phrase)];
    if (ph.firstNote < 0 || total <= 0) return;  // an empty phrase (a gap) is not rated
    const float f = std::min(1.0f, credit_[size_t(phrase)] / total);
    fraction_[size_t(phrase)] = f;
    Rating r = Rating::Awful;
    for (size_t i = 0; i < thresholds_.size(); ++i)
        if (f > thresholds_[i]) r = Rating(int(i) + 1);
    events_.push_back({Event::Kind::PhraseRated, phrase, r, f});
    int mult = 1;
    if (auto it = rules_.rb.streakMultipliers.find("vocals"); it != rules_.rb.streakMultipliers.end())
        for (const auto& [count, m] : it->second)
            if (streak_ >= count) mult = m;
    const float pv = vocal("phrase_value", 400);
    score_ += int(std::lround(pv * f)) * mult + int(std::lround(pv * bonus_[size_t(phrase)] / total)) * mult;
    if (r == Rating::Awesome) ++streak_;
    else streak_ = 0;
    if (ph.overdrive && r == Rating::Awesome) {
        meter_ = std::min(1.0f, meter_ + (rules_.rb.spotlightPhrase > 0 ? rules_.rb.spotlightPhrase : rules_.solo.starPower.phrase));
        events_.push_back({Event::Kind::PhraseCaptured, phrase, r, f});
    }
}

int VocalPlayer::phrasesRated(Rating atLeast) const {
    int n = 0;
    for (size_t i = 0; i < track_.phrases.size(); ++i) {
        const VocalPhrase& ph = track_.phrases[i];
        if (ph.firstNote < 0) continue;
        n += rated_[i] && (atLeast == Rating::Awful || fraction_[i] > thresholds_[size_t(int(atLeast) - 1)]);
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
    // Overdrive drains while active.
    const float dt = odLastT_ < -1e8f ? 0.0f : std::max(0.0f, t - odLastT_);
    odLastT_ = t;
    if (odActive_) {
        meter_ -= dt * odDrainPerMs_;
        if (meter_ <= 0) meter_ = 0, odActive_ = false;
    }
}

std::vector<VocalPlayer::Event> VocalPlayer::takeEvents() {
    std::vector<Event> out;
    out.swap(events_);
    return out;
}
