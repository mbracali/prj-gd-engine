// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/note_track.h"

#include <algorithm>
#include <cmath>

#include "game/tuning.h"

namespace {

// The fretbar interval [i, i+1] holding t (clamped to the ends).
size_t intervalAt(const std::vector<int32_t>& fb, float t) {
    if (fb.size() < 2) return 0;
    auto it = std::upper_bound(fb.begin(), fb.end(), int32_t(std::floor(t)));
    size_t i = it == fb.begin() ? 0 : size_t(it - fb.begin()) - 1;
    return std::min(i, fb.size() - 2);
}

}  // namespace

float NoteTrack::beatAt(float t) const {
    if (fretbars.size() < 2) return float(firstBeatMs);
    const size_t i = intervalAt(fretbars, t);
    return float(fretbars[i + 1] - fretbars[i]);
}

float NoteTrack::measureAt(float t) const {
    int num = 4;
    for (const TimeSig& ts : timesig)
        if (float(ts.time) <= t && ts.num > 0) num = ts.num;
    return float(num) * beatAt(t);
}

float NoteTrack::beatsBetween(float t0, float t1) const {
    if (fretbars.size() < 2 || t1 <= t0) return 0;
    float beats = 0;
    float t = t0;
    while (t < t1) {
        const size_t i = intervalAt(fretbars, t);
        const float a = float(fretbars[i]), b = float(fretbars[i + 1]);
        const float len = std::max(1.0f, b - a);
        const float end = (i + 2 >= fretbars.size()) ? t1 : std::min(t1, b);
        beats += (end - t) / len;
        if (end <= t) break;
        t = end;
    }
    return beats;
}

float hopoTolerance(const Chart& c, float scale, float t) {
    const auto& fb = c.fretbars;
    if (fb.size() < 2 || scale <= 0) return 0;
    const size_t i = intervalAt(fb, t);
    int den = 4;
    for (const TimeSig& ts : c.timesig)
        if (ts.time <= fb[i]) den = ts.den;
    return float(fb[i + 1] - fb[i]) / scale * float(den / 4);
}

NoteTrack buildTrack(const Chart& chart, Part part, int difficulty, const Tuning& tuning, float scale, bool battle) {
    NoteTrack tr;
    tr.fretbars = chart.fretbars;
    tr.timesig = chart.timesig;
    if (chart.fretbars.size() >= 2) tr.firstBeatMs = chart.fretbars[1] - chart.fretbars[0];
    const auto& src = chart.notes(part, difficulty);
    tr.phrases = battle ? chart.parts[size_t(part)].starBattle[size_t(difficulty)] : chart.parts[size_t(part)].star[size_t(difficulty)];
    tr.notes.reserve(src.size());
    const float first = float(tr.firstBeatMs);
    for (size_t i = 0; i < src.size(); ++i) {
        const ChartNote& c = src[i];
        TrackNote n;
        n.time = c.time;
        n.length = c.length;
        n.frets = c.frets;
        // Natural HOPO [C 0x82104850-0x82104908]: a previous note, the gap
        // within the tolerance, a single fret, a different mask.
        bool natural = false;
        if (i > 0) {
            const ChartNote& p = src[i - 1];
            const bool single = (c.frets & (c.frets - 1)) == 0;
            natural = float(c.time - p.time) <= hopoTolerance(chart, scale, float(c.time)) && single && c.frets != p.frets;
        }
        // Bit 0x20 flips it, on notes with a length [C 0x821047FC].
        const bool flip = c.flag20 && c.length != 0;
        n.hopo = natural != flip;
        n.sustain = float(c.length) > 0.5f * first;
        n.holdMs = n.sustain ? int32_t(float(c.length) - tuning.whammyShorten * first) : 0;
        if (n.sustain && float(c.length) > tuning.whammyShorten * first)
            n.maxSustainPoints = int(std::lround(tr.beatsBetween(float(c.time), float(c.time + c.length)) * 25.0f));
        tr.notes.push_back(n);
    }
    // Star Power phrases: the notes whose time lies in [time, time + length), the
    // phrase pointer moving on past every phrase that ends at or before the note
    // [C 0x82102B70, 0x82102830]; "last" = the phrase ends at or before the next
    // chart note (input_array word 7) [C 0x82102B70 end], i.e. its last note.
    for (size_t p = 0; p < tr.phrases.size(); ++p) {
        const ChartPhrase& ph = tr.phrases[p];
        int last = -1;
        for (size_t i = 0; i < tr.notes.size(); ++i)
            if (tr.notes[i].time >= ph.time && tr.notes[i].time < ph.time + ph.length) {
                tr.notes[i].phrase = int(p);
                last = int(i);
            }
        if (last >= 0) tr.notes[size_t(last)].phraseLast = true;
    }
    return tr;
}

NoteTrack faceOffTrack(const NoteTrack& full, const std::vector<ChartPhrase>& sections) {
    NoteTrack t = full;
    t.notes.clear();
    for (const TrackNote& n : full.notes)
        for (const ChartPhrase& sct : sections)
            if (n.time >= sct.time && n.time < sct.time + sct.length) {
                t.notes.push_back(n);
                break;
            }
    // Star Power phrases stay (face-off has Star Power: faceoff_hud_2d_elements
    // carries the tubes [C data]). "last" keeps the full chart's: a note's next
    // time is the next chart note's, whichever player it belongs to [C
    // 0x821044C8: the face-off filter skips the note, word 7 reads the chart
    // 3 words on], so a phrase whose last note is the other player's never
    // completes on this side.
    return t;
}
