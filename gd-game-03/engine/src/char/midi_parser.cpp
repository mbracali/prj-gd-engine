// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "char/midi_parser.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <bit>
#include <cstring>
#include <exception>

#include "data/game_data.h"
#include "game/chart.h"

namespace {

constexpr float kHuge = 1e30f;  // 0x7149f2ca

enum Type { kMidi = 0, kGem = 1, kText = 2, kEcho = 3, kNone = 4 };

// MidiParser params (+0x3c, defaults 0x8233D368, read 0x8233CF08) [C].
struct Params {
    bool zeroLength = false;
    float startOffset = 0, endOffset = 0;
    float minLength = 0, maxLength = kHuge;
    float minGap = 0, maxGap = kHuge;
    float idleLength = 0, idleStartGap = 0, idleEndGap = 0;
    bool hasIdle = false;
    const DataNode* idleEvent = nullptr;  // the (idle_event x) array
};

float number(const DataNode& n) {
    if (n.type == DataNode::Type::Float) return n.f;
    if (n.type == DataNode::Type::Int) return float(n.i);
    if (n.isText()) return n.s == "TRUE" ? 1.0f : 0.0f;
    return 0;
}

// 0x8233CF08: the params present in `list` (a parser or a mapping) override.
void readParams(Params& p, const DataNode* list) {
    if (!list) return;
    auto f = [&](const char* key, float& out) {
        if (const DataNode* n = list->find(key); n && n->children.size() > 1) out = number(n->children[1]);
    };
    if (const DataNode* n = list->find("zero_length"); n && n->children.size() > 1) p.zeroLength = number(n->children[1]) != 0;
    f("start_offset", p.startOffset);
    f("end_offset", p.endOffset);
    f("min_length", p.minLength);
    f("max_length", p.maxLength);
    f("min_gap", p.minGap);
    f("max_gap", p.maxGap);
    f("idle_length", p.idleLength);
    f("idle_start_gap", p.idleStartGap);
    f("idle_end_gap", p.idleEndGap);
    if (const DataNode* n = list->find("idle_event"); n && n->children.size() > 1) {
        p.idleEvent = n;
        p.hasIdle = true;
    }
}

// The 0x8233EFA0 clamp: within [lo, hi]; over hi -> hi.
float clampGap(float v, float lo, float hi) {
    if (v > hi) return hi;
    return v < lo ? lo : v;
}

bool isArray(const DataNode& n) { return n.type == DataNode::Type::Array; }

}  // namespace

struct MidiParser::Impl {
    int type = kNone;
    bool eval = false, inverted = false;
    const DataNode* mappings = nullptr;
    Params base;    // the parser's own (0x8233D130)
    Params params;  // in effect (after a mapping's)

    struct Event {
        float start = 0, end = 0;
        Value value;                    // without eval
        const DataNode* code = nullptr;  // with eval: the commands to run
    };
    std::vector<Event> events;
    struct MapSwitch {
        float beat;
        std::string name;
    };
    std::vector<MapSwitch> maps;
    size_t mapIndex = 0;
    const DataNode* curMapping = nullptr;
    const DataNode* curEvents = nullptr;
    float prevStart = -kHuge, prevEnd = -kHuge;  // +0x70 / +0x74
    size_t next = 0;
    long cur = -1;
    bool enterPending = true;
    bool loaded = false;
};

MidiParser::MidiParser(std::string name, const DataNode& def) : ScriptObject(std::move(name), "MidiParser"), impl_(std::make_unique<Impl>()), def_(def) {
    Impl& m = *impl_;
    // 0x8233D130 [C]: eval, inverted, type (midi gem text echo), mappings, params.
    if (const DataNode* n = def.find("eval"); n && n->children.size() > 1) m.eval = number(n->children[1]) != 0;
    if (const DataNode* n = def.find("inverted"); n && n->children.size() > 1) m.inverted = number(n->children[1]) != 0;
    if (const DataNode* n = def.find("type"); n && n->children.size() > 1 && n->children[1].isText()) {
        static const char* const kTypes[] = {"midi", "gem", "text", "echo"};
        for (int t = 0; t < 4; ++t)
            if (n->children[1].s == kTypes[t]) m.type = t;
    }
    m.mappings = def.find("mappings");
    readParams(m.base, &def);
    m.params = m.base;
}

MidiParser::~MidiParser() { stopHolding(); }

void MidiParser::reset() {
    impl_->next = 0;
    impl_->cur = -1;
    impl_->enterPending = true;
}

void MidiParser::enter(Interp&) { impl_->enterPending = true; }

void MidiParser::addSink(ScriptObject* sink) {
    if (sink && std::find(sinks_.begin(), sinks_.end(), sink) == sinks_.end()) sinks_.push_back(sink);
}

void MidiParser::removeSink(ScriptObject* sink) { sinks_.erase(std::remove(sinks_.begin(), sinks_.end(), sink), sinks_.end()); }

double MidiParser::nextStart() const {
    const Impl& m = *impl_;
    return m.next < m.events.size() ? double(m.events[m.next].start) : double(kHuge);
}
double MidiParser::curStart() const { return impl_->cur >= 0 ? double(impl_->events[size_t(impl_->cur)].start) : 0.0; }
double MidiParser::curEnd() const { return impl_->cur >= 0 ? double(impl_->events[size_t(impl_->cur)].end) : 0.0; }
int MidiParser::numEvents() const { return int(impl_->events.size()); }

std::vector<MidiParser::EventInfo> MidiParser::events() const {
    std::vector<EventInfo> v;
    for (const auto& e : impl_->events) v.push_back({e.start, e.end, e.code ? std::string("<eval>") : e.value.str()});
    return v;
}

namespace {

const MidiTrack* trackNamed(const MidiFile& midi, const std::string& name) {
    for (const MidiTrack& t : midi.tracks)
        if (t.name == name) return &t;
    return nullptr;
}

std::string textOf(Interp& in, const Value& v) {
    const Value e = in.evalValue(v);
    return e.type == Value::Type::Object && e.object ? e.object->name() : e.isText() ? e.s : e.str();
}

// 0x8233D5C8: does key `k` match the event value `v` for parser type `type`?
bool keyMatches(int type, const DataNode& k, const Value& v) {
    if (type == kMidi) {
        const int pitch = v.integerValue();
        if (k.type == DataNode::Type::Int) return k.i == pitch;
        if (isArray(k) && k.children.size() >= 2) return number(k.children[0]) <= float(pitch) && float(pitch) <= number(k.children[1]);
        return false;
    }
    if (type == kGem) {
        const unsigned mask = unsigned(v.integerValue());
        if (k.isText() && k.s == "default") return std::popcount(mask) == 1;
        if (k.type == DataNode::Type::Int) return (1u << (k.i - 1)) == mask;
        if (isArray(k)) {
            for (const DataNode& f : k.children)
                if (!(mask & (1u << (int(number(f)) - 1)))) return false;
            return true;
        }
        return false;
    }
    if (type == kText) return k.isText() && v.isText() && k.s == v.s;
    return false;
}

}  // namespace

bool MidiParser::load(Interp& in, const MidiFile& midi, const Song* songIn, Difficulty difficulty) {
    Impl& m = *impl_;
    m.events.clear();
    m.maps.clear();
    m.mapIndex = 0;
    m.curMapping = m.curEvents = nullptr;
    m.params = m.base;
    m.prevStart = m.prevEnd = -kHuge;
    reset();
    built_ = sent_ = 0;
    trackName_.clear();
    m.loaded = false;
    // track_name (0x8233EA68): the first value naming a track of the file.
    if (const DataNode* tn = def_.find("track_name"))
        for (size_t k = 1; k < tn->children.size() && trackName_.empty(); ++k) {
            const std::string n = textOf(in, in.eval(tn->children[k]));
            if (!n.empty() && trackNamed(midi, n)) trackName_ = n;
        }
    const MidiTrack* track = trackName_.empty() ? nullptr : trackNamed(midi, trackName_);
    if (!track) return false;
    const float perTick = 1.0f / float(midi.division ? midi.division : 480);

    ScriptObject* prevThis = in.var("this").object;
    in.var("this") = Value::obj(this);

    // One event through the mapping (0x8233EFA0) [C].
    auto addMapped = [&](float s, float e, const Value& data) {
        // The mapping in effect: one [map] switch consumed per event (0x82339230).
        const DataNode* mapping = m.curMapping;
        if (m.mappings) {
            if (m.mapIndex < m.maps.size() && m.maps[m.mapIndex].beat <= s) {
                const std::string& name = m.maps[m.mapIndex++].name;
                mapping = m.mappings->find(name);
                if (!mapping) mapping = m.curMapping;
            }
            if (!mapping && m.mappings->children.size() > 1 && isArray(m.mappings->children[1])) mapping = &m.mappings->children[1];
        }
        if (mapping && mapping != m.curMapping) {
            m.curEvents = mapping->find("events");
            m.curMapping = mapping;
            m.params = Params{};
            readParams(m.params, &def_);
            readParams(m.params, mapping);
        }
        if (!m.curEvents) return;
        // The first matching entry (0x8233EB88), evaluated with $mp.* (0x8233D7E8).
        const DataNode* entry = nullptr;
        for (size_t k = 1; k < m.curEvents->children.size() && !entry; ++k) {
            const DataNode& c = m.curEvents->children[k];
            if (isArray(c) && !c.children.empty() && keyMatches(m.type, c.children[0], data)) entry = &c;
        }
        if (!entry || entry->children.size() < 2) return;
        Value saved[3] = {in.var("mp.length"), in.var("mp.prev_start"), in.var("mp.prev_end")};
        in.var("mp.length") = Value::real(e - s);
        in.var("mp.prev_start") = Value::real(m.prevStart);
        in.var("mp.prev_end") = Value::real(m.prevEnd);
        const DataNode& vn = entry->children[1];
        Impl::Event ev;
        if (m.eval) {
            ev.code = &vn;
            ev.value = in.toValue(vn);
        } else {
            ev.value = vn.type == DataNode::Type::Command || vn.type == DataNode::Type::Var ? in.eval(vn) : in.toValue(vn);
        }
        in.var("mp.length") = saved[0];
        in.var("mp.prev_start") = saved[1];
        in.var("mp.prev_end") = saved[2];
        if (!m.eval && ev.value.type == Value::Type::Array) {
            if (!ev.value.array || ev.value.array->empty()) return;
            const auto& a = *ev.value.array;
            ev.value = in.evalValue(a[size_t(in.randomInt(0, int(a.size())))]);
        }
        const Params& p = m.params;
        const float start = s + p.startOffset;
        const float end = (p.zeroLength ? start : e) + p.endOffset;
        Impl::Event* prev = m.events.empty() ? nullptr : &m.events.back();
        if (p.hasIdle) {
            const float idleStart = p.idleStartGap + (prev ? prev->end : -kHuge);
            const float idleEnd = start - p.idleEndGap;
            if (p.idleLength <= idleEnd - idleStart) {
                if (prev) prev->end = idleStart - clampGap(idleStart - prev->end, p.minGap, p.maxGap);
                Impl::Event idle;
                idle.start = idleStart, idle.end = idleEnd;
                idle.value = in.toValue(p.idleEvent->children[1]);
                if (m.eval) idle.code = &p.idleEvent->children[1];
                m.events.push_back(idle);
                prev = nullptr;
            }
        }
        if (prev) prev->end = start - clampGap(start - prev->end, p.minGap, p.maxGap);
        ev.start = start;
        ev.end = start + clampGap(end - start, p.minLength, p.maxLength);
        m.events.push_back(std::move(ev));
        m.prevStart = s, m.prevEnd = e;
    };

    // The track in file order (0x8233F440 text, 0x8233F348 notes).
    int open[128];
    std::fill(std::begin(open), std::end(open), -1);
    for (const MidiEvent& me : track->events) {
        const float beat = float(me.tick) * perTick;
        if (me.status == 0xFF && me.data1 == 1) {
            const std::string& t = me.text;
            if (t.empty() || t[0] != '[') continue;
            std::string body = t.substr(1);
            if (size_t close = body.find(']'); close != std::string::npos) body.resize(close);
            if (t.compare(0, 5, "[map ") == 0) {
                if (m.mappings) m.maps.push_back({beat, body.substr(4)});
                continue;
            }
            if (m.type == kText) {
                addMapped(beat, beat, Value::symbol(body));
            } else if (m.type == kEcho) {
                // 0x8233EE88: the text parsed as a message, offsets applied, no mapping.
                Impl::Event ev;
                ev.start = beat + m.params.startOffset;
                ev.end = (m.params.zeroLength ? ev.start : beat) + m.params.endOffset;
                try {
                    DataNode parsed = parseDtaText(body);
                    std::vector<Value> v;
                    for (const DataNode& c : parsed.children) v.push_back(in.toValue(c));
                    ev.value = Value::list(std::move(v));
                } catch (const std::exception&) {
                    ev.value = Value::list({Value::symbol(body)});
                }
                m.events.push_back(std::move(ev));
            }
        } else if (m.type == kMidi && (me.status & 0xF0) >= 0x80 && (me.status & 0xF0) <= 0x90) {
            const int p = me.data1 & 0x7F;
            if (me.isNoteOn()) {
                if (open[p] < 0) open[p] = int(me.tick);
            } else if (me.isNoteOff() && open[p] >= 0) {
                addMapped(float(open[p]) * perTick, beat, Value::integer(p));
                open[p] = -1;
            }
        }
    }
    // The end of the track (0x8233F728): gems, the last idle, inversion.
    if (m.type == kGem) {
        Part part = Part::Guitar;
        bool found = false;
        for (int p = 0; p < kPartCount; ++p)
            if (trackName_ == partTrackName(Part(p))) part = Part(p), found = true;
        std::unique_ptr<Song> own;
        const Song* song = songIn;
        if (found && !song) {
            try {
                own = std::make_unique<Song>(buildSong(midi, 170));
                song = own.get();
            } catch (const std::exception&) {
            }
        }
        if (found && song)
            for (const Gem& g : song->chart(part, difficulty).gems) {
                const float s = float(g.tick) * perTick;
                addMapped(s, float(g.durationTicks) * perTick + s, Value::integer(g.frets));
            }
    }
    if (m.type != kEcho && !m.events.empty() && m.params.hasIdle) {
        const Params& p = m.params;
        Impl::Event& last = m.events.back();
        const float idleStart = last.end + p.idleStartGap;
        last.end = idleStart - clampGap(idleStart - last.end, p.minGap, p.maxGap);
        Impl::Event idle;
        idle.start = idleStart, idle.end = kHuge;
        idle.value = in.toValue(p.idleEvent->children[1]);
        if (m.eval) idle.code = &p.idleEvent->children[1];
        m.events.push_back(idle);
    }
    if (m.inverted) {
        float prevEnd = -kHuge;
        for (Impl::Event& e : m.events) {
            const float s = e.start;
            e.start = prevEnd;
            prevEnd = e.end;
            e.end = s;
        }
    }
    in.var("this") = Value::obj(prevThis);
    built_ = int(m.events.size());
    m.loaded = true;
    return true;
}

void MidiParser::poll(Interp& in, double beat) {
    Impl& m = *impl_;
    auto toSinks = [&](const std::string& msg, const std::vector<Value>& args) {
        const std::vector<ScriptObject*> sinks = sinks_;
        for (ScriptObject* s : sinks)
            if (std::find(sinks_.begin(), sinks_.end(), s) != sinks_.end()) in.send(s, msg, args);
    };
    auto runCode = [&](const DataNode& code) {
        ScriptObject* prev = in.var("this").object;
        in.var("this") = Value::obj(this);
        if (isArray(code)) {
            for (const DataNode& c : code.children)
                if (c.type == DataNode::Type::Command) in.eval(c);
        } else {
            in.eval(code);
        }
        in.var("this") = Value::obj(prev);
    };
    auto asMessage = [&](const Value& v) {
        if (v.type != Value::Type::Array || !v.array || v.array->empty()) return;
        std::vector<Value> args(v.array->begin() + 1, v.array->end());
        toSinks(textOf(in, (*v.array)[0]), args);
    };
    // enter_events on the first poll (0x8233E5B8, flag +0x2f) [C].
    if (m.enterPending) {
        m.enterPending = false;
        if (const DataNode* ee = def_.find("enter_events"))
            for (size_t k = 1; k < ee->children.size(); ++k) {
                if (m.eval) runCode(ee->children[k]);
                else if (isArray(ee->children[k])) asMessage(in.toValue(ee->children[k]));
            }
    }
    if (!m.loaded || m.next >= m.events.size() || double(m.events[m.next].start) > beat) return;
    m.cur = long(m.next++);
    const Impl::Event& e = m.events[size_t(m.cur)];
    ++sent_;
    if (m.eval) {
        if (e.code) runCode(*e.code);
    } else if (e.value.type == Value::Type::Array) {
        asMessage(e.value);
    } else {
        toSinks("midi_parser", {e.value, Value::real(e.start), Value::real(e.end)});
    }
}

bool MidiParser::handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) {
    const Impl& m = *impl_;
    auto object = [&](const Value& raw) -> ScriptObject* {
        const Value v = in.evalValue(raw);
        if (v.type == Value::Type::Object) return v.object;
        if (v.isText()) {
            if (ScriptObject* o = in.findObject(v.s)) return o;
            return in.resolver ? in.resolver(v.s) : nullptr;
        }
        return nullptr;
    };
    if (msg == "add_sink" || msg == "add_sinks") {
        if (!args.empty()) addSink(object(args[0]));
        return r = Value::integer(0), true;
    }
    if (msg == "remove_sink") {
        if (!args.empty()) removeSink(object(args[0]));
        return r = Value::integer(0), true;
    }
    const Impl::Event* cur = m.cur >= 0 ? &m.events[size_t(m.cur)] : nullptr;
    if (msg == "cur_length") return r = Value::real(cur ? cur->end - cur->start : 0.0f), true;
    if (msg == "cur_start") return r = Value::real(cur ? cur->start : 0.0f), true;
    if (msg == "cur_end") return r = Value::real(cur ? cur->end : 0.0f), true;
    if (msg == "next_start") return r = Value::real(float(nextStart())), true;
    if (msg == "next_start_delta") return r = Value::real(float(nextStart()) - (cur ? cur->start : 0.0f)), true;
    if (msg == "num_events") return r = Value::integer(int(m.events.size())), true;
    if (msg == "idle_event") return r = m.params.idleEvent ? in.toValue(m.params.idleEvent->children[1]) : Value::integer(0), true;
    return false;
}

// ---- the set -----------------------------------------------------------------

MidiParserSet::MidiParserSet(Interp& in, const GameData& data, const std::vector<std::string>& names) : in_(in) {
    try {
        ScriptLoader loader(data);
        // The game's macros (kBassPluck, TRUE...) are global when the parsers load.
        try {
            loader.load("config/macros.dta");
        } catch (const std::exception&) {
        }
        tree_ = &in.keep(loader.load("config/midi_parsers.dta"));
    } catch (const std::exception& e) {
        SDL_Log("midi parsers: %s", e.what());
        return;
    }
    for (const DataNode& c : tree_->children) {
        if (!isArray(c) || c.children.empty() || !c.children[0].isText() || !c.find("type")) continue;
        const std::string& n = c.children[0].s;
        if (!names.empty() && std::find(names.begin(), names.end(), n) == names.end()) continue;
        parsers_.push_back(std::make_unique<MidiParser>(n, c));
        in.registerObject(parsers_.back().get());
    }
}

MidiParserSet::~MidiParserSet() {
    for (auto& p : parsers_) in_.unregisterName(p->name(), p.get());
}

void MidiParserSet::load(const MidiFile& midi, const Song* song, Difficulty difficulty) {
    // The gems of the parsers' tracks (once for every gem parser).
    std::unique_ptr<Song> own;
    if (!song) {
        try {
            own = std::make_unique<Song>(buildSong(midi, 170));
            song = own.get();
        } catch (const std::exception&) {
        }
    }
    for (auto& p : parsers_) p->load(in_, midi, song, difficulty);
}

void MidiParserSet::enter() {
    for (auto& p : parsers_) p->enter(in_);
}

void MidiParserSet::poll(double beat) {
    for (auto& p : parsers_) p->poll(in_, beat);
}

MidiParser* MidiParserSet::find(const std::string& name) const {
    for (const auto& p : parsers_)
        if (p->name() == name) return p.get();
    return nullptr;
}
