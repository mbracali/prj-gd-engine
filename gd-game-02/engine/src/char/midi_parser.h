// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "data/midi.h"
#include "game/rules.h"
#include "script/script.h"

class GameData;
struct Song;

// GH2's MidiParser (ctor 0x8233F9F0, Init 0x8233D130, params 0x8233CF08;
// docs/band.md "MidiParser"): a named script object made from one array of
// config/midi_parsers.dta (player0_fret, player0_strum, bass_strum,
// player0_fret_pos, player0_parser, bassist_parser, drummer_parser,
// singer_parser, keyboard_parser...). For a song it turns one MIDI track into
// a list of timed events {start, end, value} (beats = tick / 480) and, as the
// song clock passes each start, sends it to its sinks [C unless noted]:
//
// - track_name (0x8233EA68): the first of its values that names a track of
//   the MIDI file (single player: '' then 'PART GUITAR').
// - type (0x8233D130): midi (notes, key = pitch or (lo hi)), gem (the track's
//   gems, key = fret 1..5 for a single gem, (frets...) a chord holding all of
//   them, () any, `default` any single gem: 0x8233D5C8), text (text events as
//   symbols through the mapping), echo (text events parsed as messages:
//   [play] -> play, sent as they are: 0x8233EE88). Text events must start
//   with '['; "[map X]" switches the mapping at that beat (only parsers with
//   mappings; 0x8233F440).
// - Each event (0x8233EFA0): the mapping in effect (one switch consumed per
//   event, 0x82339230) is selected and its params re-read over the parser's
//   (a mapping's idle_event overrides); the first `events` entry whose key
//   matches gives the value, evaluated with $mp.length (the raw event's
//   length), $mp.prev_start / $mp.prev_end (the previous mapped event,
//   0x8233D7E8); without eval a list value picks one element at random
//   (0x8233EB88 -> 0x8226DCB8). start += start_offset; end = zero_length ?
//   start : end, += end_offset; the previous event's end becomes start -
//   gap, gap = (start - prev end) clamped to [min_gap, max_gap] (gaps over
//   max_gap shrink to it); with an idle_event, a gap of idle_length or more
//   between prev end + idle_start_gap and start - idle_end_gap gets an idle
//   event there (the previous end moved to its start less the clamped
//   idle_start_gap); the length is clamped to [min_length, max_length].
// - At the track's end (0x8233F728): gem events are built from the gem list;
//   with an idle_event a last idle runs from the last end + idle_start_gap
//   on; inverted TRUE makes each event span from the previous event's end
//   (-1e30 for the first) to its own start (the hands arrive on the note).
// - Poll (0x8233E5B8, per frame from 0x8232FC08 at TaskMgr beat): the first
//   poll sends enter_events; then at most ONE event whose start has passed is
//   sent per poll: without eval a list value is the message itself (echo:
//   {sink play}), otherwise {sink midi_parser <value> <start> <end>} (beats;
//   CharDriverMidi 0x82184B28 / CharIKMidi 0x821837B8 read them); with eval
//   the value's commands run.
// - Queries (0x8233DB90): cur_length / cur_start / cur_end of the last sent
//   event, next_start of the next one (1e30 when none), next_start_delta
//   (next start - current start), num_events, idle_event.
class MidiParser : public ScriptObject {
public:
    // `def` = the parser's array in the loaded midi_parsers tree (kept alive
    // by the caller); `name` its first element.
    MidiParser(std::string name, const DataNode& def);
    ~MidiParser() override;

    // Builds the event list for `midi` (track_name evaluated now, so
    // {game ...} must answer). Gem parsers read `song`'s chart of the track
    // at `difficulty` (the player's [I]; built from `midi` when null).
    // Returns false when no track matches (the parser then sends nothing).
    bool load(Interp& in, const MidiFile& midi, const Song* song = nullptr, Difficulty difficulty = Difficulty::Expert);
    // The enter_events go out again on the next poll (GH2 sends them on the
    // first poll after the parser is made).
    void enter(Interp& in);
    // One poll at song beat `beat` (see above: enter_events, then at most one event).
    void poll(Interp& in, double beat);
    // Back to before the first event (a restart).
    void reset();

    // Sinks: add_sink / remove_sink from scripts, or from C++.
    void addSink(ScriptObject* sink);
    void removeSink(ScriptObject* sink);
    const std::vector<ScriptObject*>& sinks() const { return sinks_; }

    double nextStart() const;
    double curStart() const;
    double curEnd() const;
    int numEvents() const;
    const std::string& trackName() const { return trackName_; }

    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override;

    // Statistics for the tests: events built / sent.
    int eventsBuilt() const { return built_; }
    int eventsSent() const { return sent_; }
    // The built events (tests): start, end, printable value.
    struct EventInfo {
        float start = 0, end = 0;
        std::string value;
    };
    std::vector<EventInfo> events() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    const DataNode& def_;
    std::vector<ScriptObject*> sinks_;
    std::string trackName_;
    int built_ = 0, sent_ = 0;
};

// Every parser of config/midi_parsers.dta for one song, registered by name
// on the interpreter (scripts call {player0_fret add_sink left_hand.drv}).
class MidiParserSet {
public:
    // Loads config/midi_parsers.dta and makes the parsers named in `names`
    // (empty: all of them).
    MidiParserSet(Interp& in, const GameData& data, const std::vector<std::string>& names = {});
    ~MidiParserSet();
    MidiParserSet(const MidiParserSet&) = delete;
    MidiParserSet& operator=(const MidiParserSet&) = delete;

    // A song: every parser loads its track (gems from `song` at `difficulty`).
    void load(const MidiFile& midi, const Song* song = nullptr, Difficulty difficulty = Difficulty::Expert);
    void enter();
    void poll(double beat);
    MidiParser* find(const std::string& name) const;
    const std::vector<std::unique_ptr<MidiParser>>& parsers() const { return parsers_; }

private:
    Interp& in_;
    std::vector<std::unique_ptr<MidiParser>> parsers_;
    const DataNode* tree_ = nullptr;
};
