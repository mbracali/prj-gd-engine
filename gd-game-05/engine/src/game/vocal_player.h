// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <vector>

#include "game/chart.h"
#include "game/rules.h"

// Rock Band's vocals, the judge (rendering-free, L17). It takes, every
// frame, the singer's detected pitch (a MIDI note number with fractions, or
// a negative value for no voice) and loudness (0..1), and scores each phrase
// of PART VOCALS when it ends.
//
// From scoring.dtb (vocals ...) per difficulty [C data]: slop (ms of grace
// around a note), pitch_margin (semitones), rating_thresholds (0.6, 0.75,
// 0.9, 0.99: Awful .. Awesome), phrase_value (points), nonpitch_* for the
// talkies, tambourine_* for the percussion sections; (streaks (multipliers
// (vocals (0 1) (1 2) (2 3) (3 4)))) by phrases in a row.
// How RB weighs a phrase's notes (a note's share of the phrase by length,
// octave-free pitch matching) is [I] until the vocal scorer is read in the
// XEX (docs/FINDINGS.md "Vocals").
class VocalPlayer {
public:
    enum class Rating { Awful, Meh, Okay, Great, Awesome };
    struct Event {
        enum class Kind { PhraseRated, PhraseCaptured, Percussion, PercussionMiss };
        Kind kind;
        int phrase;
        Rating rating = Rating::Awful;
        float fraction = 0;
    };

    VocalPlayer(const Song& song, Difficulty difficulty, const Rules& rules);

    // The singer's state at song time t (call every frame, in time order).
    void sing(float t, float midiPitch, float loudness);
    // A percussion strike (any button / a tap on the mic) in a percussion section.
    void percussion(float t);
    void update(float t);

    const VocalTrack& track() const { return track_; }
    int phrases() const { return int(track_.phrases.size()); }
    int phrasesRated(Rating atLeast) const;
    float phraseFraction(int phrase) const { return fraction_[size_t(phrase)]; }
    int score() const { return score_; }
    int streak() const { return streak_; }
    float overdrive() const { return meter_; }
    std::vector<Event> takeEvents();

    // The target pitch at time t (for the pitch arrow and the autoplay), or -1.
    float targetPitch(float t) const;

private:
    float vocal(const char* key, float fallback) const;
    void finishPhrase(int phrase);

    const VocalTrack& track_;
    const Rules& rules_;
    int difficulty_;
    float slopMs_ = 120, marginSemis_ = 1.5f;
    std::vector<float> thresholds_{0.6f, 0.75f, 0.9f, 0.99f};
    std::vector<float> sungMs_;     // per note: ms sung on pitch
    std::vector<float> fraction_;   // per phrase, when rated
    std::vector<char> rated_;
    size_t nextPhrase_ = 0, nextPerc_ = 0, cursor_ = 0;
    float lastT_ = -1e9f;
    int score_ = 0, streak_ = 0;
    float meter_ = 0;
    std::vector<Event> events_;
};
