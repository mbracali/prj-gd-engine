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
#include "script/tasks.h"
#include "ui/natives.h"
#include "ui/sfx_args.h"
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

// The campaign's song lists (config/gh2.dta (campaign (order ...)), (store (song ...)), (downloads)).
struct SongLists {
    std::vector<std::pair<std::string, std::vector<std::string>>> tiers;  // venue -> songs, in order
    std::vector<std::string> store, downloads;
};

SongLists songLists(UiManager& ui) {
    SongLists l;
    const DataNode* boot = ui.bootTree();
    if (const DataNode* c = boot ? boot->find("campaign") : nullptr)
        if (const DataNode* order = c->find("order"))
            for (size_t k = 1; k < order->children.size(); ++k) {
                const DataNode& t = order->children[k];
                if (!t.isList() || t.children.empty()) continue;
                std::vector<std::string> songs;
                for (size_t s = 1; s < t.children.size(); ++s)
                    if (t.children[s].isText()) songs.push_back(t.children[s].s);
                l.tiers.push_back({t.children[0].s, songs});
            }
    if (const DataNode* st = boot ? boot->find("store") : nullptr)
        if (const DataNode* songs = st->find("song"))
            for (size_t k = 1; k < songs->children.size(); ++k)
                if (songs->children[k].isList() && !songs->children[k].children.empty()) l.store.push_back(songs->children[k].children[0].s);
    if (const DataNode* d = boot ? boot->find("downloads") : nullptr)
        for (size_t k = 1; k < d->children.size(); ++k) {
            if (d->children[k].isText()) l.downloads.push_back(d->children[k].s);
            else if (d->children[k].isList() && !d->children[k].children.empty()) l.downloads.push_back(d->children[k].children[0].s);
        }
    return l;
}

// song_provider: the song lists of the song screens (kSongsCareer 0,
// kSongsQuickplay 1, kSongsLeaderboards 2), with tier headers.
class SongProviderObj : public ListProvider {
public:
    explicit SongProviderObj(UiManager& ui) : ListProvider("song_provider", "SongProvider"), ui_(ui) {}

    int numData() override { return int(data_.size()); }
    std::string symbol(int i) override { return i >= 0 && i < int(data_.size()) ? data_[size_t(i)] : std::string(); }

    // 0x822C5A68 / 0x82287600 [C]: quickplay offers the songs of the first
    // two tiers, every song a profile has unlocked at any difficulty,
    // bought bonus songs and downloaded songs (all of them with the
    // all-access cheat), in campaign order; a header before each group.
    void build() {
        data_.clear();
        headers_.clear();
        const SongLists l = songLists(ui_);
        std::set<std::string> offered;
        for (size_t t = 0; t < l.tiers.size() && t < 2; ++t) offered.insert(l.tiers[t].second.begin(), l.tiers[t].second.end());
        Career& c = ui_.career();
        for (const Career::Profile& p : c.profiles) {
            if (!p.inUse) continue;
            // A profile's unlocked tiers: the first, and one more per completed tier [I].
            for (int d = 0; d < Career::kDifficulties; ++d)
                for (int t = 0; t <= p.status[size_t(d)] && t < int(l.tiers.size()); ++t)
                    offered.insert(l.tiers[size_t(t)].second.begin(), l.tiers[size_t(t)].second.end());
        }
        for (const auto& [item, on] : c.items)
            if (on) offered.insert(item);
        const bool all = allAccess;
        const auto have = [&](const std::string& s) { return songInfoIndex(ui_, s) >= 0; };
        for (const auto& [tier, songs] : l.tiers) {
            bool first = true;
            for (const std::string& s : songs) {
                if (!have(s) || (!all && !offered.count(s))) continue;
                if (first) headers_.push_back({int(data_.size()), tier}), first = false;
                data_.push_back(s);
            }
        }
        footer_ = int(data_.size()) - 1;  // the last tier song carries the quickplay blurb
        for (const auto* group : {&l.store, &l.downloads}) {
            bool first = true;
            for (const std::string& s : *group) {
                if (!have(s) || (!all && group == &l.store && !offered.count(s))) continue;
                if (first) headers_.push_back({int(data_.size()), group == &l.store ? "store" : "download"}), first = false;
                data_.push_back(s);
            }
        }
    }

    int numHeaders(int pos) const {
        int n = 0;
        for (const auto& [idx, tier] : headers_)
            if (idx <= pos) ++n;
        return n;
    }

    void rows(std::vector<gfx::UiView::ListRow>& out, int selected, int& selectedRow) override {
        // One row per song at slot index + headers above it: a group's first
        // song carries its header.txt (placed a slot up by the template),
        // the camera follows the slot (sel_song.view frame pos + num_headers).
        out.clear();
        selectedRow = 0;
        for (int i = 0; i < int(data_.size()); ++i) {
            gfx::UiView::ListRow r;
            r.slot = i + numHeaders(i);
            for (const auto& [idx, tier] : headers_)
                if (idx == i) r.header = true, r.texts["header.txt"] = ui_.localize("song_header_" + tier);
            const int info = songInfoIndex(ui_, data_[size_t(i)]);
            r.texts["list.txt"] = info >= 0 ? ui_.host().songs()[size_t(info)].name : data_[size_t(i)];
            // Quickplay shows no scores, stars or checkmarks outside co-op (0x822C3428 / 0x822C34E8) [C].
            if (mode == 1 && i == footer_) r.texts["blurb.txt"] = ui_.localize("song_quickplay");
            if (i == selected) selectedRow = int(out.size());
            out.push_back(std::move(r));
        }
    }

    bool handleNative(Interp&, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        if (msg == "set_mode") return mode = intArg(args, 0), build(), true;
        if (msg == "get_mode") return r = Value::integer(mode), true;
        if (msg == "init_data") return build(), true;
        if (msg == "set_store_mode") return true;
        if (msg == "num_headers") return r = Value::integer(numHeaders(intArg(args, 0))), true;
        if (msg == "get_symbol") return r = Value::symbol(symbol(intArg(args, 0))), true;
        if (msg == "list_length" || msg == "num_data") return r = Value::integer(numData()), true;
        if (msg == "has_instrument") return r = Value::integer(1), true;
        if (msg == "get_instrument") return r = Value::symbol("guitar"), true;
        if (msg == "track_num_of_instrument") return r = Value::integer(0), true;
        return false;
    }

    int mode = 0;
    bool allAccess = false;

private:
    UiManager& ui_;
    std::vector<std::string> data_;
    std::vector<std::pair<int, std::string>> headers_;
    int footer_ = -1;
};

// campaign: the save's profiles and unlocks (Campaign 0x82287F58).
class CampaignObj : public ScriptObject {
public:
    CampaignObj(UiManager& ui, SongProviderObj& sp) : ScriptObject("campaign", "Campaign"), ui_(ui), sp_(sp) {}
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>& args, Value& r) override {
        Career& c = ui_.career();
        r = Value::integer(0);
        const auto argStr = [&](size_t i) { return i < args.size() ? nameOf(args[i]) : std::string(); };
        if (msg == "clear") return slot_ = -1, true;  // the splash: no profile chosen
        if (msg == "num_profiles") return r = Value::integer(c.numProfiles()), true;
        if (msg == "tutorials_done") return r = Value::integer(slot_ >= 0 ? c.profiles[size_t(slot_)].tutorialsDone : 0), true;
        if (msg == "needs_save") return r = Value::integer(c.needsSave ? 1 : 0), true;
        if (msg == "save_complete") return c.needsSave = false, true;
        if (msg == "all_access") return r = Value::integer(sp_.allAccess ? 1 : 0), true;
        if (msg == "set_all_access") return sp_.allAccess = !args.empty() && args[0].truthy(), true;
        if (msg == "profile_slot") return r = Value::integer(slot_), true;
        if (msg == "set_profile_slot") return slot_ = intArg(args, 0, -1), true;
        if (msg == "profile_name") {
            const int s = intArg(args, 0, slot_);
            return r = Value::string(s >= 0 && s < Career::kProfiles ? c.profiles[size_t(s)].name : std::string()), true;
        }
        if (msg == "is_empty_profile") {
            const int s = intArg(args, 0, 0);
            return r = Value::integer(s >= 0 && s < Career::kProfiles && !c.profiles[size_t(s)].inUse ? 1 : 0), true;
        }
        if (msg == "is_unlocked") return r = Value::integer(sp_.allAccess || (c.items.count(argStr(0)) && c.items[argStr(0)]) ? 1 : 0), true;
        if (msg == "is_video_unlocked") return r = Value::integer(sp_.allAccess || (c.items.count(argStr(0)) && c.items[argStr(0)]) ? 1 : 0), true;
        if (msg == "is_song_passed") {
            for (const Career::Profile& p : c.profiles)
                for (const auto& d : p.results)
                    if (auto it = d.find(argStr(0)); it != d.end() && it->second.passed) return r = Value::integer(1), true;
            return true;
        }
        if (msg == "store_songs_passed" || msg == "won_campaign" || msg == "final_song" || msg == "is_encore_song" ||
            msg == "encore_unlock_potential" || msg == "encore_newly_unlocked" || msg == "is_store_song" || msg == "is_max_status" ||
            msg == "num_guitar_awards" || msg == "do_tutorial" || msg == "tutorial_access")
            return true;  // career only: nothing in a fresh quickplay session
        if (msg == "status" || msg == "cash" || msg == "starting_cash" || msg == "cash_spent") return true;
        if (msg == "pick_attract_song") {
            // A song quickplay offers (the attract mode).
            sp_.mode = 1;
            sp_.build();
            const int n = sp_.numData();
            return r = Value::symbol(n > 0 ? sp_.symbol(ui_.interp().randomInt(0, n)) : std::string()), true;
        }
        if (msg == "get_status_progress") return r = Value::string(""), true;
        return false;
    }

private:
    UiManager& ui_;
    SongProviderObj& sp_;
    int slot_ = -1;
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
    add(std::make_unique<QuietObj>(ui, "store_item_provider", std::set<std::string>{"check_achievement", "init_data"}));
    add(std::make_unique<QuietObj>(ui, "player_matcher0", std::set<std::string>{"add_sink", "remove_sink", "auto_solo", "set_auto_solo"}));
    installGameNatives(ui, in, owned);

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
    in.addBuiltin("joypad_controller_type", [](Interp&, const DataNode&) { return Value::symbol("guitar"); });
    in.addBuiltin("joypad_is_connected", [](Interp&, const DataNode&) { return Value::integer(1); });
    in.addBuiltin("timer_ms", [](Interp&, const DataNode&) { return Value::integer(0); });
    in.addBuiltin("set_timer_ms", [](Interp&, const DataNode&) { return Value::integer(0); });
}

}  // namespace ui
