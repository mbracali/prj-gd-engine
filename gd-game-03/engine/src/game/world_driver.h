// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <vector>

#include "game/chart.h"
#include "game/band_performer.h"
#include "game/player.h"

class World;

// What the song tells the venue (game/world.h), each frame:
// - the clock: song seconds and beats (the tempo map);
// - beat / downbeat on every beat and measure line ($beat set; WORLD_OBJECT_BASE
//   beat / downbeat) [I: GH2's BeatMatch poll 0x822D03E8 sends them];
// - the EVENTS track (0x822CFD90, [C] address, behaviour from PLAN.md):
//   [verse] / [chorus] / [solo] as world messages at their time and
//   one_bar_to ($upcoming_section) four beats before; [lighting (x)] as
//   lighting_change (x); the crowd texts, band_jump, sync_wag,
//   sync_head_bang and music_start as messages of the same name;
// - the band and venue notes (VenueNote: kick_drum, crash_symbal, bass_hit,
//   venue_effect, the do_lighting_*_keyframe four beats early);
// - the players: hit_p0_fret1..5 / hit_p1_fret1..5 for each fret of a hit
//   gem (0x822e40e0), build_streak (0x822ddf70) when the multiplier steps
//   up, star_power_ready / start / stop, excitement (0 Boot under the
//   warning level, 1 Bad under 0.33, 2 Okay under 0.66, else 3 Great;
//   0x822CAAB0: player 0's rating, the co-op band's (0x822C9BE8),
//   competitive: the better player's) when it changes, peak_on /
//   peak_off at 4x with a rating over 0.66 (the HUD's peak, 0x822dcef0);
// - without GH2's menus (`gameEvents`), also the game's own messages the
//   world otherwise gets as a game sink: intro_start_msg, intro_end (at
//   song time 0 [I]), game_won_msg and game_lost.
// hit_snare / hit_hihat come from the drummer's clips (char_objects.dta
// drummer add_sinks) and wait for the band (M9).
class WorldDriver {
public:
    explicit WorldDriver(bool gameEvents = false) : gameEvents_(gameEvents) {}
    // A new song; the clock starts at `startMs` (the lead-in).
    void reset(float startMs);
    // One frame at song time `ms`, with the players' events of this frame.
    // `band`: the co-op band, whose meter gives the excitement and peak.
    void update(World& world, const Song& song, float ms, const Player* p0, const std::vector<PlayerEvent>& e0, const Player* p1,
                const std::vector<PlayerEvent>& e1, const BandPerformer* band = nullptr);
    // The song's end, won or lost (the game messages; once each).
    void gameWon(World& world);
    void gameLost(World& world);

    // Excitement for a rock meter rating (0x822CAAB0).
    static int excitement(float rating, float warningLevel);
    // The game's excitement now (Game +0x338): -1 before the first update.
    int excitementLevel() const { return excitement_; }

private:
    void playerEvents(World& world, int index, const Player* p, const std::vector<PlayerEvent>& events);

    bool gameEvents_ = false;
    float lastMs_ = 0;
    bool introSent_ = false, introEndSent_ = false, overSent_ = false;
    size_t beat_ = 0, note_ = 0, lookNote_ = 0, event_ = 0, lookEvent_ = 0;
    int excitement_ = -1;
    bool peak_ = false;
};
