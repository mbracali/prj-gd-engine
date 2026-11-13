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
// [C] the tours' rules read in the XEX (DECISIONS D75): the Solo Tour's
// TourCharLocal (songs from SongMgr's ranked lists, open events and towns,
// bonus unlocks), the Band World Tour's buzz and fans.
// [I] what the port still decides (D67, D75): a tier is the town of the
// same order in (world ...) (RB's 0x823FCED8 not re-read); fame is the
// stars earned; a band event is unlocked when its (fame (town N) (world M))
// and buzz are met; the band towns' own events (tour/<town>.dta) are not
// loaded.
#include "ui/rb_meta.h"
#include "ui/rb_char_preview.h"
#include "ui/rb_multifocus.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>

#include "data/dta.h"
#include "data/json.h"
#include "char/char_objects.h"
#include "scene/milo.h"
#include "scene/world_objects.h"
#include "render/ui_view.h"
#include "game/camera_director.h"
#include "char/character.h"
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
// A (key TRUE|FALSE|n) flag: a number's non-zero, TRUE; `def` without one.
bool flag(const DataNode* n, bool def) {
    if (!n || n->children.size() < 2) return def;
    const DataNode& c = n->children[1];
    if (c.type == DataNode::Type::Int) return c.i != 0;
    if (c.type == DataNode::Type::Float) return c.f != 0;
    return c.isText() ? c.s != "FALSE" : def;
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

// A venue of a solo town with its events, and its kind (TourVenue: kTourVenueNormal 0,
// kTourVenueUnlock 1 when its events play (unlock N), kTourVenueDownload 2:
// RB 0x823FDB20) [C].
struct SoloVenue {
    std::string town, venue;
    int type = 0;
    std::vector<size_t> events;  // indices into TourData::soloEvents
};

struct TourData {
    std::vector<std::string> soloTowns, bandTowns;
    std::vector<SoloVenue> soloVenues;  // by town, in the data's order
    int initialUnlocks = 1;             // tour_solo.dta num_initial_unlock_songs (RB 0x823DC120) [C]
    int maxSong[4] = {45, 45, 45, 45};  // tour_solo.dta max_song per difficulty (RB 0x823DBED0) [C]
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
    std::string name, gender = "male", instrument = "guitar", hometown = "boston";
    int attitude = 256;  // kGenreRocker (config/macros.dta kGenre*: the scripts compare the values)
    std::map<std::string, std::string> outfit;  // category -> piece
    std::set<std::string> owned;                // pieces bought or won
    float weight = 0.5f, height = 0.5f;
    int skin = 0, eye = 0, eyebrow = 0;
    int money = 100;
    // Solo Tour.
    int difficulty = 1;
    std::map<std::string, int> stars;  // "<difficulty>:<event key>" -> stars
    std::set<std::string> seenHints, seenTowns;
    // Per difficulty (RB's 0x3c-byte records at TourCharLocal +0xd4): the
    // bonus songs unlocked (+0x2c, from num_initial_unlock_songs: RB
    // 0x823E54E8; one more for each event first completed whose song is a
    // bonus song: RB 0x823E1178) and the unlocks already announced (+0x30).
    int unlocks[4] = {-1, -1, -1, -1};  // -1: the data's initial count
    std::set<std::string> seenUnlocks[4];
    std::string town, venue, event, newTown;
    int score = 0;
};

struct BandData {
    std::string name, hometown = "boston", motto;
    bool finalized = false;
    int fans = 0, money = 0, buzz = 0;
    std::map<std::string, int> stars;  // event key -> best stars (fame)
    std::set<std::string> items, seenHints;
    // The songs the band has played (RB's band record at +0x18c, RB
    // 0x82409D00 / 0x82409F18): quick play offers them (RB 0x823D43D0) [C].
    std::set<std::string> played;
    std::string town, venue, event, unlockedItem;
    int minDifficulty = 3;
    // The members' characters (their names, unique in the profile:
    // profile_mgr is_char_name_unique): the band's money is theirs.
    std::vector<std::string> members;
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
    int lastBuzzChange = 0, lastFansChange = 0;  // the last gig's
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
    // The stars of each song of the event played so far (RB's vector at
    // 0x8298F7D0: complete_song RB 0x823D0640 appends, num_completed is its
    // size) [C].
    std::vector<int> songStars;
    // The event's reward (TourPerformer reward, RB 0x823EEAE8) [C]: the
    // event's reward_per_star times each song's stars, summed.
    int reward() const {
        const int per = event && event->ev.node ? num(event->ev.node->find("reward_per_star")) : 0;
        int sum = 0;
        for (int st : songStars) sum += per * st;
        return sum;
    }
    // The money a character is paid, up to config/tour_solo.dta's max_money [C data].
    void pay(RbCharObj& c, int amount);
    // The session's local players' characters (game get_player_config <p>
    // get_char), the band's members from now on.
    std::vector<RbCharObj*> localMembers(RbBandObj* band = nullptr);
    RbCharObj* charNamed(const std::string& name) {
        for (RbCharObj* c : chars)
            if (c->d.name == name) return c;
        return nullptr;
    }
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

    // SongMgr's ranked lists (RB 0x82434020) [C], cached per instrument.
    std::map<std::string, std::vector<std::string>> rankedCache[2];
    const std::vector<std::string>& ranked(const std::string& inst, bool unlockable) {
        auto& c = rankedCache[unlockable ? 1 : 0];
        auto it = c.find(inst);
        if (it == c.end()) it = c.emplace(inst, rankedSongs(ui_.host().songs(), inst, unlockable)).first;
        return it->second;
    }
    // One entry of an event's (songs ...) as RB resolves it (RB 0x823FE820)
    // [C]: N (or (leader N)) the Nth song of the performer's instrument's
    // ranked list, (unlock N) of its bonus songs, (<instrument> N) of that
    // instrument's (band: the band's rank); N < 0 counts from the end; out
    // of range clamps to the list. A song's name stands for itself; random
    // and any are left to the caller. "" when the entry is none of these.
    std::string resolveSong(const DataNode& s, const std::string& inst) {
        int n = 0;
        std::string listInst = inst;
        bool unlock = false;
        if (s.type == DataNode::Type::Int) {
            n = s.i;
        } else if (s.isList() && s.children.size() > 1) {
            const std::string head = text(&s, 0);
            n = num(&s, 1);
            unlock = head == "unlock";
            if (head != "leader" && head != "unlock") listInst = head;
        } else {
            return s.isText() ? s.s : std::string();
        }
        const std::vector<std::string>& l = ranked(listInst, unlock);
        if (l.empty()) return {};
        long idx = n < 0 ? long(l.size()) + n : long(n) - 1;
        idx = std::clamp<long>(idx, 0, long(l.size()) - 1);
        return l[size_t(idx)];
    }
    // An event's (songs ...) list for the performer's instrument.
    std::vector<std::string> eventSongs(const TourEvent& e, bool band, const std::string& inst) {
        std::vector<std::string> out;
        const DataNode* songs = e.node ? e.node->find("songs") : nullptr;
        if (!songs) return out;
        const std::string i = band ? "band" : inst;
        for (size_t k = 1; k < songs->children.size(); ++k) {
            const DataNode& s = songs->children[k];
            if (s.isText() && s.s == "random") {
                // tour_random_song: a song of the performer's ranked list [I: RB's pick].
                const std::vector<std::string>& l = ranked(i, false);
                if (!l.empty()) out.push_back(l[size_t(std::rand()) % l.size()]);
            } else {
                const std::string id = resolveSong(s, i);
                if (!id.empty()) out.push_back(id);
            }
        }
        return out;
    }
    // The first (songs ...) entry's number and whether it is (unlock N):
    // what the solo tour checks an event against (RB 0x823E46B0) [C].
    static int firstSongNumber(const TourEvent& e, bool& unlock, bool& symbol) {
        unlock = symbol = false;
        const DataNode* songs = e.node ? e.node->find("songs") : nullptr;
        if (!songs || songs->children.size() < 2) return 0;
        const DataNode& s = songs->children[1];
        if (s.type == DataNode::Type::Int) return s.i;
        if (s.isList()) return unlock = text(&s, 0) == "unlock", num(&s, 1);
        symbol = true;
        return 0;
    }
    // The bonus songs unlocked at a difficulty, and the most of the
    // difficulties from `from` up (RB 0x823DE708) [C].
    int unlocksAt(const CharData& c, int d) const { return c.unlocks[d] < 0 ? data.initialUnlocks : c.unlocks[d]; }
    int unlockProgress(const CharData& c, int from) const {
        int most = 0;
        for (int d = std::clamp(from, 0, 3); d < 4; ++d) most = std::max(most, unlocksAt(c, d));
        return most;
    }
    bool soloCompleted(const CharData& c, int diff, const TourEvent& e) const { return c.stars.count(std::to_string(diff) + ":" + e.key()) != 0; }
    // A venue's open events at the character's difficulty (RB 0x823E46B0)
    // [C]: (songs N) up to max_song of the difficulty, (unlock N) up to the
    // bonus songs unlocked, a named song only when downloaded (none).
    std::vector<size_t> soloOpenEvents(const CharData& c, int diff, const SoloVenue& v) const {
        std::vector<size_t> out;
        for (size_t i : v.events) {
            bool unlock = false, symbol = false;
            const int n = firstSongNumber(data.soloEvents[i], unlock, symbol);
            if (symbol) continue;
            if (unlock ? n <= unlockProgress(c, diff) : n <= data.maxSong[std::clamp(diff, 0, 3)]) out.push_back(i);
        }
        return out;
    }
    // The towns open at a difficulty (TourCharLocal vfunc +0x48, RB
    // 0x823E5BB8) [C]: in the data's order, a town with a normal venue is
    // open while the venues before it had every open event completed at
    // that difficulty or a harder one (RB 0x823DE560: the difficulty to
    // beat rises to the one they were completed at); unlock / download
    // venues are open whenever they have open events.
    std::vector<std::string> soloOpenTowns(const CharData& c, int diff) const {
        std::vector<std::string> out;
        int level = diff;
        for (const SoloVenue& v : data.soloVenues) {
            const std::vector<size_t> open = soloOpenEvents(c, diff, v);
            if (open.empty()) continue;
            if (v.type != 0) {
                out.push_back(v.town);
                continue;
            }
            if (level >= 4) continue;
            out.push_back(v.town);
            for (; level < 4; ++level) {
                bool all = true;
                for (size_t i : open) all = all && soloCompleted(c, level, data.soloEvents[i]);
                if (all) break;
            }
        }
        return out;
    }
    // Solo event_status (RB 0x823DBF80) [C]: kTourPlayed (1) once completed
    // at the character's difficulty, else kTourNew (2); the port answers
    // kTourLocked (0) for an event RB's lists leave out (its town not open
    // or the event not open, RB 0x823E5BB8 / 0x823E46B0).
    int soloStatus(const RbCharObj& c, const TourEvent& e);
    std::string newUnlock(const CharData& c);
    // Quick play's progress of a character (RB 0x823E0458) [C]: for the
    // ranked songs, the most over the difficulties of the song number of the
    // last event of the first venue of the last open town whose first venue
    // is normal; for the bonus songs, the bonus songs unlocked at any
    // difficulty.
    int soloQuickplayProgress(const CharData& c, bool bonus) const {
        if (bonus) return unlockProgress(c, 0);
        int most = 0;
        for (int d = 0; d < 4; ++d) {
            const std::vector<std::string> towns = soloOpenTowns(c, d);
            for (auto t = towns.rbegin(); t != towns.rend(); ++t) {
                const SoloVenue* first = nullptr;
                for (const SoloVenue& v : data.soloVenues)
                    if (v.town == *t && !v.events.empty()) {
                        first = &v;
                        break;
                    }
                if (!first || first->type != 0) continue;
                bool unlock = false, symbol = false;
                most = std::max(most, firstSongNumber(data.soloEvents[first->events.back()], unlock, symbol));
                break;
            }
        }
        return most;
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

    // ---- the Band World Tour's buzz and fans (config/tour_band.dta (buzz ...)).
    const DataNode* buzzBlock() const { return data.band ? data.band->find("buzz") : nullptr; }
    // Fans for a buzz (buzz_to_fans, RB 0x823EE350) [C]: the line through
    // the (fans (<buzz> <fans>) ...) points around the buzz, from (0 0).
    int buzzToFans(int buzz) const {
        const DataNode* fans = buzzBlock() ? buzzBlock()->find("fans") : nullptr;
        double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        for (size_t i = 1; fans && i < fans->children.size(); ++i) {
            const DataNode& p = fans->children[i];
            if (!p.isList() || p.children.size() < 2) continue;
            x1 = p.children[0].type == DataNode::Type::Float ? p.children[0].f : p.children[0].i;
            y1 = p.children[1].type == DataNode::Type::Float ? p.children[1].f : p.children[1].i;
            if (buzz <= x1) break;
            x0 = x1, y0 = y1;
        }
        if (x1 == x0) return int(y0);
        const double slope = (y1 - y0) / (x1 - x0);
        return int(float(buzz) * float(slope) + float(y0 - slope * x0));
    }
    // The buzz a gig earns (RB 0x823EF7D0) [C]: the venue's performance
    // table, row (the event's stars before, or new_performance for a first
    // gig) - 1, column max(stars, 2) - 2, both clamped to the table.
    // [I] the row: RB reads the band's record of the event (+0x14), taken
    // here as its best average stars.
    int buzzChange(const std::string& venue, int before, int stars) {
        const DataNode* b = buzzBlock();
        int row = before;
        if (row < 0) {
            row = 2;
            if (const DataNode* np = b ? b->find("new_performance") : nullptr; np && np->children.size() > 1) {
                try {
                    row = in_.eval(np->children[1]).integerValue();
                } catch (const std::exception&) {
                }
            }
        }
        const DataNode* venues = b ? b->find("venues") : nullptr;
        const DataNode* vn = venues ? venues->find(venue) : nullptr;
        const DataNode* perf = vn ? vn->find("performance") : nullptr;
        if (!perf || perf->children.size() < 2) return 0;
        const size_t rows = perf->children.size() - 1;
        const size_t r = size_t(std::clamp<long>(long(row) - 1, 0, long(rows) - 1));
        const DataNode& line = perf->children[1 + r];
        if (!line.isList() || line.children.empty()) return 0;
        const size_t c = size_t(std::clamp<long>(long(std::max(stars, 2)) - 2, 0, long(line.children.size()) - 1));
        return line.children[c].type == DataNode::Type::Int ? line.children[c].i : int(line.children[c].f);
    }
    int losePenalty(const std::string& venue) const {
        const DataNode* venues = buzzBlock() ? buzzBlock()->find("venues") : nullptr;
        const DataNode* vn = venues ? venues->find(venue) : nullptr;
        return num(vn ? vn->find("lose_penalty") : nullptr);
    }
    // The buzz, 0 up to the band's cap (buzz_caps: none, van, bus, jet
    // [I: the order of the caps]); the fans follow it.
    void setBuzz(RbBandObj& b, int buzz);

    // A creator palette as a list of its colours (skin.lst / color.lst set_colors).
    std::map<std::string, RbList*> palettes;
    RbList* palette(const std::string& pal) {
        if (auto it = palettes.find(pal); it != palettes.end()) return it->second;
        int n = 0;
        try {
            auto milo = scene::loadMilo(ui_.data(), "char/gen/skin.milo_xbox");
            if (auto* p = milo->root().find<scene::ColorPalette>(pal)) n = int(p->colors.size());
        } catch (const std::exception& e) {
            SDL_Log("rb meta: skin.milo: %s", e.what());
        }
        RbList* l = own(std::make_unique<RbList>("rb_palette_" + pal));
        l->build = [n] {
            std::vector<std::string> v;
            for (int i = 0; i < n; ++i) v.push_back(std::to_string(i));
            return v;
        };
        l->label = [](const std::string&) { return std::string(); };
        l->refresh();
        return palettes[pal] = l;
    }

    // ---- the save (rb_profile.json, version 1)
    std::string path() const { return prefDir() + "rb_profile.json"; }
    void save();
    void load();

    // The hint: the first (hints ...) entry whose cond holds and that the
    // performer has not seen (ONEOFF hints are added by add_seen_hint) [I].
    std::string hint;
    bool advancingHint = false;
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

const DataNode* outfitCategory(UiManager& ui, const std::string& gender, const std::string& cat);
namespace {
// An instrument model's finishes: the category's (outfits ...) whose names
// contain the model's (has_group_selection, RB 0x823E0700: strstr) [C].
std::vector<std::string> modelFinishes(UiManager& ui, const std::string& gender, const std::string& cat, const std::string& model) {
    std::vector<std::string> v;
    const DataNode* c = model.empty() ? nullptr : outfitCategory(ui, gender, cat);
    for (const auto& [o, n] : entries(c ? c->find("outfits") : nullptr))
        if (o.find(model) != std::string::npos) v.push_back(o);
    return v;
}
// The finish chosen in group_select_panel for a model, until the outfit
// panel wears it (confirm_outfit).
std::map<std::string, std::string> gChosenFinish;
}  // namespace

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
    if (msg == "get_attitude") return r = Value::integer(d.attitude), true;
    // The creator's setters take the CompositeCharacter being shown first
    // ({$tour_char set_skin_color $char {skin.lst selected_pos}},
    // charcreator.dta) [C data]: the value is the last argument.
    const size_t last = args.empty() ? 0 : args.size() - 1;
    if (msg == "set_attitude") return d.attitude = iArg(args, last, d.attitude), true;
    if (msg == "money") return r = Value::integer(d.money), true;
    if (msg == "cheat_add_money") return d.money += iArg(args, 0), true;
    if (msg == "set_money") return d.money = iArg(args, 0, d.money), m.save(), true;  // the shop pays (shop.dta) [C data]
    if (msg == "tour_score") return r = Value::integer(d.score), true;
    if (msg == "hometown") return r = Value::symbol(d.hometown), true;
    if (msg == "set_hometown") return d.hometown = sArg(args, 0), true;
    if (msg == "set_weight") return d.weight = args.empty() ? d.weight : args[last].number(), true;
    if (msg == "set_height") return d.height = args.empty() ? d.height : args[last].number(), true;
    if (msg == "set_skin_color") return d.skin = iArg(args, last), true;
    if (msg == "set_eye_color") return d.eye = iArg(args, last), true;
    if (msg == "set_eyebrow_color") return d.eyebrow = iArg(args, last), true;
    if (msg == "skin_color") return r = Value::integer(d.skin), true;
    // The character's properties the creator reads ({$char get weight},
    // skin_color_palette / _index, eye_color_palette / _index: band_objects.dta
    // Character) [C data]; the palettes are char/gen/skin.milo's skin.pal
    // and eyes.pal (the band's skin, game/rb_band.cpp).
    if (msg == "get") {
        const std::string k = sArg(args, 0);
        if (k == "weight") return r = Value::real(d.weight), true;
        if (k == "height") return r = Value::real(d.height), true;
        if (k == "skin_color_index") return r = Value::integer(d.skin), true;
        if (k == "eye_color_index") return r = Value::integer(d.eye), true;
        if (k == "skin_color_palette") return r = Value::obj(m.palette("skin.pal")), true;
        if (k == "eye_color_palette") return r = Value::obj(m.palette("eyes.pal")), true;
        return false;
    }
    if (msg == "deform" || msg == "update" || msg == "print_guid" || msg == "set_dirty") return true;
    if (msg == "has_outfit") {
        // Owned, or worn: the piece itself, or an instrument model whose
        // finish (<model>_<finish>, char/instruments.dta) is worn [I].
        const std::string cat = sArg(args, 0), o = sArg(args, 1);
        const std::string worn = d.outfit.count(cat) ? d.outfit[cat] : std::string();
        return r = Value::integer(d.owned.count(o) || worn == o || (!o.empty() && worn.rfind(o + "_", 0) == 0) ? 1 : 0), true;
    }
    // More than one finish of the model in the category (RB 0x823E0700) [C].
    if (msg == "has_group_selection") return r = Value::integer(modelFinishes(m.ui(), d.gender, sArg(args, 0), sArg(args, 1)).size() > 1 ? 1 : 0), true;
    if (msg == "outfit_list") {
        std::vector<Value> v;
        for (const auto& [k, o] : d.outfit) v.push_back(Value::list({Value::symbol(k), Value::symbol(o)}));
        return r = Value::list(std::move(v)), true;
    }
    if (msg == "get_outfit" || msg == "selected_outfit") return r = Value::symbol(d.outfit.count(sArg(args, 0)) ? d.outfit[sArg(args, 0)] : "none"), true;
    // select_outfit <char> <category> <outfit>: the shown character's piece
    // (shop.dta's instrument preview) [I: no character drawn in the menus yet].
    if (msg == "select_outfit") return true;
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
            m.songStars.clear();
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
        m.songStars.clear();
        return true;
    }
    if (msg == "next_song") {
        if (m.setlist.empty() && m.event) m.setlist = m.eventSongs(m.event->ev, false, d.instrument);
        return r = Value::symbol(m.songIndex < m.setlist.size() ? m.setlist[m.songIndex] : std::string()), true;
    }
    if (msg == "complete_song") {
        m.lastStars = iArg(args, 0), m.lastScore = iArg(args, 1);
        m.songStars.push_back(m.lastStars);
        d.score += m.lastScore;
        ++m.songIndex;
        return true;
    }
    if (msg == "last_song") return r = Value::integer(m.songIndex + 1 >= m.setlist.size() ? 1 : 0), true;  // the song now playing is the set list's last (endgame.dta asks before complete_song) [C data]
    if (msg == "first_song") return r = Value::integer(m.songIndex == 0 ? 1 : 0), true;
    if (msg == "num_completed") return r = Value::integer(int(m.songIndex)), true;
    if (msg == "is_winning") return r = Value::integer(m.winning ? 1 : 0), true;
    if (msg == "reward") return r = Value::integer(m.reward()), true;
    if (msg == "complete_event") {
        // TourCharLocal complete_event (RB 0x823E1178) [C]: the first time
        // at the difficulty, an event whose song is a bonus song unlocks the
        // next bonus song; the event's outfit; paid the reward.
        if (!m.event) return true;
        const std::string key = std::to_string(d.difficulty) + ":" + m.event->ev.key();
        const bool first = !d.stars.count(key);
        const int dd = std::clamp(d.difficulty, 0, 3);
        const std::vector<std::string> towns0 = m.soloOpenTowns(d, dd);
        if (first) {
            const std::vector<std::string> songs = m.eventSongs(m.event->ev, false, d.instrument);
            const SongInfo* si = nullptr;
            for (const SongInfo& s : m.ui().host().songs())
                if (!songs.empty() && s.id == songs[0]) si = &s;
            if (si && si->unlockable) d.unlocks[dd] = m.unlocksAt(d, dd) + 1;
        }
        d.stars[key] = std::max(d.stars[key], m.lastStars);
        m.pay(*this, m.reward());
        // The event's outfit for the instrument and gender (the reward).
        if (first)
            if (const DataNode* o = m.event->ev.node->find("outfit"))
                if (const DataNode* inst = o->find(d.instrument == "bass" ? "guitar" : d.instrument))
                    if (const DataNode* g = inst->find(d.gender)) d.owned.insert(text(g, 2));
        // A new town: one the event opened (RB 0x823E5BB8's list grew) and
        // not seen yet [I: RB's new_town is not read].
        d.newTown.clear();
        for (const std::string& t : m.soloOpenTowns(d, dd))
            if (std::find(towns0.begin(), towns0.end(), t) == towns0.end() && !d.seenTowns.count(t)) {
                d.newTown = t;
                break;
            }
        m.save();
        return true;
    }
    if (msg == "new_town") return r = Value::symbol(d.newTown), true;
    if (msg == "add_seen_town") return d.seenTowns.insert(sArg(args, 0)), d.newTown.clear(), true;
    if (msg == "unlock_town") return r = Value::symbol(d.newTown), true;
    if (msg == "new_unlock") return r = Value::symbol(m.newUnlock(d)), true;
    if (msg == "advance_new_unlock") {
        // The announced unlock is seen (RB 0x823E6AF0's record +0x30) [I: the record written].
        const std::string u = m.newUnlock(d);
        if (!u.empty()) d.seenUnlocks[std::clamp(d.difficulty, 0, 3)].insert(u);
        return m.save(), true;
    }
    if (msg == "add_seen_hint") return d.seenHints.insert(sArg(args, 0)), true;
    if (msg == "is_venue_type_unlocked") {
        // An open town has a normal venue of that kind (RB 0x823E0010) [C].
        const int dd = std::clamp(d.difficulty, 0, 3);
        for (const std::string& t : m.soloOpenTowns(d, dd))
            for (const SoloVenue& v : m.data.soloVenues)
                if (v.town == t && v.type == 0 && !v.events.empty() && v.venue == sArg(args, 0)) return r = Value::integer(1), true;
        return true;
    }
    if (msg == "is_local") return r = Value::integer(1), true;
    if (msg == "next_any_index") return r = Value::integer(-1), true;
    return false;
}

int RbMeta::soloStatus(const RbCharObj& c, const TourEvent& e) {
    const int dd = std::clamp(c.d.difficulty, 0, 3);
    const std::vector<std::string> towns = soloOpenTowns(c.d, dd);
    if (std::find(towns.begin(), towns.end(), e.town) == towns.end()) return 0;
    for (const SoloVenue& v : data.soloVenues) {
        if (v.town != e.town || v.venue != e.venue) continue;
        for (size_t i : soloOpenEvents(c.d, dd, v))
            if (data.soloEvents[i].key() == e.key()) return soloCompleted(c.d, dd, e) ? 1 : 2;
    }
    return 0;
}

std::string RbMeta::newUnlock(const CharData& c) {
    // new_unlock (RB 0x823DE7F8) [C]: with more bonus songs unlocked than
    // num_initial_unlock_songs, the newest one of the instrument's bonus
    // list (index unlocked - initial), unless already announced at the
    // difficulty or a harder one (RB 0x823DE770).
    const int dd = std::clamp(c.difficulty, 0, 3);
    const int have = unlockProgress(c, dd);
    if (have == data.initialUnlocks) return {};
    const std::vector<std::string>& l = ranked(c.instrument, true);
    const long k = long(have) - data.initialUnlocks;
    if (k < 0 || k >= long(l.size())) return {};
    for (int d = dd; d < 4; ++d)
        if (c.seenUnlocks[d].count(l[size_t(k)])) return {};
    return l[size_t(k)];
}

void RbMeta::pay(RbCharObj& c, int amount) {
    const DataNode* most = data.solo ? data.solo->find("max_money") : nullptr;
    c.d.money = int(std::min<long long>(most ? num(most) : 999999999, (long long)c.d.money + amount));
}

void RbMeta::setBuzz(RbBandObj& b, int buzz) {
    const DataNode* caps = buzzBlock() ? buzzBlock()->find("buzz_caps") : nullptr;
    const int k = b.d.items.count("tour_vehicle_jet") ? 4 : b.d.items.count("tour_vehicle_bus") ? 3 : b.d.items.count("tour_vehicle_van") ? 2 : 1;
    const int cap = num(caps, size_t(k), 999999999);
    b.d.buzz = std::clamp(buzz, 0, cap);
    b.d.fans = buzzToFans(b.d.buzz);
}

std::vector<RbCharObj*> RbMeta::localMembers(RbBandObj* band) {
    std::vector<RbCharObj*> v;
    RbBandObj* b = band ? band : bandPerf();
    for (int p = 0; p < 4; ++p) {
        if (!ui_.session().players[size_t(p)].participating) continue;
        const Value pc = in_.send("game", "get_player_config", {Value::integer(p)});
        auto* c = pc.object ? dynamic_cast<RbCharObj*>(in_.send(pc.object, "get_char", {}).object) : nullptr;
        if (!c || std::find(v.begin(), v.end(), c) != v.end()) continue;
        v.push_back(c);
        if (b && std::find(b->d.members.begin(), b->d.members.end(), c->d.name) == b->d.members.end()) b->d.members.push_back(c->d.name);
    }
    return v;
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
    if (msg == "init") return d.finalized = true, d.town = d.hometown, m.localMembers(this), m.save(), true;
    if (msg == "is_finalized") return r = Value::integer(d.finalized ? 1 : 0), true;
    if (msg == "is_local") return r = Value::integer(1), true;
    if (msg == "update_members" || msg == "update_data" || msg == "set_dirty" || msg == "print_guid") return true;
    if (msg == "has_logo_patch") return true;
    if (msg == "logo_patch" || msg == "edit_logo_patch") return true;  // no band logo yet (the patch creator, GD05-022)
    if (msg == "win_difficulty" || msg == "min_difficulty") return r = Value::integer(d.minDifficulty), true;
    if (msg == "min_difficulty_sym") return r = Value::symbol(kDiffSyms[std::clamp(d.minDifficulty, 0, 3)]), true;
    // The band's leader's character (its first member [I]): session_mgr update_band_leader.
    if (msg == "leader_char_name") return r = d.members.empty() ? Value::integer(0) : Value::string(d.members.front()), true;
    if (msg == "get_band_member_for_slot") {
        return r = Value::list({Value::integer(0), Value::string(""), Value::integer(0)}), true;
    }
    if (msg == "world_fame" || msg == "total_fame_stars") return r = Value::integer(m.bandFame(*this, "")), true;
    if (msg == "fans") return r = Value::integer(d.fans), true;
    if (msg == "money") {
        // The members' characters' money summed (TourBand money, RB 0x823EDAA0) [C].
        int sum = 0;
        for (const std::string& n : d.members)
            if (RbCharObj* c = m.charNamed(n)) sum += c->d.money;
        return r = Value::integer(sum), true;
    }
    if (msg == "cheat_add_money") return d.money += iArg(args, 0), true;
    if (msg == "buzz") return r = Value::integer(d.buzz), true;
    if (msg == "buzz_achievement") {
        const DataNode* buzz = m.data.band ? m.data.band->find("buzz") : nullptr;
        return r = Value::integer(num(buzz ? buzz->find("buzz_achievement") : nullptr, 1, 1000)), true;
    }
    if (msg == "buzz_to_fans") return r = Value::integer(m.buzzToFans(iArg(args, 0))), true;
    if (msg == "adjust_buzz") return m.setBuzz(*this, d.buzz + iArg(args, 0)), true;
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
            m.songStars.clear();
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
        if (m.songIndex < m.setlist.size() && m.setlist[m.songIndex] != "any") d.played.insert(m.setlist[m.songIndex]);
        m.lastStars += iArg(args, 0), m.lastScore += iArg(args, 1);
        m.songStars.push_back(iArg(args, 0));
        ++m.songIndex;
        return true;
    }
    if (msg == "last_song") return r = Value::integer(m.songIndex + 1 >= m.setlist.size() ? 1 : 0), true;  // the song now playing is the set list's last (endgame.dta asks before complete_song) [C data]
    if (msg == "first_song") return r = Value::integer(m.songIndex == 0 ? 1 : 0), true;
    if (msg == "num_completed") return r = Value::integer(int(m.songIndex)), true;
    if (msg == "is_winning" || msg == "is_hardcore_winning") return r = Value::integer(m.winning ? 1 : 0), true;
    if (msg == "reward") return r = Value::integer(m.reward()), true;
    if (msg == "complete_event") {
        if (!m.event) return true;
        const int songs = std::max<int>(1, int(m.setlist.size()));
        const int stars = m.lastStars / songs;
        // Every local member's character is paid the whole reward (pay,
        // RB 0x823EE4A0: each member's character's own pay) [C].
        const int reward = m.reward();
        for (RbCharObj* c : m.localMembers()) m.pay(*c, reward);
        // Buzz (RB 0x823EF7D0) [C]: the venue's (buzz (venues (<venue>
        // (performance (row) ...)))): the row the event's last performance
        // (new_performance for a first gig), the column the average stars
        // (2 at least) - 2, each clamped to the table; fans follow the buzz
        // (RB 0x823EE350).
        const int before = d.stars.count(m.event->ev.key()) ? d.stars[m.event->ev.key()] / songs : -1;
        const int change = m.buzzChange(m.event->ev.venue, before, stars), fans0 = d.fans;
        m.setBuzz(*this, d.buzz + change);
        lastFansChange = d.fans - fans0;
        lastBuzzChange = change;
        d.stars[m.event->ev.key()] = std::max(d.stars[m.event->ev.key()], m.lastStars);
        const std::string award = text(m.event->ev.node->find("award_item"));
        if (!award.empty() && !d.items.count(award)) d.items.insert(award), d.unlockedItem = award;
        m.save();
        return true;
    }
    // A failed song costs the venue's lose_penalty in buzz (config/tour_band.dta
    // (buzz (venues (<venue> (lose_penalty n))))) [C data; I: when RB applies it].
    if (msg == "lose_song") {
        const int before = d.fans;
        m.setBuzz(*this, d.buzz + m.losePenalty(d.venue.empty() && m.event ? m.event->ev.venue : d.venue));
        lastFansChange = d.fans - before;
        return m.save(), true;
    }
    if (msg == "fans_lost_on_fail") return r = Value::integer(m.buzzToFans(d.buzz) - m.buzzToFans(std::max(0, d.buzz + m.losePenalty(d.venue)))), true;
    if (msg == "fans_change") return r = Value::integer(lastFansChange), true;
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
        j.set("eye", Json::number(c->d.eye)), j.set("eyebrow", Json::number(c->d.eyebrow));
        j.set("weight", Json::number(c->d.weight)), j.set("height", Json::number(c->d.height));
        j.set("attitude", Json::number(c->d.attitude)), j.set("town", Json::string(c->d.town)), j.set("venue", Json::string(c->d.venue));
        Json seenTowns = Json::array();
        for (const std::string& s : c->d.seenTowns) seenTowns.a.push_back(Json::string(s));
        j.set("seen_towns", seenTowns);
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
        Json un = Json::array(), su = Json::array();
        for (int d = 0; d < 4; ++d) {
            un.a.push_back(Json::number(c->d.unlocks[d]));
            Json x = Json::array();
            for (const std::string& s : c->d.seenUnlocks[d]) x.a.push_back(Json::string(s));
            su.a.push_back(x);
        }
        j.set("unlocks", un), j.set("seen_unlocks", su);
        cs.a.push_back(j);
    }
    root.set("characters", cs);
    Json bs = Json::array();
    for (const RbBandObj* b : bands) {
        if (!b->d.finalized) continue;
        Json j = Json::object();
        j.set("name", Json::string(b->d.name)), j.set("hometown", Json::string(b->d.hometown)), j.set("fans", Json::number(b->d.fans));
        j.set("money", Json::number(b->d.money)), j.set("buzz", Json::number(b->d.buzz)), j.set("town", Json::string(b->d.town));
        j.set("motto", Json::string(b->d.motto)), j.set("venue", Json::string(b->d.venue)), j.set("min_difficulty", Json::number(b->d.minDifficulty));
        Json st = Json::object();
        for (const auto& [k, v] : b->d.stars) st.set(k, Json::number(v));
        j.set("stars", st);
        Json it = Json::array();
        for (const std::string& s : b->d.items) it.a.push_back(Json::string(s));
        j.set("items", it);
        Json mem = Json::array();
        for (const std::string& s : b->d.members) mem.a.push_back(Json::string(s));
        j.set("members", mem);
        Json pl = Json::array();
        for (const std::string& s : b->d.played) pl.a.push_back(Json::string(s));
        j.set("played", pl);
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
        c->d.eye = int(j["eye"].num()), c->d.eyebrow = int(j["eyebrow"].num());
        c->d.weight = float(j["weight"].num(0.5)), c->d.height = float(j["height"].num(0.5));
        c->d.attitude = int(j["attitude"].num(256));
        c->d.town = j["town"].str(), c->d.venue = j["venue"].str();
        for (const Json& s : j["seen_towns"].a) c->d.seenTowns.insert(s.str());
        for (const auto& [k, v] : j["outfit"].o) c->d.outfit[k] = v.str();
        for (const auto& [k, v] : j["stars"].o) c->d.stars[k] = int(v.num());
        for (const Json& s : j["owned"].a) c->d.owned.insert(s.str());
        for (const Json& s : j["seen_hints"].a) c->d.seenHints.insert(s.str());
        for (size_t d = 0; d < 4 && d < j["unlocks"].a.size(); ++d) c->d.unlocks[d] = int(j["unlocks"].a[d].num(-1));
        for (size_t d = 0; d < 4 && d < j["seen_unlocks"].a.size(); ++d)
            for (const Json& s : j["seen_unlocks"].a[d].a) c->d.seenUnlocks[d].insert(s.str());
        chars.push_back(c);
    }
    for (const Json& j : root["bands"].a) {
        RbBandObj* b = newBand();
        b->d.name = j["name"].str(), b->d.hometown = j["hometown"].str(), b->d.fans = int(j["fans"].num());
        b->d.money = int(j["money"].num()), b->d.buzz = int(j["buzz"].num()), b->d.town = j["town"].str(), b->d.finalized = true;
        b->d.motto = j["motto"].str(), b->d.venue = j["venue"].str(), b->d.minDifficulty = int(j["min_difficulty"].num(3));
        for (const auto& [k, v] : j["stars"].o) b->d.stars[k] = int(v.num());
        for (const Json& s : j["items"].a) b->d.items.insert(s.str());
        for (const Json& s : j["members"].a) b->d.members.push_back(s.str());
        for (const Json& s : j["played"].a) b->d.played.insert(s.str());
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
            // While a hint's (advance ...) runs, {tour hint} is that hint
            // (ONEOFF_HINT: add_seen_hint {{tour hint} get_type}, tour.dta).
            if (m_.advancingHint) return r = Value::obj(hintObj_), true;
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
            m_.advancingHint = true;
            try {
                if (const DataNode* a = h ? h->find("advance") : nullptr)
                    for (size_t i = 1; i < a->children.size(); ++i) in.eval(a->children[i]);
            } catch (...) {
                m_.advancingHint = false;
                throw;
            }
            m_.advancingHint = false;
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
            if (!creating && gMeta) {
                creating = gMeta->newChar();
                std::vector<Value> a{Value::integer(getProp(in, "char_player_num").integerValue()), Value::symbol("male")};
                handleNative(in, "request", a, r);
            }
            if (creating && creating->d.outfit.empty()) {
                std::vector<Value> a{Value::integer(getProp(in, "char_player_num").integerValue()), Value::symbol(creating->d.gender)};
                handleNative(in, "request", a, r);
            }
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

// A profile character's look for its menu preview (ui/rb_char_preview.h).
RbCharPreview::Look previewLook(const RbCharObj& c) {
    RbCharPreview::Look l;
    l.gender = c.d.gender.empty() ? "male" : c.d.gender;
    l.instrument = c.d.instrument.empty() ? "guitar" : c.d.instrument;
    l.outfit = c.d.outfit;
    l.skin = c.d.skin, l.attitude = uint32_t(c.d.attitude), l.weight = c.d.weight, l.height = c.d.height;
    return l;
}

// RB's select_cam on a menu's WorldDir panel (practice_space_panel,
// shop_char_panel): the panel draws through its <name>.shot, a BandCamShot
// of its milo (practice_space.milo creator / creator_heads, store.milo
// idle / makeup / tattoo_* ...) [C data]. The camera is the shot framed by
// CameraDirector::evaluate over the panel's objects (targets, parents,
// path); a target naming the character's slot is the previewed
// character's bone (its subpart), else its place [I: RB's targets on a
// shell character]. The shot's first frame, no blend from the last [I].
class PanelShotCamera {
public:
    void select(const std::string& shot) {
        if (shot != shot_) shot_ = shot, at_ = double(SDL_GetTicks());
    }
    const std::string& shot() const { return shot_; }
    // Sets (or clears) the view's camera; `slot` / `ch`: the character's slot and the preview.
    void apply(gfx::UiView& view, const std::string& slot, chr::Character* ch) const {
        if (legacy()) return view.clearCameraOverride();
        const auto* cs = shot_.empty() ? nullptr : dynamic_cast<const scene::CamShot*>(view.find(shot_ + ".shot"));
        if (!cs || cs->keyframes.empty()) return view.clearCameraOverride();
        const Scene scene(view, slot, ch);
        const gfx::Camera cam = CameraDirector::evaluate(scene, *cs, 0);
        view.setCameraOverride(cam);
        static const bool log = SDL_getenv("GD05_SHOTLOG") != nullptr;
        if (log) {
            static std::string last;
            if (last != shot_) {
                last = shot_;
                SDL_Log("select_cam %s: camera at %.1f %.1f %.1f, forward %.2f %.2f %.2f, fov %.3f", shot_.c_str(), cam.world.m[12], cam.world.m[13],
                        cam.world.m[14], cam.world.m[4], cam.world.m[5], cam.world.m[6], cam.yFov);
            }
        }
    }

    // GD05_SHOTCAM_LEGACY=1: the panels' own cameras and D83's creator.trans (the walks' "before").
    static bool legacy() {
        static const bool l = SDL_getenv("GD05_SHOTCAM_LEGACY") != nullptr;
        return l;
    }

private:
    class Scene : public ShotScene {
    public:
        Scene(const gfx::UiView& v, const std::string& slot, chr::Character* ch) : v_(v), slot_(slot), ch_(ch) {}
        bool targetPoint(const scene::CamTarget& t, std::array<float, 3>& out) const override {
            scene::Xfm x;
            if (!parentXfm(t, x)) return false;
            out = x.pos;
            return true;
        }
        bool parentXfm(const scene::CamTarget& t, scene::Xfm& out) const override {
            if (t.object.empty()) return false;
            if (ch_ && t.object == slot_) {
                if (!t.subpart.empty() && ch_->worldOf(t.subpart, out)) return true;
                out = ch_->xfm();
                return true;
            }
            return v_.worldXfm(t.object, out);
        }
        const scene::TransAnim* path(const std::string& name) const override { return dynamic_cast<const scene::TransAnim*>(v_.find(name)); }

    private:
        const gfx::UiView& v_;
        std::string slot_;
        chr::Character* ch_;
    };
    std::string shot_;
    double at_ = 0;
};

// The practice space (practice_space_panel, a CharSelectPanel:
// matchmaking.dta, practice_space.milo) [C data: the milo's objects]:
// - the creator (charcreator.dta: set_creator_player_num <n>, then
//   select_cam creator): the character being made stands at creator.pxy,
//   the UIProxy RB loads it into (creator.trans is the shots' target; D83
//   placed it at creator.trans, in the air);
// - a screen with player slots (the lobbies' player_panels): each slot's
//   player's character (its player config's get_char) at the slot's proxy
//   (coop0..3.pxy for four slots, hth0..1 for two, solo0 for one [I: RB's
//   CharSelectPanel 0x82827000.. not read]) while the slot's show_char
//   (matchmaking.dta MATCHMAKING_LOAD_CHAR_METHODS) is true;
// select_cam frames them (PanelShotCamera). GD05-083.
class PracticeSpacePanelObj : public QuietPanelObj {
public:
    using QuietPanelObj::QuietPanelObj;
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        if (msg == "select_cam") return cam_.select(args.empty() ? std::string() : in.evalValue(args[0]).str()), r = Value::integer(0), true;
        if (msg == "set_creator_player_num") return creator_ = args.empty() ? -1 : in.evalValue(args[0]).integerValue(), r = Value::integer(0), true;
        return QuietPanelObj::handleNative(in, msg, args, r);
    }
    void poll(double dtMs) override {
        QuietPanelObj::poll(dtMs);
        preview_.poll(dtMs);
        for (auto& p : slots_) p->poll(dtMs);
    }
    void draw() override {
        PanelObj* cc = ui.panel("charcreator_panel");
        auto* c = cc && cc->up ? dynamic_cast<RbCharObj*>(ui.interp().send(cc, "get_tour_char", {}).object) : nullptr;
        scene::Xfm at;
        const bool legacy = PanelShotCamera::legacy();
        const bool shown = showing && view && c && view->worldXfm(legacy ? "creator.trans" : "creator.pxy", at);
        if (shown) preview_.set(ui, previewLook(*c));
        // The slots' characters (not while the creator shows its own).
        struct Shown {
            RbCharPreview* p;
            scene::Xfm at;
        };
        std::vector<Shown> slotChars;
        auto& slots = ui.playerSlots();
        if (showing && view && !shown && !legacy && !slots.empty()) {
            const char* kind = slots.size() >= 4 ? "coop" : slots.size() == 2 ? "hth" : "solo";
            while (slots_.size() < slots.size()) slots_.push_back(std::make_unique<RbCharPreview>());
            for (size_t i = 0; i < slots.size(); ++i) {
                PlayerSlotObj* sl = slots[i].get();
                const int pn = sl ? sl->playerNum() : -1;
                if (pn < 0 || !sl->dispatch("show_char").truthy()) continue;
                const Value cfg = ui.interp().send("game", "get_player_config", {Value::integer(pn)});
                auto* pc = cfg.object ? dynamic_cast<RbCharObj*>(ui.interp().send(cfg.object, "get_char", {}).object) : nullptr;
                scene::Xfm sat;
                if (!pc || !view->worldXfm(std::string(kind) + std::to_string(i) + ".pxy", sat)) continue;
                if (slots_[i]->set(ui, previewLook(*pc))) slotChars.push_back({slots_[i].get(), sat});
            }
        }
        if (view) cam_.apply(*view, "creator.pxy", shown ? preview_.character() : nullptr);
        QuietPanelObj::draw();
        if (shown) preview_.draw(ui, *view, at);
        for (const Shown& sc : slotChars) sc.p->draw(ui, *view, sc.at);
    }

private:
    RbCharPreview preview_;
    std::vector<std::unique_ptr<RbCharPreview>> slots_;
    PanelShotCamera cam_;
    int creator_ = -1;
};

// Rock Band's tutorial main panels (GuitarTutorialPanel, DrumTutorialPanel,
// VocalsTutorialPanel; tutorial/*.dta) [C data: the messages; I: the
// natives]: the voice-over plays tutorial/streams/<vo>.mogg through the
// host's voice stream (tutorial_vo.dta's aliases), is_vo_playing follows
// it; the tutorial player is player0; the lesson's ambience, controller
// locks, fades, metronome and autoplayed endings are not done (quiet).
class RbTutorialPanelObj : public PanelObj {
public:
    RbTutorialPanelObj(UiManager& u, const std::string& name, const std::string& cls) : PanelObj(u, name, cls) {}
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        if (SDL_getenv("GD05_TUTLOG")) SDL_Log("tutorial: %s %s", name().c_str(), msg.c_str());
        if (msg == "play_vo" && !args.empty()) {
            std::string vo = in.evalValue(args[0]).str();
            if (aliases_.empty()) {
                try {
                    ScriptLoader loader(ui.data());
                    const DataNode& t = in.keep(loader.load("tutorial/tutorial_vo.dta"));
                    for (const DataNode& e : t.children)
                        if (e.isList() && e.children.size() >= 2 && e.children[0].isText() && e.children[1].isText()) aliases_[e.children[0].s] = e.children[1].s;
                } catch (const std::exception& e) {
                    ui.logOnce(std::string("tutorial: tutorial_vo.dta: ") + e.what());
                }
                aliases_[""] = "";
            }
            if (auto it = aliases_.find(vo); it != aliases_.end() && !it->second.empty()) vo = it->second;
            const bool ok = ui.host().playVoice("tutorial/streams/" + vo + ".mogg", 0.0f);
            if (!ok) ui.logOnce("tutorial: no voice-over " + vo);
            static const bool log = SDL_getenv("GD05_TUTLOG") != nullptr;
            if (log) SDL_Log("tutorial: voice-over %s%s", vo.c_str(), ok ? "" : " (missing)");
            return true;
        }
        if (msg == "is_vo_playing") return r = Value::integer(ui.host().voicePlaying() ? 1 : 0), true;
        if (msg == "reset_vo") return ui.host().stopVoice(), true;
        if (msg == "pause_vo" || msg == "unpause_vo") return ui.host().pauseVoice(msg == "pause_vo"), true;
        if (msg == "get_tutorial_player") {
            ScriptObject* p = in.findObject("player" + std::to_string(player_));
            return r = p ? Value::obj(p) : Value::integer(0), true;
        }
        if (msg == "set_tutorial_player") return player_ = args.empty() ? 0 : std::clamp(in.evalValue(args[0]).integerValue(), 0, 3), true;
        // The guitar lesson's tuning (frets.dta): every fret pressed once
        // ends it [I: RB's tuning rules not read; the frets held come from the host].
        if (msg == "start_tuning_mode") return tuning_ = true, tuned_ = 0, true;
        if (msg == "end_tuning_mode") return tuning_ = false, true;
        if (msg == "is_tuning") {
            if (tuning_) {
                tuned_ |= ui.host().playerFrets(player_);
                if ((tuned_ & 0x1F) == 0x1F) tuning_ = false;
            }
            return r = Value::integer(tuning_ ? 1 : 0), true;
        }
        if (msg == "all_frets_off") return true;
        static const std::set<std::string> kQuiet = {
            "add_names", "start_ambient_loop", "stop_ambient_loop", "pause_ambient_loop", "start_autoplay_bre", "stop_autoplay_bre",
            "mute_all_tracks", "disable_controller", "enable_controller", "reset_controller", "set_sfx_volume", "set_paused", "pause",
            "unpause", "hide_all_ui", "fade_song_in", "fade_song_out", "fade_top_in", "fade_top_out", "set_top_visible",
            "set_top_invisible", "set_metronome", "set_inc_dec", "end_hopo_train", "is_hopo_training",
            "beat_callback"};
        if (kQuiet.count(msg)) return ui.logOnce("STUB-quiet " + className() + " " + msg), true;
        return PanelObj::handleNative(in, msg, args, r);
    }

private:
    int player_ = 0;
    bool tuning_ = false;
    unsigned tuned_ = 0;
    std::map<std::string, std::string> aliases_;
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

// The outfit catalog (char/outfitloader.dta, the boot tree's (outfit_loader
// (types (<gender> (category (<cat> (outfits (<name> (cost n) (genre g)
// ...))))))) [C data]: what the shop sells and the creator offers.
const DataNode* outfitCategory(UiManager& ui, const std::string& gender, const std::string& cat) {
    // char/char_objects.dta #includes it (outfitloader.dta) [C data].
    static DataNode objects;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        try {
            ScriptLoader loader(ui.data());
            objects = loader.load("char/char_objects.dta");
        } catch (const std::exception& e) {
            SDL_Log("rb meta: char_objects.dta: %s", e.what());
        }
    }
    const DataNode* boot = &objects;
    std::function<const DataNode*(const DataNode&)> findTypes = [&](const DataNode& n) -> const DataNode* {
        for (const DataNode& c : n.children)
            if (c.isList() && !c.children.empty()) {
                if (c.children[0].isText() && c.children[0].s == "types" && c.find(gender)) return &c;
                if (const DataNode* f = findTypes(c)) return f;
            }
        return nullptr;
    };
    const DataNode* types = boot ? findTypes(*boot) : nullptr;
    const DataNode* g = types ? types->find(gender) : nullptr;
    const DataNode* cats = g ? g->find("category") : nullptr;
    return cats ? cats->find(cat) : nullptr;
}

// A category's outfits as a list (focus_provider): cost / token per outfit.
class OutfitList : public RbList {
public:
    using RbList::RbList;
    const DataNode* cat = nullptr;
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        if (msg == "cost") return r = Value::integer(cost(sArg(args, 0))), true;
        if (msg == "token") return r = Value::symbol(sArg(args, 0)), true;  // the outfit's own name token [C data: locale]
        if (msg == "is_closet_category") return r = Value::integer(0), true;
        return RbList::handleNative(in, msg, args, r);
    }
    int cost(const std::string& o) const {
        if (const DataNode* g = group(o)) return num(g->find("cost"));
        const DataNode* outs = cat ? cat->find("outfits") : nullptr;
        const DataNode* e = outs ? outs->find(o) : nullptr;
        return num(e ? e->find("cost") : nullptr);
    }
    // An instrument category's (groups (<model> (cost n) (default_outfit
    // <finish>)) ...) (char/instruments.dta) [C data]: the shop lists and
    // sells the models (OutfitProvider RB 0x82859E08: each group, its
    // cost; a group without a cost only when owned) [C].
    const DataNode* groups() const { return cat ? cat->find("groups") : nullptr; }
    static std::string key(const DataNode& n) {
        if (n.children.empty()) return {};
        const DataNode& k = n.children[0];
        return k.isText() ? k.s : k.type == DataNode::Type::Int ? std::to_string(k.i) : std::string();
    }
    const DataNode* group(const std::string& g) const {
        const DataNode* gs = groups();
        for (size_t i = 1; gs && i < gs->children.size(); ++i)
            if (gs->children[i].isList() && key(gs->children[i]) == g) return &gs->children[i];
        return nullptr;
    }
    // What a chosen symbol puts on the character: the finish chosen in
    // group_select_panel (a model's, or a piece with finishes: the mic's
    // mic1_silver, has_group_selection RB 0x823E0700), else a model's
    // default finish.
    std::string wear(const std::string& o) const {
        if (auto it = gChosenFinish.find(o); it != gChosenFinish.end()) return it->second;
        const DataNode* g = group(o);
        return g && g->find("default_outfit") ? text(g->find("default_outfit")) : o;
    }
};

// OutfitPanel (ui/shop.dta, charcreator.dta): the shop's and the creator's
// outfit lists. A focused <category>.lst lists the category's outfits for
// the character's gender (the store's genre, or the character's own in
// the closet); store_outfit wears the focused one, buying it first when it
// is not owned [I: RB's purchase point; the price is the catalog's].
class OutfitPanelObj : public PanelObj {
public:
    using PanelObj::PanelObj;
    std::map<std::string, OutfitList*> lists;
    RbCharObj* tourChar() {
        if (!gMeta) return nullptr;
        const Value oc = getProp(ui.interp(), "outfit_char");
        const std::string who = oc.type == Value::Type::Object && oc.object ? oc.object->name() : oc.str();
        if (who == "charcreator_panel") return dynamic_cast<RbCharObj*>(ui.interp().send("charcreator_panel", "get_tour_char", {}).object);
        const int pn = ui.interp().send("shop_char_panel", "get", {Value::symbol("char_player_num")}).integerValue();
        const Value cfg = ui.interp().send("game", "get_player_config", {Value::integer(std::max(0, pn))});
        return cfg.object ? dynamic_cast<RbCharObj*>(ui.interp().send(cfg.object, "get_char", {}).object) : nullptr;
    }
    std::string base(const std::string& n) const {
        const size_t d = n.rfind('.');
        return d == std::string::npos ? n : n.substr(0, d);
    }
    // The catalog's categories (hair, torso ...); the creator's skin.lst
    // and the sliders are the script's own (charcreator.dta set_colors).
    bool isOutfitCategory(const std::string& cat) {
        RbCharObj* c = tourChar();
        return outfitCategory(ui, c ? c->d.gender : "male", cat) != nullptr;
    }
    OutfitList* listFor(const std::string& cat) {
        RbCharObj* c = tourChar();
        const std::string gender = c ? c->d.gender : "male";
        auto& l = lists[cat];
        if (!l) {
            l = gMeta->own(std::make_unique<OutfitList>(name() + "_" + cat + "_provider"));
        }
        l->cat = outfitCategory(ui, gender, cat);
        const Value g = getProp(ui.interp(), "genre");
        const std::string genre = g.isText() ? g.str() : std::string();
        const bool closet = getProp(ui.interp(), "closet").truthy();
        const Value oc = getProp(ui.interp(), "outfit_char");
        const bool creator = (oc.type == Value::Type::Object && oc.object ? oc.object->name() : oc.str()) == "charcreator_panel";
        OutfitList* raw = l;
        l->build = [raw, genre, closet, creator, c] {
            std::vector<std::string> v;
            if (const DataNode* gs = raw->groups()) {
                for (size_t i = 1; i < gs->children.size(); ++i) {
                    if (!gs->children[i].isList()) continue;
                    const std::string g = OutfitList::key(gs->children[i]);
                    const bool owned = c && c->d.owned.count(g);
                    if (g.empty() || (closet && !owned) || (num(gs->children[i].find("cost")) == 0 && !owned)) continue;
                    v.push_back(g);
                }
                return v;
            }
            // OutfitProvider's outfits (RB 0x82859E08) [C]: no (exclude TRUE)
            // piece; none only with the category's allow_none (default
            // TRUE); in the creator not a (creator FALSE) piece; the genre's;
            // with (filter <category>) only the pieces named after the
            // character's piece of that category (eyes of its head) [C];
            // the closet's owned ones [I: the port's rule for the free ones].
            const DataNode* outs = raw->cat ? raw->cat->find("outfits") : nullptr;
            const bool allowNone = flag(raw->cat ? raw->cat->find("allow_none") : nullptr, true);
            std::string prefix;
            if (const DataNode* f = raw->cat ? raw->cat->find("filter") : nullptr; f && c)
                if (auto it = c->d.outfit.find(text(f)); it != c->d.outfit.end()) prefix = it->second;
            for (const auto& [o, n] : entries(outs)) {
                const bool none = o == "none";
                if (flag(n->find("exclude"), false) || (none && !allowNone)) continue;
                if (creator && !flag(n->find("creator"), true)) continue;
                const std::string og = text(n->find("genre"));
                if (!genre.empty() && og != genre) continue;
                if (!none && !prefix.empty() && o.rfind(prefix, 0) != 0) continue;
                if (closet && c && !c->d.owned.count(o) && num(n->find("cost")) > 0) continue;
                v.push_back(o);
            }
            return v;
        };
        l->refresh();
        return l;
    }
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        if (gMeta) gMeta->ensure();
        r = Value::integer(0);
        if (msg == "focus_category") {
            const std::string b = base(focus);
            return r = Value::symbol(focus.size() > 4 && (focus.rfind(".lst") == focus.size() - 4 || focus.rfind(".btn") == focus.size() - 4) &&
                                             b != "weight" && b != "height" && b != "skin" && b != "accessories"
                                         ? b
                                         : std::string()),
                   true;
        }
        if (msg == "focus_outfit") {
            if (focus.rfind(".lst") == std::string::npos || !isOutfitCategory(base(focus))) return r = Value::symbol(""), true;
            OutfitList* l = listFor(base(focus));
            ComponentObj* c = component(focus);
            if (c && c->provider != l) in.send(c, "set_provider", {Value::obj(l)});
            return r = Value::symbol(l->symbol(c ? c->selected : 0)), true;
        }
        if (msg == "focus_provider") return r = Value::obj(listFor(base(focus))), true;
        if (msg == "tour_char") return r = Value::obj(tourChar()), true;
        if (msg == "selected_outfit") {
            RbCharObj* c = tourChar();
            const std::string cat = base(focus);
            return r = Value::symbol(c && c->d.outfit.count(cat) ? c->d.outfit[cat] : std::string()), true;
        }
        if (msg == "store_outfit") {
            // Remembered for undo_outfit.
            RbCharObj* c = tourChar();
            const std::string cat = base(focus);
            stored_[cat] = c && c->d.outfit.count(cat) ? c->d.outfit[cat] : std::string("none");
            return true;
        }
        if (msg == "confirm_outfit" || msg == "undo_outfit") {
            // Native first, then the script's camera: wear (buying when not
            // owned, at the catalog's price) or put back, and the focus
            // back on the category's button.
            RbCharObj* c = tourChar();
            const std::string cat = base(focus);
            if (focus.rfind(".lst") != std::string::npos && c) {
                if (msg == "confirm_outfit") {
                    OutfitList* l = listFor(cat);
                    ComponentObj* comp = component(focus);
                    const std::string o = l->symbol(comp ? comp->selected : 0);
                    // The script pays (set_money, shop_confirm_screen); this
                    // keeps it owned (an instrument model: with its finish).
                    if (!o.empty()) c->d.owned.insert(o), c->d.owned.insert(l->wear(o));
                    if (!o.empty() && (c->d.owned.count(o) || l->cost(o) == 0)) c->d.outfit[cat] = l->wear(o), ++bought_;
                    gChosenFinish.erase(o);
                } else if (stored_.count(cat)) {
                    c->d.outfit[cat] = stored_[cat];
                }
                gMeta->save();
                // The focus back on the category's button; a panel of lists
                // only (shop_instrument.milo: guitar / bass / drum / mic.lst)
                // keeps none, so its screen's back works again (RB
                // 0x8283EAD0 / 0x8283EC08 -> 0x8283E638) [I: 0x8283E638 not read].
                if (component(cat + ".btn")) setFocus(cat + ".btn", true);
                else setFocus("", true);
            }
            return handler(msg) == nullptr;  // the script's camera runs next when it has one
        }
        if (msg == "find_outfit_config" || msg == "find_patch_renderer" || msg == "find_patch_color") return r = Value::symbol(""), true;
        if (msg == "num_bought") return r = Value::integer(bought_), true;
        // Lists' providers when a list gets the focus.
        if (msg == "set_focus") {
            const bool done = PanelObj::handleNative(in, msg, args, r);
            if (focus.rfind(".lst") != std::string::npos && isOutfitCategory(base(focus)))
                if (ComponentObj* c = component(focus)) in.send(c, "set_provider", {Value::obj(listFor(base(focus)))});
            return done;
        }
        return PanelObj::handleNative(in, msg, args, r);
    }
    bool nativeFirst(const std::string& msg) const override { return msg == "confirm_outfit" || msg == "undo_outfit"; }
    // A category button opens its list (X); back on a list puts the outfit
    // back (undo_outfit) [I: OutfitPanel's own navigation].
    bool buttonDown(int button, int player) override {
        const std::string before = focus;
        const bool onBtn = before.size() > 4 && before.rfind(".btn") == before.size() - 4;
        const bool r = PanelObj::buttonDown(button, player);
        if (button == kPad_X && onBtn && focus == before && component(base(before) + ".lst") && isOutfitCategory(base(before))) {
            ComponentObj* l = component(base(before) + ".lst");
            ui.interp().send(l, "set_provider", {Value::obj(listFor(base(before)))});
            ui.interp().send(this, "store_outfit", {});
            setFocus(base(before) + ".lst", true);
            ui.callScript(this, "update");
            return true;
        }
        if (button == kPad_Tri && before.rfind(".lst") != std::string::npos && before.rfind(".lst") == before.size() - 4) {
            ui.interp().send(this, "undo_outfit", {});
            ui.callScript(this, "update");
            return true;
        }
        return r;
    }

private:
    std::map<std::string, std::string> stored_;
    int bought_ = 0;
};

// CharPanel shop_char_panel: the shopped character's view [I: no 3D view of
// the character in the menus yet; the camera and appearance messages are
// answered].
class CharPanelObj : public PanelObj {
public:
    using PanelObj::PanelObj;
    // The shopped character at the store's char0 (stores/store.milo's
    // CompositeCharacter slot) in its look (GD05-083, ui/rb_char_preview.h).
    void poll(double dtMs) override {
        PanelObj::poll(dtMs);
        preview_.poll(dtMs);
    }
    void draw() override {
        auto* c = dynamic_cast<RbCharObj*>(ui.interp().send(this, "get_tour_char", {}).object);
        scene::Xfm at;
        const bool shown = showing && view && c && view->worldXfm("char0", at);
        if (shown) preview_.set(ui, previewLook(*c));
        if (view) cam_.apply(*view, "char0", shown ? preview_.character() : nullptr);
        PanelObj::draw();
        if (shown) preview_.draw(ui, *view, at);
    }
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        if (msg == "select_cam") return cam_.select(args.empty() ? std::string() : in.evalValue(args[0]).str()), r = Value::integer(0), true;
        if (msg == "update_appearance" || msg == "load_char") return r = Value::integer(0), true;
        // get_tour_char, or the property ({shop_char_panel get tour_char}: shop.dta's back of shop_instrument_screen).
        if (msg == "get_tour_char" || (msg == "get" && sArg(args, 0) == "tour_char")) {
            const int pn = getProp(in, "char_player_num").integerValue();
            const Value cfg = in.send("game", "get_player_config", {Value::integer(std::max(0, pn))});
            return r = cfg.object ? in.send(cfg.object, "get_char", {}) : Value::integer(0), true;
        }
        return PanelObj::handleNative(in, msg, args, r);
    }

private:
    RbCharPreview preview_;
    PanelShotCamera cam_;
};

// AudioLatencyPanel (ui/options.dta, the A/V calibration): its click track
// [I: the port's calibration is Enhanced Options' (GDALL-038); the panel's
// audio is answered with the time since start_audio].
class AudioLatencyPanelObj : public PanelObj {
public:
    using PanelObj::PanelObj;
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        if (msg == "preload_audio" || msg == "stop_audio") return r = Value::integer(0), true;
        if (msg == "start_audio") return startMs_ = double(SDL_GetTicks()), r = Value::integer(0), true;
        if (msg == "get_audio_time") return r = Value::real(startMs_ > 0 ? float(double(SDL_GetTicks()) - startMs_) : 0.0f), true;
        return PanelObj::handleNative(in, msg, args, r);
    }

private:
    double startMs_ = 0;
};

// OutfitGroupPanel group_select_panel (ui/shop.dta): init <outfit panel>
// lists the focused model's finishes in outfit.lst (the outfits that
// has_group_selection counts, RB 0x823E0700 [C]; the list's order the
// catalog's [I: RB's OutfitGroupPanel 0x8282AD38 not read]); X keeps the
// chosen finish for the outfit panel's confirm_outfit (the script then
// buys it through shop_confirm_screen or wears it).
class OutfitGroupPanelObj : public PanelObj {
public:
    using PanelObj::PanelObj;
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        if (msg == "init") {
            props["outfit_panel"] = args.empty() ? Value::integer(0) : args[0];
            ScriptObject* op = args.empty() ? nullptr : args[0].object;
            model_ = op ? in.send(op, "focus_outfit", {}).str() : std::string();
            const std::string cat = op ? in.send(op, "focus_category", {}).str() : std::string();
            auto* c = op ? dynamic_cast<RbCharObj*>(in.send(op, "tour_char", {}).object) : nullptr;
            if (!list_ && gMeta) list_ = gMeta->own(std::make_unique<RbList>(name() + "_provider"));
            if (list_) {
                const std::vector<std::string> f = modelFinishes(ui, c ? c->d.gender : "male", cat, model_);
                list_->build = [f] { return f; };
                list_->label = [this](const std::string& o) {
                    const std::string l = ui.localize(o);
                    return l.empty() ? o : l;
                };
                list_->refresh();
                attach();
            }
            return r = Value::integer(0), true;
        }
        if (msg == "selected_outfit" || msg == "focus_outfit") return r = Value::symbol(selected()), true;
        return PanelObj::handleNative(in, msg, args, r);
    }
    void enter() override {
        PanelObj::enter();
        attach();
    }
    bool buttonDown(int button, int player) override {
        if (button == kPad_X && !model_.empty() && !selected().empty()) {
            gChosenFinish[model_] = selected();
            // The SELECT_MSG buys when {&& {! has_outfit} $cost}: RB's
            // script reads the $cost the outfit panel's handlers left (its
            // do's locals); the port's do restores them, so the model's
            // price is set here [I: RB's do semantics].
            const Value op = getProp(ui.interp(), "outfit_panel");
            const Value prov = op.object ? ui.interp().send(op.object, "focus_provider", {}) : Value::integer(0);
            ui.interp().var("cost") = prov.object ? ui.interp().send(prov.object, "cost", {Value::symbol(model_)}) : Value::integer(0);
        }
        return PanelObj::buttonDown(button, player);
    }

private:
    void attach() {
        if (ComponentObj* c = component("outfit.lst"); c && list_ && c->provider != list_) ui.interp().send(c, "set_provider", {Value::obj(list_)});
    }
    std::string selected() {
        ComponentObj* c = component("outfit.lst");
        return list_ ? list_->symbol(c ? c->selected : 0) : std::string();
    }
    RbList* list_ = nullptr;
    std::string model_;
};

// OutfitConfigPanel color_select_panel (ui/shop.dta): init <outfit panel>
// keeps the panel the confirm screen answers to [C data: the uses; I: no
// colour choice yet].
class OutfitConfigPanelObj : public PanelObj {
public:
    using PanelObj::PanelObj;
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        if (msg == "init") return props["outfit_panel"] = args.empty() ? Value::integer(0) : args[0], r = Value::integer(0), true;
        if (msg == "clear" || msg == "store" || msg == "undo") return r = Value::integer(0), true;
        return PanelObj::handleNative(in, msg, args, r);
    }
};

std::unique_ptr<ScriptObject> makeRbMetaClass(UiManager& ui, const std::string& cls, const std::string& name) {
    if (cls == "GuitarTutorialPanel" || cls == "DrumTutorialPanel" || cls == "VocalsTutorialPanel") return std::make_unique<RbTutorialPanelObj>(ui, name, cls);
    if (cls == "OutfitConfigPanel") return std::make_unique<OutfitConfigPanelObj>(ui, name, cls);
    if (cls == "OutfitGroupPanel") return std::make_unique<OutfitGroupPanelObj>(ui, name, cls);
    // (not the calibrate_* screens: calibrate_av_chooser_screen was made a
    // panel, so OPTIONS > CALIBRATE found no screen)
    if (cls == "AudioLatencyPanel" || (name.rfind("calibrate_", 0) == 0 && cls != "UIScreen")) return std::make_unique<AudioLatencyPanelObj>(ui, name, cls);
    if (cls == "OutfitPanel") return std::make_unique<OutfitPanelObj>(ui, name, cls);
    if (cls == "CharPanel") return std::make_unique<CharPanelObj>(ui, name, cls);
    if (cls == "BandCreatePanel") return std::make_unique<BandCreatePanelObj>(ui, name, cls);
    if (cls == "DialogPanel") return std::make_unique<QuietPanelObj>(ui, name, cls, std::set<std::string>{"set_player_num"});
    if (cls == "CharCreatorPanel") return std::make_unique<CharCreatorPanelObj>(ui, name, cls);
    if (cls == "WorldMapPanel") return std::make_unique<QuietPanelObj>(ui, name, cls, std::set<std::string>{"sync"});
    if (cls == "PracticePanel")
        return std::make_unique<QuietPanelObj>(ui, name, cls, std::set<std::string>{"in_vocal_mode", "set_in_vocal_mode", "set_pitch_shift_ratio"});
    if (cls == "PracticeBackgroundPanel") return std::make_unique<QuietPanelObj>(ui, name, cls, std::set<std::string>{"select_cam", "select_camera_shot"});
    if (name == "practice_space_panel")
        return std::make_unique<PracticeSpacePanelObj>(ui, name, cls, std::set<std::string>{"play_attitude_group", "set_char_player_num"});
    return nullptr;
}

std::vector<std::string> rbCategoryOutfits(UiManager& ui, const std::string& gender, const std::string& category) {
    std::vector<std::string> v;
    const DataNode* c = outfitCategory(ui, gender, category);
    for (const auto& [o, n] : entries(c ? c->find("outfits") : nullptr)) v.push_back(o);
    return v;
}

RbTourUnlocks rbTourUnlocks() {
    RbTourUnlocks u;
    if (!gMeta) return u;
    gMeta->ensure();
    for (const RbCharObj* c : gMeta->chars)
        for (int i = 0; i < 3; ++i)
            if (c->d.instrument == RbTourUnlocks::kInstruments[i]) {
                u.ranked[i] = std::max(u.ranked[i], gMeta->soloQuickplayProgress(c->d, false));
                u.bonus[i] = std::max(u.bonus[i], gMeta->soloQuickplayProgress(c->d, true));
            }
    std::set<std::string> played;
    for (const RbBandObj* b : gMeta->bands) played.insert(b->d.played.begin(), b->d.played.end());
    u.bandPlayed.assign(played.begin(), played.end());
    return u;
}

int rbOutfitCost(UiManager& ui, const std::string& gender, const std::string& category, const std::string& outfit) {
    const DataNode* cat = outfitCategory(ui, gender, category);
    OutfitList l("");
    l.cat = cat;
    if (const DataNode* g = l.group(outfit)) return num(g->find("cost"));  // an instrument model
    const DataNode* outs = cat ? cat->find("outfits") : nullptr;
    const DataNode* e = outs ? outs->find(outfit) : nullptr;
    return e ? num(e->find("cost")) : -1;
}

RbModel rbFirstModel(UiManager& ui, const std::string& gender, const std::string& category) {
    OutfitList l("");
    l.cat = outfitCategory(ui, gender, category);
    const DataNode* gs = l.groups();
    for (size_t i = 1; gs && i < gs->children.size(); ++i)
        if (gs->children[i].isList()) {
            const std::string m = OutfitList::key(gs->children[i]);
            return {m, l.wear(m), num(gs->children[i].find("cost"))};
        }
    return {};
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
            // The solo venues and their kinds (RB 0x823FDB20) [C], max_song and
            // num_initial_unlock_songs (tour_solo.dta) [C data].
            for (size_t i = 0; i < m.data.soloEvents.size(); ++i) {
                const TourEvent& e = m.data.soloEvents[i];
                if (m.data.soloVenues.empty() || m.data.soloVenues.back().town != e.town || m.data.soloVenues.back().venue != e.venue)
                    m.data.soloVenues.push_back({e.town, e.venue, 0, {}});
                SoloVenue& v = m.data.soloVenues.back();
                v.events.push_back(i);
                const DataNode* songs = e.node ? e.node->find("songs") : nullptr;
                const DataNode* s0 = songs && songs->children.size() > 1 ? &songs->children[1] : nullptr;
                if (s0 && s0->isList() && text(s0, 0) == "unlock") v.type = 1;
                if (s0 && s0->isText() && s0->s == "download") v.type = 2;
            }
            if (const DataNode* n = m.data.solo ? m.data.solo->find("num_initial_unlock_songs") : nullptr) m.data.initialUnlocks = num(n);
            for (const auto& [k, n] : entries(m.data.solo ? m.data.solo->find("max_song") : nullptr)) {
                static const char* const kSyms[] = {"kDifficultyEasy", "kDifficultyMedium", "kDifficultyHard", "kDifficultyExpert"};
                for (int d = 0; d < 4; ++d)
                    if (k == kSyms[d]) m.data.maxSong[d] = num(n);
            }
            if (const DataNode* ms = m.data.solo ? m.data.solo->find("max_song") : nullptr)  // the macros as numbers
                for (size_t i = 1; i < ms->children.size(); ++i) {
                    const DataNode& c = ms->children[i];
                    if (c.isList() && c.children.size() > 1 && c.children[0].type == DataNode::Type::Int && c.children[0].i >= 0 && c.children[0].i < 4)
                        m.data.maxSong[c.children[0].i] = num(&c);
                }
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
    // The open towns' open events (RB 0x823E5BB8 / 0x823E46B0) [C].
    m.soloEvents->build = [&m] {
        std::vector<std::string> v;
        RbCharObj* c = m.soloChar();
        if (!c) return v;
        const int dd = std::clamp(c->d.difficulty, 0, 3);
        for (const std::string& t : m.soloOpenTowns(c->d, dd))
            for (const SoloVenue& sv : m.data.soloVenues)
                if (sv.town == t)
                    for (size_t i : m.soloOpenEvents(c->d, dd, sv)) v.push_back(m.data.soloEvents[i].name);  // unique across the towns [C data]
        if (SDL_getenv("GD05_UILOG")) SDL_Log("tour: solo events %d (difficulty %d)", int(v.size()), dd);
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
        if (s == "create_new") return m.localizeOr("char_provider_create_new");
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
    // The band lobby's character lists, one per player (get_char_provider
    // <player>: RB 0x828572F0 makes four, CharProvider handler RB
    // 0x82857878) [C]: first (Create New) when the mode's allow_create_char,
    // then (Default) (the instrument's premade character) when its
    // show_prefab_chars, then the profile's characters of the instrument
    // filter (RB 0x82857660 / 0x82855BD0) that another player has not
    // taken [I: 0x82421B68 not read]. is_create_new / is_non_prefab_char /
    // get_char by those header rows (RB 0x828560C8, 0x82855DB8, 0x82856558).
    for (int p = 0; p < 4; ++p) {
        RbList* l = list("rb_band_char_provider" + std::to_string(p));
        struct State {
            std::string filter;
            int saved = 0;
        };
        auto st = std::make_shared<State>();
        auto header = [&m] {
            return int(gameCfgValue(m.ui(), "allow_create_char").truthy()) + int(gameCfgValue(m.ui(), "show_prefab_chars").truthy());
        };
        l->build = [&m, st, p, header] {
            std::vector<std::string> v;
            if (gameCfgValue(m.ui(), "allow_create_char").truthy()) v.push_back("create_new");
            if (gameCfgValue(m.ui(), "show_prefab_chars").truthy()) v.push_back("prefab");
            const Session& s = m.ui().session();
            for (RbCharObj* c : m.chars) {
                if (!st->filter.empty() && st->filter != "none" && c->d.instrument != (st->filter == "bass" ? "guitar" : st->filter)) continue;
                bool taken = false;
                for (int q = 0; q < 4; ++q) taken |= q != p && s.joined(q) && s.players[size_t(q)].hasChar && s.players[size_t(q)].character == c->name();
                if (!taken) v.push_back(c->name());
            }
            (void)header;
            return v;
        };
        l->label = [&m](const std::string& sym) {
            if (sym == "create_new") return m.localizeOr("char_provider_create_new");
            if (sym == "prefab") return m.localizeOr("char_provider_prefab");
            for (RbCharObj* c : m.chars)
                if (c->name() == sym) return c->d.name;
            return sym;
        };
        l->extra = [&m, l, st, p, header](Interp& in2, const std::string& msg, std::vector<Value>& a, Value& r) {
            if (msg == "set_instrument_filter") return st->filter = a.empty() ? std::string() : a[0].str(), l->refresh(), true;
            if (l->data.empty()) l->refresh();
            const int pos = iArg(a, 0);
            const bool create = gameCfgValue(m.ui(), "allow_create_char").truthy() && pos == 0;
            if (msg == "is_create_new") return r = Value::integer(create ? 1 : 0), true;
            if (msg == "is_char") return r = Value::integer(create ? 0 : 1), true;
            if (msg == "is_non_prefab_char") return r = Value::integer(pos >= header() ? 1 : 0), true;
            if (msg == "get_char") {
                if (pos < header()) {
                    const std::string inst = st->filter.empty() || st->filter == "none" ? std::string("guitar") : st->filter;
                    return r = Value::obj(in2.findObject("prefab_" + inst)), true;
                }
                ScriptObject* o = in2.findObject(l->symbol(pos));
                return r = o ? Value::obj(o) : Value::integer(0), true;
            }
            if (msg == "get_pos_from_char") return r = Value::integer(a.empty() || !a[0].object ? -1 : l->indexOf(a[0].object->name())), true;
            if (msg == "save_relative_pos") return st->saved = pos, true;
            if (msg == "get_saved_pos") return r = Value::integer(std::clamp(st->saved, 0, std::max(0, l->numData() - 1))), true;
            (void)p;
            return false;
        };
    }
    in.addBuiltin("get_char_provider", [&m](Interp& in2, const DataNode& cmd) {
        const int p = cmd.children.size() > 1 ? std::clamp(in2.eval(cmd.children[1]).integerValue(), 0, 3) : 0;
        if (cmd.children.size() < 2) return Value::obj(m.charList);
        return Value::obj(in2.findObject("rb_band_char_provider" + std::to_string(p)));
    });
    m.bandList = list("rb_band_provider");
    m.bandList->build = [&m] {
        std::vector<std::string> v;
        for (RbBandObj* b : m.bands) v.push_back(b->name());
        v.push_back("create_new");
        return v;
    };
    m.bandList->label = [&m](const std::string& s) {
        if (s == "create_new") return m.localizeOr("char_provider_create_new");
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
    // The band lobby (matchmaking_band_screen) runs its own scripts
    // (GD05-082, DECISIONS D80: the slots of ui/rb_multifocus.h, session_mgr,
    // the players' character lists). GD05_BAND_PLAYERS=<n> (the walks that
    // test what comes after it) skips it as the stand-in did (D10): n
    // players joined with their instrument's character, player 1 leading,
    // straight to the mode's post_matchmaking_screen; the head to head
    // lobby (matchmaking_hth_screen) is still the stand-in [I].
    ui.redirect = [&m, &ui](const std::string& name, bool back) -> std::string {
        const bool hth = name == "matchmaking_hth_screen";
        if ((name != "matchmaking_band_screen" && !hth) || back || (!hth && !SDL_getenv("GD05_BAND_PLAYERS"))) return {};
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
            // The band's members: each player's profile character of the
            // instrument, when there is one (RB's lobby has the player pick
            // or make it) [I: the stand-in picks the first one not taken].
            if (!hth)
                for (RbCharObj* c : m.chars) {
                    bool taken = false;
                    for (int q = 0; q < p; ++q) taken |= s.players[size_t(q)].character == c->name();
                    if (!taken && c->d.instrument == inst) {
                        pc.character = c->name();
                        break;
                    }
                }
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
    // The options' character / band data lists (ui/options.dta): the
    // profile's characters and bands; deleting one removes it from the save.
    RbList* charData = list("char_data_provider");
    charData->build = [&m] {
        std::vector<std::string> v;
        for (RbCharObj* c : m.chars) v.push_back(c->name());
        return v;
    };
    charData->label = m.charList->label;
    charData->extra = [&m, charData](Interp& in2, const std::string& msg, std::vector<Value>& a, Value& r) {
        if (msg == "init") return charData->refresh(), true;
        if (msg == "get_char") return r = Value::obj(in2.findObject(charData->symbol(iArg(a, 0)))), true;
        if (msg == "delete_character") {
            const std::string id = charData->symbol(iArg(a, 0));
            m.chars.erase(std::remove_if(m.chars.begin(), m.chars.end(), [&](RbCharObj* c) { return c->name() == id; }), m.chars.end());
            m.save(), charData->refresh(), m.charList->refresh();
            return true;
        }
        return false;
    };
    for (const char* n : {"band_data_provider", "band_quit_provider"}) {
        RbList* bandData = list(n);
        bandData->build = [&m] {
            std::vector<std::string> v;
            for (RbBandObj* b : m.bands) v.push_back(b->name());
            return v;
        };
        bandData->label = m.bandList->label;
        bandData->extra = [&m, bandData](Interp& in2, const std::string& msg, std::vector<Value>& a, Value& r) {
            if (msg == "init") return bandData->refresh(), true;
            if (msg == "get_band") return r = Value::obj(in2.findObject(bandData->symbol(iArg(a, 0)))), true;
            if (msg == "delete_band" || msg == "remove_band") {
                const std::string id = bandData->symbol(iArg(a, 0));
                m.bands.erase(std::remove_if(m.bands.begin(), m.bands.end(), [&](RbBandObj* b) { return b->name() == id; }), m.bands.end());
                m.save(), bandData->refresh(), m.bandList->refresh();
                return true;
            }
            return false;
        };
    }
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
