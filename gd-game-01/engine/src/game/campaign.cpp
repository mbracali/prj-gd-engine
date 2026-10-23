// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/campaign.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "data/dta.h"
#include "data/game_data.h"
#include "data/json.h"

namespace {

const char* const kDiffNames[] = {"easy", "medium", "hard", "expert"};
const char* const kCatNames[] = {"venue", "song", "character", "guitar", "skin", "video"};

int diffIndex(const std::string& s) {
    static const char* const k[] = {"kDifficultyEasy", "kDifficultyMedium", "kDifficultyHard", "kDifficultyExpert"};
    for (int i = 0; i < 4; ++i)
        if (s == k[i]) return i;
    return -1;
}

// The highscore_dummy_0..4 defaults.
std::vector<Campaign::HighScore> defaultHighScores() {
    return {{"Kendall", 50000}, {"Marc", 40000}, {"Kris", 30000}, {"Sinc", 20000}, {"Keith", 10000}};
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

Campaign::Campaign(const GameData& data) {
    DataNode campaign = parseDtb(data.read(dtbPath("config/campaign.dta")));
    // (order (basement s1 .. s5) (small_club ...) ...)
    if (const DataNode* order = campaign.find("order"))
        for (size_t k = 1; k < order->children.size(); ++k) {
            const DataNode& v = order->children[k];
            if (!v.isList() || v.children.empty() || !v.children[0].isText()) continue;
            venues_.push_back(v.children[0].s);
            for (size_t n = 1; n < v.children.size(); ++n)
                if (v.children[n].isText()) venueSongs_[v.children[0].s].push_back(v.children[n].s);
        }
    if (const DataNode* r = campaign.find("required_songs"))
        for (size_t k = 1; k < r->children.size(); ++k)
            if (int d = r->children[k].isList() && r->children[k].children.size() > 1 ? diffIndex(r->children[k].children[0].s) : -1; d >= 0)
                requiredSongs_[size_t(d)] = r->children[k].children[1].i;
    if (const DataNode* cash = campaign.find("cash")) {
        if (const DataNode* s = cash->find("starting"); s && s->children.size() > 1) startingCash_ = s->children[1].i;
        for (auto [key, table] : {std::pair{"star_awards", &starAwards_}, std::pair{"status_awards", &statusAwards_}})
            if (const DataNode* t = cash->find(key))
                for (size_t k = 1; k < t->children.size(); ++k) {
                    const DataNode& row = t->children[k];
                    int d = row.isList() && !row.children.empty() ? diffIndex(row.children[0].s) : -1;
                    if (d < 0) continue;
                    for (size_t n = 1; n < row.children.size(); ++n) (*table)[size_t(d)].push_back(row.children[n].i);
                }
    }
    DataNode store = parseDtb(data.read(dtbPath("config/store.dta")));
    for (const DataNode& cat : store.children) {
        if (!cat.isList() || cat.children.empty() || !cat.children[0].isText()) continue;
        for (size_t k = 1; k < cat.children.size(); ++k) {
            const DataNode& it = cat.children[k];
            if (!it.isList() || it.children.empty() || !it.children[0].isText()) continue;
            const DataNode* p = it.find("price");
            prices_[cat.children[0].s][it.children[0].s] = p && p->children.size() > 1 ? p->children[1].i : 0;
            if (cat.children[0].s == "song") storeSongs_.push_back(it.children[0].s);
            if (cat.children[0].s == "video") {
                videos_.push_back(it.children[0].s);
                if (const DataNode* f = it.find("file"); f && f->children.size() > 1) videoFiles_[it.children[0].s] = f->textAt(1);
            }
        }
    }
    DataNode chars = parseDtb(data.read(dtbPath("config/characters.dta")));
    for (const DataNode& c : chars.children)
        if (c.isList() && !c.children.empty() && c.children[0].isText()) characters_.push_back(c.children[0].s);
    DataNode guitars = parseDtb(data.read(dtbPath("config/guitars.dta")));
    for (const DataNode& g : guitars.children) {
        if (!g.isList() || g.children.empty() || !g.children[0].isText()) continue;
        guitars_.push_back(g.children[0].s);
        if (const DataNode* sk = g.find("skins"))
            for (size_t k = 1; k < sk->children.size(); ++k)
                if (sk->children[k].isList() && !sk->children[k].children.empty()) {
                    skins_.push_back(sk->children[k].children[0].s);
                    skinGuitar_[skins_.back()] = g.children[0].s;
                }
    }
    for (int s = 0; s < 8; ++s) profiles_[size_t(s)] = defaultProfile(s);
    profiles_[kGlitchesSlot] = glitchesProfile();
}

// ---------------------------------------------------------------- config

std::vector<std::string> Campaign::songsOf(const std::string& venue) const {
    auto it = venueSongs_.find(venue);
    return it == venueSongs_.end() ? std::vector<std::string>{} : it->second;
}

std::vector<std::string> Campaign::careerSongs(int diff) const {
    std::vector<std::string> out;
    for (int v = 0; v < numVenues(diff) && v < int(venues_.size()); ++v)
        for (const std::string& s : songsOf(venues_[size_t(v)])) out.push_back(s);
    return out;
}

std::string Campaign::venueOf(const std::string& song) const {
    for (const auto& [v, songs] : venueSongs_)
        if (std::find(songs.begin(), songs.end(), song) != songs.end()) return v;
    return "";
}

int Campaign::venueIndex(const std::string& venue) const {
    auto it = std::find(venues_.begin(), venues_.end(), venue);
    return it == venues_.end() ? -1 : int(it - venues_.begin());
}

bool Campaign::isStoreSong(const std::string& song) const {
    return std::find(storeSongs_.begin(), storeSongs_.end(), song) != storeSongs_.end();
}

// FUN_0013b6c8: the last venue of the difficulty needs all 5 songs.
int Campaign::required(int diff, const std::string& venue) const {
    return venueIndex(venue) == numVenues(diff) - 1 ? 5 : requiredSongs_[size_t(diff)];
}

std::string Campaign::nextVenue(const std::string& venue, int diff) const {
    int i = venueIndex(venue);
    return i >= 0 && i + 1 < numVenues(diff) ? venues_[size_t(i + 1)] : "";
}

int Campaign::price(const std::string& category, const std::string& item) const {
    auto c = prices_.find(category);
    if (c == prices_.end()) return 0;
    auto i = c->second.find(item);
    return i == c->second.end() ? 0 : i->second;
}

std::string Campaign::guitarOfSkin(const std::string& skin) const {
    auto it = skinGuitar_.find(skin);
    return it == skinGuitar_.end() ? "" : it->second;
}

// FUN_0013b920: cumulative star money, 3 stars = the first entry.
int Campaign::starCum(int diff, int stars) const {
    int sum = 0;
    const auto& t = starAwards_[size_t(diff)];
    for (int i = 0; i <= stars - 3 && size_t(i) < t.size(); ++i) sum += t[size_t(i)];
    return sum;
}

// The 116 items in order (docs/career.md §1.5).
std::vector<Campaign::Item> Campaign::defaultItems(int) const {
    std::vector<Item> items;
    auto storeHas = [&](const char* cat, const std::string& s) {
        auto c = prices_.find(cat);
        return c != prices_.end() && c->second.count(s);
    };
    for (size_t v = 0; v < venues_.size(); ++v) items.push_back({kVenue, venues_[v], false, v == 0, false});
    for (size_t v = 0; v < venues_.size(); ++v)
        for (const std::string& s : songsOf(venues_[v])) items.push_back({kSong, s, false, v == 0, false});
    for (const std::string& s : storeSongs_) items.push_back({kSong, s, true, false, false});
    for (const std::string& c : characters_) {
        bool st = storeHas("character", c);
        items.push_back({kCharacter, c, st, !st, false});
    }
    for (const std::string& g : guitars_) {
        bool st = storeHas("guitar", g);
        items.push_back({kGuitar, g, st, !st && g != "battleaxe", false});
    }
    for (const std::string& s : skins_) {
        bool st = storeHas("skin", s);
        items.push_back({kSkin, s, st, !st, false});
    }
    for (const std::string& v : videos_) items.push_back({kVideo, v, true, false, true});
    return items;
}

Campaign::Profile Campaign::defaultProfile(int slot) const {
    Profile p;
    p.name = "profile_" + std::to_string(slot);
    for (int d = 0; d < 4; ++d) {
        p.careers[size_t(d)].cash = startingCash_;
        p.careers[size_t(d)].items = defaultItems(d);
    }
    return p;
}

Campaign::Profile Campaign::glitchesProfile() const {
    Profile p = defaultProfile(kGlitchesSlot);
    p.empty = false;
    p.name = "The Glitches";
    p.tutorialsDone = 3;
    p.lastDifficulty = 3;
    for (Career& c : p.careers) {
        c.character = characters_.empty() ? "" : characters_.front();
        c.guitar = guitars_.empty() ? "" : guitars_.front();
        c.skin.clear();
        for (const std::string& s : skins_)
            if (auto g = skinGuitar_.find(s); g != skinGuitar_.end() && g->second == c.guitar) {
                c.skin = s;
                break;
            }
        c.cash = 9999999;
        for (Item& it : c.items) {
            it.unlocked = true;
            if (it.cat == kSong || it.cat == kVenue) it.beaten = true, it.stars = it.cat == kSong ? 5 : 0;
        }
    }
    return p;
}

// ---------------------------------------------------------------- profiles

void Campaign::setSlot(int s) { slot_ = std::clamp(s, 0, kGlitchesSlot); }

int Campaign::numProfiles() const {  // the save slots in use
    return int(std::count_if(profiles_.begin(), profiles_.begin() + 8, [](const Profile& p) { return !p.empty; }));
}

int Campaign::emptySlot() const {
    for (int s = 0; s < 8; ++s)
        if (profiles_[size_t(s)].empty) return s;
    return -1;
}

bool Campaign::hasProfileName(const std::string& name, bool editing) const {
    for (int s = 0; s < 8; ++s) {
        if (profiles_[size_t(s)].empty || (editing && s == slot_)) continue;
        if (lower(profiles_[size_t(s)].name) == lower(name)) return true;
    }
    return false;
}

void Campaign::deleteSlot(int s) {
    if (s == kGlitchesSlot) profiles_[size_t(s)] = glitchesProfile();
    if (s >= 0 && s < 8) profiles_[size_t(s)] = defaultProfile(s);
}

// ---------------------------------------------------------------- careers

Campaign::Item* Campaign::item(int diff, const std::string& sym) {
    for (Item& it : career(diff).items)
        if (it.sym == sym) return &it;
    return nullptr;
}

bool Campaign::isUnlocked(int diff, const std::string& sym) {
    if (allAccess) return true;
    const Item* it = item(diff, sym);
    return it && it->unlocked;
}

bool Campaign::isUnlockedAny(const std::string& sym) {
    for (int d = 0; d < 4; ++d)
        if (isUnlocked(d, sym)) return true;
    return false;
}

int Campaign::beatenInVenue(int diff, const std::string& venue) {
    int n = 0;
    for (const std::string& s : songsOf(venue))
        if (const Item* it = item(diff, s); it && it->beaten) ++n;
    return n;
}

bool Campaign::venueComplete(int diff, const std::string& venue) { return beatenInVenue(diff, venue) >= required(diff, venue); }

int Campaign::completedVenues(int diff) {
    int n = 0;
    for (int v = 0; v < numVenues(diff); ++v)
        if (isUnlocked(diff, venues_[size_t(v)]) && venueComplete(diff, venues_[size_t(v)])) ++n;
    return n;
}

// FUN_0013e7e8 / FUN_0013e940.
int Campaign::status(int diff) {
    bool allFive = true;
    for (const std::string& s : careerSongs(diff))
        if (const Item* it = item(diff, s); !it || it->stars != 5) allFive = false;
    if (diff != 0)
        for (const std::string& s : storeSongs_)
            if (const Item* it = item(diff, s); !it || it->stars != 5) allFive = false;
    return completedVenues(diff) + (allFive ? 1 : 0);
}

int Campaign::songsBeaten(int diff) {
    int n = 0;
    for (const std::string& s : careerSongs(diff))
        if (const Item* it = item(diff, s); it && it->beaten) ++n;
    return n;
}

int Campaign::careerScore(int diff) {
    int sum = 0;
    for (const Item& it : career(diff).items)
        if (it.cat == kSong && it.unlocked) sum += it.score;
    return sum;
}

// FUN_0013eb20.
bool Campaign::finalSong(int diff, const std::string& song) {
    if (isStoreSong(song)) return false;
    std::string v = venueOf(song);
    const Item* it = item(diff, song);
    if (v.empty() || !it || it->beaten) return false;
    return venueIndex(v) == numVenues(diff) - 1 && beatenInVenue(diff, v) == required(diff, v) - 1;
}

// FUN_0013e130.
std::string Campaign::unlockNextVenue(int diff, const std::string& song) {
    if (isStoreSong(song)) return "";
    std::string v = venueOf(song);
    if (v.empty() || !venueComplete(diff, v)) return "";
    std::string nv = nextVenue(v, diff);
    if (nv.empty()) return "";
    Item* nvi = item(diff, nv);
    if (!nvi || nvi->unlocked) return "";
    nvi->unlocked = true;
    for (const std::string& s : songsOf(nv))
        if (Item* it = item(diff, s)) it->unlocked = true;
    return nv;
}

// FUN_0013dd80 (+ the handler FUN_00138ec0).
Campaign::FinishResult Campaign::finishSong(int diff, const std::string& song, int score, const std::function<int(int)>& starsOf) {
    FinishResult r;
    int oldStatus = status(diff);
    std::vector<CashAward> awards;
    Item* it = item(diff, song);
    if (!it) return r;
    if (!it->beaten) {
        it->beaten = true;
        r.first = true;
        std::string nv = unlockNextVenue(diff, song);
        if (!nv.empty()) r.newVenue = nv, r.newStatus = venueIndex(nv);
    }
    if (it->score < score) {
        int oldStars = it->score ? starsOf(it->score) : 0;
        int newStars = starsOf(score);
        it->score = score;
        if (oldStars < newStars) it->stars = newStars;
        int cash = 0;
        if (!isStoreSong(song) && oldStars < newStars) cash = starCum(diff, newStars) - (oldStars ? starCum(diff, oldStars) : 0);
        int newStatus = status(diff);
        bool rose = oldStatus < newStatus;
        if (rose) {
            r.newStatus = newStatus;
            const auto& t = statusAwards_[size_t(diff)];
            // status_awards element index `status` (element 0 is the difficulty symbol).
            if (newStatus >= 1 && size_t(newStatus - 1) < t.size()) cash += t[size_t(newStatus - 1)];
        }
        if (cash > 0) {
            awards.push_back({cash, "award_" + std::to_string(newStars) + "_star" + (rose ? "_status" : "")});
            career(diff).cash += cash;
        }
    }
    awards_.assign(awards.begin(), awards.end());
    if (r.first || !awards.empty()) needsSave = true;
    return r;
}

bool Campaign::nextCashAward(CashAward& out) {
    if (awards_.empty()) return false;
    out = awards_.front();
    awards_.pop_front();
    return true;
}

bool Campaign::buyItem(int diff, const std::string& sym, int price) {
    needsSave = true;  // always (FUN_00138538)
    Item* it = item(diff, sym);
    Career& c = career(diff);
    if (!it || price > c.cash) return false;
    it->unlocked = true;
    c.cash -= price;
    if (it->global)
        for (int d = 0; d < 4; ++d)
            if (Item* g = item(d, sym)) g->unlocked = true;
    return true;
}

void Campaign::unlockGlobalItem(const std::string& sym) {
    for (int d = 0; d < 4; ++d)
        if (Item* it = item(d, sym)) it->unlocked = true;
}

void Campaign::addCash(int diff, int amount) {
    career(diff).cash += amount;
    needsSave = true;
}

// ---------------------------------------------------------------- high scores

const std::vector<Campaign::HighScore>& Campaign::highScores(const std::string& song, int diff) {
    auto& lists = highScores_[song];
    auto& l = lists[size_t(slot_ * 4 + diff)];
    if (l.empty()) l = defaultHighScores();
    return l;
}

// FUN_002a2ad8: the first entry with a lower score (ties go below), -1 past the 5th.
int Campaign::checkHighScore(const std::string& song, int diff, int score) {
    const auto& l = highScores(song, diff);
    for (size_t i = 0; i < l.size() && i < 5; ++i)
        if (l[i].score < score) return int(i);
    return -1;
}

void Campaign::addHighScore(const std::string& song, int diff, const std::string& name, int score) {
    int rank = checkHighScore(song, diff, score);
    if (rank < 0) return;
    auto& l = highScores_[song][size_t(slot_ * 4 + diff)];
    l.insert(l.begin() + rank, {name.substr(0, 8), score});
    if (l.size() > 5) l.resize(5);
    needsSave = true;
}

// ---------------------------------------------------------------- save file

bool Campaign::save(const std::string& path) {
    Json root = Json::object();
    root.set("format", Json::string("gd01-career"));
    root.set("version", Json::number(6));
    Json profiles = Json::array();
    for (int s = 0; s < 8; ++s) {
        const Profile& p = profiles_[size_t(s)];
        Json jp = Json::object();
        jp.set("empty", Json::boolean(p.empty));
        jp.set("name", Json::string(p.name));
        jp.set("tutorials_done", Json::number(p.tutorialsDone));
        jp.set("last_difficulty", Json::string(kDiffNames[std::clamp(p.lastDifficulty, 0, 3)]));
        Json o = Json::object();
        Json lefty = Json::array();
        lefty.a = {Json::boolean(p.options.lefty[0]), Json::boolean(p.options.lefty[1])};
        o.set("lefty", lefty);
        o.set("band_volume", Json::number(p.options.band));
        o.set("guitar_volume", Json::number(p.options.guitar));
        o.set("fx_volume", Json::number(p.options.fx));
        o.set("stereo", Json::boolean(p.options.stereo));
        jp.set("options", o);
        Json careers = Json::object();
        std::vector<Item> defaults = defaultItems(0);
        for (int d = 0; d < 4; ++d) {
            const Career& c = p.careers[size_t(d)];
            Json jc = Json::object();
            jc.set("character", Json::string(c.character));
            jc.set("guitar", Json::string(c.guitar));
            jc.set("skin", Json::string(c.skin));
            jc.set("cash", Json::number(c.cash));
            Json items = Json::object();
            for (size_t i = 0; i < c.items.size(); ++i) {
                const Item& it = c.items[i];
                const Item* def = i < defaults.size() ? &defaults[i] : nullptr;
                if (def && def->unlocked == it.unlocked && !it.beaten && it.score == 0 && it.stars == 0) continue;
                Json ji = Json::object();
                ji.set("unlocked", Json::boolean(it.unlocked));
                if (it.cat == kSong) {
                    ji.set("beaten", Json::boolean(it.beaten));
                    ji.set("score", Json::number(it.score));
                    ji.set("stars", Json::number(it.stars));
                }
                items.set(std::string(kCatNames[it.cat]) + ":" + it.sym, ji);
            }
            jc.set("items", items);
            careers.set(kDiffNames[d], jc);
        }
        jp.set("careers", careers);
        profiles.a.push_back(jp);
    }
    root.set("profiles", profiles);
    Json hs = Json::object();
    for (const auto& [song, lists] : highScores_) {
        Json slots = Json::array();
        for (int s = 0; s < 8; ++s) {
            Json diffs = Json::array();
            for (int d = 0; d < 4; ++d) {
                Json entries = Json::array();
                const auto& l = lists[size_t(s * 4 + d)].empty() ? defaultHighScores() : lists[size_t(s * 4 + d)];
                for (const HighScore& h : l) {
                    Json e = Json::object();
                    e.set("name", Json::string(h.name));
                    e.set("score", Json::number(h.score));
                    entries.a.push_back(e);
                }
                diffs.a.push_back(entries);
            }
            slots.a.push_back(diffs);
        }
        hs.set(song, slots);
    }
    root.set("highscores", hs);
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << root.dump(1) << "\n";
        if (!f) return false;
    }
    std::remove(path.c_str());
    if (std::rename(tmp.c_str(), path.c_str()) != 0) return false;
    needsSave = false;
    return true;
}

bool Campaign::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    // Every property is checked: one of the wrong type or out of range keeps
    // its default (the file may come from Enhanced Settings > Load User Progress).
    auto boolOr = [](const Json& j, bool def) { return j.type == Json::Type::Bool ? j.b : def; };
    auto numOr = [](const Json& j, double def, double lo, double hi) {
        return j.type == Json::Type::Number && std::isfinite(j.n) && j.n >= lo && j.n <= hi ? j.n : def;
    };
    auto symOr = [](const Json& j, const std::string& def) {  // a symbol: a-z, 0-9, _
        if (j.type != Json::Type::String || j.s.size() > 64) return def;
        for (char c : j.s)
            if (!(std::islower(uint8_t(c)) || std::isdigit(uint8_t(c)) || c == '_')) return def;
        return j.s;
    };
    try {
        Json root = Json::parse(ss.str());
        if (root.type != Json::Type::Object || (root["format"].str() != "gd01-career" && root["format"].str() != "gh1re-career")) return false;
        const Json& profiles = root["profiles"];
        for (size_t s = 0; s < 8 && profiles.type == Json::Type::Array && s < profiles.a.size(); ++s) {
            const Json& jp = profiles.a[s];
            Profile p = defaultProfile(int(s));
            if (jp.type != Json::Type::Object) {
                profiles_[s] = std::move(p);
                continue;
            }
            p.empty = boolOr(jp["empty"], true);
            if (jp["name"].type == Json::Type::String && jp["name"].s.size() <= 32) p.name = jp["name"].s;
            p.tutorialsDone = int(numOr(jp["tutorials_done"], 0, 0, 3));
            for (int d = 0; d < 4; ++d)
                if (jp["last_difficulty"].str() == kDiffNames[d]) p.lastDifficulty = d;
            const Json& o = jp["options"];
            if (o["lefty"].type == Json::Type::Array && o["lefty"].a.size() == 2)
                p.options.lefty[0] = boolOr(o["lefty"].a[0], false), p.options.lefty[1] = boolOr(o["lefty"].a[1], false);
            p.options.band = float(numOr(o["band_volume"], 1, 0, 1));
            p.options.guitar = float(numOr(o["guitar_volume"], 1, 0, 1));
            p.options.fx = float(numOr(o["fx_volume"], 1, 0, 1));
            p.options.stereo = boolOr(o["stereo"], true);
            for (int d = 0; d < 4; ++d) {
                const Json& jc = jp["careers"][kDiffNames[d]];
                Career& c = p.careers[size_t(d)];
                // A character, guitar or skin must be one the game has.
                auto known = [&](Category cat, const std::string& sym) {
                    for (const Item& it : c.items)
                        if (it.cat == cat && it.sym == sym) return true;
                    return false;
                };
                if (std::string v = symOr(jc["character"], ""); !v.empty() && known(kCharacter, v)) c.character = v;
                if (std::string v = symOr(jc["guitar"], ""); !v.empty() && known(kGuitar, v)) c.guitar = v;
                if (std::string v = symOr(jc["skin"], ""); !v.empty() && known(kSkin, v)) c.skin = v;
                c.cash = int(numOr(jc["cash"], startingCash_, 0, 99999999));
                for (Item& it : c.items) {
                    const Json& ji = jc["items"][std::string(kCatNames[it.cat]) + ":" + it.sym];
                    if (ji.type != Json::Type::Object) continue;
                    it.unlocked = boolOr(ji["unlocked"], it.unlocked);
                    it.beaten = boolOr(ji["beaten"], false);
                    it.score = int(numOr(ji["score"], 0, 0, 99999999));
                    it.stars = int(numOr(ji["stars"], 0, 0, 5));
                }
            }
            profiles_[s] = std::move(p);
        }
        if (root["highscores"].type == Json::Type::Object)
            for (const auto& [song, slots] : root["highscores"].o)
                for (size_t s = 0; s < 8 && slots.type == Json::Type::Array && s < slots.a.size(); ++s)
                    for (size_t d = 0; d < 4 && slots.a[s].type == Json::Type::Array && d < slots.a[s].a.size(); ++d) {
                        std::vector<HighScore> l;
                        if (slots.a[s].a[d].type != Json::Type::Array) continue;
                        for (const Json& e : slots.a[s].a[d].a) {
                            if (e.type != Json::Type::Object || e["name"].type != Json::Type::String || e["name"].s.size() > 32) continue;
                            int score = int(numOr(e["score"], -1, 0, 99999999));
                            if (score >= 0) l.push_back({e["name"].s, score});
                        }
                        if (l.size() > 5) l.resize(5);
                        highScores_[song][s * 4 + d] = l;
                    }
    } catch (const std::exception& ex) {
        SDL_Log("career save %s: %s", path.c_str(), ex.what());
        return false;
    }
    needsSave = false;
    return true;
}
