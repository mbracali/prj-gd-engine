// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/career_objects.h"

#include <algorithm>
#include <cstdio>

#include "game/campaign.h"

namespace {

Value sym(const std::string& s) { return Value::symbol(s); }
Value integer(int i) { return Value::integer(i); }

// An input argument of a message whose variables bind by name: a name is read from the variable.
Value input(Interp& in, const Value& v) {
    if (v.type != Value::Type::Symbol) return v;
    std::string n = v.str();
    if (!n.empty() && n[0] == '$') n = n.substr(1);
    return in.var(n);
}

std::string varName(const Value& v) {
    std::string n = v.str();
    return !n.empty() && n[0] == '$' ? n.substr(1) : n;
}

// `campaign` (Campaign_v1 @0x1395e0; docs/career.md §2.1).
class CampaignObj : public ScriptObject {
public:
    explicit CampaignObj(CareerContext ctx) : ScriptObject("campaign"), ctx_(std::move(ctx)) {}
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& a, Value& r) override {
        Campaign& c = *ctx_.campaign;
        auto arg = [&](size_t k) { return k < a.size() ? a[k] : Value::integer(0); };
        const int d = ctx_.difficulty();
        r = integer(0);
        if (msg == "all_access") return r = integer(c.allAccess), true;
        if (msg == "set_all_access") return c.allAccess = arg(0).truthy(), changed(), true;
        if (msg == "num_profiles") return r = integer(c.numProfiles()), true;
        if (msg == "max_num_profiles") return r = integer(8), true;
        if (msg == "has_profile_name") return r = integer(c.hasProfileName(arg(0).str(), arg(1).truthy())), true;
        if (msg == "set_profile_name") {
            c.profile().name = arg(0).str();
            c.profile().empty = false;
            c.needsSave = true;
            return true;
        }
        if (msg == "profile_name") {
            int s = a.empty() ? c.slot() : std::clamp(arg(0).integerValue(), 0, 7);
            return r = Value::string(c.profile(s).empty ? "Someone" : c.profile(s).name), true;
        }
        if (msg == "is_empty_profile") return r = integer(c.profile(std::clamp(arg(0).integerValue(), 0, Campaign::kGlitchesSlot)).empty), true;
        if (msg == "empty_slot") return r = integer(c.emptySlot()), true;
        if (msg == "delete_slot") return c.deleteSlot(arg(0).integerValue()), changed(), true;
        if (msg == "profile_slot") return r = integer(c.slot()), true;
        if (msg == "set_profile_slot") return c.setSlot(arg(0).integerValue()), changed(), true;
        if (msg == "new_campaign") return r = integer(c.newCampaign(d)), true;
        if (msg == "set_character_info") {  // FUN_001393b0
            std::string ch, g, sk;
            ctx_.getPlayer(ch, g, sk);
            Campaign::Career& cr = c.career(d);
            if (cr.character != ch || cr.guitar != g || cr.skin != sk) c.needsSave = true;
            cr.character = ch, cr.guitar = g, cr.skin = sk;
            return true;
        }
        if (msg == "get_character_info") {
            const Campaign::Career& cr = c.career(d);
            ctx_.setPlayer(cr.character, cr.guitar, cr.skin);
            return true;
        }
        if (msg == "is_unlocked") return r = integer(c.isUnlocked(d, arg(0).str())), true;
        if (msg == "is_store_song") return r = integer(c.isStoreSong(arg(0).str())), true;
        if (msg == "foreach_venue" && a.size() >= 2) {  // FUN_00138cf0
            std::string var = varName(a[0]);
            Value saved = in.var(var);
            for (const std::string& v : c.venues()) {
                in.var(var) = sym(v);
                for (size_t k = 1; k < a.size(); ++k)
                    if (a[k].type == Value::Type::Code && a[k].code) in.eval(*a[k].code);
            }
            in.var(var) = saved;
            return true;
        }
        if (msg == "finish_song") {  // FUN_00138ec0
            Campaign::FinishResult fr = c.finishSong(d, ctx_.song(), input(in, arg(0)).integerValue(), ctx_.starsOf);
            if (a.size() > 1) in.var(varName(a[1])) = sym(fr.newVenue);
            if (a.size() > 2) in.var(varName(a[2])) = integer(fr.newStatus);
            changed();
            return true;
        }
        if (msg == "cheat_beat_song") {
            if (Campaign::Item* it = c.item(d, arg(0).str())) {
                it->beaten = true;
                it->score = std::max(it->score, arg(1).integerValue());
                if (arg(1).integerValue() == 1000000) it->stars = 5;
                c.unlockNextVenue(d, it->sym);
            }
            changed();
            return true;
        }
        if (msg == "has_cash_award") return r = integer(c.hasCashAward()), true;
        if (msg == "next_cash_award") {
            Campaign::CashAward aw{0, ""};
            bool got = c.nextCashAward(aw);
            if (a.size() > 0) in.var(varName(a[0])) = integer(got ? aw.amount : 0);
            if (a.size() > 1) in.var(varName(a[1])) = got ? sym(aw.reason) : integer(0);
            return r = integer(got), true;
        }
        if (msg == "status") return r = integer(c.status(d)), true;
        if (msg == "get_cur_status_sym") return r = sym("status_" + std::to_string(c.status(d))), true;
        if (msg == "get_status_progress") {  // FUN_00139440
            int dd = a.empty() ? d : std::clamp(arg(0).integerValue(), 0, 3);
            int beaten = c.songsBeaten(dd);
            if (beaten == 0) return r = Value::string(""), true;
            char buf[160];
            std::snprintf(buf, sizeof buf, ctx_.localize("status_progress").c_str(),
                          ctx_.localize("status_" + std::to_string(c.status(dd))).c_str(), beaten, c.totalSongs(dd));
            return r = Value::string(buf), true;
        }
        if (msg == "mark_needs_save") return c.needsSave = true, true;
        if (msg == "needs_save") return r = integer(c.needsSave), true;
        if (msg == "save_complete") return c.needsSave = false, true;
        if (msg == "add_cash") return c.addCash(d, arg(0).integerValue()), true;
        if (msg == "cash") return r = integer(c.career(d).cash), true;
        if (msg == "career_score") return r = integer(c.careerScore(d)), true;
        if (msg == "high_score") {
            const Campaign::Item* it = c.item(d, arg(0).str());
            return r = integer(it ? it->score : 0), true;
        }
        if (msg == "buy_item") return r = integer(c.buyItem(d, arg(0).str(), arg(1).integerValue())), changed(), true;
        if (msg == "unlock_global_item") return c.unlockGlobalItem(arg(0).str()), changed(), true;
        if (msg == "skip_tutorials") return c.skipTutorials = arg(0).truthy(), true;
        if (msg == "tutorials_done")
            return r = integer(c.skipTutorials || c.tutorialAccess ? 3 : c.profile().tutorialsDone), true;
        if (msg == "do_tutorial") {
            int n = arg(0).integerValue();
            if (n > c.profile().tutorialsDone) c.profile().tutorialsDone = n, c.needsSave = true;
            return true;
        }
        if (msg == "tutorial_access") return r = integer(c.tutorialAccess), true;
        if (msg == "set_tutorial_access") return c.tutorialAccess = arg(0).truthy(), true;
        if (msg == "update_difficulty") return c.profile().lastDifficulty = d, true;
        if (msg == "last_difficulty") return r = integer(c.profile().lastDifficulty), true;
        if (msg == "final_song") return r = integer(c.finalSong(d, ctx_.song())), true;
        if (msg == "get_random_song") {  // FUN_00138620
            std::vector<std::string> all = c.careerSongs(3);
            for (const std::string& s : c.storeSongs())
                if (c.isUnlocked(d, s)) all.push_back(s);
            return r = sym(all.empty() ? "" : all[size_t(std::rand()) % all.size()]), true;
        }
        return false;  // beat_song: unhandled in the original too
    }

private:
    void changed() {
        if (ctx_.changed) ctx_.changed();
    }
    CareerContext ctx_;
};

// `highscores` (HighScoreDB_v1 @0x151298).
class HighScoresObj : public ScriptObject {
public:
    explicit HighScoresObj(CareerContext ctx) : ScriptObject("highscores"), ctx_(std::move(ctx)) {}
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& a, Value& r) override {
        Campaign& c = *ctx_.campaign;
        auto arg = [&](size_t k) { return k < a.size() ? a[k] : Value::integer(0); };
        r = integer(0);
        if (msg == "check_highscore") return r = integer(c.checkHighScore(ctx_.song(), ctx_.difficulty(), arg(0).integerValue())), true;
        if (msg == "get_highscore") {
            const auto& l = c.highScores(ctx_.song(), ctx_.difficulty());
            size_t i = size_t(std::clamp(input(in, arg(0)).integerValue(), 0, 4));
            if (a.size() > 1) in.var(varName(a[1])) = Value::string(i < l.size() ? l[i].name : "");
            if (a.size() > 2) in.var(varName(a[2])) = integer(i < l.size() ? l[i].score : 0);
            return true;
        }
        if (msg == "add") return c.addHighScore(ctx_.song(), ctx_.difficulty(), arg(0).str(), arg(1).integerValue()), true;
        return false;
    }

private:
    CareerContext ctx_;
};

// `options`: the current profile's copy (docs/career.md §1.3).
class OptionsObj : public ScriptObject {
public:
    explicit OptionsObj(CareerContext ctx) : ScriptObject("options"), ctx_(std::move(ctx)) {}
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>& a, Value& r) override {
        Campaign::Options& o = ctx_.campaign->profile().options;
        auto arg = [&](size_t k) { return k < a.size() ? a[k] : Value::integer(0); };
        r = integer(0);
        if (msg == "get_fx_volume") return r = Value::real(o.fx), true;
        if (msg == "get_band_volume") return r = Value::real(o.band), true;
        if (msg == "get_guitar_volume") return r = Value::real(o.guitar), true;
        // [I] A changed option marks the save (options live in the profile), so
        // autosave_goto writes it: Lefty Flip and the volumes survive a restart.
        auto set = [&](auto& field, auto value) {
            if (field != value) field = value, ctx_.campaign->needsSave = true;
            return true;
        };
        if (msg == "set_fx_volume") return set(o.fx, arg(0).number());
        if (msg == "set_band_volume") return set(o.band, arg(0).number());
        if (msg == "set_guitar_volume") return set(o.guitar, arg(0).number());
        if (msg == "stereo" || msg == "get_stereo") return r = integer(o.stereo), true;
        if (msg == "set_stereo") return set(o.stereo, arg(0).truthy());
        if (msg == "lefty" || msg == "get_lefty") return r = integer(o.lefty[std::clamp(arg(0).integerValue(), 0, 1)]), true;
        if (msg == "set_lefty") return set(o.lefty[std::clamp(arg(0).integerValue(), 0, 1)], arg(1).truthy());
        return true;
    }

private:
    CareerContext ctx_;
};

// A list provider over campaign items (store and character providers, §2.3).
class ItemProviderObj : public ScriptObject {
public:
    ItemProviderObj(const std::string& name, CareerContext ctx, std::function<std::vector<std::string>(bool storeMode)> list,
                    std::function<std::string(const std::string&)> parent = {})
        : ScriptObject(name), ctx_(std::move(ctx)), list_(std::move(list)), parent_(std::move(parent)) {}
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>& a, Value& r) override {
        std::vector<std::string> items = list_(storeMode_);
        int i = a.empty() ? 0 : a[0].integerValue();
        const std::string item = i >= 0 && i < int(items.size()) ? items[size_t(i)] : "";
        r = integer(0);
        if (msg == "list_length" || msg == "num_data") return r = integer(int(items.size())), true;
        if (msg == "get_symbol") return r = sym(item), true;
        if (msg == "get_text") return r = Value::string(ctx_.localize(item)), true;
        if (msg == "get_guitar" && parent_) return r = sym(parent_(item)), true;
        if (msg == "set_store_mode") return storeMode_ = !a.empty() && a[0].truthy(), true;
        // VideoProvider_v1 @0x11bbe0.
        if (msg == "get_video_file" && name() == "video_provider" && !a.empty())
            return r = Value::string(ctx_.campaign->videoFile(a[0].str())), true;
        return false;
    }

private:
    CareerContext ctx_;
    std::function<std::vector<std::string>(bool)> list_;
    std::function<std::string(const std::string&)> parent_;
    bool storeMode_ = false;
};

std::vector<std::string> itemsOf(Campaign& c, int d, Campaign::Category cat, bool storeOnly, bool unlockedOnly) {
    std::vector<std::string> out;
    for (const Campaign::Item& it : c.career(d).items)
        if (it.cat == cat && (!storeOnly || it.isStore) && (!unlockedOnly || it.unlocked || c.allAccess)) out.push_back(it.sym);
    return out;
}

}  // namespace

std::vector<std::unique_ptr<ScriptObject>> makeCareerObjects(const CareerContext& ctx) {
    std::vector<std::unique_ptr<ScriptObject>> out;
    out.push_back(std::make_unique<CampaignObj>(ctx));
    out.push_back(std::make_unique<HighScoresObj>(ctx));
    out.push_back(std::make_unique<OptionsObj>(ctx));
    Campaign* c = ctx.campaign;
    auto diff = ctx.difficulty;
    out.push_back(std::make_unique<ItemProviderObj>("character_provider", ctx, [c, diff](bool store) {
        return store ? itemsOf(*c, diff(), Campaign::kCharacter, true, false) : itemsOf(*c, diff(), Campaign::kCharacter, false, true);
    }));
    out.push_back(std::make_unique<ItemProviderObj>("store_guitar_provider", ctx, [c, diff](bool) {
        return itemsOf(*c, diff(), Campaign::kGuitar, true, false);
    }));
    // Store skins whose parent guitar is unlocked (FUN_0011ac28); config/guitars.dta skins follow their guitar.
    auto parentOf = [c](const std::string& skin) { return c->guitarOfSkin(skin); };
    out.push_back(std::make_unique<ItemProviderObj>(
        "store_skin_provider", ctx,
        [c, diff, parentOf](bool) {
            std::vector<std::string> list;
            for (const std::string& s : itemsOf(*c, diff(), Campaign::kSkin, true, false))
                if (c->isUnlocked(diff(), parentOf(s))) list.push_back(s);
            return list;
        },
        parentOf));
    out.push_back(std::make_unique<ItemProviderObj>("video_provider", ctx, [c, diff](bool) {
        return itemsOf(*c, diff(), Campaign::kVideo, false, false);
    }));
    return out;
}
