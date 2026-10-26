// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/world_driver.h"

#include <algorithm>
#include <string>

#include "game/band.h"
#include "game/light_presets.h"
#include "game/world.h"

namespace {

bool isSection(const std::string& t) { return t == "verse" || t == "chorus" || t == "solo"; }

// EVENTS texts the world answers under their own name (world_objects.dta
// handlers and WORLDEVENTS).
bool isWorldText(const std::string& t) {
    return isSection(t) || t == "music_start" || t == "band_jump" || t == "sync_wag" || t == "sync_head_bang" ||
           t.rfind("crowd_", 0) == 0;
}

}  // namespace

int WorldDriver::excitement(float rating, float warningLevel) {
    if (rating < warningLevel) return 0;  // kExcitementBoot
    if (rating < 0.33f) return 1;         // kExcitementBad
    if (rating < 0.66f) return 2;         // kExcitementOkay
    return 3;                             // kExcitementGreat
}

void WorldDriver::reset(float startMs) {
    lastMs_ = startMs;
    introSent_ = introEndSent_ = overSent_ = false;
    beat_ = note_ = lookNote_ = event_ = lookEvent_ = 0;
    excitement_ = -1;
    peak_ = false;
}

void WorldDriver::update(World& world, const Song& song, float ms, const Player* p0, const std::vector<PlayerEvent>& e0,
                         const Player* p1, const std::vector<PlayerEvent>& e1) {
    const int division = std::max(1, song.tempo.division());
    const double beats = ms >= 0 ? song.tempo.msToTick(ms) / division : ms / 500.0;
    world.setClock(ms / 1000.0, beats);
    if (gameEvents_ && !introSent_) {
        introSent_ = true;
        world.message("intro_start_msg", {Value::integer(0), Value::integer(0)});
    }
    if (!introEndSent_ && ms >= 0) {
        introEndSent_ = true;
        world.message("intro_end");
    }
    // Beats and measure lines.
    for (; beat_ < song.beats.size() && song.beats[beat_].ms <= ms; ++beat_) {
        world.interp().var("beat") = Value::integer(int(beat_));
        world.message("beat");
        if (song.beats[beat_].measure) world.message("downbeat");
    }
    // Band and venue notes; the lighting ones four beats early (start_offset -4).
    const auto& notes = song.venueNotes;
    auto isLighting = [](const VenueNote& n) { return n.track == VenueNote::Track::Triggers && n.pitch >= 48 && n.pitch <= 50; };
    for (; note_ < notes.size() && notes[note_].ms <= ms; ++note_) {
        const VenueNote& n = notes[note_];
        if (isLighting(n)) continue;
        if (n.track == VenueNote::Track::Drums) world.message(n.pitch == 36 ? "kick_drum" : "crash_symbal");
        else if (n.track == VenueNote::Track::Bass) world.message("bass_hit");
        else if (n.pitch == 52) world.message("venue_effect");
    }
    for (; lookNote_ < notes.size(); ++lookNote_) {
        const VenueNote& n = notes[lookNote_];
        if (!isLighting(n)) continue;
        if (song.tempo.tickToMs(std::max(0, n.tick - 4 * division)) > ms) break;
        static const char* const kMsg[] = {"do_lighting_next_keyframe", "do_lighting_prev_keyframe", "do_lighting_first_keyframe"};
        world.message(kMsg[n.pitch - 48]);
    }
    // The EVENTS track; sections announced a bar (four beats) early.
    for (; event_ < song.events.size() && song.events[event_].ms <= ms; ++event_) {
        const std::string& t = song.events[event_].text;
        std::vector<std::string> adjectives;
        if (lighting::parseEvent(t, adjectives)) continue;  // sent a bar early (below)
        if (isWorldText(t)) world.message(t);
    }
    for (; lookEvent_ < song.events.size(); ++lookEvent_) {
        const SongEvent& e = song.events[size_t(lookEvent_)];
        if (song.tempo.tickToMs(std::max(0, e.tick - 4 * division)) > ms) break;
        // [lighting (x)] four beats early (0x822CFD90's look-ahead): the
        // presets' delayed switch (now + 4 - fade_in / 480 beats) then ends
        // its fade on the event [C: PLAN.md].
        std::vector<std::string> adj;
        if (lighting::parseEvent(e.text, adj)) {
            std::vector<Value> list;
            for (auto& a : adj) list.push_back(Value::symbol(a));
            world.message("lighting_change", {Value::list(std::move(list))});
            continue;
        }
        if (!isSection(e.text)) continue;
        world.interp().var("upcoming_section") = Value::symbol(e.text);
        world.message("one_bar_to");
    }
    // The players.
    playerEvents(world, 0, p0, e0);
    playerEvents(world, 1, p1, e1);
    int ex = -1;
    bool peak = false;
    for (const Player* p : {p0, p1}) {
        if (!p) continue;
        ex = std::max(ex, excitement(p->rockMeter(), p->warningLevel()));
        peak = peak || (p->baseMultiplier() >= 4 && p->rockMeter() > 0.66f);
    }
    if (ex >= 0 && ex != excitement_) {
        excitement_ = ex;
        world.message("excitement", {Value::integer(ex)});
    }
    if (peak != peak_) {
        peak_ = peak;
        world.message(peak ? "peak_on" : "peak_off");
    }
    lastMs_ = ms;
}

void WorldDriver::playerEvents(World& world, int index, const Player* p, const std::vector<PlayerEvent>& events) {
    if (!p) return;
    const std::string prefix = "hit_p" + std::to_string(index) + "_fret";
    for (const PlayerEvent& e : events) {
        switch (e.kind) {
        case PlayerEvent::Kind::Hit:
            if (e.gem >= 0 && size_t(e.gem) < p->chart().gems.size()) {
                const Gem& g = p->chart().gems[size_t(e.gem)];
                for (int f = 0; f < 5; ++f)
                    if (g.frets & (1u << f)) world.message(prefix + std::to_string(f + 1));
            }
            break;
        case PlayerEvent::Kind::BuildStreak: world.message("build_streak"); break;
        case PlayerEvent::Kind::StarPowerReady: world.message("star_power_ready"); break;
        case PlayerEvent::Kind::StarPowerStart:
            world.message("star_power_start");
            // The game tells the player's character (0x822E3840 -> GuitarCharacter 0x822D25A8).
            if (world.band) world.band->starPowerStart(index);
            break;
        case PlayerEvent::Kind::StarPowerEnd: world.message("star_power_stop"); break;
        default: break;
        }
    }
}

void WorldDriver::gameWon(World& world) {
    if (overSent_) return;
    overSent_ = true;
    // The players' result first ({$this player} won); GH2's game then sends
    // the result and game_over after it (0x822CB5B8).
    if (world.band) world.band->setResult(true);
    if (gameEvents_) world.message("game_won_msg", {Value::integer(0)}), world.message("game_over");
}

void WorldDriver::gameLost(World& world) {
    if (overSent_) return;
    overSent_ = true;
    if (world.band) world.band->setResult(false);
    if (gameEvents_) world.message("game_lost"), world.message("game_over");
}
