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

// Rock Band's MidiParser (ctor RB 0x824BB708, parse RB 0x824BB198, poll
// RB 0x824B8210, handlers RB 0x824BC0E8) [C unless noted]: the band's
// animation parsers of config/midi_parsers.dta, written as script objects
// ({new MidiParser guitar_fret (track_name 'PART GUITAR') (init ...) (gem
// ...)}), unlike GH2's table-driven ones (char/midi_parser.h). Later
// Harmonix games (Rock Band 2, The Beatles) use the same class.
//
// Parse: the track's text events, its notes (only `allowed_notes` when the
// parser lists them) and its chart gems (difficulty Expert [I]) are walked
// in time order (ties: texts, then notes, then gems); each one sets the
// globals $mp.start / $mp.end / $mp.length (beats), $mp.prev_start /
// $mp.prev_end (start minus the previous item's start / end), $mp.val (the
// note's pitch, the gem's fret mask kMPGem1..5, a text's first word),
// $mp.data, $mp.single_bit / $mp.lowest_bit (gems) and runs the parser's
// `text` / `midi` / `gem` handler, which adds events with add_message
// (start, end = the item's) or add_message_se. `init` runs first, `term`
// last.
//
// Each added event (RB 0x824BB9D8 / gap RB 0x824BB8F0): start +=
// start_offset, end = zero_length ? start : end, += end_offset; inserted
// after the last event starting at or before it; the event before it (or
// the parser's lead-in) ends at start - clamp(start - its end, min_gap,
// max_gap), unless an idle_message fits (a gap of idle_length or more
// between that end + idle_start_gap and start - idle_end_gap): then an idle
// event goes there first. Its length is clamped to [min_length,
// max_length]. After `term`, with an idle_message, a last idle runs from the
// last end + idle_start_gap; `inverted` makes every event span from the
// previous one's end to its own start (the hands arrive on the note).
//
// Poll (each frame at the song beat): every event whose start has passed
// goes to the sinks as {sink <message_type> <values...> [<length>]}
// (message_type empty: the values are the message; append_length adds the
// event's length in beats), with $mp.start / $mp.end / $mp.length the
// event's and $mp.parser the parser.
class RbMidiParser : public ScriptObject, public SinkHolder {
public:
    // `def` = the {new MidiParser <name> ...} command (kept alive by the caller).
    RbMidiParser(std::string name, const DataNode& def);
    ~RbMidiParser() override;

    // Builds the events for `midi`; false when no track matches.
    bool load(Interp& in, const MidiFile& midi, const Song* song, Difficulty difficulty = Difficulty::Expert);
    void poll(Interp& in, double beat);
    void reset();

    void addSink(ScriptObject* sink);
    void removeSink(ScriptObject* sink);
    void sinkDestroyed(ScriptObject* sink) override { removeSink(sink); }
    const std::vector<ScriptObject*>& sinks() const { return sinks_; }

    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override;
    Value getProp(Interp& in, const std::string& key) override;
    void setProp(Interp& in, const std::string& key, const Value& v) override;

    // Tests / logs.
    struct EventInfo {
        float start = 0, end = 0;
        std::string message;
    };
    std::vector<EventInfo> events() const;
    int eventsSent() const { return sent_; }
    const std::string& trackName() const { return trackName_; }
    const std::string& messageType() const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
    const DataNode& def_;
    std::vector<ScriptObject*> sinks_;
    std::string trackName_;
    int sent_ = 0;
};

// Every parser of config/midi_parsers.dta that the band needs, registered
// by name on the interpreter ({guitar_fret add_sink fret.dmidi}).
class RbMidiParserSet {
public:
    // `names` empty: every parser of the file.
    RbMidiParserSet(Interp& in, const GameData& data, const std::vector<std::string>& names = {});
    ~RbMidiParserSet();
    RbMidiParserSet(const RbMidiParserSet&) = delete;
    RbMidiParserSet& operator=(const RbMidiParserSet&) = delete;

    void load(const MidiFile& midi, const Song* song);
    void poll(double beat);
    RbMidiParser* find(const std::string& name) const;
    const std::vector<std::unique_ptr<RbMidiParser>>& parsers() const { return parsers_; }

private:
    Interp& in_;
    std::vector<std::unique_ptr<RbMidiParser>> parsers_;
};
