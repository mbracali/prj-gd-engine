// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Rock Band's meta game, wave 02 (GD05-009): the profile (characters,
// bands, money, the tours' progress), the Solo Tour and the Band World
// Tour as RB's ui/tour*.dta scripts drive them.
//
// [C data] what comes from the disc: the tour blocks of config/band.dta
// (tour (solo (towns <town> <venue> <event> ...)) (band (tiers ...)
// (world ...) (buzz ...) (hints ...))), every event's songs, rewards,
// requirements and outfit unlocks, the hints' conditions and screens, the
// songs' ranks (songs.dtb). The messages and what the scripts do with them
// were listed from the scripts (docs/tour_and_shop.md).
// [I] what the port decides (RB's TourPerformer / TourBand natives are not
// read in the XEX): the Nth song of a solo event is the Nth song of the
// instrument's rank order; a band event's (band N) the Nth of the band's;
// a tier is the town of the same order in (world ...); fame is the stars
// earned; fans grow with the venue's (buzz (venues ...)) table by stars;
// an event is unlocked when its (fame (town N) (world M)) and buzz are met.
#include "ui/rb_meta.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>

#include "data/dta.h"
#include "data/json.h"
#include "game/song_db.h"
#include "platform/paths.h"
#include "ui/natives.h"
#include "ui/ui_host.h"
#include "ui/ui_manager.h"
#include "ui/ui_objects.h"

namespace ui {

namespace {

std::string sArg(const std::vector<Value>& a, size_t i) {
    if (i >= a.size()) return {};
    if (a[i].type == Value::Type::Object && a[i].object) return a[i].object->name();
    return a[i].str();
}
int iArg(const std::vector<Value>& a, size_t i, int def = 0) { return i < a.size() && a[i].isNumber() ? a[i].integerValue() : def; }
std::string text(const DataNode* n, size_t i = 1) { return n && n->children.size() > i && n->children[i].isText() ? n->children[i].s : std::string(); }
int num(const DataNode* n, size_t i = 1, int def = 0) {
    if (!n || n->children.size() <= i) return def;
    const DataNode& c = n->children[i];
    return c.type == DataNode::Type::Int ? c.i : c.type == DataNode::Type::Float ? int(c.f) : def;
}
// The children lists of `n` that start with a symbol (key, node).
std::vector<std::pair<std::string, const DataNode*>> entries(const DataNode* n) {
    std::vector<std::pair<std::string, const DataNode*>> v;
    if (!n) return v;
    for (size_t i = 1; i < n->children.size(); ++i) {
        const DataNode& c = n->children[i];
        if (c.isList() && !c.children.empty() && c.children[0].isText()) v.emplace_back(c.children[0].s, &c);
    }
    return v;
}
const char* const kDiffSyms[] = {"easy", "medium", "hard", "expert"};

// ---------------------------------------------------------------- the data

struct TourEvent {
    std::string town, venue, name;  // the key: town/venue/name
    const DataNode* node = nullptr;
    std::string key() const { return town + "/" + venue + "/" + name; }
};

struct TourData {
    std::vector<std::string> soloTowns, bandTowns;
    std::vector<TourEvent> soloEvents, bandEvents;
    std::map<std::string, std::string> tierTown;  // band tier -> world town [I]
    const DataNode* solo = nullptr;
    const DataNode* band = nullptr;
    std::string hometown = "boston";
    int money = 100;
};

void loadEvents(const DataNode* towns, std::vector<std::string>& townNames, std::vector<TourEvent>& out) {
    for (const auto& [town, tn] : entries(towns)) {
        townNames.push_back(town);
        for (const auto& [venue, vn] : entries(tn))
            for (const auto& [ev, en] : entries(vn)) out.push_back({town, venue, ev, en});
    }
}

// ------------------------------------------------------------ the profile

struct CharData {
    std::string name, gender = "male", instrument = "guitar", hometown = "boston", attitude = "kGenreRocker";
    std::map<std::string, std::string> outfit;  // category -> piece
    std::set<std::string> owned;                // pieces bought or won
    float weight = 0.5f, height = 0.5f;
    int skin = 0, eye = 0, eyebrow = 0;
    int money = 100;
    // Solo Tour.
    int difficulty = 1;
    std::map<std::string, int> stars;  // "<difficulty>:<event key>" -> stars
    std::set<std::string> seenHints, seenTowns;
    std::string town, venue, event, newTown, newUnlock;
    int score = 0;
};

struct BandData {
    std::string name, hometown = "boston", motto;
    bool finalized = false;
    int fans = 0, money = 0, buzz = 0;
    std::map<std::string, int> stars;  // event key -> best stars (fame)
    std::set<std::string> items, seenHints;
    std::string town, venue, event, unlockedItem;
    int minDifficulty = 3;
};

}  // namespace

class RbMeta;

// A character of the profile (TourCharLocal / the solo performer).
class RbCharObj : public ScriptObject {
public:
    RbCharObj(RbMeta& m, const std::string& id) : ScriptObject(id, "TourCharLocal"), meta_(m) {}
    CharData d;
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override;

private:
    RbMeta& meta_;
};

// A band (TourBand / the band performer).
class RbBandObj : public ScriptObject {
public:
    RbBandObj(RbMeta& m, const std::string& id) : ScriptObject(id, "TourBand"), meta_(m) {}
    BandData d;
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override;

private:
    RbMeta& meta_;
};

// An event (get event_name / venue / award_item / min_difficulty ...).
class RbEventObj : public ScriptObject {
public:
    RbEventObj(RbMeta& m, const TourEvent& e, bool band)
        : ScriptObject("tour_event_" + e.town + "_" + e.venue + "_" + e.name, "TourEvent"), ev(e), band_(band), meta_(m) {}
    TourEvent ev;
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override;

private:
    bool band_;
    RbMeta& meta_;
};

// A list of symbols with a label per row (the tour's and the profile's lists).
class RbList : public ListProvider {
public:
    RbList(const std::string& name) : ListProvider(name, "RbList") {}
    std::function<std::vector<std::string>()> build;
    std::function<std::string(const std::string&)> label;
    std::function<bool(const std::string&)> enabled;
    std::function<bool(Interp&, const std::string&, std::vector<Value>&, Value&)> extra;
    std::vector<std::string> data;
    int selected = 0;
    std::function<void()> before;
    void refresh() {
        if (before) before();
        data = build ? build() : std::vector<std::string>{};
    }
    void initData() override { refresh(); }
    int numData() override { return int(data.size()); }
    std::string symbol(int i) override { return i >= 0 && i < int(data.size()) ? data[size_t(i)] : std::string(); }
    bool active(int i) override { return !enabled || enabled(symbol(i)); }
    void rows(std::vector<gfx::UiView::ListRow>& out, int sel, int& selRow) override {
        out.clear();
        for (size_t i = 0; i < data.size(); ++i) {
            gfx::UiView::ListRow r;
            const std::string t = label ? label(data[i]) : data[i];
            r.texts["list.lbl"] = t;
            r.texts["name.lbl"] = t;
            r.disabled = !active(int(i));
            out.push_back(std::move(r));
        }
        selRow = sel;
    }
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        if (extra && extra(in, msg, args, r)) return true;
        if (msg == "refresh" || msg == "refresh_first_showing" || msg == "reload") return refresh(), true;
        if (msg == "num_data") return r = Value::integer(numData()), true;
        if (msg == "get_symbol" || msg == "data_symbol") return r = Value::symbol(symbol(iArg(args, 0))), true;
        if (msg == "get_pos" || msg == "pos_from_sym") return r = Value::integer(indexOf(sArg(args, 0))), true;
        return false;
    }
};

class RbMeta {
public:
    RbMeta(UiManager& ui, Interp& in, std::vector<std::unique_ptr<ScriptObject>>& owned) : ui_(ui), in_(in), owned_(owned) {}
    UiManager& ui() { return ui_; }
    Interp& in() { return in_; }
    TourData data;
    std::vector<RbCharObj*> chars;
    std::vector<RbBandObj*> bands;
    ScriptObject* performer = nullptr;
    RbEventObj* event = nullptr;
    int bwtmode = -1;
    std::map<std::string, Value> props;
    RbList *soloEvents = nullptr, *bandEvents = nullptr, *venues = nullptr, *towns = nullptr, *hometowns = nullptr, *eventSongList = nullptr,
           *requirements = nullptr, *charList = nullptr, *bandList = nullptr;
    // The current event's set list and its position.
    std::vector<std::string> setlist;
    size_t songIndex = 0;
    int lastStars = 0, lastScore = 0;
    bool winning = false;

    std::function<void()> ensure;  // the tour data, once the boot scripts are loaded
    bool loaded = false;
    template <class T> T* own(std::unique_ptr<T> o) {
        T* raw = o.get();
        in_.registerObject(raw);
        owned_.push_back(std::move(o));
        return raw;
    }
    RbCharObj* newChar() {
        static int n = 0;
        auto* c = own(std::make_unique<RbCharObj>(*this, "rb_char" + std::to_string(n++)));
        c->d.hometown = data.hometown, c->d.money = data.money;
        return c;
    }
    RbBandObj* newBand() {
        static int n = 0;
        auto* b = own(std::make_unique<RbBandObj>(*this, "rb_band" + std::to_string(n++)));
        b->d.hometown = data.hometown;
        return b;
    }
    RbEventObj* eventObj(const TourEvent& e, bool band) {
        const std::string name = "tour_event_" + e.town + "_" + e.venue + "_" + e.name;
        if (ScriptObject* o = in_.findObject(name)) return dynamic_cast<RbEventObj*>(o);
        return own(std::make_unique<RbEventObj>(*this, e, band));
    }
    const TourEvent* bandEvent(const RbBandObj& b, const std::string& name) {
        for (const TourEvent& e : data.bandEvents)
            if (e.name == name && townOf(e, b) == b.d.town) return &e;
        for (const TourEvent& e : data.bandEvents)
            if (e.name == name) return &e;
        return nullptr;
    }
    const TourEvent* findEvent(bool band, const std::string& town, const std::string& venue, const std::string& name) {
        for (const TourEvent& e : band ? data.bandEvents : data.soloEvents)
            if (e.name == name && (town.empty() || e.town == town) && (venue.empty() || e.venue == venue)) return &e;
        return nullptr;
    }
    RbCharObj* soloChar() { return dynamic_cast<RbCharObj*>(performer); }
    RbBandObj* bandPerf() { return dynamic_cast<RbBandObj*>(performer); }

    // The songs by an instrument's rank (the tours' order) [I].
    std::vector<std::string> rankOrder(const std::string& inst) {
        std::vector<std::pair<int, std::string>> v;
        for (const SongInfo& s : ui_.host().songs()) {
            if (s.tutorial) continue;
            auto it = s.rank.find(inst);
            if (it == s.rank.end()) it = s.rank.find("band");
            if (it != s.rank.end()) v.emplace_back(it->second, s.id);
        }
        std::stable_sort(v.begin(), v.end());
        std::vector<std::string> out;
        for (auto& p : v) out.push_back(p.second);
        return out;
    }
    // An event's songs (its (songs ...) list) for the performer's instrument.
    std::vector<std::string> eventSongs(const TourEvent& e, bool band, const std::string& inst) {
        std::vector<std::string> out;
        const DataNode* songs = e.node ? e.node->find("songs") : nullptr;
        if (!songs) return out;
        const std::vector<std::string> order = rankOrder(band ? "band" : inst);
        for (size_t i = 1; i < songs->children.size(); ++i) {
            const DataNode& s = songs->children[i];
            if (s.type == DataNode::Type::Int) {
                if (s.i >= 1 && size_t(s.i) <= order.size()) out.push_back(order[size_t(s.i - 1)]);
            } else if (s.isList()) {  // (band N) / (unlock N)
                const int k = num(&s, 1, 1);
                const std::vector<std::string> o2 = text(&s, 0) == "unlock" ? unranked() : order;
                if (k >= 1 && size_t(k) <= o2.size()) out.push_back(o2[size_t(k - 1)]);
                else if (!order.empty()) out.push_back(order[size_t(k) % order.size()]);
            } else if (s.isText()) {
                if (s.s == "random" && !order.empty()) out.push_back(order[size_t(std::rand()) % order.size()]);
                else if (s.s == "any") out.push_back("any");
                else out.push_back(s.s);
            }
        }
        return out;
    }
    std::vector<std::string> unranked() {
        std::vector<std::string> v;
        for (const SongInfo& s : ui_.host().songs())
            if (!s.tutorial && s.rank.empty()) v.push_back(s.id);
        return v;
    }
    std::string localizeOr(const std::string& tok) {
        const std::string l = ui_.localize(tok);
        return l.empty() ? tok : l;
    }
    std::string songName(const std::string& id) {
        for (const SongInfo& s : ui_.host().songs())
            if (s.id == id) return s.name;
        return id;
    }
    // Solo: an event is open once the one before it in its town is played,
    // and a town once the town before it has every event played [I].
    int soloStatus(const RbCharObj& c, const TourEvent& e) {
        const std::string dk = std::to_string(c.d.difficulty) + ":";
        if (c.d.stars.count(dk + e.key())) return 1 | (1 << (2 + c.d.difficulty));  // played / completed
        const auto& evs = data.soloEvents;
        for (size_t i = 0; i < evs.size(); ++i) {
            if (&evs[i] != &e && evs[i].key() != e.key()) continue;
            if (i == 0) return 2;  // kTourNew
            const TourEvent& prev = evs[i - 1];
            if (prev.town == e.town) return c.d.stars.count(dk + prev.key()) ? 2 : 0;
            return c.d.stars.count(dk + prev.key()) ? 2 : 0;
        }
        return 0;
    }
    // A band's hometown plays the first tier's gigs, and the first tier's
    // town the hometown's [I: RB starts every band at home].
    std::string townOf(const TourEvent& e, const RbBandObj& b) const {
        const std::string first = data.bandTowns.empty() ? std::string() : data.bandTowns.front();
        if (e.town == first) return b.d.hometown;
        if (e.town == b.d.hometown) return first;
        return e.town;
    }
    int bandFame(const RbBandObj& b, const std::string& town) {
        int f = 0;
        for (const TourEvent& e : data.bandEvents)
            if (auto it = b.d.stars.find(e.key()); it != b.d.stars.end() && (town.empty() || townOf(e, b) == town)) f += it->second;
        return f;
    }
    int bandStatus(const RbBandObj& b, const TourEvent& e) {
        if (b.d.stars.count(e.key())) return 1 | (1 << (2 + b.d.minDifficulty));
        const DataNode* fame = e.node ? e.node->find("fame") : nullptr;
        const int townReq = num(fame ? fame->find("town") : nullptr), worldReq = num(fame ? fame->find("world") : nullptr);
        const int buzzReq = num(e.node ? e.node->find("buzz") : nullptr);
        if (bandFame(b, townOf(e, b)) < townReq || bandFame(b, "") < worldReq || b.d.buzz < buzzReq) return 0;
        return 2;
    }

    // ---- the save (rb_profile.json, version 1)
    std::string path() const { return prefDir() + "rb_profile.json"; }
    void save();
    void load();

    // The hint: the first (hints ...) entry whose cond holds and that the
    // performer has not seen (ONEOFF hints are added by add_seen_hint) [I].
    std::string hint;
    std::string currentHint() {
        const DataNode* block = soloChar() ? data.solo : bandPerf() ? data.band : nullptr;
        const DataNode* hints = block ? block->find("hints") : nullptr;
        const std::set<std::string>* seen = soloChar() ? &soloChar()->d.seenHints : bandPerf() ? &bandPerf()->d.seenHints : nullptr;
        for (const auto& [name, h] : entries(hints)) {
            if (seen && seen->count(name)) continue;
            const DataNode* cond = h->find("cond");
            bool ok = false;
            try {
                ok = cond && cond->children.size() > 1 && in_.eval(cond->children[1]).truthy();
            } catch (const std::exception&) {
            }
            if (ok) return name;
        }
        return {};
    }

private:
    UiManager& ui_;
    Interp& in_;
    std::vector<std::unique_ptr<ScriptObject>>& owned_;
};

// ------------------------------------------------------------ characters

bool RbCharObj::handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) {
    (void)in;
    r = Value::integer(0);
    RbMeta& m = meta_;
    if (msg == "get_char_name" || msg == "char_name") return r = Value::string(d.name), true;
    if (msg == "set_char_name") return d.name = sArg(args, 0), m.save(), true;
    if (msg == "get_gender") return r = Value::symbol(d.gender), true;
    if (msg == "set_gender") return d.gender = sArg(args, 0), true;
    if (msg == "get_instrument") return r = Value::symbol(d.instrument), true;
    if (msg == "set_instrument") return d.instrument = sArg(args, 0), true;
    if (msg == "instrument_icon" || msg == "track_icon") {
        const std::string t = msg == "track_icon" && !sArg(args, 0).empty() ? sArg(args, 0) : d.instrument;
        return r = Value::string(t == "drum" ? "D" : t == "vocals" ? "V" : t == "bass" ? "B" : "G"), true;
    }
    if (msg == "get_attitude") return r = Value::symbol(d.attitude), true;
    if (msg == "set_attitude") return d.attitude = sArg(args, 0), true;
    if (msg == "money") return r = Value::integer(d.money), true;
    if (msg == "cheat_add_money") return d.money += iArg(args, 0), true;
    if (msg == "tour_score") return r = Value::integer(d.score), true;
    if (msg == "hometown") return r = Value::symbol(d.hometown), true;
    if (msg == "set_hometown") return d.hometown = sArg(args, 0), true;
    if (msg == "set_weight") return d.weight = args.empty() ? d.weight : args[0].number(), true;
    if (msg == "set_height") return d.height = args.empty() ? d.height : args[0].number(), true;
    if (msg == "set_skin_color") return d.skin = iArg(args, 0), true;
    if (msg == "set_eye_color") return d.eye = iArg(args, 0), true;
    if (msg == "set_eyebrow_color") return d.eyebrow = iArg(args, 0), true;
    if (msg == "skin_color") return r = Value::integer(d.skin), true;
    if (msg == "deform" || msg == "update" || msg == "print_guid" || msg == "set_dirty") return true;
    if (msg == "has_outfit") return r = Value::integer(d.owned.count(sArg(args, 1)) || (d.outfit.count(sArg(args, 0)) && d.outfit[sArg(args, 0)] == sArg(args, 1)) ? 1 : 0), true;
    if (msg == "get_outfit") return r = Value::symbol(d.outfit.count(sArg(args, 0)) ? d.outfit[sArg(args, 0)] : "none"), true;
    if (msg == "set_outfit") return d.outfit[sArg(args, 0)] = sArg(args, 1), true;
    // The solo performer.
    if (msg == "town") return r = Value::symbol(d.town), true;
    if (msg == "venue") return r = Value::symbol(d.venue), true;
    if (msg == "set_town") return d.town = sArg(args, 0), true;
    if (msg == "set_venue") return d.venue = sArg(args, 0), true;
    if (msg == "solo_difficulty") return r = Value::integer(d.difficulty), true;
    if (msg == "solo_difficulty_sym") return r = Value::symbol(kDiffSyms[std::clamp(d.difficulty, 0, 3)]), true;
    if (msg == "set_solo_difficulty") {
        const std::string s = sArg(args, 0);
        for (int k = 0; k < 4; ++k)
            if (s == kDiffSyms[k]) d.difficulty = k;
        if (!args.empty() && args[0].isNumber()) d.difficulty = std::clamp(args[0].integerValue(), 0, 3);
        return true;
    }
    if (msg == "world_fame" || msg == "total_world_fame") {
        int diff = d.difficulty;
        const std::string s = sArg(args, 0);
        for (int k = 0; k < 4; ++k)
            if (s == kDiffSyms[k]) diff = k;
        if (msg == "total_world_fame") return r = Value::integer(int(m.data.soloEvents.size())), true;
        int n = 0;
        for (const auto& [k, st] : d.stars) n += k.rfind(std::to_string(diff) + ":", 0) == 0;
        return r = Value::integer(n), true;
    }
    if (msg == "set_event") {
        if (const TourEvent* e = m.findEvent(false, "", "", sArg(args, 0))) {
            d.event = e->name, d.town = e->town, d.venue = e->venue;
            m.event = m.eventObj(*e, false);
        }
        return true;
    }
    if (msg == "event_status") {
        const RbEventObj* e = args.empty() ? m.event : dynamic_cast<RbEventObj*>(args[0].object);
        if (!e && !args.empty())
            if (const TourEvent* te = m.findEvent(false, d.town, d.venue, sArg(args, 0))) return r = Value::integer(m.soloStatus(*this, *te)), true;
        return r = Value::integer(e ? m.soloStatus(*this, e->ev) : 0), true;
    }
    if (msg == "first_unplayed_event") {
        for (const TourEvent& e : m.data.soloEvents)
            if (m.soloStatus(*this, e) == 2) return r = Value::obj(m.eventObj(e, false)), true;
        return true;
    }
    if (msg == "set_random_songs") {
        m.setlist = m.event ? m.eventSongs(m.event->ev, false, d.instrument) : std::vector<std::string>{};
        m.songIndex = 0;
        return true;
    }
    if (msg == "next_song") {
        if (m.setlist.empty() && m.event) m.setlist = m.eventSongs(m.event->ev, false, d.instrument);
        return r = Value::symbol(m.songIndex < m.setlist.size() ? m.setlist[m.songIndex] : std::string()), true;
    }
    if (msg == "complete_song") {
        m.lastStars = iArg(args, 0), m.lastScore = iArg(args, 1);
        d.score += m.lastScore;
        ++m.songIndex;
        return true;
    }
    if (msg == "last_song") return r = Value::integer(m.songIndex + 1 >= m.setlist.size() ? 1 : 0), true;  // the song now playing is the set list's last (endgame.dta asks before complete_song) [C data]
    if (msg == "first_song") return r = Value::integer(m.songIndex == 0 ? 1 : 0), true;
    if (msg == "num_completed") return r = Value::integer(int(m.songIndex)), true;
    if (msg == "is_winning") return r = Value::integer(m.winning ? 1 : 0), true;
    if (msg == "reward") {
        const int per = m.event ? num(m.event->ev.node->find("reward_per_star"), 1, 5) : 5;
        return r = Value::integer(per * m.lastStars), true;
    }
    if (msg == "complete_event") {
        if (!m.event) return true;
        const std::string key = std::to_string(d.difficulty) + ":" + m.event->ev.key();
        const bool first = !d.stars.count(key);
        d.stars[key] = std::max(d.stars[key], m.lastStars);
        const int per = num(m.event->ev.node->find("reward_per_star"), 1, 5);
        d.money += per * m.lastStars;
        // The event's outfit for the instrument and gender (the reward).
        if (first)
            if (const DataNode* o = m.event->ev.node->find("outfit"))
                if (const DataNode* inst = o->find(d.instrument == "bass" ? "guitar" : d.instrument))
                    if (const DataNode* g = inst->find(d.gender)) d.owned.insert(text(g, 2));
        // A new town: the next event is in a town not seen yet.
        d.newTown.clear();
        for (const TourEvent& e : m.data.soloEvents)
            if (m.soloStatus(*this, e) == 2) {
                if (e.town != m.event->ev.town && !d.seenTowns.count(e.town)) d.newTown = e.town;
                break;
            }
        m.save();
        return true;
    }
    if (msg == "new_town") return r = Value::symbol(d.newTown), true;
    if (msg == "add_seen_town") return d.seenTowns.insert(sArg(args, 0)), d.newTown.clear(), true;
    if (msg == "unlock_town") return r = Value::symbol(d.newTown), true;
    if (msg == "new_unlock") return r = Value::symbol(d.newUnlock), true;
    if (msg == "advance_new_unlock") return d.newUnlock.clear(), true;
    if (msg == "add_seen_hint") return d.seenHints.insert(sArg(args, 0)), true;
    if (msg == "is_venue_type_unlocked") {
        for (const TourEvent& e : m.data.soloEvents)
            if (e.venue == sArg(args, 0) && m.soloStatus(*this, e) != 0) return r = Value::integer(1), true;
        return true;
    }
    if (msg == "is_local") return r = Value::integer(1), true;
    if (msg == "next_any_index") return r = Value::integer(-1), true;
    return false;
}

// ----------------------------------------------------------------- bands

bool RbBandObj::handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) {
    (void)in;
    r = Value::integer(0);
    RbMeta& m = meta_;
    if (msg == "band_name") return r = Value::string(d.name), true;
    if (msg == "set_band_name") return d.name = sArg(args, 0), true;
    if (msg == "motto") return r = Value::string(d.motto), true;
    if (msg == "set_motto") return d.motto = sArg(args, 0), true;
    if (msg == "hometown") return r = Value::symbol(d.hometown), true;
    if (msg == "set_hometown") return d.hometown = sArg(args, 0), true;
    if (msg == "init") return d.finalized = true, d.town = d.hometown, m.save(), true;
    if (msg == "is_finalized") return r = Value::integer(d.finalized ? 1 : 0), true;
    if (msg == "is_local") return r = Value::integer(1), true;
    if (msg == "update_members" || msg == "update_data" || msg == "set_dirty" || msg == "print_guid") return true;
    if (msg == "has_logo_patch") return true;
    if (msg == "logo_patch" || msg == "edit_logo_patch") return true;  // no band logo yet (the patch creator, GD05-022)
    if (msg == "win_difficulty" || msg == "min_difficulty") return r = Value::integer(d.minDifficulty), true;
    if (msg == "min_difficulty_sym") return r = Value::symbol(kDiffSyms[std::clamp(d.minDifficulty, 0, 3)]), true;
    if (msg == "get_band_member_for_slot") {
        return r = Value::list({Value::integer(0), Value::string(""), Value::integer(0)}), true;
    }
    if (msg == "world_fame" || msg == "total_fame_stars") return r = Value::integer(m.bandFame(*this, "")), true;
    if (msg == "fans") return r = Value::integer(d.fans), true;
    if (msg == "money") return r = Value::integer(d.money), true;
    if (msg == "cheat_add_money") return d.money += iArg(args, 0), true;
    if (msg == "buzz") return r = Value::integer(d.buzz), true;
    if (msg == "buzz_achievement") {
        const DataNode* buzz = m.data.band ? m.data.band->find("buzz") : nullptr;
        return r = Value::integer(num(buzz ? buzz->find("buzz_achievement") : nullptr, 1, 1000)), true;
    }
    if (msg == "buzz_to_fans") return r = Value::integer(iArg(args, 0) * 10), true;
    if (msg == "adjust_buzz") return d.buzz += iArg(args, 0), true;
    if (msg == "town_fame") return r = Value::integer(m.bandFame(*this, args.empty() ? d.town : sArg(args, 0))), true;
    if (msg == "total_town_fame" || msg == "total_venue_fame") {
        int n = 0;
        const std::string t = args.empty() ? d.town : sArg(args, 0);
        for (const TourEvent& e : m.data.bandEvents) n += m.townOf(e, *this) == t && (msg != "total_venue_fame" || e.venue == d.venue);
        return r = Value::integer(n * 5), true;
    }
    if (msg == "venue_fame") {
        int f = 0;
        for (const auto& [k, s] : d.stars) f += k.rfind(d.town + "/" + d.venue + "/", 0) == 0 ? s : 0;
        return r = Value::integer(f), true;
    }
    if (msg == "town") return r = Value::symbol(d.town), true;
    if (msg == "venue") return r = Value::symbol(d.venue), true;
    if (msg == "set_town") return d.town = sArg(args, 0), true;
    if (msg == "set_venue") return d.venue = sArg(args, 0), true;
    // Leaving home needs the van; another venue at home, a first gig [I].
    if (msg == "can_leave_hometown") return r = Value::integer(d.items.count("tour_vehicle_van") ? 1 : 0), true;
    if (msg == "can_leave_home_venue") return r = Value::integer(d.stars.empty() ? 0 : 1), true;
    if (msg == "venue_status") {
        for (const TourEvent& e : m.data.bandEvents)
            if (m.townOf(e, *this) == d.town && e.venue == sArg(args, 0) && m.bandStatus(*this, e) != 0) return r = Value::integer(2), true;
        return true;
    }
    if (msg == "num_new_town_events" || msg == "num_new_venue_events") {
        int n = 0;
        for (const TourEvent& e : m.data.bandEvents)
            if (m.townOf(e, *this) == d.town && (msg == "num_new_town_events" || e.venue == d.venue) && m.bandStatus(*this, e) == 2) ++n;
        return r = Value::integer(n), true;
    }
    if (msg == "set_event") {
        if (const TourEvent* e = m.bandEvent(*this, sArg(args, 0))) {
            d.event = e->name;
            m.event = m.eventObj(*e, true);
            m.setlist = m.eventSongs(*e, true, "band");
            m.songIndex = 0;
            m.lastStars = m.lastScore = 0;
            if (SDL_getenv("GD05_UILOG")) {
                std::string l;
                for (const std::string& x : m.setlist) l += " " + x;
                SDL_Log("tour: band event %s/%s/%s:%s", e->town.c_str(), e->venue.c_str(), e->name.c_str(), l.c_str());
            }
        }
        return true;
    }
    if (msg == "event_status") {
        const RbEventObj* e = args.empty() ? m.event : dynamic_cast<RbEventObj*>(args[0].object);
        if (!e && !args.empty())
            if (const TourEvent* te = m.bandEvent(*this, sArg(args, 0))) return r = Value::integer(m.bandStatus(*this, *te)), true;
        return r = Value::integer(e ? m.bandStatus(*this, e->ev) : 0), true;
    }
    if (msg == "next_any_index") {
        for (size_t i = m.songIndex; i < m.setlist.size(); ++i)
            if (m.setlist[i] == "any") return r = Value::integer(int(i)), true;
        return r = Value::integer(-1), true;
    }
    if (msg == "set_any_song") {
        for (std::string& s : m.setlist)
            if (s == "any") return s = sArg(args, 0), true;
        return true;
    }
    if (msg == "set_random_songs") return true;
    if (msg == "next_song") return r = Value::symbol(m.songIndex < m.setlist.size() ? m.setlist[m.songIndex] : std::string()), true;
    if (msg == "complete_song") {
        m.lastStars += iArg(args, 0), m.lastScore += iArg(args, 1);
        ++m.songIndex;
        return true;
    }
    if (msg == "last_song") return r = Value::integer(m.songIndex + 1 >= m.setlist.size() ? 1 : 0), true;  // the song now playing is the set list's last (endgame.dta asks before complete_song) [C data]
    if (msg == "first_song") return r = Value::integer(m.songIndex == 0 ? 1 : 0), true;
    if (msg == "num_completed") return r = Value::integer(int(m.songIndex)), true;
    if (msg == "is_winning" || msg == "is_hardcore_winning") return r = Value::integer(m.winning ? 1 : 0), true;
    if (msg == "reward") {
        const int per = m.event ? num(m.event->ev.node->find("reward_per_star"), 1, 25) : 25;
        return r = Value::integer(per * m.lastStars), true;
    }
    if (msg == "complete_event") {
        if (!m.event) return true;
        const int songs = std::max<int>(1, int(m.setlist.size()));
        const int stars = m.lastStars / songs;
        d.stars[m.event->ev.key()] = std::max(d.stars[m.event->ev.key()], m.lastStars);
        d.money += num(m.event->ev.node->find("reward_per_star"), 1, 25) * m.lastStars;
        // Fans: the venue's (buzz (venues (<venue> (performance ...)))) by stars [I: the table's axes].
        const DataNode* buzz = m.data.band ? m.data.band->find("buzz") : nullptr;
        const DataNode* venues = buzz ? buzz->find("venues") : nullptr;
        const DataNode* vn = venues ? venues->find(m.event->ev.venue) : nullptr;
        const DataNode* perf = vn ? vn->find("performance") : nullptr;
        int gain = stars * 5;
        if (perf && perf->children.size() > 1 && perf->children[1].isList()) gain = num(&perf->children[1], size_t(std::clamp(stars - 1, 0, 3)), gain);
        d.buzz += gain;
        d.fans += gain * 10;
        const std::string award = text(m.event->ev.node->find("award_item"));
        if (!award.empty() && !d.items.count(award)) d.items.insert(award), d.unlockedItem = award;
        m.save();
        return true;
    }
    if (msg == "lose_song") return d.buzz = std::max(0, d.buzz - 5), true;
    if (msg == "fans_lost_on_fail") return r = Value::integer(50), true;
    if (msg == "fans_change") return r = Value::integer(0), true;
    if (msg == "fans_update_str") return r = Value::string(std::to_string(d.fans)), true;
    if (msg == "has_item") return r = Value::integer(d.items.count(sArg(args, 0)) ? 1 : 0), true;
    if (msg == "item_to_str") return r = Value::string("*"), true;
    if (msg == "can_win_item") {
        // An open gig awards it (award_item) [I].
        if (d.items.count(sArg(args, 0))) return true;
        for (const TourEvent& e : m.data.bandEvents)
            if (text(e.node->find("award_item")) == sArg(args, 0) && m.bandStatus(*this, e) == 2) return r = Value::integer(1), true;
        return true;
    }
    if (msg == "have_lost_fans") return true;
    if (msg == "unlocked_item") return r = Value::symbol(d.unlockedItem), true;
    if (msg == "clear_unlocked_item") return d.unlockedItem.clear(), true;
    if (msg == "add_seen_hint") return d.seenHints.insert(sArg(args, 0)), true;
    if (msg == "has_next_moment") return true;
    if (msg == "buzz_state") return true;
    if (msg == "event_unlock" || msg == "unlocked_venue_token" || msg == "unlocked_venue_town") return r = Value::symbol(""), true;
    if (msg == "is_town_complete") {
        for (const TourEvent& e : m.data.bandEvents)
            if (m.townOf(e, *this) == d.town && !d.stars.count(e.key())) return true;
        return r = Value::integer(1), true;
    }
    if (msg == "num_unlocked_events" || msg == "num_unlocked_venues" || msg == "num_unlocked_towns") return true;
    if (msg == "next_item_event" || msg == "find_event_town") return true;
    return false;
}

bool RbEventObj::handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) {
    (void)in;
    r = Value::integer(0);
    if (msg == "get") {
        const std::string k = sArg(args, 0);
        if (k == "event_name") return r = Value::symbol(ev.name), true;
        if (k == "venue") return r = Value::symbol(ev.venue), true;
        if (k == "town") return r = Value::symbol(ev.town), true;
        if (k == "award_item") return r = Value::symbol(text(ev.node->find("award_item"))), true;
        if (k == "min_difficulty") return r = Value::integer(num(ev.node->find("min_difficulty"))), true;
        if (k == "warning_screen") return r = Value::symbol(""), true;
        if (k == "sellout") return true;
        if (k == "world_fame_required" || k == "town_fame_required") {
            const DataNode* fame = ev.node->find("fame");
            return r = Value::integer(num(fame ? fame->find(k == "world_fame_required" ? "world" : "town") : nullptr)), true;
        }
        if (k == "buzz_required") return r = Value::integer(num(ev.node->find("buzz"))), true;
        return true;
    }
    if (msg == "num_songs") {
        const DataNode* s = ev.node->find("songs");
        return r = Value::integer(s ? int(s->children.size()) - 1 : 1), true;
    }
    if (msg == "total_fame") return r = Value::integer(5), true;
    if (msg == "unlock_category" || msg == "unlock_outfit") {
        const DataNode* o = ev.node->find("outfit");
        const std::string inst = sArg(args, 0) == "bass" ? "guitar" : sArg(args, 0);
        const DataNode* g = o && o->find(inst) ? o->find(inst)->find(sArg(args, 1)) : nullptr;
        return r = Value::symbol(text(g, msg == "unlock_category" ? 1 : 2)), true;
    }
    (void)band_;
    (void)meta_;
    return false;
}

// ------------------------------------------------------------ the save

void RbMeta::save() {
    Json root = Json::object();
    root.set("version", Json::number(1));
    Json cs = Json::array();
    for (const RbCharObj* c : chars) {
        Json j = Json::object();
        j.set("name", Json::string(c->d.name)), j.set("gender", Json::string(c->d.gender)), j.set("instrument", Json::string(c->d.instrument));
        j.set("hometown", Json::string(c->d.hometown)), j.set("money", Json::number(c->d.money)), j.set("score", Json::number(c->d.score));
        j.set("difficulty", Json::number(c->d.difficulty)), j.set("skin", Json::number(c->d.skin));
        Json o = Json::object();
        for (const auto& [k, v] : c->d.outfit) o.set(k, Json::string(v));
        j.set("outfit", o);
        Json st = Json::object();
        for (const auto& [k, v] : c->d.stars) st.set(k, Json::number(v));
        j.set("stars", st);
        Json ow = Json::array();
        for (const std::string& s : c->d.owned) ow.a.push_back(Json::string(s));
        j.set("owned", ow);
        Json sh = Json::array();
        for (const std::string& s : c->d.seenHints) sh.a.push_back(Json::string(s));
        j.set("seen_hints", sh);
        cs.a.push_back(j);
    }
    root.set("characters", cs);
    Json bs = Json::array();
    for (const RbBandObj* b : bands) {
        if (!b->d.finalized) continue;
        Json j = Json::object();
        j.set("name", Json::string(b->d.name)), j.set("hometown", Json::string(b->d.hometown)), j.set("fans", Json::number(b->d.fans));
        j.set("money", Json::number(b->d.money)), j.set("buzz", Json::number(b->d.buzz)), j.set("town", Json::string(b->d.town));
        Json st = Json::object();
        for (const auto& [k, v] : b->d.stars) st.set(k, Json::number(v));
        j.set("stars", st);
        Json it = Json::array();
        for (const std::string& s : b->d.items) it.a.push_back(Json::string(s));
        j.set("items", it);
        Json sh = Json::array();
        for (const std::string& s : b->d.seenHints) sh.a.push_back(Json::string(s));
        j.set("seen_hints", sh);
        bs.a.push_back(j);
    }
    root.set("bands", bs);
    SDL_IOStream* io = SDL_IOFromFile(path().c_str(), "wb");
    if (!io) return;
    const std::string s = root.dump(1);
    SDL_WriteIO(io, s.data(), s.size());
    SDL_CloseIO(io);
}

void RbMeta::load() {
    size_t size = 0;
    void* raw = SDL_LoadFile(path().c_str(), &size);
    if (!raw) return;
    std::string textData(static_cast<char*>(raw), size);
    SDL_free(raw);
    Json root;
    try {
        root = Json::parse(textData);
    } catch (const std::exception& e) {
        SDL_Log("rb profile: %s", e.what());
        return;
    }
    for (const Json& j : root["characters"].a) {
        RbCharObj* c = newChar();
        c->d.name = j["name"].str(), c->d.gender = j["gender"].str(), c->d.instrument = j["instrument"].str();
        c->d.hometown = j["hometown"].str(), c->d.money = int(j["money"].num(100)), c->d.score = int(j["score"].num());
        c->d.difficulty = int(j["difficulty"].num(1)), c->d.skin = int(j["skin"].num());
        for (const auto& [k, v] : j["outfit"].o) c->d.outfit[k] = v.str();
        for (const auto& [k, v] : j["stars"].o) c->d.stars[k] = int(v.num());
        for (const Json& s : j["owned"].a) c->d.owned.insert(s.str());
        for (const Json& s : j["seen_hints"].a) c->d.seenHints.insert(s.str());
        chars.push_back(c);
    }
    for (const Json& j : root["bands"].a) {
        RbBandObj* b = newBand();
        b->d.name = j["name"].str(), b->d.hometown = j["hometown"].str(), b->d.fans = int(j["fans"].num());
        b->d.money = int(j["money"].num()), b->d.buzz = int(j["buzz"].num()), b->d.town = j["town"].str(), b->d.finalized = true;
        for (const auto& [k, v] : j["stars"].o) b->d.stars[k] = int(v.num());
        for (const Json& s : j["items"].a) b->d.items.insert(s.str());
        for (const Json& s : j["seen_hints"].a) b->d.seenHints.insert(s.str());
        bands.push_back(b);
    }
}

// ------------------------------------------------------------- the tour

class RbTourObj : public ScriptObject {
public:
    explicit RbTourObj(RbMeta& m) : ScriptObject("tour", "Tour"), m_(m) {}
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        (void)in;
        r = Value::integer(0);
        m_.ensure();
        if (msg == "performer") return r = m_.performer ? Value::obj(m_.performer) : Value::integer(0), true;
        if (msg == "set_performer") {
            m_.performer = args.empty() ? nullptr : args[0].object;
            if (RbCharObj* c = m_.soloChar(); c && c->d.town.empty() && !m_.data.soloEvents.empty())
                c->d.town = m_.data.soloEvents[0].town, c->d.venue = m_.data.soloEvents[0].venue;
            return true;
        }
        if (msg == "clear_performer") return m_.performer = nullptr, m_.event = nullptr, true;
        if (msg == "create_new_band") return r = Value::obj(m_.newBand()), true;
        if (msg == "give_new_band_to_profile") {
            if (RbBandObj* b = m_.bandPerf(); b && std::find(m_.bands.begin(), m_.bands.end(), b) == m_.bands.end()) m_.bands.push_back(b);
            return m_.save(), true;
        }
        if (msg == "bwtmode") return r = Value::integer(m_.bwtmode), true;
        if (msg == "set_bwtmode") return m_.bwtmode = iArg(args, 0), true;
        if (msg == "event") return r = m_.event ? Value::obj(m_.event) : Value::integer(0), true;
        if (msg == "event_display_name") {
            const RbEventObj* e = args.empty() ? m_.event : dynamic_cast<RbEventObj*>(args[0].object);
            return r = Value::string(e ? m_.localizeOr(e->ev.name) : std::string()), true;
        }
        if (msg == "event_description") {
            if (!m_.event) return r = Value::string(""), true;
            std::string s;
            for (const std::string& song : m_.setlist) s += (s.empty() ? "" : ", ") + (song == "any" ? std::string("?") : m_.songName(song));
            return r = Value::string(s), true;
        }
        if (msg == "event_concrete_song") return r = Value::symbol(!m_.setlist.empty() && m_.setlist[0] != "any" ? m_.setlist[0] : std::string()), true;
        if (msg == "hint") {
            m_.hint = m_.currentHint();
            if (m_.hint.empty()) return true;
            return r = Value::obj(hintObj_), true;
        }
        if (msg == "town") return r = Value::symbol(m_.soloChar() ? m_.soloChar()->d.town : m_.bandPerf() ? m_.bandPerf()->d.town : std::string()), true;
        if (msg == "venue" || msg == "venue_variant") {
            std::string v = m_.soloChar() ? m_.soloChar()->d.venue : m_.bandPerf() ? m_.bandPerf()->d.venue : std::string();
            if (msg == "venue_variant") {
                // The venue's kind -> one of its venues (config/macros.dta VENUES) [I: the original's pick].
                const std::string kind = v.empty() ? "small_club" : v;
                static const std::map<std::string, int> counts = {{"small_club", 12}, {"big_club", 12}, {"theater", 9}, {"arena", 8}};
                const int n = counts.count(kind) ? counts.at(kind) : 1;
                char b[48];
                std::snprintf(b, sizeof b, "%s_%02d", counts.count(kind) ? kind.c_str() : "small_club", 1 + std::rand() % n);
                return r = Value::symbol(b), true;
            }
            return r = Value::symbol(v), true;
        }
        if (msg == "venue_token" || msg == "venue_desc_token") return r = Value::symbol(args.size() > 1 ? sArg(args, 1) : m_.bandPerf() ? m_.bandPerf()->d.venue : ""), true;
        if (msg == "solo_event_provider") return r = Value::obj(m_.soloEvents), true;
        if (msg == "event_provider") return r = Value::obj(m_.bandEvents), true;
        if (msg == "venue_provider") return r = Value::obj(m_.venues), true;
        if (msg == "town_provider") return r = Value::obj(m_.towns), true;
        if (msg == "hometown_provider") return r = Value::obj(m_.hometowns), true;
        if (msg == "event_song_provider") return r = Value::obj(m_.eventSongList), true;
        if (msg == "requirement_provider") return r = Value::obj(m_.requirements), true;
        if (msg == "get") return r = m_.props.count(sArg(args, 0)) ? m_.props[sArg(args, 0)] : Value::integer(0), true;
        if (msg == "set") {
            if (args.size() > 1) m_.props[sArg(args, 0)] = args[1];
            if (sArg(args, 0) == "win_sequence") m_.winning = args.size() > 1 && args[1].truthy();
            return true;
        }
        if (msg == "create_test_event" || msg == "cheat_win_event") return true;
        return false;
    }
    ScriptObject* hintObj_ = nullptr;

private:
    RbMeta& m_;
};

class RbHintObj : public ScriptObject {
public:
    explicit RbHintObj(RbMeta& m) : ScriptObject("tour_hint", "TourHint"), m_(m) {}
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        const DataNode* block = m_.soloChar() ? m_.data.solo : m_.data.band;
        const DataNode* hints = block ? block->find("hints") : nullptr;
        const DataNode* h = hints ? hints->find(m_.hint) : nullptr;
        if (msg == "get_type") return r = Value::symbol(m_.hint), true;
        if (msg == "get" && sArg(args, 0) == "screen") return r = Value::symbol(text(h ? h->find("screen") : nullptr)), true;
        if (msg == "advance") {
            // ONEOFF hints: seen once; every hint's (advance ...) script.
            if (RbCharObj* c = m_.soloChar()) c->d.seenHints.insert(m_.hint);
            if (RbBandObj* b = m_.bandPerf()) b->d.seenHints.insert(m_.hint);
            if (const DataNode* a = h ? h->find("advance") : nullptr)
                for (size_t i = 1; i < a->children.size(); ++i) in.eval(a->children[i]);
            return true;
        }
        return false;
    }

private:
    RbMeta& m_;
};

namespace {
RbMeta* gMeta = nullptr;

// CharCreatorPanel (ui/charcreator.dta): the character being made, for the
// player of char_player_num. request / free load the preview [I: no
// preview drawn yet]; add_char_to_profile keeps it.
class CharCreatorPanelObj : public PanelObj {
public:
    using PanelObj::PanelObj;
    RbCharObj* creating = nullptr;
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        if (gMeta) gMeta->ensure();
        if (msg == "request") {
            if (gMeta && (!creating || std::find(gMeta->chars.begin(), gMeta->chars.end(), creating) != gMeta->chars.end())) creating = gMeta->newChar();
            if (creating) {
                if (args.size() > 1) creating->d.gender = sArg(args, 1);
                const int pn = iArg(args, 0);
                creating->d.instrument = ui.interp().send("game", "get_player_config", {Value::integer(pn)}).object
                                             ? ui.interp().send(ui.interp().send("game", "get_player_config", {Value::integer(pn)}).object, "get_controller_sym", {}).str()
                                             : "guitar";
                // The premade look of the instrument and gender (characters.dtb (creator ...)) [C data].
                if (const DataNode* boot = ui.bootTree())
                    if (const DataNode* chs = boot->find("characters"))
                        if (const DataNode* cr = chs->find("creator"))
                            if (const DataNode* g = cr->find(creating->d.gender))
                                if (const DataNode* ins = g->find(creating->d.instrument == "bass" ? "guitar" : creating->d.instrument))
                                    if (const DataNode* o = ins->find("outfit"))
                                        for (const auto& [cat, n] : entries(o)) creating->d.outfit[cat] = text(n);
            }
            return r = Value::integer(0), true;
        }
        if (msg == "free" || msg == "free_all") return r = Value::integer(0), true;
        if (msg == "get_char" || msg == "get_tour_char") {
            if (!creating && gMeta) creating = gMeta->newChar();
            return r = Value::obj(creating), true;
        }
        if (msg == "random_char_name") {
            static const char* const kNames[] = {"Sid Vicious Jr", "Ziggy", "Roxy", "Jett", "Axl", "Joan", "Nikki", "Lux", "Stiv", "Wendy"};
            return r = Value::string(kNames[std::rand() % 10]), true;
        }
        if (msg == "add_char_to_profile") {
            if (gMeta && creating && std::find(gMeta->chars.begin(), gMeta->chars.end(), creating) == gMeta->chars.end()) {
                if (creating->d.name.empty()) creating->d.name = "Rocker";
                gMeta->chars.push_back(creating);
                gMeta->save();
                gMeta->charList->refresh();
            }
            return r = Value::integer(0), true;
        }
        return PanelObj::handleNative(in, msg, args, r);
    }
};

// Panels whose own natives only steer what the port does not draw yet (the
// practice space's cameras, the world map's zoom): answered, logged once [I].
class QuietPanelObj : public PanelObj {
public:
    QuietPanelObj(UiManager& u, const std::string& name, const std::string& cls, std::set<std::string> quiet)
        : PanelObj(u, name, cls), quiet_(std::move(quiet)) {}
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        if (quiet_.count(msg)) return ui.logOnce("STUB-quiet " + className() + " " + msg), r = Value::integer(0), true;
        return PanelObj::handleNative(in, msg, args, r);
    }

private:
    std::set<std::string> quiet_;
};

}  // namespace

// The band create panel's random_band_name (tour_band.dta) [I: RB's list of names is not read].
class BandCreatePanelObj : public PanelObj {
public:
    using PanelObj::PanelObj;
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        if (msg == "random_band_name") {
            static const char* const kNames[] = {"The Glitches", "Port Authority", "The Pixel Pushers", "Dream Machine", "The Offscreens"};
            return r = Value::string(kNames[std::rand() % 5]), true;
        }
        return PanelObj::handleNative(in, msg, args, r);
    }
};

std::unique_ptr<ScriptObject> makeRbMetaClass(UiManager& ui, const std::string& cls, const std::string& name) {
    if (cls == "BandCreatePanel") return std::make_unique<BandCreatePanelObj>(ui, name, cls);
    if (cls == "DialogPanel") return std::make_unique<QuietPanelObj>(ui, name, cls, std::set<std::string>{"set_player_num"});
    if (cls == "CharCreatorPanel") return std::make_unique<CharCreatorPanelObj>(ui, name, cls);
    if (cls == "WorldMapPanel") return std::make_unique<QuietPanelObj>(ui, name, cls, std::set<std::string>{"sync"});
    if (name == "practice_space_panel")
        return std::make_unique<QuietPanelObj>(ui, name, cls, std::set<std::string>{"select_cam", "set_creator_player_num", "play_attitude_group", "set_char_player_num"});
    return nullptr;
}

void installRbMeta(UiManager& ui, Interp& in, std::vector<std::unique_ptr<ScriptObject>>& owned) {
    static std::unique_ptr<RbMeta> meta;
    meta = std::make_unique<RbMeta>(ui, in, owned);
    RbMeta& m = *meta;
    gMeta = &m;
    m.ensure = [&m, &ui] {
        if (m.loaded) return;
        const DataNode* boot = ui.bootTree();
        if (!boot) return;
        m.loaded = true;
        if (const DataNode* tour = boot->find("tour")) {
            m.data.solo = tour->find("solo"), m.data.band = tour->find("band");
            if (const DataNode* h = tour->find("default_hometown")) m.data.hometown = text(h);
            if (const DataNode* mo = tour->find("default_money")) m.data.money = num(mo);
            loadEvents(m.data.solo ? m.data.solo->find("towns") : nullptr, m.data.soloTowns, m.data.soloEvents);
            loadEvents(m.data.band ? m.data.band->find("tiers") : nullptr, m.data.bandTowns, m.data.bandEvents);
            // A tier is the world town of the same order [I].
            std::vector<std::pair<int, std::string>> world;
            for (const auto& [t, n] : entries(m.data.band ? m.data.band->find("world") : nullptr)) world.emplace_back(num(n->find("order"), 1, 99), t);
            std::stable_sort(world.begin(), world.end());
            for (size_t i = 0; i < m.data.bandTowns.size(); ++i) {
                const std::string town = i < world.size() ? world[i].second : m.data.bandTowns[i];
                m.data.tierTown[m.data.bandTowns[i]] = town;
            }
            for (TourEvent& e : m.data.bandEvents) e.town = m.data.tierTown[e.town];
            for (std::string& t : m.data.bandTowns) t = m.data.tierTown[t];
        }
        SDL_Log("rb meta: solo tour %d towns %d events, band tour %d towns %d events", int(m.data.soloTowns.size()), int(m.data.soloEvents.size()),
                int(m.data.bandTowns.size()), int(m.data.bandEvents.size()));
    };
    m.load();
    auto* tour = m.own(std::make_unique<RbTourObj>(m));
    tour->hintObj_ = m.own(std::make_unique<RbHintObj>(m));
    auto list = [&](const std::string& name) {
        RbList* l = m.own(std::make_unique<RbList>(name));
        l->before = [&m] { m.ensure(); };
        return l;
    };
    // Solo events: every event of every town in order (the list moves the town as it scrolls).
    m.soloEvents = list("solo_event_provider");
    m.soloEvents->build = [&m] {
        std::vector<std::string> v;
        for (const TourEvent& e : m.data.soloEvents) v.push_back(e.name);  // unique across the towns [C data]
        return v;
    };
    auto evOf = [&m](const std::string& sym) -> const TourEvent* { return m.findEvent(false, "", "", sym); };
    m.soloEvents->label = [&m, evOf](const std::string& sym) {
        const TourEvent* e = evOf(sym);
        if (!e) return sym;
        RbCharObj* c = m.soloChar();
        const std::vector<std::string> songs = m.eventSongs(*e, false, c ? c->d.instrument : "guitar");
        return m.localizeOr(e->town) + ": " + (songs.empty() ? m.localizeOr(e->name) : m.songName(songs[0]));
    };
    m.soloEvents->enabled = [&m, evOf](const std::string& sym) {
        const TourEvent* e = evOf(sym);
        RbCharObj* c = m.soloChar();
        return e && c && m.soloStatus(*c, *e) != 0;
    };
    m.soloEvents->extra = [&m, evOf](Interp&, const std::string& msg, std::vector<Value>& a, Value& r) {
        RbList& l = *m.soloEvents;
        if (l.data.empty()) l.refresh();
        const TourEvent* e = evOf(l.symbol(iArg(a, 0)));
        if (msg == "set" || msg == "get") return r = Value::integer(0), true;  // mode
        if (msg == "town") return r = Value::symbol(e ? e->town : ""), true;
        if (msg == "venue") return r = Value::symbol(e ? e->venue : ""), true;
        if (msg == "event") return r = Value::symbol(e ? e->name : ""), true;
        if (msg == "event_song") {
            RbCharObj* c = m.soloChar();
            const std::vector<std::string> s = e ? m.eventSongs(*e, false, c ? c->d.instrument : "guitar") : std::vector<std::string>{};
            return r = Value::symbol(s.empty() ? std::string() : s[0]), true;
        }
        if (msg == "selected_sym" || msg == "get_symbol") return r = Value::symbol(e ? e->name : ""), true;
        if (msg == "first_event" || msg == "last_event") {
            int found = -1;
            for (size_t i = 0; i < l.data.size(); ++i)
                if (const TourEvent* x = evOf(l.data[i]); x && x->town == sArg(a, 0)) {
                    if (found < 0 || msg == "last_event") found = int(i);
                }
            return r = Value::integer(found), true;
        }
        return false;
    };
    // Band: the events of the band's town and venue, its venues, the towns.
    m.bandEvents = list("tour_event_provider");
    m.bandEvents->build = [&m] {
        std::vector<std::string> v;
        if (RbBandObj* b = m.bandPerf())
            for (const TourEvent& e : m.data.bandEvents)
                if (m.townOf(e, *b) == b->d.town && (b->d.venue.empty() || e.venue == b->d.venue)) v.push_back(e.name);
        return v;
    };
    m.bandEvents->label = [&m](const std::string& s) { return m.localizeOr(s); };
    m.venues = list("tour_venue_provider");
    m.venues->build = [&m] {
        std::vector<std::string> v;
        if (RbBandObj* b = m.bandPerf())
            for (const TourEvent& e : m.data.bandEvents)
                if (m.townOf(e, *b) == b->d.town && std::find(v.begin(), v.end(), e.venue) == v.end()) v.push_back(e.venue);
        return v;
    };
    m.venues->label = [&m](const std::string& s) { return m.localizeOr(s); };
    m.towns = list("tour_town_provider");
    m.towns->build = [&m] { return m.data.bandTowns; };
    m.towns->label = [&m](const std::string& s) { return m.localizeOr(s); };
    m.towns->extra = [&m](Interp&, const std::string& msg, std::vector<Value>&, Value& r) {
        if (msg == "num_towns") return r = Value::integer(int(m.data.bandTowns.size())), true;
        return false;
    };
    m.hometowns = list("tour_hometown_provider");
    m.hometowns->build = [&m] {
        std::vector<std::string> v;
        for (const auto& [t, n] : entries(m.data.band ? m.data.band->find("world") : nullptr)) v.push_back(t);
        if (v.empty()) v.push_back(m.data.hometown);
        return v;
    };
    m.hometowns->label = [&m](const std::string& s) { return m.localizeOr(s); };
    m.hometowns->extra = [&m](Interp&, const std::string& msg, std::vector<Value>& a, Value& r) {
        RbList& l = *m.hometowns;
        if (msg == "set_selected") return l.selected = std::max(0, l.indexOf(sArg(a, 0))), true;
        if (msg == "store" || msg == "undo") return true;
        if (msg == "confirm") return r = Value::symbol(l.symbol(l.selected)), true;
        return false;
    };
    m.eventSongList = list("tour_event_song_provider");
    m.eventSongList->build = [&m] { return m.setlist; };
    m.eventSongList->label = [&m](const std::string& s) { return s == "any" ? std::string("?") : m.songName(s); };
    m.requirements = list("tour_requirement_provider");
    m.requirements->build = [] { return std::vector<std::string>{}; };
    m.requirements->extra = [](Interp&, const std::string& msg, std::vector<Value>&, Value&) { return msg == "set_by_venue" || msg == "set_by_event"; };
    // The profile's characters for the Solo Tour (get_solo_char_provider):
    // the player's characters of the instrument, then "create new".
    m.charList = list("rb_char_provider");
    m.charList->build = [&m] {
        std::vector<std::string> v;
        for (RbCharObj* c : m.chars) v.push_back(c->name());
        v.push_back("create_new");
        return v;
    };
    m.charList->label = [&m](const std::string& s) {
        if (s == "create_new") return m.localizeOr("create_new_char");
        for (RbCharObj* c : m.chars)
            if (c->name() == s) return c->d.name;
        return s;
    };
    m.charList->extra = [&m](Interp& in2, const std::string& msg, std::vector<Value>& a, Value& r) {
        RbList& l = *m.charList;
        if (l.data.empty()) l.refresh();
        if (msg == "set_instrument_filter") return l.refresh(), true;
        if (msg == "is_create_new") return r = Value::integer(l.symbol(iArg(a, 0)) == "create_new" ? 1 : 0), true;
        if (msg == "is_non_prefab_char") return r = Value::integer(l.symbol(iArg(a, 0)) != "create_new" ? 1 : 0), true;
        if (msg == "get_char") {
            ScriptObject* o = in2.findObject(l.symbol(iArg(a, 0)));
            return r = o ? Value::obj(o) : Value::integer(0), true;
        }
        if (msg == "get_pos_from_char") return r = Value::integer(a.empty() || !a[0].object ? -1 : l.indexOf(a[0].object->name())), true;
        return false;
    };
    in.addBuiltin("get_solo_char_provider", [&m](Interp&, const DataNode&) { return Value::obj(m.charList); });
    in.addBuiltin("get_char_provider", [&m](Interp&, const DataNode&) { return Value::obj(m.charList); });
    m.bandList = list("rb_band_provider");
    m.bandList->build = [&m] {
        std::vector<std::string> v;
        for (RbBandObj* b : m.bands) v.push_back(b->name());
        v.push_back("create_new");
        return v;
    };
    m.bandList->label = [&m](const std::string& s) {
        if (s == "create_new") return m.localizeOr("create_new_band");
        for (RbBandObj* b : m.bands)
            if (b->name() == s) return b->d.name;
        return s;
    };
    m.bandList->extra = [&m](Interp& in2, const std::string& msg, std::vector<Value>& a, Value& r) {
        RbList& l = *m.bandList;
        if (l.data.empty()) l.refresh();
        if (msg == "is_create_new" || msg == "is_pos_create_band") return r = Value::integer(l.symbol(iArg(a, 0)) == "create_new" ? 1 : 0), true;
        if (msg == "is_pos_join_band") return r = Value::integer(0), true;  // joining a remote band: online, not in the port
        if (msg == "get_band") {
            ScriptObject* o = in2.findObject(l.symbol(iArg(a, 0)));
            return r = o ? Value::obj(o) : Value::integer(0), true;
        }
        if (msg == "is_local" || msg == "is_band_local") return r = Value::integer(1), true;
        return false;
    };
    // The Xbox autosave (ui/saveload_xbox.dta xbox_autosave_start <type>
    // <next screen>): the port saves rb_profile.json at once and moves on as
    // the save would have (kMetaNextType*: PushPop 0 stays, PushPopGoto 1,
    // GotoGoto 3, GotoPopGoto 4 go to the next screen, GotoPop 2 pops)
    // [C data: the types; I: no save screens] (DECISIONS D4).
    in.addBuiltin("xbox_autosave_start", [&m, &ui](Interp& in2, const DataNode& cmd) {
        const int type = cmd.children.size() > 1 ? in2.eval(cmd.children[1]).integerValue() : 0;
        Value next = cmd.children.size() > 2 ? in2.eval(cmd.children[2]) : Value::integer(0);
        const std::string screen = next.type == Value::Type::Object && next.object ? next.object->name() : next.str();
        m.save();
        if (SDL_getenv("GD05_UILOG")) SDL_Log("ui: autosave type %d -> '%s'", type, screen.c_str());
        if ((type == 1 || type == 3 || type == 4) && !screen.empty()) ui.gotoScreen(screen);
        else if (type == 2) ui.popScreen("");
        return Value::integer(0);
    });
    // The band lobby (matchmaking_band_screen), a stand-in [I] (DECISIONS
    // D10): RB's lobby has each player join in a slot with a button, pick a
    // character and a leader; the port joins every local player who has a
    // controller (GD05_BAND_PLAYERS=<n> for the tests) with the premade
    // character of their instrument, player 1 leading, and goes on to the
    // mode's post_matchmaking_screen as the lobby's continue does.
    ui.redirect = [&m, &ui](const std::string& name, bool back) -> std::string {
        const bool hth = name == "matchmaking_hth_screen";
        if ((name != "matchmaking_band_screen" && !hth) || back || SDL_getenv("GD05_REAL_LOBBY")) return {};
        Session& s = ui.session();
        int want = 0;
        if (const char* e = SDL_getenv("GD05_BAND_PLAYERS")) want = std::clamp(std::atoi(e), 1, 4);
        if (hth) want = 2;  // head to head: two players [C data: modes.dta HTH max_players 2]
        for (int p = 0; p < 4; ++p) {
            Session::PlayerCfg& pc = s.players[size_t(p)];
            const bool join = p == 0 || (want ? p < want : ui.host().padConnected(p));
            pc.participating = join;
            if (!join) continue;
            pc.controllerType = ui.host().controllerType(p);
            const std::string inst = pc.controllerType == 0 ? "drum" : pc.controllerType == 2 ? "vocals" : "guitar";
            pc.character = "prefab_" + inst;
            // Two guitar players: the second plays the bass [I: RB asks in seldiff].
            pc.trackType = pc.controllerType == 0 ? 0 : pc.controllerType == 2 ? 3 : 2;
            if (pc.trackType == 2 && !hth)
                for (int q = 0; q < p; ++q)
                    if (s.players[size_t(q)].participating && s.players[size_t(q)].trackType == 2) pc.trackType = 1;
        }
        m.in().send("gamecfg", "set", {Value::symbol("leader_num"), Value::integer(0)}, false);
        if (m.performer) {
            if (RbBandObj* b = m.bandPerf(); b && b->d.hometown.empty()) b->d.hometown = m.data.hometown;
        }
        if (ScriptObject* hub = m.in().findObject("tour_band_hub_panel")) hub->props["reset"] = Value::integer(1);
        Value next = gameCfgValue(ui, "post_matchmaking_screen");
        std::string to = next.type == Value::Type::Object && next.object ? next.object->name() : next.str();
        return to.empty() ? std::string("qp_selsong_screen") : to;
    };
    in.addBuiltin("get_band_provider", [&m](Interp&, const DataNode&) { return Value::obj(m.bandList); });
    // The creator's new character (charcreator_panel get_tour_char) and the
    // Solo Tour's stand-in when no character was made: a profile character
    // from the instrument's first premade look [I].
    in.addBuiltin("rb_new_char", [&m](Interp& in2, const DataNode& cmd) {
        RbCharObj* c = m.newChar();
        if (cmd.children.size() > 1) c->d.instrument = in2.eval(cmd.children[1]).str();
        return Value::obj(c);
    });
    in.addBuiltin("rb_add_char", [&m](Interp& in2, const DataNode& cmd) {
        if (cmd.children.size() > 1)
            if (auto* c = dynamic_cast<RbCharObj*>(in2.eval(cmd.children[1]).object))
                if (std::find(m.chars.begin(), m.chars.end(), c) == m.chars.end()) m.chars.push_back(c), m.save(), m.charList->refresh();
        return Value::integer(0);
    });
}

}  // namespace ui
