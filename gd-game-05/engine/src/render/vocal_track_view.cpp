// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/vocal_track_view.h"

#include <algorithm>
#include <cmath>

#include "game/chart.h"
#include "game/vocal_player.h"
#include "render/canvas.h"

namespace {

constexpr float kAheadMs = 3200;   // how far ahead the band shows [I]
constexpr float kNowX = 0.18f;     // the now line, a share of the width [I]

}  // namespace

void VocalTrackView::draw(Canvas& c, const VocalTrack& track, const VocalPlayer& judge, const State& s) {
    notesDrawn_ = lyricsDrawn_ = 0;
    const float W = kScreenW * s.scale, x0 = (kScreenW - W) * 0.5f;
    const float top = s.screenTop + 18, h = 150 * s.scale, nowX = x0 + W * kNowX;
    const float pxPerMs = (W * (1 - kNowX)) / kAheadMs;
    // The band.
    c.fillRect(x0, top, W, h, {8, 10, 24, 170});
    c.fillRect(x0, top, W, 2, {120, 140, 200, 200});
    c.fillRect(x0, top + h - 2, W, 2, {120, 140, 200, 200});
    // The pitch range: the notes of the phrases in view, at least an octave [I].
    int lo = 127, hi = 0;
    for (const VocalNote& n : track.notes) {
        if (n.ms + n.durationMs < s.t - 1000) continue;
        if (n.ms > s.t + kAheadMs + 1000) break;
        if (n.unpitched) continue;
        lo = std::min(lo, n.pitch), hi = std::max(hi, n.pitch);
    }
    if (lo > hi) lo = 55, hi = 67;
    const float mid = 0.5f * float(lo + hi);
    const float span = std::max(12.0f, float(hi - lo) + 4.0f);
    auto yOf = [&](float pitch) { return top + h * 0.5f - (pitch - mid) / span * (h - 24); };
    auto xOf = [&](float ms) { return nowX + (ms - s.t) * pxPerMs; };
    // Phrase markers.
    const int cur = judge.phraseAt(s.t);
    for (size_t i = 0; i < track.phrases.size(); ++i) {
        const VocalPhrase& p = track.phrases[i];
        if (p.endMs < s.t - 1000) continue;
        if (p.startMs > s.t + kAheadMs) break;
        const float xs = xOf(p.startMs);
        if (xs >= x0 && xs <= x0 + W) c.fillRect(xs, top + 4, 2, h - 8, {90, 100, 140, 160});
    }
    // The notes: tubes at their pitch; talkies as hollow bars on the centre line.
    for (const VocalNote& n : track.notes) {
        if (n.ms + n.durationMs < s.t - 1000) continue;
        if (n.ms > s.t + kAheadMs) break;
        float xa = std::max(x0, xOf(n.ms)), xb = std::min(x0 + W, xOf(n.ms + std::max(n.durationMs, 60.0f)));
        if (xb <= xa) continue;
        const bool od = n.phrase >= 0 && size_t(n.phrase) < track.phrases.size() && track.phrases[size_t(n.phrase)].overdrive;
        const SDL_Color col = od ? SDL_Color{240, 200, 70, 230} : SDL_Color{70, 190, 255, 230};
        if (n.unpitched) {
            const float y = top + h * 0.5f;
            c.fillRect(xa, y - 7, xb - xa, 2, col);
            c.fillRect(xa, y + 5, xb - xa, 2, col);
        } else {
            const float y = yOf(float(n.pitch));
            c.fillRect(xa, y - 6, xb - xa, 12, col);
        }
        ++notesDrawn_;
    }
    // The now line and the singer's arrow (octave-folded to the target).
    c.fillRect(nowX - 1, top, 3, h, {230, 230, 240, 220});
    if (s.voicePitch >= 0 && s.loudness > 0.01f) {
        float p = s.voicePitch;
        const float target = judge.targetPitch(s.t);
        if (target >= 0) {
            while (p - target > 6) p -= 12;
            while (target - p > 6) p += 12;
        } else {
            while (p > float(hi) + 6) p -= 12;
            while (p < float(lo) - 6) p += 12;
        }
        const float y = std::clamp(yOf(p), top + 6, top + h - 6);
        const bool on = target >= 0 && std::fabs(p - target) < 1.5f;
        const SDL_Color ac = on ? SDL_Color{120, 255, 120, 255} : SDL_Color{255, 255, 255, 230};
        for (int k = 0; k < 8; ++k) c.fillRect(nowX - 16 + float(k) * 2, y - float(8 - k), 2, float(2 * (8 - k)), ac);
    }
    // The phrase meter (the share sung of the phrase so far) [I: its look].
    if (cur >= 0) {
        const float share = judge.sungShare(cur);
        c.fillRect(x0 + 10, top + h - 14, 120, 8, {40, 40, 60, 200});
        c.fillRect(x0 + 10, top + h - 14, 120 * share, 8, {120, 220, 120, 230});
    }
    // Lyrics: the phrase being sung (or the next), then the one after it.
    int first = cur;
    if (first < 0)
        for (size_t i = 0; i < track.phrases.size(); ++i)
            if (track.phrases[i].startMs > s.t) {
                first = int(i);
                break;
            }
    float ly = top + h + 6;
    for (int k = 0; k < 2 && first >= 0 && size_t(first + k) < track.phrases.size(); ++k) {
        const VocalPhrase& ph = track.phrases[size_t(first + k)];
        if (ph.firstNote < 0) continue;
        // Width first, to centre it.
        std::string line;
        for (int i = ph.firstNote; i <= ph.lastNote; ++i) {
            const VocalNote& n = track.notes[size_t(i)];
            if (n.text.empty() || n.text == "+") continue;
            line += n.text + (n.joinNext ? "" : " ");
        }
        const float scale = k == 0 ? 2.0f : 1.5f, g = 8 * scale;
        float x = x0 + W * 0.5f - float(line.size()) * g * 0.5f;
        for (int i = ph.firstNote; i <= ph.lastNote; ++i) {
            const VocalNote& n = track.notes[size_t(i)];
            if (n.text.empty() || n.text == "+") continue;
            const std::string w = n.text + (n.joinNext ? "" : " ");
            const bool sung = k == 0 && n.ms <= s.t;
            const SDL_Color col = k == 0 ? (sung ? SDL_Color{110, 200, 255, 255} : SDL_Color{240, 240, 245, 255}) : SDL_Color{150, 150, 165, 230};
            c.text(x, ly, scale, col, w);
            x += float(w.size()) * g;
            ++lyricsDrawn_;
        }
        ly += g + 6;
    }
    // The phrase rating.
    if (s.lastRating && s.t - s.lastRatingMs < 1200) c.textCentered(nowX + 150, top + 12, 2.5f, {255, 230, 120, 255}, s.lastRating);
}
