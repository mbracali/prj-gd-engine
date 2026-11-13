// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "char/rb_midi_parser.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <exception>

#include "data/game_data.h"
#include "game/chart.h"

namespace {

constexpr float kHuge = 1e30f;  // 0x7149f2ca

float num(const Value& v) { return v.isNumber() ? v.number() : (v.isText() && v.s == "TRUE" ? 1.0f : 0.0f); }

// The 0x824BB8F0 / 0x824BB9D8 clamp: v over hi -> hi, under lo -> lo.
float clampGap(float v, float lo, float hi) {
    if (v > hi) return hi;
    return v < lo ? lo : v;
}

}  // namespace

struct RbMidiParser::Impl {
    // Params (the struct at +0x50, defaults RB 0x824B7E18) [C].
    bool zeroLength = false;
    float startOffset = 0, endOffset = 0;
    float minLength = 0, maxLength = kHuge;
    float minGap = -kHuge, maxGap = kHuge;
    float idleLength = 0, idleStartGap = 0, idleEndGap = 0;
    std::string idleMessage;   // +0x78
    std::string messageType;   // +0x8c
    bool appendLength = false;  // +0x90
    bool inverted = false;      // +0x4c

    struct Event {
        float start = 0, end = 0;
        std::vector<Value> values;      // the message (without type and length)
        const DataNode* script = nullptr;  // add_script
        Value scriptValue;
    };
    std::vector<Event> events;
    float leadIn = -kHuge;  // +0x84: the end before the first event

    // The items being parsed (RB 0x824BB198): texts, notes, gems.
    struct Item {
        float start = 0, end = 0;
        Value data;
    };
    enum List { kTexts, kNotes, kGems, kNone };
    std::vector<Item> lists[3];
    int curList = kNone;
    int index = 0;
    float prevStart = -kHuge, prevEnd = -kHuge;  // +0x7c / +0x80
    const TempoMap* tempo = nullptr;
    int division = 480;
    size_t next = 0;
    const Event* cur = nullptr;
    bool loaded = false;

    double beatToSec(double beat) const { return tempo ? tempo->tickToMs(beat * division) / 1000.0 : beat * 0.5; }
    double secToBeat(double sec) const { return tempo ? tempo->msToTick(sec * 1000.0) / division : sec * 2.0; }
};

RbMidiParser::RbMidiParser(std::string name, const DataNode& def) : ScriptObject(std::move(name), "MidiParser"), impl_(std::make_unique<Impl>()), def_(def) {
    // {new MidiParser <name> (props / handlers)...}
    setTypeDef(&def, 3);
    if (const DataNode* n = def.find("inverted"); n && n->children.size() > 1) {
        const DataNode& v = n->children[1];
        impl_->inverted = v.type == DataNode::Type::Int ? v.i != 0 : (v.isText() && v.s == "TRUE");
    }
}

RbMidiParser::~RbMidiParser() { stopHolding(); }

const std::string& RbMidiParser::messageType() const { return impl_->messageType; }

void RbMidiParser::reset() {
    impl_->next = 0;
    impl_->cur = nullptr;
}

void RbMidiParser::addSink(ScriptObject* sink) {
    if (sink && std::find(sinks_.begin(), sinks_.end(), sink) == sinks_.end()) sinks_.push_back(sink);
}

void RbMidiParser::removeSink(ScriptObject* sink) { sinks_.erase(std::remove(sinks_.begin(), sinks_.end(), sink), sinks_.end()); }

std::vector<RbMidiParser::EventInfo> RbMidiParser::events() const {
    std::vector<EventInfo> v;
    for (const auto& e : impl_->events) {
        std::string s;
        for (const Value& x : e.values) s += (s.empty() ? "" : " ") + x.str();
        v.push_back({e.start, e.end, e.script ? std::string("<script>") : s});
    }
    return v;
}

namespace {

// SetGlobalVars (RB 0x824B8608) [C]: the item's globals; prev deltas against
// the last item set, which this one becomes.
void setGlobals(Interp& in, RbMidiParser::Impl& m, int list, int index) {
    const auto& items = m.lists[list];
    if (index < 0 || size_t(index) >= items.size()) {
        in.var("mp.out_of_bounds") = Value::integer(1);
        return;
    }
    const auto& it = items[size_t(index)];
    in.var("mp.out_of_bounds") = Value::integer(0);
    in.var("mp.start") = Value::real(it.start);
    in.var("mp.end") = Value::real(it.end);
    in.var("mp.length") = Value::real(it.end - it.start);
    in.var("mp.prev_start") = Value::real(it.start - m.prevStart);
    in.var("mp.prev_end") = Value::real(it.start - m.prevEnd);
    in.var("mp.data") = it.data;
    if (list == RbMidiParser::Impl::kTexts) {
        in.var("mp.val") = it.data.type == Value::Type::Array && it.data.array && !it.data.array->empty() ? (*it.data.array)[0] : it.data;
    } else {
        in.var("mp.val") = it.data;
        if (list == RbMidiParser::Impl::kGems) {
            const int mask = it.data.integerValue();
            int bit = 1;
            while (bit < 0x40 && !(mask & bit)) bit <<= 1;
            in.var("mp.lowest_bit") = Value::integer(bit);
            in.var("mp.single_bit") = Value::integer(mask == bit ? 1 : 0);
        }
    }
    m.prevStart = it.start, m.prevEnd = it.end;
}

// AddEvent (RB 0x824BB9D8) with the gap / idle step (RB 0x824BB8F0) [C].
void addEvent(RbMidiParser::Impl& m, float s, float e, std::vector<Value> values, const DataNode* script = nullptr, Value scriptValue = {}) {
    const float start = s + m.startOffset;
    const float end = (m.zeroLength ? start : e) + m.endOffset;
    long idx = long(m.events.size()) - 1;
    while (idx >= 0 && m.events[size_t(idx)].start > start) --idx;
    float* prevEnd = idx < 0 ? &m.leadIn : &m.events[size_t(idx)].end;
    bool gapDone = false;
    if (!m.idleMessage.empty()) {
        const float idleEnd = start - m.idleEndGap;
        const float idleStart = m.idleStartGap + *prevEnd;
        if (m.idleLength <= idleEnd - idleStart) {
            *prevEnd = idleStart - clampGap(idleStart - *prevEnd, m.minGap, m.maxGap);
            RbMidiParser::Impl::Event idle;
            idle.start = idleStart, idle.end = idleEnd;
            idle.values = {Value::symbol(m.idleMessage)};
            m.events.insert(m.events.begin() + (idx + 1), std::move(idle));
            ++idx;
            gapDone = true;
        }
    }
    if (!gapDone) *prevEnd = start - clampGap(start - *prevEnd, m.minGap, m.maxGap);
    RbMidiParser::Impl::Event ev;
    ev.start = start;
    ev.end = start + clampGap(end - start, m.minLength, m.maxLength);
    ev.values = std::move(values);
    ev.script = script;
    ev.scriptValue = std::move(scriptValue);
    m.events.insert(m.events.begin() + (idx + 1), std::move(ev));
}

const MidiTrack* trackNamed(const MidiFile& midi, const std::string& name) {
    for (const MidiTrack& t : midi.tracks)
        if (t.name == name) return &t;
    return nullptr;
}

}  // namespace

bool RbMidiParser::load(Interp& in, const MidiFile& midi, const Song* song, Difficulty difficulty) {
    Impl& m = *impl_;
    const bool inverted = m.inverted;
    m = Impl{};
    m.inverted = inverted;
    sent_ = 0;
    trackName_.clear();
    m.tempo = song ? &song->tempo : nullptr;
    m.division = midi.division ? midi.division : 480;
    // track_name: the first value naming a track of the file.
    if (const DataNode* tn = def_.find("track_name"))
        for (size_t k = 1; k < tn->children.size() && trackName_.empty(); ++k) {
            const Value v = in.evalValue(in.toValue(tn->children[k]));
            const std::string n = v.isText() ? v.s : v.str();
            if (!n.empty() && trackNamed(midi, n)) trackName_ = n;
        }
    const MidiTrack* track = trackName_.empty() ? nullptr : trackNamed(midi, trackName_);
    if (!track) return false;
    const float perTick = 1.0f / float(m.division);
    const DataNode* hText = handler("text");
    const DataNode* hMidi = handler("midi");
    const DataNode* hGem = handler("gem");
    std::vector<int> allowed;
    if (const DataNode* a = def_.find("allowed_notes"))
        for (size_t k = 1; k < a->children.size(); ++k)
            if (a->children[k].type == DataNode::Type::Int) allowed.push_back(a->children[k].i);
    // The track's texts ("[map HandMap_DropD2]" -> (map HandMap_DropD2)) and notes.
    int open[128];
    std::fill(std::begin(open), std::end(open), -1);
    for (const MidiEvent& me : track->events) {
        const float beat = float(me.tick) * perTick;
        if (me.status == 0xFF && me.data1 == 1 && hText) {
            std::string body = me.text;
            if (!body.empty() && body[0] == '[') {
                body = body.substr(1);
                if (size_t close = body.find(']'); close != std::string::npos) body.resize(close);
            }
            Value data;
            try {
                DataNode parsed = parseDtaText(body);
                std::vector<Value> v;
                for (const DataNode& c : parsed.children) v.push_back(in.toValue(c));
                data = Value::list(std::move(v));
            } catch (const std::exception&) {
                data = Value::list({Value::symbol(body)});
            }
            m.lists[Impl::kTexts].push_back({beat, beat, std::move(data)});
        } else if (hMidi && (me.status & 0xF0) >= 0x80 && (me.status & 0xF0) <= 0x90) {
            const int p = me.data1 & 0x7F;
            if (me.isNoteOn()) {
                if (open[p] < 0) open[p] = int(me.tick);
            } else if (me.isNoteOff() && open[p] >= 0) {
                if (allowed.empty() || std::find(allowed.begin(), allowed.end(), p) != allowed.end())
                    m.lists[Impl::kNotes].push_back({float(open[p]) * perTick, beat, Value::integer(p)});
                open[p] = -1;
            }
        }
    }
    auto byStart = [](const Impl::Item& a, const Impl::Item& b) { return a.start < b.start; };
    std::stable_sort(m.lists[Impl::kNotes].begin(), m.lists[Impl::kNotes].end(), byStart);
    if (hGem && song)
        for (int p = 0; p < 4; ++p)
            if (trackName_ == partTrackName(Part(p))) {
                for (const Gem& g : song->chart(Part(p), difficulty).gems) {
                    const float s = float(g.tick) * perTick;
                    m.lists[Impl::kGems].push_back({s, s + float(g.durationTicks) * perTick, Value::integer(g.frets)});
                }
                break;
            }

    ScriptObject* prevThis = in.var("this").object;
    in.var("this") = Value::obj(this);
    in.send(this, "init", {}, false);
    // The items in time order (ties: texts, notes, gems) through their handlers.
    size_t pos[3] = {0, 0, 0};
    const DataNode* handlers[3] = {hText, hMidi, hGem};
    for (;;) {
        int best = Impl::kNone;
        float t = kHuge;
        for (int l = 0; l < 3; ++l)
            if (handlers[l] && pos[l] < m.lists[l].size() && m.lists[l][pos[l]].start < t) best = l, t = m.lists[l][pos[l]].start;
        if (best == Impl::kNone) break;
        m.curList = best;
        m.index = int(pos[best]);
        setGlobals(in, m, best, m.index);
        in.send(this, best == Impl::kTexts ? "text" : best == Impl::kNotes ? "midi" : "gem", {}, false);
        in.var("mp.data") = Value::integer(0);
        ++pos[best];
    }
    m.curList = Impl::kNone;
    in.send(this, "term", {}, false);
    // The last idle, then the inversion (RB 0x824BB198's tail) [C].
    if (!m.events.empty() && !m.idleMessage.empty()) {
        Impl::Event& last = m.events.back();
        const float idleStart = m.idleStartGap + last.end;
        last.end = idleStart - clampGap(idleStart - last.end, m.minGap, m.maxGap);
        Impl::Event idle;
        idle.start = idleStart, idle.end = kHuge;
        idle.values = {Value::symbol(m.idleMessage)};
        m.events.push_back(std::move(idle));
    }
    if (m.inverted) {
        float prevEnd = m.leadIn;
        for (Impl::Event& e : m.events) {
            const float s = e.start;
            e.start = prevEnd;
            prevEnd = e.end;
            e.end = s;
        }
    }
    in.var("this") = Value::obj(prevThis);
    m.loaded = true;
    return true;
}

void RbMidiParser::poll(Interp& in, double beat) {
    Impl& m = *impl_;
    if (!m.loaded) return;
    while (m.next < m.events.size() && double(m.events[m.next].start) <= beat) {
        const Impl::Event& e = m.events[m.next++];
        m.cur = &e;
        ++sent_;
        in.var("mp.start") = Value::real(e.start);
        in.var("mp.end") = Value::real(e.end);
        in.var("mp.length") = Value::real(e.end - e.start);
        in.var("mp.parser") = Value::obj(this);
        if (e.script) {
            ScriptObject* prev = in.var("this").object;
            in.var("this") = Value::obj(this);
            if (e.script->type == DataNode::Type::Array) {
                for (const DataNode& c : e.script->children) in.eval(c);
            } else {
                in.eval(*e.script);
            }
            in.var("this") = Value::obj(prev);
            continue;
        }
        std::vector<Value> args = e.values;
        std::string msg = m.messageType;
        if (msg.empty()) {
            if (args.empty()) continue;
            const Value head = in.evalValue(args.front());
            msg = head.isText() ? head.s : head.str();
            args.erase(args.begin());
        }
        if (m.appendLength) args.push_back(Value::real(e.end - e.start));
        const std::vector<ScriptObject*> sinks = sinks_;
        for (ScriptObject* s : sinks)
            if (std::find(sinks_.begin(), sinks_.end(), s) != sinks_.end()) in.send(s, msg, args, false);
    }
}

Value RbMidiParser::getProp(Interp& in, const std::string& key) {
    const Impl& m = *impl_;
    if (key == "zero_length") return Value::integer(m.zeroLength);
    if (key == "start_offset") return Value::real(m.startOffset);
    if (key == "end_offset") return Value::real(m.endOffset);
    if (key == "min_length") return Value::real(m.minLength);
    if (key == "max_length") return Value::real(m.maxLength);
    if (key == "min_gap") return Value::real(m.minGap);
    if (key == "max_gap") return Value::real(m.maxGap);
    if (key == "idle_length") return Value::real(m.idleLength);
    if (key == "idle_start_gap") return Value::real(m.idleStartGap);
    if (key == "idle_end_gap") return Value::real(m.idleEndGap);
    if (key == "idle_message") return Value::symbol(m.idleMessage);
    if (key == "message_type") return Value::symbol(m.messageType);
    if (key == "append_length") return Value::integer(m.appendLength);
    if (key == "index") return Value::integer(m.index);
    return ScriptObject::getProp(in, key);
}

void RbMidiParser::setProp(Interp& in, const std::string& key, const Value& raw) {
    Impl& m = *impl_;
    const Value v = in.evalValue(raw);
    const auto text = [&] { return v.isText() ? v.s : v.type == Value::Type::Int && v.i == 0 ? std::string() : v.str(); };
    if (key == "zero_length") m.zeroLength = num(v) != 0;
    else if (key == "start_offset") m.startOffset = num(v);
    else if (key == "end_offset") m.endOffset = num(v);
    else if (key == "min_length") m.minLength = num(v);
    else if (key == "max_length") m.maxLength = num(v);
    else if (key == "min_gap") m.minGap = num(v);
    else if (key == "max_gap") m.maxGap = num(v);
    else if (key == "idle_length") m.idleLength = num(v);
    else if (key == "idle_start_gap") m.idleStartGap = num(v);
    else if (key == "idle_end_gap") m.idleEndGap = num(v);
    else if (key == "idle_message") m.idleMessage = text();
    else if (key == "message_type") m.messageType = text();
    else if (key == "append_length") m.appendLength = num(v) != 0;
    else if (key == "index") {
        // SetIndex (RB 0x824B9040): the globals of that item of the list being parsed.
        m.index = v.integerValue();
        if (m.curList != Impl::kNone) setGlobals(in, m, m.curList, m.index);
    } else {
        ScriptObject::setProp(in, key, v);
    }
}

bool RbMidiParser::handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) {
    Impl& m = *impl_;
    auto object = [&](const Value& raw) -> ScriptObject* {
        const Value v = in.evalValue(raw);
        if (v.type == Value::Type::Object) return v.object;
        if (v.isText()) return in.findObject(v.s);
        return nullptr;
    };
    auto arg = [&](size_t k) { return k < args.size() ? num(in.evalValue(args[k])) : 0.0f; };
    r = Value::integer(0);
    if (msg == "add_sink" || msg == "add_sinks") {
        if (!args.empty()) addSink(object(args[0]));
        return true;
    }
    if (msg == "remove_sink") {
        if (!args.empty()) removeSink(object(args[0]));
        return true;
    }
    // RB 0x824BC0E8 [C names; I: argument forms read from the scripts].
    if (msg == "add_message") {
        // The current item's start and end; the value an array is the message.
        if (args.empty()) return true;
        const Value v = in.evalValue(args[0]);
        if (v.isUnhandled()) return true;  // {switch ... kDataUnhandled}: nothing
        std::vector<Value> values = v.type == Value::Type::Array && v.array ? *v.array : std::vector<Value>{v};
        for (size_t k = 1; k < args.size(); ++k) values.push_back(in.evalValue(args[k]));
        addEvent(m, num(in.var("mp.start")), num(in.var("mp.end")), std::move(values));
        return true;
    }
    if (msg == "add_message_se") {
        if (args.size() < 3) return true;
        const Value v = in.evalValue(args[2]);
        if (v.isUnhandled()) return true;
        std::vector<Value> values = v.type == Value::Type::Array && v.array ? *v.array : std::vector<Value>{v};
        addEvent(m, arg(0), arg(1), std::move(values));
        return true;
    }
    if (msg == "add_script") {
        if (!args.empty() && args[0].code) addEvent(m, num(in.var("mp.start")), num(in.var("mp.end")), {}, args[0].code, args[0]);
        return true;
    }
    if (msg == "get_start") {
        const int i = int(arg(0));
        const auto& items = m.curList != Impl::kNone ? m.lists[m.curList] : m.lists[Impl::kNotes];
        r = Value::real(i >= 0 && size_t(i) < items.size() ? items[size_t(i)].start : kHuge);
        return true;
    }
    if (msg == "beat_to_sec") return r = Value::real(float(m.beatToSec(arg(0)))), true;
    if (msg == "sec_to_beat") return r = Value::real(float(m.secToBeat(arg(0)))), true;
    if (msg == "beat_to_sec_length") {
        // A length in beats from the current event's start, in seconds [I].
        const double s = num(in.var("mp.start"));
        return r = Value::real(float(m.beatToSec(s + arg(0)) - m.beatToSec(s))), true;
    }
    if (msg == "sec_offset_all") {
        // Every event moved by that many seconds (WORLD_OFFSET_SEC) [I: ends
        // past the song (the last idle) stay].
        const double d = arg(0);
        auto shift = [&](float& b) {
            if (b > -kHuge * 0.5f && b < kHuge * 0.5f) b = float(m.secToBeat(m.beatToSec(b) + d));
        };
        for (auto& e : m.events) shift(e.start), shift(e.end);
        shift(m.leadIn);
        return true;
    }
    if (msg == "idle") return r = Value::integer(m.idleMessage.empty() ? 0 : 1), true;
    if (msg == "num_events") return r = Value::integer(int(m.events.size())), true;
    if (msg == "next_start_delta") {
        const float next = m.next < m.events.size() ? m.events[m.next].start : kHuge;
        return r = Value::real(next - (m.cur ? m.cur->start : 0.0f)), true;
    }
    return false;
}

// ---- the set -----------------------------------------------------------------

RbMidiParserSet::RbMidiParserSet(Interp& in, const GameData& data, const std::vector<std::string>& names) : in_(in) {
    const DataNode* tree = nullptr;
    try {
        ScriptLoader loader(data);
        try {
            loader.load("config/macros.dta");
        } catch (const std::exception&) {
        }
        tree = &in.keep(loader.load("config/midi_parsers.dta"));
    } catch (const std::exception& e) {
        SDL_Log("rb midi parsers: %s", e.what());
        return;
    }
    for (const DataNode& c : tree->children) {
        if (c.type != DataNode::Type::Command || c.children.size() < 3 || !c.children[0].isText() || c.children[0].s != "new" ||
            !c.children[1].isText() || c.children[1].s != "MidiParser" || !c.children[2].isText())
            continue;
        const std::string& n = c.children[2].s;
        if (!names.empty() && std::find(names.begin(), names.end(), n) == names.end()) continue;
        parsers_.push_back(std::make_unique<RbMidiParser>(n, c));
        in.registerObject(parsers_.back().get());
    }
}

RbMidiParserSet::~RbMidiParserSet() {
    for (auto& p : parsers_) in_.unregisterName(p->name(), p.get());
}

void RbMidiParserSet::load(const MidiFile& midi, const Song* song) {
    for (auto& p : parsers_) p->load(in_, midi, song);
}

void RbMidiParserSet::poll(double beat) {
    for (auto& p : parsers_) p->poll(in_, beat);
}

RbMidiParser* RbMidiParserSet::find(const std::string& name) const {
    for (const auto& p : parsers_)
        if (p->name() == name) return p.get();
    return nullptr;
}
