// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstddef>

#include "game/note_track.h"
#include "game/player.h"

// The self-test's (and --autoplay's) player: strums each note on the first
// frame at or after its time (half a frame early at most), holding exactly
// its frets for two frames or to the end of its sustain; with `tapHopos`
// it plays every HOPO whose previous note it hit by changing frets without
// strumming. Deploys Star Power as soon as it can. Rendering-free.
class Autoplay {
public:
    Autoplay(const NoteTrack& t, bool tapHopos) : t_(t), tap_(tapHopos) {}

    FrameInput input(double nowMs, double frameMs, const Player& player) {
        FrameInput in;
        while (next_ < t_.notes.size() && double(t_.notes[next_].time) <= nowMs + frameMs * 0.5) {
            const TrackNote& n = t_.notes[next_];
            held_ = n.frets;
            holdUntil_ = double(n.time) + (n.sustain ? n.holdMs + frameMs : frameMs * 2);
            const bool prevHit = next_ > 0 && player.current() >= next_;
            // A HOPO on the same fret as the note before cannot be tapped
            // (the tap needs another fret, 0x82120160): strum it.
            const bool tappable = n.hopo && !n.chord() && prevHit && n.frets != t_.notes[next_ - 1].frets;
            // A note the judge already took (an early tap while a chord
            // sustain's top fret matched it) is not strummed again: a second
            // strum within strum_before_forming would be a bum note.
            const bool taken = player.current() > next_;
            if (!taken && !(tap_ && tappable)) in.strum = true;
            ++next_;
            break;  // one note per frame (the judge hits one per frame)
        }
        // The frets stay down for the note (and its sustain), then come up:
        // a held chord would otherwise tap the next HOPO early (the
        // original's rule, 0x82120160), which a player would not do.
        in.frets = nowMs <= holdUntil_ ? held_ : 0;
        in.starPower = player.stats().starPower >= 50.0f && !player.stats().starPowerActive;
        return in;
    }
    bool done() const { return next_ >= t_.notes.size(); }
    // A mid-song start: begins at the first note at or after `ms`.
    void skipTo(double ms) {
        while (next_ < t_.notes.size() && double(t_.notes[next_].time) < ms) ++next_;
    }

private:
    const NoteTrack& t_;
    bool tap_;
    size_t next_ = 0;
    uint8_t held_ = 0;
    double holdUntil_ = -1;
};
