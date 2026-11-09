// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

class Canvas;
class VocalPlayer;
struct VocalTrack;

// Rock Band's vocal track (GD05-006, wave 02), drawn on the 2D canvas: a
// band across the top of the screen where the notes scroll from right to
// left towards the now line as tubes at their pitch, the lyrics under it
// (the phrase being sung, the sung syllables lit, the next phrase after
// it), the singer's pitch arrow at the now line, the phrase meter and each
// phrase's rating. [C data] the notes, lyrics, phrases, talkies and
// Overdrive phrases (PART VOCALS); [I] the look: RB draws its vocal track
// with its own code and track/vocals.dta's widgets, not ported yet (a
// stand-in in the ledger).
class VocalTrackView {
public:
    struct State {
        float t = 0;            // song ms (the drawing clock)
        float voicePitch = -1;  // the detected pitch (MIDI), -1 none
        float loudness = 0;
        const char* lastRating = nullptr;  // the latest phrase rating shown
        float lastRatingMs = -1e9f;
        float screenTop = 0;    // the band's top (players' track layout)
        float scale = 1;        // 1 full width
    };
    void draw(Canvas& c, const VocalTrack& track, const VocalPlayer& judge, const State& s);
    int notesDrawn() const { return notesDrawn_; }  // the walk's counter
    int lyricsDrawn() const { return lyricsDrawn_; }

private:
    int notesDrawn_ = 0, lyricsDrawn_ = 0;
};
