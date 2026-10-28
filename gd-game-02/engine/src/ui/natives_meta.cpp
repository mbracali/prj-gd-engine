// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The front end's native objects: campaign (0x82287F58), song_provider
// (0x822C2160), memcard (0x822822E0), profilemgr (0x82273B68), options
// (0x822B1488), highscores, tips (0x82282830), the sound objects and the
// Xbox Live services the port does not have (logged STUBs).
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>

#include "audio/audio_device.h"
#include "game/song_db.h"
#include "input/input.h"
#include "script/tasks.h"
#include "ui/campaign.h"
#include "ui/char_select.h"
#include "ui/natives.h"
#include "ui/sfx_args.h"
#include "ui/store.h"
#include "ui/ui_host.h"
#include "ui/ui_objects.h"

namespace ui {

int ListProvider::indexOf(const std::string& sym) {
    for (int i = 0; i < numData(); ++i)
        if (symbol(i) == sym) return i;
    return -1;
}

namespace {

std::string nameOf(const Value& v) { return v.type == Value::Type::Object && v.object ? v.object->name() : v.str(); }
int intArg(const std::vector<Value>& a, size_t i, int def = 0) { return i < a.size() && a[i].isNumber() ? a[i].integerValue() : def; }

// The OS user's name (profilemgr get_name; the default high score name).
std::string userName() {
    const char* n = SDL_getenv("USER");
    if (!n || !*n) n = SDL_getenv("USERNAME");
    return n && *n ? n : "PLAYER";
}

// song_provider (0x822C2160): the song lists of the song screens
// (kSongsCareer 0, kSongsQuickplay 1, kSongsLeaderboards 2), with headers.
class SongProviderObj : public ListProvider {
public:
    explicit SongProviderObj(UiManager& ui) : ListProvider("song_provider", "SongProvider"), ui_(ui) {}

    int numData() override { return int(data_.size()); }
    std::string symbol(int i) override { return i >= 0 && i < int(data_.size()) ? data_[size_t(i)] : std::string(); }
    // 0x822C33C0: an encore is active once it is unlocked.
    bool active(int i) override {
        const std::string s = symbol(i);
        return !campaign::isEncore(ui_, s) || campaign::isUnlocked(ui_, s);
    }

    // 0x822C5A68: career, the band's songs (0x822A75D0); quick play, the
    // first two tiers' songs, every song a band has unlocked, bought songs
    // and downloads (0x82287600); in campaign order, a header before each
    // venue's songs, the store's and the downloads'.
    void build() {
        data_.clear();
        headers_.clear();
        const auto have = [&](const std::string& s) { return songInfoIndex(ui_, s) >= 0; };
        for (const std::string& s : mode == 0 ? campaign::careerSongs(ui_, storeMode_) : campaign::quickplaySongs(ui_))
            if (have(s)) data_.push_back(s);
        std::string last;
        footer_ = -1;
        for (int i = 0; i < int(data_.size()); ++i) {
            const std::string& s = data_[size_t(i)];
            std::string group = campaign::isDownload(ui_, s) ? "download" : campaign::isStoreSong(ui_, s) ? "store" : campaign::venueOf(ui_, s);
            if ((group == "store" || group == "download") && footer_ < 0) footer_ = i - 1;
            if (group != last) headers_.push_back({i, group}), last = group;
        }
        // The last tier song carries the blurb, the one before when it is inactive.
        if (footer_ < 0) footer_ = int(data_.size()) - 1;
        if (footer_ >= 0 && !active(footer_)) --footer_;
    }

    int numHeaders(int pos) const {
        int n = 0;
        for (const auto& [idx, tier] : headers_)
            if (idx <= pos) ++n;
        return n;
    }

    // The footer row's blurb (0x822C0070).
    std::string blurb() {
        Career::Profile* p = campaign::profile(ui_);
        if (mode == 1) {
            // Until a band is a tier from the end at medium or harder.
            for (int d = 1; p && d < Career::kDifficulties; ++d)
                if (int(campaign::config(ui_).tiers.size()) - 1 <= campaign::status(ui_, *p, d)) return {};
            return ui_.localize("song_quickplay");
        }
        if (mode != 0 || !p) return {};
        const int d = campaign::difficulty(ui_), nt = campaign::numTiers(ui_, d), st = campaign::status(ui_, *p, d);
        if (nt <= st) return {};
        const std::string venue = ui_.session().venue;
        const int vi = campaign::venueIndex(ui_, venue);
        if (vi < st) return ui_.localize("song_old_venue");
        const bool encore = campaign::isUnlocked(ui_, campaign::encoreOf(ui_, venue));
        const int req = campaign::requiredSongs(ui_, d, venue);
        char buf[160];
        if (vi < nt - 1 || d != 0) {
            if (encore) return ui_.localize(vi < nt - 1 ? "song_current_venue_encore" : "song_win_other");
            std::snprintf(buf, sizeof buf, ui_.localize("song_current_venue").c_str(), d == 0 ? req : req - 1);
            return buf;
        }
        std::snprintf(buf, sizeof buf, ui_.localize("song_win_easy").c_str(), req, req);
        return buf;
    }

    void rows(std::vector<gfx::UiView::ListRow>& out, int selected, int& selectedRow) override {
        // One row per song at slot index + headers above it: a group's first
        // song carries its header.txt (placed a slot up by the template),
        // the camera follows the slot (sel_song.view frame pos + num_headers).
        out.clear();
        selectedRow = 0;
        Career::Profile* p = campaign::profile(ui_);
        const int d = campaign::difficulty(ui_);
        const std::string foot = blurb();
        for (int i = 0; i < int(data_.size()); ++i) {
            const std::string& s = data_[size_t(i)];
            gfx::UiView::ListRow r;
            r.slot = i + numHeaders(i);
            for (const auto& [idx, tier] : headers_)
                if (idx == i) r.header = true, r.texts["header.txt"] = ui_.localize("song_header_" + tier);
            const int info = songInfoIndex(ui_, s);
            r.texts["list.txt"] = info >= 0 ? ui_.host().songs()[size_t(info)].name : s;
            r.disabled = !active(i);
            // Career rows: the best score, the stars ("***", gold after a
            // perfect run) and the check of a passed tour song
            // (0x822C3428 / 0x822C34E8 / 0x822C41D8); quick play shows none
            // outside co-op.
            // Quick play in co-op (0x822C3428 / 0x822C34E8 with mode
            // multi_coop): the co-op record's score and stars (Campaign +0x4c).
            if (mode == 1 && gameCfgValue(ui_, "mode").str() == "multi_coop")
                if (const Career::CoopEntry* e = campaign::coopEntry(ui_, s)) {
                    if (e->score) r.texts["score.txt"] = std::to_string(e->score);
                    if (!e->perfect && e->stars >= 3 && e->stars <= 5) r.texts["stars.txt"] = std::string(size_t(e->stars), '*');
                    if (e->perfect && e->stars > 2) r.mats["stars_gold.mesh"] = "stars_gold_" + std::to_string(std::min(e->stars, 5)) + ".mat";
                }
            if (mode == 0 && p) {
                const Career::Entry& e = campaign::record(ui_, *p, d)[s];
                if (e.score) r.texts["score.txt"] = std::to_string(e.score);
                if (!e.perfect && e.stars >= 3 && e.stars <= 5) r.texts["stars.txt"] = std::string(size_t(e.stars), '*');
                if (e.perfect && e.stars > 2) r.mats["stars_gold.mesh"] = "stars_gold_" + std::to_string(std::min(e.stars, 5)) + ".mat";
                if (e.stars >= 3 && !campaign::isStoreSong(ui_, s)) r.mats["checkmark.mesh"] = "checkmark.mat";
            }
            if (i == footer_ && !foot.empty()) r.texts["blurb.txt"] = foot;
            if (i == selected) selectedRow = int(out.size());
            out.push_back(std::move(r));
        }
    }

    bool handleNative(Interp&, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        if (msg == "set_mode") return mode = intArg(args, 0), build(), true;
        if (msg == "get_mode") return r = Value::integer(mode), true;
        if (msg == "init_data") return build(), true;
        if (msg == "set_store_mode") return storeMode_ = !args.empty() && args[0].truthy(), true;
        if (msg == "num_headers") return r = Value::integer(numHeaders(intArg(args, 0))), true;
        if (msg == "get_symbol") return r = Value::symbol(symbol(intArg(args, 0))), true;
        if (msg == "list_length" || msg == "num_data") return r = Value::integer(numData()), true;
        // 0x822BF2C0 / 0x822BF1F8: the song's (tracks ...) of the mode's
        // song_block (else `song`, 0x822B38F8): instrument n, and whether
        // (the index of) an instrument is there [C].
        if (msg == "get_instrument" || msg == "has_instrument" || msg == "track_num_of_instrument") {
            const std::vector<std::string> inst = songInstruments(ui_, args.empty() ? std::string() : nameOf(args[0]));
            if (msg == "get_instrument") {
                const int i = intArg(args, 1);
                return r = Value::symbol(i >= 0 && i < int(inst.size()) ? inst[size_t(i)] : std::string()), true;
            }
            const std::string want = args.size() > 1 ? nameOf(args[1]) : std::string();
            const auto it = std::find(inst.begin(), inst.end(), want);
            const int idx = it == inst.end() ? -1 : int(it - inst.begin());
            return r = Value::integer(msg == "has_instrument" ? (idx >= 0 ? 1 : 0) : idx), true;
        }
        return false;
    }

    int mode = 0;

private:
    UiManager& ui_;
    std::vector<std::string> data_;
    std::vector<std::pair<int, std::string>> headers_;
    int footer_ = -1;
    bool storeMode_ = false;
};

// campaign: the save's bands, their tours and the store (Campaign 0x82287F58;
// the rules in ui/campaign.h).
class CampaignObj : public ScriptObject {
public:
    CampaignObj(UiManager& ui, SongProviderObj& sp) : ScriptObject("campaign", "Campaign"), ui_(ui), sp_(sp) {}
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        Career& c = ui_.career();
        Session& ss = ui_.session();
        int& slot = ss.profileSlot;
        r = Value::integer(0);
        const auto argStr = [&](size_t i) { return i < args.size() ? nameOf(args[i]) : std::string(); };
        if (msg == "clear") return slot = -1, true;  // the splash: no profile chosen
        if (msg == "num_profiles") return r = Value::integer(c.numProfiles()), true;
        if (msg == "max_num_profiles") return r = Value::integer(Career::kProfiles), true;
        if (msg == "tutorials_done") {
            const int done = skipTutorials_ || tutorialAccess_ ? 3 : slot >= 0 ? c.profiles[size_t(slot)].tutorialsDone : 0;
            return r = Value::integer(done), true;
        }
        if (msg == "skip_tutorials") return skipTutorials_ = !args.empty() && args[0].truthy(), true;
        if (msg == "tutorial_access") return r = Value::integer(tutorialAccess_ ? 1 : 0), true;
        if (msg == "set_tutorial_access") return tutorialAccess_ = !args.empty() && args[0].truthy(), true;
        if (msg == "needs_save") return r = Value::integer(c.needsSave ? 1 : 0), true;
        if (msg == "mark_needs_save") return c.touch(), true;
        if (msg == "save_complete") return c.needsSave = false, true;
        if (msg == "all_access") return r = Value::integer(ss.allAccess ? 1 : 0), true;
        if (msg == "set_all_access") return ss.allAccess = !args.empty() && args[0].truthy(), true;
        if (msg == "profile_slot") return r = Value::integer(slot), true;
        if (msg == "set_profile_slot") return slot = intArg(args, 0, -1), true;
        if (msg == "profile_name") {
            // A band in use's name, else "Someone" [C].
            const int s = intArg(args, 0, slot);
            const bool used = s >= 0 && s < Career::kProfiles && c.profiles[size_t(s)].inUse;
            return r = Value::string(used ? c.profiles[size_t(s)].name : std::string("Someone")), true;
        }
        if (msg == "is_empty_profile") {
            const int s = intArg(args, 0, 0);
            return r = Value::integer(s >= 0 && s < Career::kProfiles && !c.profiles[size_t(s)].inUse ? 1 : 0), true;
        }
        if (msg == "empty_slot") {
            for (int s = 0; s < Career::kProfiles; ++s)
                if (!c.profiles[size_t(s)].inUse) return r = Value::integer(s), true;
            return r = Value::integer(-1), true;
        }
        if (msg == "delete_slot") {
            const int s = intArg(args, 0, -1);
            if (s >= 0 && s < Career::kProfiles) c.profiles[size_t(s)] = Career::Profile{}, c.touch();
            return true;
        }
        if (msg == "is_unlocked") return r = Value::integer(campaign::isUnlocked(ui_, argStr(0)) ? 1 : 0), true;
        if (msg == "is_video_unlocked") {
            // 0x82284890: any band in use has bought it.
            if (ss.allAccess) return r = Value::integer(1), true;
            for (Career::Profile& p : c.profiles)
                if (p.inUse && p.items.count(argStr(0)) && p.items[argStr(0)]) return r = Value::integer(1), true;
            return true;
        }
        if (msg == "is_store_song") return r = Value::integer(campaign::isStoreSong(ui_, argStr(0)) ? 1 : 0), true;
        if (msg == "foreach_venue") {
            // {campaign foreach_venue $v body...}: $v by name, each venue in order.
            if (args.empty()) return true;
            std::string var = args[0].str();
            if (var.size() > 1 && var[0] == '$') var = var.substr(1);
            for (const auto& [venue, songs] : campaign::config(ui_).tiers) {
                in.var(var) = Value::symbol(venue);
                for (size_t k = 1; k < args.size(); ++k) in.evalValue(args[k]);
            }
            return true;
        }
        if (msg == "pick_attract_song") {
            // A song quickplay offers (the attract mode).
            sp_.mode = 1;
            sp_.build();
            const int n = sp_.numData();
            return r = Value::symbol(n > 0 ? sp_.symbol(ui_.interp().randomInt(0, n)) : std::string()), true;
        }
        if (msg == "finish_coop_song") {
            // 0x822861A8: the band's result into co-op's record (the band and
            // the players as the game left them: band / player0 / player1).
            const int score = in.send("band", "score", {}).integerValue();
            const int stars = in.send("band", "num_stars", {}).integerValue();
            const int p0 = in.send("player0", "percent_hit", {}).integerValue();
            const int p1 = in.send("player1", "percent_hit", {}).integerValue();
            campaign::finishCoopSong(ui_, score, stars, p0, p1, ss.players[0].difficulty, ss.players[1].difficulty);
            return true;
        }
        if (msg == "num_guitar_awards") return r = Value::integer(int(ss.guitarAwards.size())), true;
        if (msg == "next_guitar_award") {
            if (ss.guitarAwards.empty()) return r = Value::symbol(""), true;
            r = Value::symbol(ss.guitarAwards.back());
            ss.guitarAwards.pop_back();
            return true;
        }
        if (msg == "starting_cash") return r = Value::integer(campaign::config(ui_).startingCash), true;
        // The band chosen (Campaign +0x48) at the player's difficulty.
        Career::Profile* prof = campaign::profile(ui_);
        const int diff = campaign::difficulty(ui_);
        if (msg == "has_profile_name") {
            // Another band (or, editing, any but the slot's) with the name [I: case-insensitive as the entry is upper case].
            const std::string n = argStr(0);
            const bool editing = args.size() > 1 && args[1].truthy();
            for (int i = 0; i < Career::kProfiles; ++i)
                if (c.profiles[size_t(i)].inUse && c.profiles[size_t(i)].name == n && !(editing && i == slot)) return r = Value::integer(1), true;
            return true;
        }
        if (msg == "set_profile_name") {
            const int s = intArg(args, 1, slot);
            if (s < 0 || s >= Career::kProfiles) return true;
            Career::Profile& p = c.profiles[size_t(s)];
            if (!p.inUse) p = Career::Profile{}, p.inUse = true;
            p.name = argStr(0);
            slot = s;
            c.touch();
            return true;
        }
        if (!prof) {
            // Without a band: GH2 reads slot 0's; nothing to report here.
            static const std::set<std::string> quiet{"last_difficulty", "update_difficulty", "cash", "add_cash", "cash_spent", "career_score",
                                                     "new_campaign", "set_character_info", "get_character_info", "status", "get_cur_status_sym",
                                                     "get_status_progress", "is_max_status", "won_campaign", "is_song_passed", "high_score",
                                                     "final_song", "is_encore_song", "encore_unlock_potential", "encore_newly_unlocked",
                                                     "get_cur_encore", "store_songs_passed", "check_practice_achievement", "do_tutorial",
                                                     "buy_item", "finish_song", "cheat_beat_song", "cheat_gold_song"};
            if (msg == "last_difficulty") return r = Value::integer(1), true;
            if (msg == "get_status_progress") return r = Value::string(""), true;
            return quiet.count(msg) != 0;
        }
        Career::Profile& p = *prof;
        const std::string song = ss.song;
        if (msg == "last_difficulty") return r = Value::integer(p.lastDifficulty), true;
        if (msg == "update_difficulty") {
            if (p.lastDifficulty != diff) p.lastDifficulty = diff, c.touch();
            return true;
        }
        // The band's cash (config/campaign.dta (cash ...)) [C].
        if (msg == "cash") return r = Value::integer(p.cash), true;
        if (msg == "cash_spent") return r = Value::integer(p.cashSpent), true;
        if (msg == "add_cash") return p.cash += intArg(args, 0), c.touch(), true;
        // 0x82287310: each song's best score at any difficulty, summed.
        if (msg == "career_score") {
            int sum = 0;
            for (const std::string& s : campaign::allSongs(ui_)) {
                int best = 0;
                for (int d = 0; d < Career::kDifficulties; ++d) best = std::max(best, campaign::record(ui_, p, d)[s].score);
                sum += best;
            }
            return r = Value::integer(sum), true;
        }
        if (msg == "high_score") return r = Value::integer(campaign::record(ui_, p, diff)[argStr(0)].score), true;
        // The band's look per difficulty (set / get_character_info
        // 0x82284E88 / 0x82284E00; new_campaign 0x822A8A88: none stored yet).
        if (msg == "new_campaign") return r = Value::integer(p.chars[size_t(diff)].character.empty() ? 1 : 0), true;
        if (msg == "set_character_info") {
            Session::PlayerCfg& pc = ss.players[0];
            chars::validate(ui_, pc);
            Career::CharInfo ci{pc.character, pc.outfit, pc.guitar, pc.skin};
            Career::CharInfo& cur = p.chars[size_t(diff)];
            if (cur.character != ci.character || cur.outfit != ci.outfit || cur.guitar != ci.guitar || cur.skin != ci.skin) cur = ci, c.touch();
            return true;
        }
        if (msg == "get_character_info") {
            const Career::CharInfo& ci = p.chars[size_t(diff)];
            Session::PlayerCfg& pc = ss.players[0];
            chars::setCharacter(ui_, pc, ci.outfit, false);
            chars::setGuitar(ui_, pc, ci.guitar, ci.skin, false);
            return true;
        }
        // The tour (ui/campaign.h).
        const int st = campaign::status(ui_, p, diff), nt = campaign::numTiers(ui_, diff);
        if (msg == "status") return r = Value::integer(st), true;
        if (msg == "get_cur_status_sym") return r = Value::symbol("status_" + std::to_string(st)), true;
        if (msg == "is_max_status") return r = Value::integer(nt < st ? 1 : 0), true;
        if (msg == "won_campaign") return r = Value::integer(nt <= st ? 1 : 0), true;
        if (msg == "get_status_progress") {
            // 0x82284F18: "(n OF m SONGS)" at difficulty arg, " " before the first.
            const int d = std::clamp(intArg(args, 0, diff), 0, Career::kDifficulties - 1);
            int done = 0, total = 0;
            const auto& tiers = campaign::config(ui_).tiers;
            for (int t = 0; t < campaign::numTiers(ui_, d) && t < int(tiers.size()); ++t) {
                for (const std::string& s : tiers[size_t(t)].second) done += campaign::passed(ui_, p, d, s) ? 1 : 0;
                total += int(tiers[size_t(t)].second.size()) - (d == 0 ? 1 : 0);
            }
            if (!done) return r = Value::string(ui_.localize("no_progress")), true;
            char buf[96];
            std::snprintf(buf, sizeof buf, ui_.localize("status_progress").c_str(), done, total);
            return r = Value::string(buf), true;
        }
        if (msg == "is_song_passed") return r = Value::integer(campaign::passed(ui_, p, diff, argStr(0)) ? 1 : 0), true;
        if (msg == "is_encore_song") return r = Value::integer(campaign::isEncore(ui_, argStr(0)) ? 1 : 0), true;
        if (msg == "get_cur_encore") return r = Value::symbol(campaign::encoreOf(ui_, campaign::venueOf(ui_, song))), true;
        if (msg == "final_song") {
            // 0x822A93B8: a tour song not passed yet that completes the last tier.
            if (campaign::isStoreSong(ui_, song) || campaign::isDownload(ui_, song) || campaign::passed(ui_, p, diff, song)) return true;
            const std::string v = campaign::venueOf(ui_, song);
            const bool last = !v.empty() && campaign::venueIndex(ui_, v) == nt - 1 &&
                              campaign::numPassed(ui_, p, diff, v) == campaign::requiredSongs(ui_, diff, v) - 1;
            return r = Value::integer(last ? 1 : 0), true;
        }
        if (msg == "encore_unlock_potential") {
            // 0x822AAB58: passing this song would unlock its venue's encore.
            if (campaign::isStoreSong(ui_, song) || campaign::isDownload(ui_, song) || diff == 0) return true;
            const std::string v = campaign::venueOf(ui_, song), enc = campaign::encoreOf(ui_, v);
            if (v.empty() || campaign::passed(ui_, p, diff, v) || campaign::unlockedAt(ui_, p, diff, enc) || enc == song) return true;
            const bool potential = campaign::numPassed(ui_, p, diff, v) + 1 == campaign::requiredSongs(ui_, diff, v) - 1 &&
                                   !campaign::passed(ui_, p, diff, song);
            return r = Value::integer(potential ? 1 : 0), true;
        }
        if (msg == "encore_newly_unlocked") {
            // 0x822AAA78: $freebird (by name) = the encore is Free Bird; true once the song is passed.
            const std::string enc = campaign::encoreOf(ui_, campaign::venueOf(ui_, song));
            if (!args.empty()) {
                std::string var = args[0].str();
                if (var.size() > 1 && var[0] == '$') var = var.substr(1);
                in.var(var) = Value::integer(enc == "freebird" ? 1 : 0);
            }
            return r = Value::integer(enc != song && campaign::passed(ui_, p, diff, song) ? 1 : 0), true;
        }
        if (msg == "store_songs_passed") {
            for (const std::string& s : campaign::config(ui_).storeSongs)
                if (!campaign::passed(ui_, p, diff, s)) return true;
            return r = Value::integer(1), true;
        }
        if (msg == "check_practice_achievement" || msg == "do_tutorial") return true;  // achievements; the tutorials (M11)
        if (msg == "finish_song") {
            // {campaign finish_song $score $cash $reason $venue $status $status_award}: the outputs by name.
            // finish_song's $vars come by name (Interp::byNameMessages): $score is read.
            int score = intArg(args, 0);
            if (!args.empty() && !args[0].isNumber()) {
                std::string var = args[0].str();
                if (var.size() > 1 && var[0] == '$') var = var.substr(1);
                score = in.var(var).integerValue();
            }
            const int stars = in.send("player0", "num_stars", {}).integerValue();
            const int hit = in.send("player0", "percent_hit", {}).integerValue();
            const campaign::FinishResult fr = campaign::finishSong(ui_, score, stars, hit);
            const Value outs[] = {Value::integer(fr.cash), Value::symbol(fr.cashReason), Value::symbol(fr.newVenue), Value::integer(fr.newStatus),
                                  Value::integer(fr.statusAward)};
            for (size_t k = 1; k < args.size() && k <= 5; ++k) {
                std::string var = args[k].str();
                if (var.size() > 1 && var[0] == '$') var = var.substr(1);
                in.var(var) = outs[k - 1];
            }
            SDL_Log("campaign: finish_song %s score %d stars %d: cash %d (%s) venue '%s' status %d (+%d)", song.c_str(), score, stars, fr.cash,
                    fr.cashReason.c_str(), fr.newVenue.c_str(), fr.newStatus, fr.statusAward);
            return true;
        }
        if (msg == "cheat_beat_song") return campaign::cheatBeatSong(ui_, argStr(0), intArg(args, 1)), true;
        if (msg == "cheat_gold_song") {
            // 0x822A8C20: the song's perfect flag.
            campaign::record(ui_, p, diff)[argStr(0)].perfect = true;
            return c.touch(), true;
        }
        if (msg == "buy_item") return campaign::buyItem(ui_, argStr(0), intArg(args, 1)), true;
        return false;
    }

private:
    UiManager& ui_;
    SongProviderObj& sp_;
    bool skipTutorials_ = false, tutorialAccess_ = false;  // +0x6c, +0x6b
};

// memcard: the save device. Results come back as memcard_result
// (MEMCARD_RESULT_MSG) on the next frame, as the 360's async calls do.
class MemcardObj : public ScriptObject {
public:
    explicit MemcardObj(UiManager& ui) : ScriptObject("memcard", "Memcard"), ui_(ui) {}
    void result(int code) {
        ui_.defer([this, code] { ui_.route("memcard_result", {Value::integer(code)}); });
    }
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        const int kNoError = ui_.macroInt("kMCNoError", 0), kExists = ui_.macroInt("kMCFileExists", -120),
                  kNotFound = ui_.macroInt("kMCFileNotFound", -301), kGeneral = ui_.macroInt("kMCGeneralError", -110);
        if (msg == "auto_load" || msg == "device_selector") return result(ui_.saveFile().exists() ? kExists : kNotFound), true;
        if (msg == "is_storage_device_valid") return r = Value::integer(ui_.saveFile().writable() ? 1 : 0), true;
        if (msg == "load_game") {
            int code = 0;
            ui_.loadSave(code);
            ui_.applyOptions();
            return result(code == 0 ? kNoError : code), true;
        }
        if (msg == "save_game") {
            std::string error;
            const bool ok = ui_.saveFile().write(ui_.career(), error);
            if (!ok) SDL_Log("save: %s", error.c_str());
            (void)args;
            return result(ok ? kNoError : kGeneral), true;
        }
        return false;
    }

private:
    UiManager& ui_;
};

// profilemgr: one local profile, always signed in (not to Xbox Live).
class ProfileMgrObj : public ScriptObject {
public:
    explicit ProfileMgrObj(UiManager& ui) : ScriptObject("profilemgr", "ProfileMgr"), ui_(ui) {}
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        if (msg == "active_padnum") return r = Value::integer(pad_), true;
        if (msg == "set_active_pad_from_player") return pad_ = std::max(0, intArg(args, 0)), true;
        if (msg == "clear_active_pad") return true;
        if (msg == "is_active_pad_signed_in") return r = Value::integer(1), true;
        if (msg == "is_active_pad_signed_in_live" || msg == "guide_showing") return true;
        if (msg == "get_name") return r = Value::string(userName()), true;
        if (msg == "enable_xmp" || msg == "disable_xmp" || msg == "add_sink" || msg == "remove_sink") return true;
        if (msg == "signin" || msg == "show_marketplace") {
            in.onStub("profilemgr", msg + " (Xbox Live)", r);
            return true;
        }
        (void)ui_;
        return false;
    }

private:
    UiManager& ui_;
    int pad_ = 0;
};

// options (0x822B1488): lefty, the volume indices, the sync offset.
class OptionsObj : public ScriptObject {
public:
    explicit OptionsObj(UiManager& ui) : ScriptObject("options", "Options"), ui_(ui) {}
    static float db(int idx) { return idx <= 0 ? -96.0f : 20.0f * std::log10(float(idx) / 11.0f); }
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>& args, Value& r) override {
        Career::Options& o = ui_.career().options;
        r = Value::integer(0);
        const auto field = [&](const std::string& k) -> int* {
            if (k == "band_volume_idx") return &o.bandVolumeIdx;
            if (k == "guitar_volume_idx") return &o.guitarVolumeIdx;
            if (k == "fx_volume_idx") return &o.fxVolumeIdx;
            return nullptr;
        };
        if (msg == "get") {
            int* f = args.empty() ? nullptr : field(args[0].str());
            return r = Value::integer(f ? *f : 0), true;
        }
        if (msg == "set") {
            if (int* f = args.size() < 2 ? nullptr : field(args[0].str())) *f = std::clamp(args[1].integerValue(), 0, 11), changed();
            return true;
        }
        if (msg == "get_lefty") return r = Value::integer(o.lefty[size_t(std::clamp(intArg(args, 0), 0, 1))] ? 1 : 0), true;
        if (msg == "set_lefty") {
            o.lefty[size_t(std::clamp(intArg(args, 0), 0, 1))] = args.size() > 1 && args[1].truthy();
            return changed(), true;
        }
        if (msg == "get_sync_offset") return r = Value::integer(o.syncOffsetMs), true;
        if (msg == "set_sync_offset") return o.syncOffsetMs = intArg(args, 0), changed(), true;
        if (msg == "get_volume_from_idx") return r = Value::real(db(intArg(args, 0, 11))), true;
        if (msg == "get_band_volume") return r = Value::real(db(o.bandVolumeIdx)), true;
        if (msg == "get_guitar_volume") return r = Value::real(db(o.guitarVolumeIdx)), true;
        if (msg == "get_fx_volume") return r = Value::real(db(o.fxVolumeIdx)), true;
        if (msg == "set_mute_fx") return true;
        return false;
    }
    void changed() {
        ui_.career().touch();  // L12: every setter marks the save
        ui_.applyOptions();
    }

private:
    UiManager& ui_;
};

// highscores: the current song's table at player 0's difficulty.
class HighScoresObj : public ScriptObject {
public:
    explicit HighScoresObj(UiManager& ui) : ScriptObject("highscores", "HighScores"), ui_(ui) {}
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        Career& c = ui_.career();
        const Session& s = ui_.session();
        const int d = s.players[0].difficulty;
        r = Value::integer(0);
        if (msg == "check_highscore") return r = Value::integer(c.checkHighScore(s.song, d, intArg(args, 0))), true;
        if (msg == "add") {
            if (args.size() >= 2) c.addHighScore(s.song, d, args[0].str(), args[1].integerValue());
            return true;
        }
        if (msg == "get_highscore") {
            // ($slot $name $score): $name and $score by name.
            // get_highscore is a by-name message: every $var comes as its name.
            const auto& t = c.highScores(s.song, d);
            const auto var = [](const Value& v) { std::string n = v.str(); return !n.empty() && n[0] == '$' ? n.substr(1) : n; };
            const int slot = args.empty() ? 0 : args[0].isNumber() ? args[0].integerValue() : in.var(var(args[0])).integerValue();
            if (slot >= 0 && slot < int(t.size()) && args.size() >= 3) {
                in.var(var(args[1])) = Value::string(t[size_t(slot)].name);
                in.var(var(args[2])) = Value::integer(t[size_t(slot)].score);
            }
            return true;
        }
        if (msg == "get_default_name") {
            std::string n = c.defaultName;
            if (n.empty()) {
                for (char ch : userName()) {
                    const char u = char(std::toupper(static_cast<unsigned char>(ch)));
                    if ((u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9')) n += u;
                }
                if (n.size() > 10) n.resize(10);
            }
            return r = Value::string(n), true;
        }
        if (msg == "set_default_name") return c.defaultName = args.empty() ? "" : args[0].str(), c.touch(), true;
        return false;
    }

private:
    UiManager& ui_;
};

// tips random_tip (0x82282830): the mode's tips (tips_<mode>, else
// tips_general) that fit player 0's difficulty.
class TipsObj : public ScriptObject {
public:
    explicit TipsObj(UiManager& ui) : ScriptObject("tips", "Tips"), ui_(ui) {}
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>&, Value& r) override {
        if (msg != "random_tip") return false;
        const DataNode* tips = ui_.bootTree() ? ui_.bootTree()->find("tips") : nullptr;
        const DataNode* list = tips ? tips->find("tips_" + ui_.session().mode) : nullptr;
        if (!list && tips) list = tips->find("tips_general");
        std::vector<std::string> pick;
        const int d = ui_.session().players[0].difficulty;
        for (size_t k = 1; list && k < list->children.size(); ++k) {
            const DataNode& t = list->children[k];
            if (!t.isList() || t.children.empty() || !t.children[0].isText()) continue;
            if (const DataNode* diff = t.find("diff")) {
                bool ok = false;
                for (size_t j = 1; j < diff->children.size(); ++j) ok = ok || (diff->children[j].type == DataNode::Type::Int && diff->children[j].i == d);
                if (!ok) continue;
            }
            pick.push_back(t.children[0].s);
        }
        r = Value::string(pick.empty() ? std::string() : ui_.localize(pick[size_t(in.randomInt(0, int(pick.size())))]));
        return true;
    }

private:
    UiManager& ui_;
};

// Sounds (synth, play_sfx): GH2's Synth through the host (audio/synth.h).
class SynthObj : public ScriptObject {
public:
    explicit SynthObj(UiManager& ui) : ScriptObject("synth", "Synth"), ui_(ui) {}
    // Synth::Handle 0x822EAB08 and Synth360's 0x82682338 [C].
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        if (msg == "play_sequence" || msg == "play") {
            const SfxArgs a = sfxArgs(in, args, 1);
            return ui_.playSfx(args.empty() ? std::string() : args[0].str(), a.volume, a.pan, a.transpose), true;
        }
        if (msg == "stop_all_sfx") return ui_.host().stopAllSfx(), true;
        if (msg == "pause_all_sfx") return ui_.host().pauseAllSfx(args.empty() || args[0].truthy()), true;
        // The master Fader (0x822F7878): the mute cheat's kDbSilence / 0.
        if (msg == "set_master_vol") {
            const Value v = args.empty() ? Value::integer(0) : in.evalValue(args[0]);
            return ui_.host().setSfxMasterDb(v.isNumber() ? v.number() : -96.0f), true;
        }
        if (msg == "master_vol") return r = Value::real(ui_.host().sfxMasterDb()), true;
        // The master limiter on the mastering voice (0x826822C8).
        if (msg == "toggle_limiter") return audiodev::toggleLimiter(), true;
        // set_fx / set_fx_vol: the PS2's SPU2 effect cores; Synth360's slots
        // for them are empty (vtable 0x820D4CF4) [C]. toggle_hud: Synth360's
        // debug overlay.
        if (msg == "set_fx" || msg == "set_fx_vol" || msg == "toggle_hud") return true;
        return false;
    }

private:
    UiManager& ui_;
};

// meta_music start / stop: the menus' music (config/synth.dta metamusic).
class MetaMusicObj : public ScriptObject {
public:
    explicit MetaMusicObj(UiManager& ui) : ScriptObject("meta_music", "MetaMusic"), ui_(ui) {}
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>&, Value& r) override {
        r = Value::integer(0);
        if (msg == "start") return ui_.host().menuMusic(true), true;
        if (msg == "stop") return ui_.host().menuMusic(false), true;
        return false;
    }

private:
    UiManager& ui_;
};

// Objects whose messages the port answers with nothing on purpose (render
// and audio switches of the 360 build, the world before M8).
class QuietObj : public ScriptObject {
public:
    QuietObj(UiManager& ui, const std::string& name, std::set<std::string> msgs) : ScriptObject(name, "Native"), ui_(ui), msgs_(std::move(msgs)) {}
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        if (msg == "play_sfx" || msg == "play_meta_sfx") {
            const SfxArgs a = sfxArgs(in, args, 1);
            return ui_.playSfx(args.empty() ? "" : args[0].str(), a.volume, a.pan, a.transpose), true;
        }
        if (msg == "show" && name() == "cheat_display") return SDL_Log("cheat: %s", args.empty() ? "" : args[0].str().c_str()), true;
        return msgs_.count(msg) != 0;
    }

private:
    UiManager& ui_;
    std::set<std::string> msgs_;
};

}  // namespace

void installNatives(UiManager& ui, Interp& in, std::vector<std::unique_ptr<ScriptObject>>& owned) {
    auto add = [&](std::unique_ptr<ScriptObject> o) -> ScriptObject* {
        ScriptObject* raw = o.get();
        in.registerObject(raw);
        owned.push_back(std::move(o));
        return raw;
    };
    auto sp = std::make_unique<SongProviderObj>(ui);
    SongProviderObj& spRef = *sp;
    add(std::move(sp));
    add(std::make_unique<CampaignObj>(ui, spRef));
    add(std::make_unique<MemcardObj>(ui));
    add(std::make_unique<ProfileMgrObj>(ui));
    add(std::make_unique<OptionsObj>(ui));
    add(std::make_unique<HighScoresObj>(ui));
    add(std::make_unique<TipsObj>(ui));
    add(std::make_unique<SynthObj>(ui));
    add(std::make_unique<MetaMusicObj>(ui));
    // Xbox Live and its services: logged STUBs (tests/ui_stubs.txt).
    for (const char* stub : {"achievements", "leaderboards", "content_mgr"}) add(std::make_unique<StubObject>(stub, "XboxLive"));
    add(std::make_unique<QuietObj>(ui, "rnd", std::set<std::string>{"suspend", "unset_depth_of_field", "show_overlay", "toggle_overlay",
                                                                     "reflect", "reset_postproc", "overlay_showing", "aspect"}));
    add(std::make_unique<QuietObj>(ui, "crowd_audio", std::set<std::string>{"turn_off", "enabled", "set_enabled", "set_paused"}));
    add(std::make_unique<QuietObj>(ui, "world", std::set<std::string>{"set", "get", "current_shot", "add_sink", "lighting"}));
    add(std::make_unique<QuietObj>(ui, "music_fader", std::set<std::string>{"fade"}));
    add(std::make_unique<QuietObj>(ui, "encore_stream", std::set<std::string>{"play", "stop"}));
    add(std::make_unique<QuietObj>(ui, "cheat_display", std::set<std::string>{"show"}));
    // The PlayerMatchers (vtable 0x8204a1ac): their handlers are gh2.dta's
    // (beatmatch (player_matcher_handlers ...)) (0x822E44B0), added after
    // the boot. on_start_starpower / on_stop_starpower tell the player's
    // character (0x822E3840 / 0x822E36B8), which the program's band does on
    // the Star Power events (game/world_driver.cpp) [C].
    for (const char* m : {"player_matcher0", "player_matcher1"})
        add(std::make_unique<QuietObj>(ui, m, std::set<std::string>{"add_sink", "remove_sink", "auto_solo", "set_auto_solo",
                                                                    "on_start_starpower", "on_stop_starpower", "refresh_track_buttons"}));
    installGameNatives(ui, in, owned);
    chars::installNatives(ui, in, owned);
    store::installNatives(ui, in, owned);

    // Builtins the host gives.
    in.addBuiltin("play_sfx", [&ui](Interp& i, const DataNode& c) {
        const SfxArgs a = sfxArgs(i, i.evalArgs(c, 2), 0);
        ui.playSfx(i.evalArg(c, 1).str(), a.volume, a.pan, a.transpose);
        return Value::integer(0);
    });
    in.addBuiltin("stop_sfx", [&ui](Interp& i, const DataNode& c) {
        ui.host().stopSfx(i.evalArg(c, 1).str());
        return Value::integer(0);
    });
    in.addBuiltin("song_preview", [&ui](Interp& i, const DataNode& c) {
        const std::string s = c.children.size() > 1 ? i.evalArg(c, 1).str() : std::string();
        if (s != ui.session().lastPreview) {
            ui.session().lastPreview = s;
            ui.host().songPreview(s);
        }
        return Value::integer(0);
    });
    // {joypad_is_connected n} / {joypad_controller_type n} (0x8227CA48's
    // natives): the port's pad n is its input slot n (the keyboard's split
    // layout plays both slots); a guitar is ro_guitar_xbox, a pad analog
    // (joypad.dta's HX_XBOX controllers) [C data; I: the keyboard as a guitar].
    in.addBuiltin("joypad_is_connected", [&ui](Interp& i, const DataNode& c) {
        const int n = i.evalArg(c, 1).integerValue();
        return Value::integer(n >= 0 && n < 2 && ui.input().padConnected(n) ? 1 : 0);
    });
    in.addBuiltin("joypad_controller_type", [&ui](Interp& i, const DataNode& c) {
        const int n = i.evalArg(c, 1).integerValue();
        if (n < 0 || n >= 2 || !ui.input().padConnected(n)) return Value::symbol("none");
        for (const Input::Device& d : ui.input().devices())
            if (d.id != "keyboard" && d.player == n)
                return Value::symbol(ui.input().gh2Type(d.id).find("joypad") != std::string::npos ? "analog" : "ro_guitar_xbox");
        return Value::symbol("ro_guitar_xbox");  // the keyboard
    });
    in.addBuiltin("timer_ms", [](Interp&, const DataNode&) { return Value::integer(0); });
    in.addBuiltin("set_timer_ms", [](Interp&, const DataNode&) { return Value::integer(0); });
}

}  // namespace ui
