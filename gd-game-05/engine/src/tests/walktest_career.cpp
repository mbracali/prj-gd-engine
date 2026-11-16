// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Career walks' checks (GD05-118 / GD05-119, the career rundown of
// 2026-11-16): a test aid that completes the tours' gigs through the tour
// objects' own natives, as RB's endgame scripts call them (ui/endgame.dta:
// complete_song <stars> <score> per song, then complete_event, then the
// hints' advance), so a walk steps through every tier and town in seconds;
// and the expectations recomputed from the disc's data alone
// (config/tour_solo.dta, config/tour_band.dta, songs.dtb's ranks). The
// gigs the walks must see end the tour are left for the menus.
#include "tests/walktest.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "data/dta.h"
#include "render/ui_view.h"
#include "game/song_db.h"
#include "script/script.h"
#include "ui/natives.h"
#include "ui/ui_host.h"
#include "ui/ui_manager.h"

namespace {

int intOf(const DataNode& n) { return n.type == DataNode::Type::Int ? n.i : n.type == DataNode::Type::Float ? int(n.f) : 0; }

std::vector<std::string> rankedList(ui::UiManager& ui, const std::string& inst, bool bonus) {
    std::vector<const SongInfo*> v;
    for (const SongInfo& si : ui.host().songs())
        if (!si.tutorial && si.unlockable == bonus && si.rank.count(inst)) v.push_back(&si);
    std::stable_sort(v.begin(), v.end(), [&](const SongInfo* a, const SongInfo* b) { return a->rank.at(inst) < b->rank.at(inst); });
    std::vector<std::string> out;
    for (const SongInfo* si : v) out.push_back(si->id);
    return out;
}

struct Ctx {
    ui::UiManager& ui;
    Interp& in;
    int rewardBefore = -1;  // the band's last gig's reward, asked before complete_event
    Value send(ScriptObject* o, const char* msg, std::vector<Value> a = {}) { return o ? in.send(o, msg, a) : Value::integer(0); }
    ui::ListProvider* list(const char* name) {
        auto* l = dynamic_cast<ui::ListProvider*>(in.findObject(name));
        if (l) in.send(l, "refresh", {});
        return l;
    }
    std::vector<std::string> rows(const char* name) {
        std::vector<std::string> v;
        if (ui::ListProvider* l = list(name))
            for (int i = 0; i < l->numData(); ++i) v.push_back(l->symbol(i));
        return v;
    }
    // One gig as endgame.dta finishes it: each set song completed with
    // `stars`, the event completed, the hints' advance run.
    void playEvent(ScriptObject* perf, int stars, std::vector<std::string>* songs, bool band) {
        send(perf, "set_random_songs");
        for (int guard = 0; guard < 80; ++guard) {
            std::string s = send(perf, "next_song").str();
            if (s.empty()) break;
            if (s == "any") {  // a set the band makes (diy, rockaid): the band's first ranked song not in it yet
                for (const std::string& id : rankedList(ui, "band", false))
                    if (!songs || std::find(songs->begin(), songs->end(), id) == songs->end()) {
                        s = id;
                        break;
                    }
                send(perf, "set_any_song", {Value::symbol(s)});
            }
            if (songs) songs->push_back(s);
            send(perf, "complete_song", {Value::integer(stars), Value::integer(100000)});
        }
        if (band) rewardBefore = send(perf, "reward").integerValue();
        send(perf, "complete_event");
        if (band) {
            send(perf, "clear_unlocked_item");
        } else {
            if (!send(perf, "new_unlock").str().empty()) send(perf, "advance_new_unlock");
            const std::string t = send(perf, "new_town").str();
            if (!t.empty()) send(perf, "add_seen_town", {Value::symbol(t)});
        }
    }
};

const char* kDiffSyms[4] = {"kDifficultyEasy", "kDifficultyMedium", "kDifficultyHard", "kDifficultyExpert"};

// ------------------------------------------------------------ Solo Tour

// rb_full_solo_autoplay: every open gig of the performer's Solo Tour at its
// difficulty, in the gig guide's order, but the one that wins the tour
// (is_winning, RB 0x823E47E0) which the walk plays through the menus.
// Expected from config/tour_solo.dta alone: the towns whose first venue
// has (songs N) events open in the data's order (N up to max_song of the
// difficulty), each event playing the instrument's Nth ranked song; the
// bonus venue's (unlock N) gigs each unlocking the next bonus song; the
// last of them wins.
bool soloAutoplay(Ctx& c, std::string& err) {
    ScriptObject* perf = c.in.send("tour", "performer", {}).object;
    if (!perf) return err = "no tour performer", false;
    const DataNode* tour = c.ui.bootTree() ? c.ui.bootTree()->find("tour") : nullptr;
    const DataNode* solo = tour ? tour->find("solo") : nullptr;
    const DataNode* towns = solo ? solo->find("towns") : nullptr;
    if (!towns) return err = "no (tour (solo (towns ...)))", false;
    const int diff = c.send(perf, "solo_difficulty").integerValue();
    const std::string inst = c.send(perf, "get_instrument").str();
    int maxSong = 45;
    if (const DataNode* ms = solo->find("max_song"))
        for (size_t i = 1; i < ms->children.size(); ++i) {
            const DataNode& m = ms->children[i];
            const int d = m.children[0].type == DataNode::Type::Int ? m.children[0].i : int(std::find(kDiffSyms, kDiffSyms + 4, m.textAt(0)) - kDiffSyms);
            if (d == diff) maxSong = m.intAt(1);
        }
    const std::vector<std::string> ranked = rankedList(c.ui, inst, false), bonus = rankedList(c.ui, inst, true);
    // The data's events: (town, event) -> song number and kind.
    struct Ev {
        std::string town, name;
        int number = 0;
        bool unlock = false;
    };
    std::vector<Ev> data;
    std::vector<std::string> normalTowns;  // towns whose first venue plays (songs N) events open at the difficulty
    int bonusEvents = 0;
    for (size_t t = 1; t < towns->children.size(); ++t) {
        const DataNode& tn = towns->children[t];
        if (!tn.isList() || tn.children.empty()) continue;
        bool firstVenue = true, normalOpen = false;
        for (size_t v = 1; v < tn.children.size(); ++v) {
            const DataNode& vn = tn.children[v];
            if (!vn.isList()) continue;
            for (size_t e = 1; e < vn.children.size(); ++e) {
                const DataNode& en = vn.children[e];
                const DataNode* sg = en.isList() ? en.find("songs") : nullptr;
                if (!sg || sg->children.size() < 2) continue;
                const DataNode& f = sg->children[1];
                Ev x{tn.textAt(0), en.textAt(0), f.isList() ? f.intAt(1) : intOf(f), f.isList() && f.textAt(0) == "unlock"};
                if (!f.isList() && f.type != DataNode::Type::Int) continue;  // a download venue
                data.push_back(x);
                if (firstVenue && !x.unlock && x.number <= maxSong) normalOpen = true;
                if (x.unlock && x.number <= int(bonus.size())) ++bonusEvents;
            }
            firstVenue = false;
        }
        if (normalOpen) normalTowns.push_back(tn.textAt(0));
    }
    std::vector<std::string> wantOrder;  // the normal events open at the difficulty, in the data's order
    for (const Ev& e : data)
        if (!e.unlock && e.number <= maxSong) wantOrder.push_back(e.name);
    const std::string wantFinal = wantOrder.empty() ? std::string() : wantOrder.back();

    std::vector<std::string> played, townsOpened;
    int wrongSongs = 0, early = 0, bonusPlayed = 0, winningSeen = 0;
    std::string final, list;
    for (int guard = 0; guard < 400; ++guard) {
        std::vector<std::string> rows = c.rows("solo_event_provider");
        // The bonus gigs sit behind the list's "unlock" row (the provider's
        // kTourVenueUnlock mode, GD05-123): its rows after the normal ones.
        if (auto u = std::find(rows.begin(), rows.end(), "unlock"); u != rows.end()) {
            rows.erase(u);
            ScriptObject* prov = c.in.findObject("solo_event_provider");
            c.send(prov, "set", {Value::symbol("mode"), Value::integer(1)});
            for (const std::string& r : c.rows("solo_event_provider")) rows.push_back(r);
            c.send(prov, "set", {Value::symbol("mode"), Value::integer(0)});
            c.list("solo_event_provider");
        }
        std::string pick, winner;
        for (const std::string& r : rows) {
            c.send(perf, "set_event", {Value::symbol(r)});  // the gig guide's scroll sets the town / venue
            if (c.send(perf, "event_status").integerValue() != 2) continue;
            if (c.send(perf, "is_winning").truthy()) {
                winner = r;
                continue;
            }
            pick = r;
            break;
        }
        // A town's events listed before the towns before it were completed.
        for (const std::string& r : rows) {
            const auto it = std::find_if(data.begin(), data.end(), [&](const Ev& e) { return e.name == r; });
            if (it == data.end() || it->unlock) continue;
            if (std::find(townsOpened.begin(), townsOpened.end(), it->town) == townsOpened.end()) {
                const size_t k = size_t(std::find(normalTowns.begin(), normalTowns.end(), it->town) - normalTowns.begin());
                for (size_t j = 0; j < k && j < normalTowns.size(); ++j)
                    for (const Ev& e : data)
                        if (e.town == normalTowns[j] && !e.unlock && e.number <= maxSong && std::find(played.begin(), played.end(), e.name) == played.end())
                            ++early;
                townsOpened.push_back(it->town);
            }
        }
        if (pick.empty()) {
            final = winner;
            winningSeen = !winner.empty();
            break;
        }
        c.send(perf, "set_event", {Value::symbol(pick)});
        std::vector<std::string> songs;
        c.playEvent(perf, 5, &songs, false);
        played.push_back(pick);
        const auto it = std::find_if(data.begin(), data.end(), [&](const Ev& e) { return e.name == pick; });
        if (it != data.end()) {
            const std::vector<std::string>& l = it->unlock ? bonus : ranked;
            const std::string want = it->number >= 1 && size_t(it->number) <= l.size() ? l[size_t(it->number - 1)] : "?";
            const std::string got = songs.empty() ? "" : songs[0];
            wrongSongs += got != want;
            bonusPlayed += it->unlock;
            list += " " + it->town + "/" + pick + "=" + got + (got == want ? "" : "(want " + want + ")");
        }
    }
    std::printf("  solo tour %s, %s (max_song %d): played %zu gigs through the natives:%s\n", inst.c_str(), kDiffSyms[std::clamp(diff, 0, 3)], maxSong,
                played.size(), list.c_str());
    std::string order;
    for (const std::string& t : townsOpened) order += " " + t;
    std::string wantTowns;
    for (const std::string& t : normalTowns) wantTowns += " " + t;
    std::printf("  towns as they opened:%s; expected (data order):%s; events listed before the towns before theirs were done: %d\n", order.c_str(),
                wantTowns.c_str(), early);
    const int normalPlayed = int(played.size()) - bonusPlayed;
    std::printf("  expected: %zu tier gigs (+1 left to win: %s), %d bonus gigs; measured %d tier gigs (+ %s left), %d bonus gigs, %d wrong songs, "
                "cash %d\n",
                wantOrder.size() - 1, wantFinal.c_str(), bonusEvents, normalPlayed, final.c_str(), bonusPlayed, wrongSongs,
                c.send(perf, "money").integerValue());
    SDL_setenv_unsafe("GD05_WALK_FINAL", final.c_str(), 1);
    if (!winningSeen || final != wantFinal) return err = "the tour's winning gig is '" + final + "', the data's last is '" + wantFinal + "'", false;
    if (normalPlayed + 1 != int(wantOrder.size())) return err = "not every tier gig could be played", false;
    if (bonusPlayed != bonusEvents) return err = "the bonus gigs played are not the bonus songs' count", false;
    if (wrongSongs || early) return err = "wrong songs or towns opened early", false;
    return true;
}

// ------------------------------------------------------------ Band World Tour

// The travel a town needs from the hometown, from (world (<town> (region a
// b))) alone: 0 home, 1 its half, 2 the other half, 3 elsewhere.
int need(const DataNode* world, const std::string& home, const std::string& town) {
    auto reg = [&](const std::string& t, int i) {
        const DataNode* n = world->find(t);
        const DataNode* r = n ? n->find("region") : nullptr;
        return r && int(r->children.size()) > i ? r->textAt(size_t(i)) : std::string();
    };
    if (reg(town, 2).empty() || reg(town, 1) != reg(home, 1)) return 3;
    if (reg(town, 2) != reg(home, 2)) return 2;
    return town == home ? 0 : 1;
}

// rb_full_band_autoplay: every gig the band can play, town by town (the
// town list the game offers), venue by venue, 5 stars a song, until none is
// left but the one that wins the tour (win_item's hall of fame, which the
// walk plays through the menus; hardcore_item's endless setlist needs the
// hall of fame's item, (item tour_halloffame_complete), so it stays locked).
bool bandAutoplay(Ctx& c, std::string& err) {
    ScriptObject* band = c.in.send("tour", "performer", {}).object;
    const DataNode* tour = c.ui.bootTree() ? c.ui.bootTree()->find("tour") : nullptr;
    const DataNode* cfg = tour ? tour->find("band") : nullptr;
    const DataNode* world = cfg ? cfg->find("world") : nullptr;
    if (!band || !world) return err = "no band or (world ...)", false;
    // The band plays at Expert: RB caps a band's buzz at buzz_caps of its
    // lowest difficulty (RB 0x8240A088, GD05-125) and the late gigs need
    // buzz 355-580 and Hard (BWT_BUZZ_MEDIUMBLOCK*), so a Medium band
    // (capped at 350) cannot reach them.
    for (int p = 0; p < 4; ++p)
        if (c.ui.session().joined(p)) c.ui.session().players[size_t(p)].difficulty = 3;
    const std::string win = cfg->find("win_item") ? cfg->find("win_item")->textAt(1) : "";
    const std::string hard = cfg->find("hardcore_item") ? cfg->find("hardcore_item")->textAt(1) : "";
    std::vector<std::string> vehicles;
    if (const DataNode* m = cfg->find("moments"))
        if (const DataNode* v = m->find("vehicles"))
            for (size_t i = 1; i < v->children.size(); ++i) vehicles.push_back(v->children[i].isList() ? v->children[i].textAt(0) : v->children[i].s);
    int tierEvents = 0;
    std::set<std::string> tierNames;
    if (const DataNode* tiers = cfg->find("tiers"))
        for (size_t t = 1; t < tiers->children.size(); ++t) {
            tierNames.insert(tiers->children[t].textAt(0));
            for (size_t v = 1; v < tiers->children[t].children.size(); ++v) tierEvents += int(tiers->children[t].children[v].children.size()) - 1;
        }
    const std::string home = c.send(band, "hometown").str();
    std::map<std::string, std::string> tierOfTown;  // the towns the tiers' events were played in
    std::vector<std::string> townOrder, log;
    std::set<std::string> playedKeys;
    int travelErrors = 0, gigs = 0, tierGigs = 0, momentErrors = 0;
    std::map<std::string, int> momentsTaken;
    std::vector<std::string> finals;
    for (int guard = 0; guard < 600; ++guard) {
        bool found = false;
        for (const std::string& t : c.rows("tour_town_provider")) {
            c.send(band, "set_town", {Value::symbol(t)});
            for (const std::string& v : c.rows("tour_venue_provider")) {
                c.send(band, "set_venue", {Value::symbol(v)});
                for (const std::string& e : c.rows("tour_event_provider")) {
                    if (c.send(band, "event_status", {Value::symbol(e)}).integerValue() != 2) continue;
                    c.send(band, "set_event", {Value::symbol(e)});
                    ScriptObject* ev = c.in.send("tour", "event", {}).object;
                    const std::string award = c.send(ev, "get", {Value::symbol("award_item")}).str();
                    const std::string key = t + "/" + v + "/" + e;
                    if (award == win || award == hard) {
                        if (std::find(finals.begin(), finals.end(), key) == finals.end()) finals.push_back(key);
                        continue;
                    }
                    // The vehicles owned allow the town (data's regions).
                    int level = 0;
                    for (size_t k = 0; k < vehicles.size(); ++k)
                        if (c.send(band, "has_item", {Value::symbol(vehicles[k])}).truthy()) level = int(k) + 1;
                    if (need(world, home, t) > level) ++travelErrors;
                    if (std::find(townOrder.begin(), townOrder.end(), t) == townOrder.end()) townOrder.push_back(t);
                    // GD05-125: a moment offered is taken (the active moment
                    // screen's yes); its reward per star (tour_band.dta) at 5
                    // stars a song: x2, x4, x0 (charity), x3 (sellout), else x1.
                    std::string moment;
                    if (c.send(band, "has_next_moment").truthy()) {
                        const Value mv = c.send(band, "next_moment");
                        moment = mv.type == Value::Type::Array && mv.array && !mv.array->empty() ? (*mv.array)[0].str() : std::string("?");
                        c.send(band, "set_active_moment", {mv});
                    }
                    std::vector<std::string> songs;
                    c.playEvent(band, 5, &songs, true);
                    if (!moment.empty()) {
                        const int per = c.send(ev, "get", {Value::symbol("reward_per_star")}).integerValue();
                        const int factor = moment == "tour_am_doubleornothingfourstar" ? 2 : moment == "tour_am_quadrupleornothingfivestar" ? 4
                                         : moment == "tour_am_charity" ? 0 : moment == "tour_am_sellout" ? 3 : 1;
                        const int want = per * 5 * int(songs.size()) * factor;
                        ++momentsTaken[moment];
                        if (c.rewardBefore != want) {
                            ++momentErrors;
                            std::printf("  moment %s at %s: reward %d, expected %d\n", moment.c_str(), key.c_str(), c.rewardBefore, want);
                        }
                    }
                    ++gigs;
                    playedKeys.insert(key);
                    // A tier's event: its locale token names the tier (the event object's data town).
                    const std::string dataTown = c.send(ev, "get", {Value::symbol("data_town")}).str();
                    if (tierNames.count(dataTown)) {
                        ++tierGigs;
                        if (tierOfTown.count(t) && tierOfTown[t] != dataTown) ++travelErrors;
                        tierOfTown[t] = dataTown;
                    }
                    if (!award.empty()) log.push_back(award + " at " + key + " (gig " + std::to_string(gigs) + ")");
                    found = true;
                    break;
                }
                if (found) break;
            }
            if (found) break;
        }
        if (!found) break;
    }
    std::string mt;
    int momentsTotal = 0;
    for (const auto& [k, n] : momentsTaken) mt += " " + k + " x" + std::to_string(n), momentsTotal += n;
    std::printf("  active moments taken: %d:%s; rewards not as the moment's: %d\n", momentsTotal, mt.c_str(), momentErrors);
    std::string towns, tiers, items;
    for (const std::string& t : townOrder) towns += " " + t;
    for (const auto& [t, tier] : tierOfTown) tiers += " " + t + "=" + tier;
    for (const std::string& l : log) items += "; " + l;
    std::printf("  band tour from %s: %d gigs played through the natives (%d of the tiers' %d), towns in the order first played:%s\n", home.c_str(), gigs,
                tierGigs, tierEvents, towns.c_str());
    std::printf("  tiers played in:%s\n  items:%s\n", tiers.c_str(), items.c_str());
    std::string f;
    for (const std::string& k : finals) f += " " + k;
    std::printf("  left for the end:%s; buzz %d, fans %d, world fame %d, money %d; towns entered without the vehicle: %d\n", f.c_str(),
                c.send(band, "buzz").integerValue(), c.send(band, "fans").integerValue(), c.send(band, "world_fame").integerValue(),
                c.send(band, "money").integerValue(), travelErrors);
    size_t worldTowns = 0;
    for (size_t i = 1; i < world->children.size(); ++i) worldTowns += world->children[i].isList();
    std::string hof;
    for (const std::string& k : finals)
        if (k.find("halloffame") != std::string::npos) hof = k;
    SDL_setenv_unsafe("GD05_WALK_FINAL", hof.c_str(), 1);
    if (travelErrors) return err = "a town was played without the vehicle it needs, or two tiers in one town", false;
    if (momentErrors || (!SDL_getenv("GD05_MOMENTS_LEGACY") && momentsTotal == 0)) return err = "no active moment offered, or its reward was not the moment's", false;
    if (townOrder.size() != worldTowns) return err = std::to_string(townOrder.size()) + " of the " + std::to_string(worldTowns) + " towns were played", false;
    if (tierGigs != tierEvents) return err = "the tiers' events were not all played", false;
    for (const std::string& v : vehicles)
        if (!c.send(band, "has_item", {Value::symbol(v)}).truthy()) return err = "the band never won " + v, false;
    // The endless setlist needs the hall of fame's item ((item ...)), so only the hall of fame is left open.
    if (finals.size() != 1 || hof.empty()) return err = "the tour's end (the hall of fame) is not what is left", false;
    return true;
}

// rb_full_band_end: the endless setlist (hardcore_item) through the natives
// after the hall of fame: is_winning and is_hardcore_winning before, the
// item after.
bool bandEnd(Ctx& c, std::string& err) {
    ScriptObject* band = c.in.send("tour", "performer", {}).object;
    const DataNode* tour = c.ui.bootTree() ? c.ui.bootTree()->find("tour") : nullptr;
    const DataNode* cfg = tour ? tour->find("band") : nullptr;
    if (!band || !cfg) return err = "no band", false;
    const std::string win = cfg->find("win_item")->textAt(1), hard = cfg->find("hardcore_item")->textAt(1);
    const bool hasWin = c.send(band, "has_item", {Value::symbol(win)}).truthy();
    // GD05-122: the hall of fame gave each member's character its
    // instrument's (win_instruments ...) item (RB 0x823EF0F0), and the gig's
    // unlock is win_instruments (the gig complete screen's line).
    const DataNode* wi = cfg->find("win_instruments");
    const std::string unlock = c.send(band, "event_unlock").str();
    int members = 0, awarded = 0;
    std::string who;
    for (int p = 0; p < 4; ++p) {
        if (!c.ui.session().joined(p)) continue;
        ScriptObject* pc = c.in.send("game", "get_player_config", {Value::integer(p)}).object;
        ScriptObject* ch = pc ? c.send(pc, "get_char").object : nullptr;
        if (!ch || ch->className() != "TourCharLocal") continue;
        const std::string inst = c.send(ch, "get_instrument").str();
        const DataNode* e = wi ? wi->find(inst) : nullptr;
        const std::string item = e ? e->textAt(1) : "";
        const bool owns = c.send(ch, "has_outfit", {Value::symbol(inst == "vocals" ? "mic" : inst), Value::symbol(item)}).truthy();
        ++members, awarded += owns;
        who += " " + inst + ":" + item + "=" + (owns ? "owned" : "not owned");
    }
    std::printf("  win instruments: event_unlock '%s'; members%s (%d of %d)\n", unlock.c_str(), who.c_str(), awarded, members);
    if (unlock != "win_instruments" || !members || awarded != members)
        return err = "the hall of fame did not award the win instruments", false;
    std::string endless;
    for (const std::string& t : c.rows("tour_town_provider")) {
        c.send(band, "set_town", {Value::symbol(t)});
        for (const std::string& v : c.rows("tour_venue_provider")) {
            c.send(band, "set_venue", {Value::symbol(v)});
            for (const std::string& e : c.rows("tour_event_provider")) {
                c.send(band, "set_event", {Value::symbol(e)});
                ScriptObject* ev = c.in.send("tour", "event", {}).object;
                if (c.send(ev, "get", {Value::symbol("award_item")}).str() == hard) endless = t + "/" + v + "/" + e;
                if (!endless.empty()) break;
            }
            if (!endless.empty()) break;
        }
        if (!endless.empty()) break;
    }
    if (endless.empty()) return err = "no endless setlist gig", false;
    const int status = c.send(band, "event_status").integerValue();
    const bool winning = c.send(band, "is_winning").truthy(), hardcore = c.send(band, "is_hardcore_winning").truthy();
    std::vector<std::string> songs;
    if (status == 2) c.playEvent(band, 5, &songs, true);
    const bool got = c.send(band, "has_item", {Value::symbol(hard)}).truthy();
    std::printf("  endless setlist %s: %s won before: %d; status %d, is_winning %d, is_hardcore_winning %d, %zu songs, %s after: %d\n", endless.c_str(),
                win.c_str(), hasWin, status, winning, hardcore, songs.size(), hard.c_str(), got);
    if (!hasWin) return err = "the hall of fame did not award " + win, false;
    if (status != 2 || !winning || !hardcore || !got) return err = "the endless setlist did not end the tour", false;
    SDL_setenv_unsafe("GD05_WALK_FAME", std::to_string(c.send(band, "world_fame").integerValue()).c_str(), 1);
    return true;
}

}  // namespace

// rb_bwt_diy's: the band's first gig through the natives (the hometown's
// first new one), then the walk's target the hometown small club's (songs
// any any) gig (Make a Setlist), which the walk plays through the menus.
bool bandFirstGig(Ctx& c, std::string& err) {
    ScriptObject* band = c.in.send("tour", "performer", {}).object;
    if (!band) return err = "no band", false;
    const std::string home = c.send(band, "hometown").str();
    c.send(band, "set_town", {Value::symbol(home)});
    c.send(band, "set_venue", {Value::symbol("small_club")});
    std::string first, diy;
    for (const std::string& e : c.rows("tour_event_provider")) {
        c.send(band, "set_event", {Value::symbol(e)});
        const int st = c.send(band, "event_status").integerValue();
        if (first.empty() && st == 2) first = e;
        if (e == "diy") diy = e;
    }
    if (first.empty() || diy.empty()) return err = "no new gig or no diy gig at " + home + "'s small club", false;
    c.send(band, "set_event", {Value::symbol(first)});
    std::vector<std::string> songs;
    c.playEvent(band, 5, &songs, true);
    c.send(band, "set_event", {Value::symbol(diy)});
    const int st = c.send(band, "event_status").integerValue();
    std::printf("  first gig %s/small_club/%s (%zu songs), world fame %d; %s status %d (2 new)\n", home.c_str(), first.c_str(), songs.size(),
                c.send(band, "world_fame").integerValue(), diy.c_str(), st);
    SDL_setenv_unsafe("GD05_WALK_FINAL", (home + "/small_club/" + diy).c_str(), 1);
    if (st != 2) return err = "the hometown's Make a Setlist gig is not open after a first gig", false;
    return true;
}

// The career checks of tests/walktest.cpp's runCheck: true when `name` is one.
bool walkCareerCheck(ui::UiManager& ui, const std::string& name, std::string& err, bool& ok) {
    Ctx c{ui, ui.interp()};
    if (name == "rb_full_solo_autoplay") return ok = soloAutoplay(c, err), true;
    if (name == "rb_full_band_autoplay") return ok = bandAutoplay(c, err), true;
    if (name == "rb_full_band_end") return ok = bandEnd(c, err), true;
    if (name == "rb_bwt_first_gig") return ok = bandFirstGig(c, err), true;
    if (name.rfind("rb_gig_rows:", 0) == 0) {
        // rb_gig_rows:<provider>:<labels>: every row's texts as the list
        // draws them (the provider's rows); each label of <labels> (comma
        // list, e.g. star,new) must be filled on at least one row.
        const std::string rest = name.substr(12);
        const std::string prov = rest.substr(0, rest.find(':'));
        const std::string want = rest.find(':') == std::string::npos ? "" : rest.substr(rest.find(':') + 1);
        ui::ListProvider* l = c.list(prov.c_str());
        if (!l) return err = "no " + prov, ok = false, true;
        std::vector<gfx::UiView::ListRow> rows;
        int sel = 0;
        l->rows(rows, 0, sel);
        std::map<std::string, int> filled;
        std::printf("  %s: %zu rows\n", prov.c_str(), rows.size());
        for (size_t i = 0; i < rows.size(); ++i) {
            std::string line;
            for (const auto& [k, v] : rows[i].texts)
                if (k != "list.lbl" && k != "name.lbl" && !v.empty()) line += " " + k + "='" + v + "'", ++filled[k.substr(0, k.find('.'))];
            std::printf("    %zu %s:%s\n", i, l->symbol(int(i)).c_str(), line.c_str());
        }
        std::string missing;
        for (size_t a0 = 0, b0; a0 < want.size(); a0 = b0 + 1) {
            b0 = want.find(',', a0);
            if (b0 == std::string::npos) b0 = want.size();
            const std::string k = want.substr(a0, b0 - a0);
            std::printf("    %s filled on %d rows\n", k.c_str(), filled[k]);
            if (!filled[k]) missing += " " + k;
        }
        ok = missing.empty();
        if (!ok) err = prov + ": no row shows" + missing;
        return true;
    }
    if (name.rfind("rb_solo_mode:", 0) == 0) {
        // The solo gig guide's mode (0 normal, 1 the unlock rows' bonus gigs).
        const int want = std::atoi(name.substr(13).c_str());
        const int got = c.in.send("solo_event_provider", "get", {Value::symbol("mode")}).integerValue();
        std::vector<std::string> rows = c.rows("solo_event_provider");
        std::string l;
        for (const std::string& r : rows) l += " " + r;
        std::printf("  solo gig guide mode %d (expected %d):%s\n", got, want, l.c_str());
        ok = got == want;
        if (!ok) err = "the solo gig guide is not in mode " + std::to_string(want);
        return true;
    }
    if (name == "rb_solo_first_gig") {
        // The first open gig completed through the natives (4 stars, a score),
        // so the list has a played row.
        ScriptObject* perf = c.in.send("tour", "performer", {}).object;
        std::vector<std::string> rows = c.rows("solo_event_provider");
        if (rows.empty()) return err = "no solo gig", ok = false, true;
        c.send(perf, "set_event", {Value::symbol(rows[0])});
        c.send(perf, "set_random_songs");
        c.send(perf, "next_song");
        c.send(perf, "complete_song", {Value::integer(4), Value::integer(123456)});
        c.send(perf, "complete_event");
        if (!c.send(perf, "new_unlock").str().empty()) c.send(perf, "advance_new_unlock");
        std::printf("  solo first gig %s completed (4 stars, 123456)\n", rows[0].c_str());
        return ok = true, true;
    }
    if (name == "rb_hometowns") {
        // The create screen's hometown list against RB's (HometownProvider,
        // RB 0x823FC338): the towns with both parts of a (region a b),
        // sorted by their localized names (strcmp, RB 0x823F4768).
        const DataNode* tour = ui.bootTree() ? ui.bootTree()->find("tour") : nullptr;
        const DataNode* world = tour && tour->find("band") ? tour->find("band")->find("world") : nullptr;
        std::vector<std::pair<std::string, std::string>> want;
        for (size_t i = 1; world && i < world->children.size(); ++i) {
            const DataNode& t = world->children[i];
            const DataNode* r = t.isList() ? t.find("region") : nullptr;
            if (r && r->children.size() > 2) want.emplace_back(ui.localize(t.textAt(0)), t.textAt(0));
        }
        std::stable_sort(want.begin(), want.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        const std::vector<std::string> got = c.rows("tour_hometown_provider");
        std::string g, w;
        for (const std::string& x : got) g += " " + x;
        for (const auto& x : want) w += " " + x.second;
        std::printf("  hometowns: expected%s\n  measured%s\n", w.c_str(), g.c_str());
        ok = g == w;
        if (!ok) err = "the hometown list is not RB's";
        return true;
    }
    if (name == "rb_bwt_min_difficulty") {
        // Every gig of every town: the event's get min_difficulty (what
        // seldiff.dta disables the buttons below) against the data's
        // (min_difficulty kDifficulty*) (macros.dta: Easy 0 .. Expert 3).
        ScriptObject* band = c.in.send("tour", "performer", {}).object;
        const std::string town0 = c.send(band, "town").str(), venue0 = c.send(band, "venue").str();
        int gigs = 0, withMin = 0, wrong = 0;
        std::string list;
        const DataNode* tour = ui.bootTree() ? ui.bootTree()->find("tour") : nullptr;
        const DataNode* world = tour && tour->find("band") ? tour->find("band")->find("world") : nullptr;
        for (size_t i = 1; world && i < world->children.size(); ++i) {
            const std::string t = world->children[i].textAt(0);
            c.send(band, "set_town", {Value::symbol(t)});
            c.send(band, "set_venue", {Value::symbol("")});
            for (const std::string& v : c.rows("tour_venue_provider")) {
                c.send(band, "set_venue", {Value::symbol(v)});
                for (const std::string& e : c.rows("tour_event_provider")) {
                    c.send(band, "set_event", {Value::symbol(e)});
                    ScriptObject* ev = c.in.send("tour", "event", {}).object;
                    ++gigs;
                    const int got = c.send(ev, "get", {Value::symbol("min_difficulty")}).integerValue();
                    if (got > 0) ++withMin, list += " " + t + "/" + v + "/" + e + "=" + std::to_string(got);
                }
            }
        }
        // The data's count: the town files' and the tiers' events with (min_difficulty ...).
        c.send(band, "set_town", {Value::symbol(town0)}), c.send(band, "set_venue", {Value::symbol(venue0)});
        std::printf("  min_difficulty: %d gigs listed, %d with a minimum above Easy:%s\n", gigs, withMin, list.c_str());
        ok = withMin > 0;
        if (!ok) err = "no gig reports its min_difficulty";
        (void)wrong;
        return true;
    }
    if (name == "rb_bwt_diy_set") {
        // The set the band chose in tour_selsong_screen: no (any) left, each a song of the disc.
        auto* l = dynamic_cast<ui::ListProvider*>(c.in.findObject("tour_event_song_provider"));
        if (l) c.in.send(l, "refresh", {});
        std::string set;
        int any = 0, n = 0;
        for (int i = 0; l && i < l->numData(); ++i) {
            const std::string s = l->symbol(i);
            ++n, any += s == "any";
            set += " " + s;
        }
        ScriptObject* band = c.in.send("tour", "performer", {}).object;
        std::printf("  the band's set:%s (next_any_index %d); playing %s\n", set.c_str(), c.send(band, "next_any_index").integerValue(),
                    ui.session().song.c_str());
        ok = n == 2 && !any && c.send(band, "next_any_index").integerValue() == -1;
        if (!ok) err = "the Make a Setlist gig's songs were not all chosen";
        return true;
    }
    if (name == "rb_full_band_reloaded") {
        ScriptObject* band = c.in.send("tour", "performer", {}).object;
        const char* fame = SDL_getenv("GD05_WALK_FAME");
        const int got = c.send(band, "world_fame").integerValue();
        const DataNode* cfg = ui.bootTree() && ui.bootTree()->find("tour") ? ui.bootTree()->find("tour")->find("band") : nullptr;
        const std::string hard = cfg && cfg->find("hardcore_item") ? cfg->find("hardcore_item")->textAt(1) : "";
        const bool item = c.send(band, "has_item", {Value::symbol(hard)}).truthy();
        std::printf("  expected (before the restart): world fame %s, %s owned; measured: world fame %d, owned %d\n", fame ? fame : "?", hard.c_str(), got, item);
        ok = fame && got == std::atoi(fame) && item;
        if (!ok) err = "the band's tour was not kept through the restart";
        return true;
    }
    if (name == "rb_full_solo_reloaded") {
        ScriptObject* perf = c.in.send("tour", "performer", {}).object;
        const char* want = SDL_getenv("GD05_WALK_FAME");
        const int got = c.send(perf, "world_fame").integerValue();
        std::printf("  expected (before the restart): %s gigs completed; measured %d\n", want ? want : "?", got);
        ok = want && got == std::atoi(want);
        if (!ok) err = "the solo tour was not kept through the restart";
        return true;
    }
    if (name == "rb_full_solo_won") {
        ScriptObject* perf = c.in.send("tour", "performer", {}).object;
        const char* fin = SDL_getenv("GD05_WALK_FINAL");
        const int status = c.send(perf, "event_status", {Value::symbol(fin ? fin : "")}).integerValue();
        const int fame = c.send(perf, "world_fame").integerValue();
        std::printf("  the winning gig %s: status %d (1 played); gigs completed %d\n", fin ? fin : "?", status, fame);
        SDL_setenv_unsafe("GD05_WALK_FAME", std::to_string(fame).c_str(), 1);
        ok = status == 1;
        if (!ok) err = "the winning gig is not completed";
        // The win screen's "Enjoy your new golden instrument": the winning
        // gig's (outfit (<instrument> (<gender> <category> <item>))) owned.
        ScriptObject* ev = c.in.send("tour", "event", {}).object;
        const std::string inst = c.send(perf, "get_instrument").str(), gender = c.send(perf, "get_gender").str();
        std::string cat, item;
        if (const DataNode* tour = ui.bootTree() ? ui.bootTree()->find("tour") : nullptr)
            if (const DataNode* towns = tour->find("solo") ? tour->find("solo")->find("towns") : nullptr)
                for (size_t t = 1; t < towns->children.size(); ++t)
                    for (size_t v = 1; v < towns->children[t].children.size(); ++v)
                        if (const DataNode* en = towns->children[t].children[v].isList() ? towns->children[t].children[v].find(fin ? fin : "") : nullptr)
                            if (const DataNode* o = en->find("outfit"))
                                if (const DataNode* i = o->find(inst))
                                    if (const DataNode* g = i->find(gender)) cat = g->textAt(1), item = g->textAt(2);
        const bool owns = !item.empty() && c.send(perf, "has_outfit", {Value::symbol(cat), Value::symbol(item)}).truthy();
        std::printf("  the win's instrument: %s %s %s %s\n", inst.c_str(), cat.c_str(), item.c_str(), owns ? "owned" : "NOT owned");
        (void)ev;
        if (ok && !owns) ok = false, err = "the winning gig's golden instrument is not owned";
        return true;
    }
    return false;
}
