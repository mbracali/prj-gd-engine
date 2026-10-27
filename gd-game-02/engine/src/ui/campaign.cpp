// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// GH2's career rules (ui/campaign.h, docs/career_and_store.md).
#include "ui/campaign.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <set>

#include "scene/ui_styles.h"
#include "ui/char_select.h"
#include "ui/natives.h"
#include "ui/ui_manager.h"

namespace ui::campaign {

namespace {

const std::vector<std::string> kNone;

int difficultyOf(const DataNode& key) {
    if (key.type == DataNode::Type::Int) return std::clamp(int(key.i), 0, Career::kDifficulties - 1);
    static const char* names[] = {"kDifficultyEasy", "kDifficultyMedium", "kDifficultyHard", "kDifficultyExpert"};
    for (int d = 0; d < Career::kDifficulties; ++d)
        if (key.isText() && key.s == names[d]) return d;
    return -1;
}

std::string firstText(const DataNode& n) { return n.isList() && !n.children.empty() && n.children[0].isText() ? n.children[0].s : std::string(); }

bool isCareer(UiManager& ui) { return gameCfgValue(ui, "mode").str() == "career"; }

bool contains(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

}  // namespace

const Config& config(UiManager& ui) {
    static Config c;
    static bool loaded = false;
    if (loaded) return c;
    loaded = true;
    const DataNode* boot = ui.bootTree();
    if (const DataNode* camp = boot ? boot->find("campaign") : nullptr) {
        if (const DataNode* order = camp->find("order"))
            for (size_t k = 1; k < order->children.size(); ++k) {
                const DataNode& t = order->children[k];
                if (firstText(t).empty()) continue;
                std::vector<std::string> songs;
                for (size_t s = 1; s < t.children.size(); ++s)
                    if (t.children[s].isText()) songs.push_back(t.children[s].s);
                c.tiers.push_back({t.children[0].s, songs});
            }
        if (const DataNode* req = camp->find("required_songs"))
            for (size_t k = 1; k < req->children.size(); ++k) {
                const DataNode& r = req->children[k];
                const int d = r.isList() && !r.children.empty() ? difficultyOf(r.children[0]) : -1;
                if (d >= 0 && r.children.size() > 2) c.required[size_t(d)] = {r.intAt(1), r.intAt(2)};
            }
        if (const DataNode* cash = camp->find("cash")) {
            if (const DataNode* st = cash->find("starting"); st && st->children.size() > 1) c.startingCash = st->intAt(1);
            if (const DataNode* sa = cash->find("star_awards"))
                for (size_t k = 1; k < sa->children.size(); ++k) {
                    const DataNode& r = sa->children[k];
                    const int d = r.isList() && !r.children.empty() ? difficultyOf(r.children[0]) : -1;
                    for (size_t i = 1; d >= 0 && i < r.children.size() && i <= 3; ++i) c.starAwards[size_t(d)][i - 1] = r.intAt(i);
                }
            if (const DataNode* sa = cash->find("status_awards"))
                for (size_t k = 1; k < sa->children.size(); ++k) {
                    const DataNode& r = sa->children[k];
                    const int d = r.isList() && !r.children.empty() ? difficultyOf(r.children[0]) : -1;
                    for (size_t i = 1; d >= 0 && i < r.children.size(); ++i) c.statusAwards[size_t(d)].push_back(r.intAt(i));
                }
        }
    }
    if (const DataNode* store = boot ? boot->find("store") : nullptr)
        for (size_t k = 1; k < store->children.size(); ++k) {
            const DataNode& cat = store->children[k];
            if (firstText(cat).empty()) continue;
            auto& items = c.store[cat.children[0].s];
            for (size_t i = 1; i < cat.children.size(); ++i) {
                const DataNode& it = cat.children[i];
                if (firstText(it).empty()) continue;
                const DataNode* pr = it.find("price");
                items.push_back({it.children[0].s, pr && pr->children.size() > 1 ? pr->intAt(1) : 0});
            }
        }
    for (const auto& [item, price] : c.store["song"]) c.storeSongs.push_back(item);
    if (const DataNode* d = boot ? boot->find("downloads") : nullptr)
        for (size_t k = 1; k < d->children.size(); ++k) {
            if (d->children[k].isText()) c.downloads.push_back(d->children[k].s);
            else if (!firstText(d->children[k]).empty()) c.downloads.push_back(d->children[k].children[0].s);
        }
    // The profile's unlock table (0x822A88C8): guitars (store.dta's are
    // sold, 0x822A7B30), skins (0x822A7C60), the characters and their
    // outfits (0x822A7DD8), the store's videos (sold, 0x822A7FA8).
    const auto sold = [&](const char* cat, const std::string& item) {
        for (const auto& [i, p] : c.store[cat])
            if (i == item) return true;
        return false;
    };
    const chars::Data& cd = chars::data(ui);
    for (const chars::Guitar& g : cd.guitars) {
        c.tableItems[g.name] = sold("guitar", g.name);
        for (const chars::Skin& s : g.skins) c.tableItems[s.name] = sold("skin", s.name);
    }
    for (const auto& [ch, outfits] : cd.characters) {
        c.tableItems[ch] = sold("character", ch) || sold("outfit", ch);
        for (const std::string& o : outfits) c.tableItems[o] = sold("character", o) || sold("outfit", o);
    }
    for (const auto& [v, p] : c.store["video"]) c.tableItems[v] = true;
    // guitars.dta's tour rewards (type guitar, require, difficulty).
    try {
        const DataNode gs = scene::loadPlainDta(ui.data(), "config/gen/guitars.dtb");
        for (const DataNode& g : gs.children) {
            if (firstText(g).empty()) continue;
            const DataNode* type = g.find("type");
            const DataNode* req = g.find("require");
            const DataNode* diff = g.find("difficulty");
            // (require (songs n) (stars s)): co-op's bass rewards.
            if (const DataNode* songs = req ? req->find("songs") : nullptr) {
                const DataNode* stars = req->find("stars");
                c.coopRewards.push_back({g.children[0].s, songs->intAt(1), stars ? stars->intAt(1) : 3});
                continue;
            }
            if (!type || type->textAt(1) != "guitar" || !req || req->children.size() < 2 || !req->children[1].isText() || !diff) continue;
            const std::string r = req->children[1].s;
            if (r != "tour_passed" && r != "tour_5_star") continue;
            const int d = diff->children.size() > 1 ? difficultyOf(diff->children[1]) : -1;
            if (d >= 0) c.rewards.push_back({g.children[0].s, r == "tour_5_star", d});
        }
    } catch (const std::exception& e) {
        SDL_Log("campaign: guitars.dtb: %s", e.what());
    }
    return c;
}

Career::Profile* profile(UiManager& ui) {
    const int s = ui.session().profileSlot;
    return s >= 0 && s < Career::kProfiles ? &ui.career().profiles[size_t(s)] : nullptr;
}

int difficulty(UiManager& ui) { return std::clamp(ui.session().players[0].difficulty, 0, Career::kDifficulties - 1); }

std::map<std::string, Career::Entry>& record(UiManager& ui, Career::Profile& p, int d) {
    auto& rec = p.entries[size_t(std::clamp(d, 0, Career::kDifficulties - 1))];
    const Config& c = config(ui);
    const auto init = [&](const std::string& sym, bool unlocked) {
        Career::Entry& e = rec[sym];
        if (!e.known) e.known = true, e.unlocked = unlocked;
    };
    // 0x822A9E90: the venues, the first unlocked; 0x822A9FA0: their songs
    // (the first venue's unlocked, the last of each the encore), then the
    // store's songs (locked).
    for (size_t v = 0; v < c.tiers.size(); ++v) init(c.tiers[v].first, v == 0);
    for (size_t v = 0; v < c.tiers.size(); ++v)
        for (const std::string& s : c.tiers[v].second) init(s, v == 0);
    for (const std::string& s : c.storeSongs) init(s, false);
    return rec;
}

int numTiers(UiManager& ui, int d) {
    const int n = int(config(ui).tiers.size());
    return d == 0 ? n - 1 : n;
}

int venueIndex(UiManager& ui, const std::string& venue) {
    const auto& t = config(ui).tiers;
    for (size_t i = 0; i < t.size(); ++i)
        if (t[i].first == venue) return int(i);
    return -1;
}

std::string venueOf(UiManager& ui, const std::string& song) {
    for (const auto& [v, songs] : config(ui).tiers)
        if (contains(songs, song)) return v;
    return {};
}

const std::vector<std::string>& songsOf(UiManager& ui, const std::string& venue) {
    for (const auto& [v, songs] : config(ui).tiers)
        if (v == venue) return songs;
    return kNone;
}

std::string encoreOf(UiManager& ui, const std::string& venue) {
    const auto& s = songsOf(ui, venue);
    return s.empty() ? std::string() : s.back();
}

bool isStoreSong(UiManager& ui, const std::string& song) { return contains(config(ui).storeSongs, song); }
bool isDownload(UiManager& ui, const std::string& song) { return contains(config(ui).downloads, song); }

bool isEncore(UiManager& ui, const std::string& song) {
    for (const auto& [v, songs] : config(ui).tiers)
        if (!songs.empty() && songs.back() == song) return true;
    return false;
}

int requiredSongs(UiManager& ui, int d, const std::string& venue) {
    const auto& r = config(ui).required[size_t(std::clamp(d, 0, Career::kDifficulties - 1))];
    return venueIndex(ui, venue) < numTiers(ui, d) - 1 ? r[0] : r[1];
}

std::vector<std::string> allSongs(UiManager& ui) {
    const Config& c = config(ui);
    std::vector<std::string> out;
    for (const auto& [v, songs] : c.tiers) out.insert(out.end(), songs.begin(), songs.end());
    out.insert(out.end(), c.storeSongs.begin(), c.storeSongs.end());
    out.insert(out.end(), c.downloads.begin(), c.downloads.end());
    return out;
}

bool passed(UiManager& ui, Career::Profile& p, int d, const std::string& sym) {
    auto& rec = record(ui, p, d);
    auto it = rec.find(sym);
    if (it == rec.end()) return false;
    // A venue (type 0): completed; a song (type 1): three stars or more.
    return venueIndex(ui, sym) >= 0 ? it->second.completed : it->second.stars >= 3;
}

int numPassed(UiManager& ui, Career::Profile& p, int d, const std::string& venue) {
    int n = 0;
    for (const std::string& s : songsOf(ui, venue)) n += passed(ui, p, d, s) ? 1 : 0;
    return n;
}

// 0x822A9578: an encore is unlocked once its venue's other songs are passed.
static bool encoreUnlocked(UiManager& ui, Career::Profile& p, int d, const std::string& song) {
    const std::string v = venueOf(ui, song);
    return numPassed(ui, p, d, v) >= requiredSongs(ui, d, v) - 1;
}

bool unlockedAt(UiManager& ui, Career::Profile& p, int d, const std::string& sym) {
    if (!isEncore(ui, sym)) {
        auto& rec = record(ui, p, d);
        auto it = rec.find(sym);
        return it != rec.end() && it->second.unlocked;
    }
    if (d == 0) return false;  // easy has no encores
    return encoreUnlocked(ui, p, d, sym);
}

int tiersCompleted(UiManager& ui, Career::Profile& p, int d) {
    auto& rec = record(ui, p, d);
    int n = 0;
    const auto& t = config(ui).tiers;
    for (int i = 0; i < numTiers(ui, d) && i < int(t.size()); ++i) {
        const Career::Entry& e = rec[t[size_t(i)].first];
        n += e.unlocked && e.completed ? 1 : 0;
    }
    return n;
}

bool allFiveStars(UiManager& ui, Career::Profile& p, int d) {
    auto& rec = record(ui, p, d);
    const auto& t = config(ui).tiers;
    for (int i = 0; i < numTiers(ui, d) && i < int(t.size()); ++i)
        for (const std::string& s : t[size_t(i)].second)
            if ((!isEncore(ui, s) || d != 0) && rec[s].stars != 5) return false;
    return true;
}

int status(UiManager& ui, Career::Profile& p, int d) { return tiersCompleted(ui, p, d) + (allFiveStars(ui, p, d) ? 1 : 0); }

bool inTable(UiManager& ui, const std::string& sym) { return config(ui).tableItems.count(sym) != 0; }

// 0x822A7218: a band's item at difficulty `d` (its table, else its record).
static bool profileUnlocked(UiManager& ui, Career::Profile& p, const std::string& sym, int d) {
    const Config& c = config(ui);
    if (auto it = c.tableItems.find(sym); it != c.tableItems.end()) {
        if (!it->second) return true;  // not sold: unlocked from the start
        auto b = p.items.find(sym);
        return b != p.items.end() && b->second;
    }
    return unlockedAt(ui, p, d, sym);
}

bool isCoopReward(UiManager& ui, const std::string& guitar) {
    for (const Config::CoopReward& r : config(ui).coopRewards)
        if (r.guitar == guitar) return true;
    return false;
}

const Career::CoopEntry* coopEntry(UiManager& ui, const std::string& song) {
    auto it = ui.career().coop.find(song);
    return it == ui.career().coop.end() ? nullptr : &it->second;
}

bool finishCoopSong(UiManager& ui, int bandScore, int bandStars, int percent0, int percent1, int diff0, int diff1) {
    Career& c = ui.career();
    ui.session().guitarAwards.clear();  // Campaign +0x7c emptied
    const std::string song = ui.session().song;
    Career::CoopEntry& e = c.coop[song];
    // 0x822A6848: a higher score keeps it (0x822AE8A0) and its stars
    // (0x822AE8B8: never fewer, the gold flag kept), then the rewards.
    bool changed = e.score < bandScore;
    if (changed) {
        e.score = std::max(e.score, bandScore);
        e.stars = std::max(e.stars, bandStars);
        // 0x822A5FF0: the entries with 3, 4, 5 stars or more; a reward is
        // awarded once its (songs n) entries have its (stars s) [C].
        std::array<int, 3> count{};
        for (const auto& [s, x] : c.coop)
            for (int k = 0; k < 3; ++k) count[size_t(k)] += x.stars >= k + 3 ? 1 : 0;
        for (const Config::CoopReward& r : config(ui).coopRewards) {
            if (auto it = c.coopAwards.find(r.guitar); it != c.coopAwards.end() && it->second) continue;
            if (r.songs <= count[size_t(std::clamp(r.stars, 3, 5) - 3)]) {
                c.coopAwards[r.guitar] = true;
                ui.session().guitarAwards.push_back(r.guitar);
            }
        }
    }
    if (percent0 == 100 && percent1 == 100 && diff0 == 3 && diff1 == 3) e.perfect = true, changed = true;
    if (changed) c.touch();
    return changed;
}

bool isUnlocked(UiManager& ui, const std::string& sym) {
    if (ui.session().allAccess) return true;
    if (sym == "multi_fo") return ui.career().proFaceOff;
    // Co-op's bass rewards: locked until co-op awards them [I: the reward
    // entry's bit 0x40 (0x822A66E0) as the guitar's unlock].
    if (isCoopReward(ui, sym)) {
        auto it = ui.career().coopAwards.find(sym);
        return it != ui.career().coopAwards.end() && it->second;
    }
    if (isCareer(ui)) {
        Career::Profile* p = profile(ui);
        return p && profileUnlocked(ui, *p, sym, difficulty(ui));  // 0x822A7150
    }
    // Any band (empty slots too: their defaults) at any difficulty.
    for (int d = 0; d < Career::kDifficulties; ++d)
        for (Career::Profile& p : ui.career().profiles)
            if (profileUnlocked(ui, p, sym, d)) return true;
    return false;
}

// 0x822A9BB0: a song that completes its venue unlocks the next one.
static std::string completeVenue(UiManager& ui, Career::Profile& p, int d, const std::string& song) {
    if (isStoreSong(ui, song) || isDownload(ui, song) || (d != 0 && !isEncore(ui, song))) return {};
    const std::string v = venueOf(ui, song);
    if (v.empty()) return {};
    auto& rec = record(ui, p, d);
    if (requiredSongs(ui, d, v) <= numPassed(ui, p, d, v) && (d == 0 || passed(ui, p, d, encoreOf(ui, v)))) rec[v].completed = true;
    if (!rec[v].completed) return {};
    // 0x822A3010: the next venue, none after the difficulty's last tier.
    const int i = venueIndex(ui, v);
    if (i < 0 || i == numTiers(ui, d) - 1 || i + 1 >= int(config(ui).tiers.size())) return {};
    const std::string next = config(ui).tiers[size_t(i + 1)].first;
    if (unlockedAt(ui, p, d, next)) return {};
    // 0x822A9088: the venue and its songs unlocked.
    rec[next].unlocked = true;
    for (const std::string& s : songsOf(ui, next)) rec[s].unlocked = true;
    return next;
}

static int starAward(UiManager& ui, int d, int stars) {
    if (stars < 3) return 0;
    return config(ui).starAwards[size_t(d)][size_t(std::min(stars, 5) - 3)];
}

static int statusAward(UiManager& ui, int d, int status) {
    const auto& a = config(ui).statusAwards[size_t(d)];
    return status >= 1 && status <= int(a.size()) ? a[size_t(status - 1)] : 0;
}

FinishResult finishSong(UiManager& ui, int score, int stars, int percentHit) {
    FinishResult r;
    ui.session().guitarAwards.clear();
    Career::Profile* pp = profile(ui);
    if (!pp) return r;
    Career::Profile& p = *pp;
    const std::string song = ui.session().song;
    const int cur = difficulty(ui);
    p.lastDifficulty = cur;  // profile +0x2c = the game's difficulty
    const int old = status(ui, p, cur);
    // 0x822AA598 on each record up to the difficulty played.
    for (int d = 0; d <= cur; ++d) {
        auto& rec = record(ui, p, d);
        Career::Entry& e = rec[song];
        e.known = true;
        if (d == cur) {
            const int before = status(ui, p, d);
            if (e.score < score) {
                e.score = score;
                r.changed = true;
                if (stars >= e.stars) e.stars = stars;  // 0x822AE8B8
            }
            r.newVenue = completeVenue(ui, p, d, song);
            for (int s = before + 1; s <= status(ui, p, d); ++s) r.newStatus = s, r.statusAward += statusAward(ui, d, s);
            char reason[32];
            std::snprintf(reason, sizeof reason, "award_%d_star", stars);
            r.cashReason = reason;
            if (percentHit == 100) e.perfect = true, r.changed = true;
        }
        if (e.maxStars < stars) {
            const int was = e.maxStars;
            e.maxStars = stars;
            if (!isStoreSong(ui, song) && !isDownload(ui, song))
                for (int s = was + 1; s <= stars; ++s) r.cash += starAward(ui, d, s);
        }
    }
    p.cash += r.cash;
    if (r.newStatus != 0) {
        // The venue played: none of the status cash when a harder
        // difficulty completed it, else the easier ones' too until one did.
        const std::string venue = ui.session().venue;
        bool harder = false;
        for (int d = cur + 1; d < Career::kDifficulties; ++d)
            if (passed(ui, p, d, venue)) {
                harder = true, r.statusAward = 0;
                break;
            }
        if (!harder)
            for (int d = cur - 1; d >= 0; --d) {
                if (passed(ui, p, d, venue)) break;
                if (numTiers(ui, d) >= old + 1) r.statusAward += statusAward(ui, d, old + 1);
            }
    }
    p.cash += r.statusAward;
    // The tour's guitars (guitars.dta (require tour_passed | tour_5_star)).
    const int now = status(ui, p, cur), nt = numTiers(ui, cur);
    const bool tourPassed = nt > old && now >= nt, fiveStar = now > old && now == nt + 1;
    for (const Config::Reward& g : config(ui).rewards)
        if (g.difficulty == cur && ((!g.fiveStar && tourPassed) || (g.fiveStar && fiveStar))) ui.session().guitarAwards.push_back(g.guitar);
    // 0x82286C28: pro face-off once a tour is won.
    if (r.changed && int(config(ui).tiers.size()) <= now) ui.career().proFaceOff = true;
    ui.career().touch();  // L12 (GH2 marks the save when the result changed)
    return r;
}

void cheatBeatSong(UiManager& ui, const std::string& song, int score) {
    Career::Profile* p = profile(ui);
    if (!p) return;
    const int d = difficulty(ui);
    Career::Entry& e = record(ui, *p, d)[song];
    e.known = true;
    if (!passed(ui, *p, d, song)) e.stars = std::max(e.stars, 3);  // 0x822AE940
    if (e.score < score) {
        e.score = score;
        const int stars = score > 999999 ? 5 : 3;
        if (stars >= e.stars) e.stars = stars;
    }
    completeVenue(ui, *p, d, song);
    ui.career().touch();
}

void buyItem(UiManager& ui, const std::string& item, int price) {
    Career::Profile* p = profile(ui);
    if (!p || price > p->cash) return;
    if (inTable(ui, item)) p->items[item] = true;
    else
        for (int d = 0; d < Career::kDifficulties; ++d) record(ui, *p, d)[item].unlocked = true;
    p->cash -= price;
    p->cashSpent += price;
    ui.career().touch();
}

int price(UiManager& ui, const std::string& category, const std::string& item) {
    const auto& s = config(ui).store;
    auto it = s.find(category);
    if (it == s.end()) return -1;
    for (const auto& [i, pr] : it->second)
        if (i == item) return pr;
    return -1;
}

std::vector<std::string> careerSongs(UiManager& ui, bool storeMode) {
    std::vector<std::string> out;
    Career::Profile* p = profile(ui);
    if (!p) return out;
    const int d = difficulty(ui);
    auto& rec = record(ui, *p, d);
    const bool all = ui.session().allAccess;
    const Config& c = config(ui);
    // 0x822AEA68: the record's songs in order, sold ones in store mode, else
    // the unlocked ones (all with the cheat).
    for (const auto& [v, songs] : c.tiers)
        for (const std::string& s : songs)
            if (!storeMode && (all || rec[s].unlocked)) out.push_back(s);
    for (const std::string& s : c.storeSongs)
        if (storeMode || all || rec[s].unlocked) out.push_back(s);
    // 0x822AA3F8: before the tour is complete, the newest venue's songs only
    // when that venue (or a later one) is the one chosen.
    if (tiersCompleted(ui, *p, d) != numTiers(ui, d)) {
        const int st = status(ui, *p, d);
        if (venueIndex(ui, ui.session().venue) < st && st < int(c.tiers.size())) {
            const auto& hide = c.tiers[size_t(st)].second;
            out.erase(std::remove_if(out.begin(), out.end(), [&](const std::string& s) { return contains(hide, s); }), out.end());
        }
    }
    return out;
}

std::vector<std::string> quickplaySongs(UiManager& ui) {
    const Config& c = config(ui);
    std::set<std::string> offered;
    for (size_t t = 0; t < c.tiers.size() && t < 2; ++t) offered.insert(c.tiers[t].second.begin(), c.tiers[t].second.end());
    for (Career::Profile& p : ui.career().profiles) {
        if (!p.inUse) continue;
        for (int d = 0; d < Career::kDifficulties; ++d)
            for (const auto& [sym, e] : record(ui, p, d))  // 0x822AA920: the unlocked songs
                if (e.unlocked && venueIndex(ui, sym) < 0) offered.insert(sym);
    }
    std::vector<std::string> out;
    for (const std::string& s : allSongs(ui))
        if (ui.session().allAccess || offered.count(s) || isDownload(ui, s)) out.push_back(s);
    return out;
}

void setCareerVenue(UiManager& ui) {
    Session& s = ui.session();
    const auto& t = config(ui).tiers;
    if (isUnlocked(ui, s.venue)) {
        Career::Profile* p = profile(ui);
        const int d = difficulty(ui);
        const bool won = p && status(ui, *p, d) >= numTiers(ui, d);  // won_campaign (0x82285730)
        if (!isCareer(ui) || won) return;
    }
    int last = std::max(0, venueIndex(ui, s.venue));
    for (size_t i = 0; i < t.size(); ++i) {
        if (!isUnlocked(ui, t[i].first)) break;
        last = int(i);
    }
    if (!t.empty()) s.venue = t[size_t(last)].first;
}

void setCareerSong(UiManager& ui) {
    Session& s = ui.session();
    Career::Profile* p = profile(ui);
    const int d = difficulty(ui);
    const bool won = p && status(ui, *p, d) >= numTiers(ui, d);
    if (won && isUnlocked(ui, s.song)) return;
    const auto& songs = songsOf(ui, s.venue);
    if (songs.empty()) return;
    size_t pick = 0;
    for (size_t i = 0; i < songs.size(); ++i) {
        if (!isUnlocked(ui, songs[i])) continue;
        pick = i;
        if (!(p && passed(ui, *p, d, songs[i]))) break;
    }
    s.song = songs[pick];
}

bool wantEncoreFx(UiManager& ui) {
    if (!gameCfgValue(ui, "encore_fx").truthy()) return false;
    const Session& s = ui.session();
    return s.mode != "tutorial" && isEncore(ui, s.song) && isUnlocked(ui, encoreOf(ui, s.venue));
}

}  // namespace ui::campaign
