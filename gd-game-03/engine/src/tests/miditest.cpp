// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/miditest.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <exception>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "char/midi_parser.h"
#include "data/game_data.h"
#include "data/midi.h"
#include "game/song_db.h"
#include "scene/milo.h"

namespace {

// The expected log of the golden song: line count and FNV-1a hash. A change
// in the parser rules or the scripts moves them; check the log
// (GD03_MIDI_LOG=<file>) and update both.
constexpr int kGoldenLines = 6630;
constexpr uint32_t kGoldenHash = 0xa8e90ce2u;

const char* const kParsers[] = {"player0_fret",     "player0_strum",   "bass_strum",     "player0_fret_pos", "player0_parser",
                                "bassist_parser",   "drummer_parser",  "singer_parser",  "keyboard_parser"};

// The texts the band characters' scripts answer (char_objects.dta BAND_COMMON,
// drummer, CHAR_COMMON) [C data]; anything else on a BAND track falls through.
const std::set<std::string> kBandVocab = {"play", "idle", "normal_tempo", "half_tempo", "double_tempo"};
const std::set<std::string> kDrumVocab = {"play", "idle", "normal_tempo", "half_tempo", "double_tempo",
                                          "allbeat", "nobeat", "double_time", "half_time"};

uint32_t fnv(const std::string& s, uint32_t h) {
    for (unsigned char c : s) h = (h ^ c) * 16777619u;
    return h;
}

// {game ...} for track_name: one player, guitar.
class GameStub : public ScriptObject {
public:
    GameStub() : ScriptObject("game", "Game") {}
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>&, Value& r) override {
        if (msg == "multiplayer" || msg == "multiplayer_vs") return r = Value::integer(0), true;
        if (msg == "get_track_sym") return r = Value::symbol("guitar"), true;
        return false;
    }
};

// A sink that logs every message.
class Recorder : public ScriptObject {
public:
    Recorder(std::string parser, std::vector<std::string>& log, const double& beat)
        : ScriptObject("rec_" + parser), parser_(std::move(parser)), log_(log), beat_(beat) {}
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>& args, Value& r) override {
        char b[48];
        std::snprintf(b, sizeof b, "%9.3f ", beat_);
        std::string l = b + parser_ + " " + msg;
        for (const Value& a : args) {
            if (a.type == Value::Type::Float) {
                std::snprintf(b, sizeof b, " %.3f", double(a.f));
                l += b;
            } else {
                l += " " + a.str();
            }
        }
        log_.push_back(l);
        messages.push_back({msg, args});
        r = Value::integer(0);
        return true;
    }
    std::vector<std::pair<std::string, std::vector<Value>>> messages;

private:
    std::string parser_;
    std::vector<std::string>& log_;
    const double& beat_;
};

std::string firstWord(const std::string& v) {
    std::string s = v;
    while (!s.empty() && (s[0] == '(' || s[0] == ' ')) s.erase(0, 1);
    size_t e = s.find_first_of(" )");
    return e == std::string::npos ? s : s.substr(0, e);
}

}  // namespace

int runMidiParserTest(const GameData& data) {
    int failures = 0;
    const std::vector<SongInfo> songs = loadSongs(data);
    if (songs.empty()) {
        std::printf("midi parsers: no songs\n");
        return 1;
    }
    const SongInfo* info = &songs.front();
    for (const SongInfo& s : songs)
        if (s.id == "hangar18") info = &s;

    // The hand clips the guitarists have (every <c>_fret / <c>_strum set).
    std::set<std::string> clips;
    for (const char* c : {"alterna1", "alterna2", "classic", "deathmetal1", "funk1", "glam1", "goth1", "grim", "metal1", "punk1", "rock1",
                          "rockabill1", "rockabill2", "metal_bass"})
        for (const char* set : {"_fret", "_strum"}) {
            const std::string p = std::string("char/") + c + "/anims/gen/" + c + set + ".milo_ps2";
            if (!data.exists(p)) continue;
            try {
                auto m = scene::loadMilo(data, p);
                m->root().forEach([&](scene::Object& o) {
                    if (o.cls == "CharClipSamples") clips.insert(o.name);
                });
            } catch (const std::exception& e) {
                std::printf("midi parsers: %s: %s\n", p.c_str(), e.what());
            }
        }

    Interp in;
    in.seed(0x6d696469u);
    GameStub game;
    in.registerObject(&game);
    std::vector<std::string> log;
    double beat = 0;
    int badSpots = 0, badClips = 0;
    std::map<std::string, std::map<std::string, int>> unknown;  // track -> text -> count
    {
        std::vector<std::string> names(std::begin(kParsers), std::end(kParsers));
        MidiParserSet set(in, data, names);
        std::vector<std::unique_ptr<Recorder>> recs;
        for (const auto& p : set.parsers()) {
            recs.push_back(std::make_unique<Recorder>(p->name(), log, beat));
            p->addSink(recs.back().get());
        }
        MidiFile midi;
        try {
            midi = parseMidi(data.read(info->audio.midiPath));
        } catch (const std::exception& e) {
            std::printf("midi parsers: %s: %s\n", info->id.c_str(), e.what());
            return 1;
        }
        set.load(midi);
        TempoMap tempo;
        tempo.build(midi);
        double lastTick = 0;
        for (const MidiTrack& t : midi.tracks)
            if (!t.events.empty()) lastTick = std::max(lastTick, double(t.events.back().tick));
        const double endMs = tempo.tickToMs(lastTick) + 2000;
        for (double ms = -3000; ms < endMs; ms += 1000.0 / 60.0) {
            beat = tempo.tickToMs(0) <= ms ? tempo.msToTick(ms) / double(midi.division) : (ms / 500.0);
            set.poll(beat);
        }
        for (size_t i = 0; i < recs.size(); ++i) {
            const std::string& pn = set.parsers()[i]->name();
            for (const auto& [msg, args] : recs[i]->messages) {
                if (msg != "midi_parser" || args.empty()) continue;
                const Value& v = args[0];
                if (pn == "player0_fret_pos") {
                    const std::string s = v.str();
                    if (s.size() != 21 || s.compare(0, 14, "spot_neck_fret") != 0) ++badSpots;
                } else if (v.isText() && !clips.count(v.s)) {
                    ++badClips;
                    if (badClips < 5) std::printf("midi parsers: %s sends unknown clip %s\n", pn.c_str(), v.s.c_str());
                }
            }
        }
        for (const auto& p : set.parsers())
            std::printf("midi parsers: %-17s %-14s %4d events, %4d sent, %zu sinks\n", p->name().c_str(),
                        p->trackName().empty() ? "-" : ("'" + p->trackName() + "'").c_str(), p->eventsBuilt(), p->eventsSent(),
                        p->sinks().size());

        // Every song: the parsers build; the echo tracks' unknown texts.
        int songsOk = 0, errors = 0;
        for (const SongInfo& s : songs) {
            try {
                MidiFile m = parseMidi(data.read(s.audio.midiPath));
                set.load(m);
                ++songsOk;
                for (const auto& p : set.parsers()) {
                    const bool band = p->name() == "bassist_parser" || p->name() == "singer_parser" || p->name() == "keyboard_parser";
                    const bool drums = p->name() == "drummer_parser";
                    if (!band && !drums) continue;
                    for (const auto& e : p->events()) {
                        const std::string w = firstWord(e.value);
                        if (!(drums ? kDrumVocab : kBandVocab).count(w)) ++unknown[p->trackName()][w];
                    }
                }
            } catch (const std::exception& e) {
                ++errors;
                std::printf("midi parsers: %s: %s\n", s.id.c_str(), e.what());
            }
        }
        std::printf("midi parsers: %d of %zu songs built, %d errors\n", songsOk, songs.size(), errors);
        failures += errors ? 1 : 0;
    }
    in.unregisterName("game", &game);
    for (const auto& [track, m] : unknown) {
        std::string l;
        for (const auto& [t, n] : m) l += " [" + t + "] x" + std::to_string(n);
        std::printf("midi parsers: unknown texts on '%s':%s\n", track.c_str(), l.c_str());
    }
    uint32_t h = 2166136261u;
    for (const std::string& l : log) h = fnv(l + "\n", h);
    if (const char* out = SDL_getenv("GD03_MIDI_LOG"))
        if (FILE* f = std::fopen(out, "w")) {
            for (const std::string& l : log) std::fprintf(f, "%s\n", l.c_str());
            std::fclose(f);
        }
    const bool golden = int(log.size()) == kGoldenLines && h == kGoldenHash;
    if (!golden) ++failures;
    if (badSpots || badClips) ++failures;
    std::printf("midi parsers: %s: %d messages, hash %08x (expected %d, %08x)%s; %d bad fret spots, %d unknown hand clips\n", info->id.c_str(),
                int(log.size()), unsigned(h), kGoldenLines, unsigned(kGoldenHash), golden ? "" : " MISMATCH", badSpots, badClips);
    return failures;
}
