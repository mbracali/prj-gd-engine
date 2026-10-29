// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/uitest.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "data/game_data.h"
#include "data/json.h"
#include "game/song_db.h"
#include "input/input.h"
#include "render/canvas.h"
#include "scene/milo.h"
#include "ui/campaign.h"
#include "ui/natives.h"
#include "ui/ui_host.h"
#include "ui/ui_manager.h"
#include "ui/ui_objects.h"

namespace {

// A host without a song or window: the checks run the scripts only.
class NullHost final : public UiHost {
public:
    explicit NullHost(std::vector<SongInfo> s) : songs_(std::move(s)) {}
    const std::vector<SongInfo>& songs() const override { return songs_; }
    bool gameLoad(const GameSetup&, std::string&) override { return false; }
    void gameBegin() override {}
    void gameEnd() override {}
    bool gameLoaded() const override { return false; }
    void gameSetPaused(bool) override {}
    bool gamePaused() const override { return false; }
    double gameTimeMs() const override { return 0; }
    float gameBeat() const override { return 0; }
    float songDurationMs() const override { return 0; }
    const Player* gamePlayer(int) const override { return nullptr; }
    int gameCounter(int, const std::string&) const override { return 0; }
    void setMusicSpeed(float) override {}
    void setMusicPaused(bool) override {}
    void trackCommand(const std::string&, const std::vector<Value>&) override {}
    void hudCommand(const std::string&, const std::vector<Value>&) override {}
    void drawTrack() override {}
    void drawHud() override {}
    void songPreview(const std::string&) override {}
    void menuMusic(bool) override {}
    void creditsMusic(bool) override {}
    void playMovie(const std::string&) override {}
    void applyOptions(float, float, float, bool, bool, int) override {}
    void quit() override {}

private:
    std::vector<SongInfo> songs_;
};

bool componentName(const std::string& s) {
    static const char* const kExt[] = {".lbl", ".btn", ".lst", ".view", ".grp", ".mesh", ".tnm", ".mnm", ".sld", ".chk", ".ten", ".pic", ".filt", ".txt"};
    if (s.find('%') != std::string::npos) return false;
    for (const char* e : kExt) {
        const size_t n = std::strlen(e);
        if (s.size() > n && s.compare(s.size() - n, n, e) == 0) return true;
    }
    return false;
}

// Component names in a handler; {<other panel> find x} names another
// panel's component and is skipped.
void collectNames(const DataNode& n, const std::string& self, std::set<std::string>& out) {
    if (n.type == DataNode::Type::Symbol && componentName(n.s)) out.insert(n.s);
    if (n.type == DataNode::Type::Command && n.children.size() >= 3 && n.children[1].isText() && n.children[1].s == "find" &&
        n.children[0].isText() && n.children[0].s != self) {
        collectNames(n.children[0], self, out);
        return;
    }
    for (const DataNode& c : n.children) collectNames(c, self, out);
}

// Every {<object> <message> ...} the scripts send, as "object message".
void collectSends(const DataNode& n, std::set<std::string>& out) {
    if (n.type == DataNode::Type::Command && n.children.size() >= 2 && n.children[1].isText()) {
        if (n.children[0].isText()) out.insert(n.children[0].s + " " + n.children[1].s);
        if (n.children[0].type == DataNode::Type::Var)
            out.insert((n.children[0].s.empty() || n.children[0].s[0] != '$' ? "$" : "") + n.children[0].s + " " + n.children[1].s);
    }
    for (const DataNode& c : n.children) collectSends(c, out);
}

// A handler some code can reach: a message the UI sends (lifecycle, the
// _MSG macros), or one a script sends to the panel by name or as $this.
bool reachable(const std::string& name, const std::string& panel, const std::set<std::string>& sends, const std::set<std::string>& own) {
    static const std::set<std::string> kMessages = {"enter", "exit", "poll", "load", "finish_load", "unload", "button_down", "button_up",
        "component_select", "component_select_start", "component_focus", "component_scroll", "transition_complete", "screen_change",
        "text_entry", "char_added", "char_removed", "component_bad_selection", "memcard_result", "slider_start_msg", "slider_select_cancel"};
    return kMessages.count(name) || sends.count(panel + " " + name) || own.count("$this " + name);
}

// The milos a panel's (file ...) can name ({if_else ... a.milo b.milo}).
void fileChoices(const DataNode& n, std::vector<std::string>& out) {
    if (n.isText() && n.s.size() > 5 && n.s.compare(n.s.size() - 5, 5, ".milo") == 0) out.push_back(n.s);
    for (const DataNode& c : n.children) fileChoices(c, out);
}

// The object names of a milo, its inline and ObjectDir subdirectories.
void miloNames(const GameData& data, scene::MiloLibrary& lib, const std::string& path, std::set<std::string>& out, int depth = 0) {
    if (depth > 4 || !data.exists(path)) return;
    scene::Milo& m = lib.load(path);
    m.root().forEach([&](scene::Object& o) { out.insert(o.name); });
    const auto* own = m.root().self ? m.root().self->as<scene::DirObj>() : nullptr;
    if (!own) return;
    for (const std::string& sub : own->objectDir.subdirs) miloNames(data, lib, scene::findMiloRelative(data, path, sub), out, depth + 1);
}

}  // namespace

// GH2's career rules (ui/campaign.h) on a scratch band, Hard: the cash of
// stars (this and the easier difficulties), the encore, the tier, the
// store. Returns the failures.
static int campaignChecks(ui::UiManager& ui) {
    namespace cp = ui::campaign;
    int fails = 0, n = 0;
    auto check = [&](bool ok, const std::string& what) {
        ++n;
        if (!ok) std::printf("  FAIL campaign: %s\n", what.c_str()), ++fails;
    };
    Career saved = ui.career();
    ui::Session& s = ui.session();
    const ui::Session keep = s;
    ui.career().profiles = {};  // GH2's rules on a save without the port's band
    Career::Profile& p = ui.career().profiles[0];
    p = Career::Profile{};
    p.inUse = true, p.name = "T", p.cash = 50;
    s.profileSlot = 0, s.mode = "career", s.players[0].difficulty = 2, s.venue = "battle", s.allAccess = false;
    // This disc's campaign.dta: six tiers of five songs (the last the
    // encore), required songs Easy 3/4, Medium 4/5, Hard and Expert 5/5,
    // star awards 100 150 200 at Medium..Expert (0 at Easy), status awards
    // 100 300 ...; Easy plays one tier less (0x00130c58) [C].
    check(cp::numTiers(ui, 0) == 5 && cp::numTiers(ui, 1) == 6, "easy has 5 tiers, the others 6");
    check(cp::requiredSongs(ui, 1, "battle") == 4 && cp::requiredSongs(ui, 1, "arena") == 5 && cp::requiredSongs(ui, 0, "fest") == 4 &&
              cp::requiredSongs(ui, 0, "battle") == 3,
          "required songs (campaign.dta; the last tier's second value, 0x001313d8)");
    check(cp::encoreOf(ui, "battle") == "18andlife" && cp::isEncore(ui, "playwithme"), "the encores are the tiers' last songs");
    check(cp::isUnlocked(ui, "bangyourhead") && !cp::isUnlocked(ui, "18andlife") && !cp::isUnlocked(ui, "small1"), "a new tour");
    auto play = [&](const std::string& song, int stars) {
        s.song = song;
        return cp::finishSong(ui, 1000, stars, 50);
    };
    cp::FinishResult r = play("bangyourhead", 5);
    check(r.cash == 900 && r.cashReason == "award_5_star", "five stars at Hard: 100+150+200 at Hard and Medium, 0 at Easy (got " +
                                                                std::to_string(r.cash) + ")");
    for (const char* song : {"wegotthebeat", "iran"}) play(song, 3);
    s.song = "ballstothewall";
    check(ui.interp().send("campaign", "encore_unlock_potential", {}).truthy(), "the fourth song may unlock the encore at Hard");
    r = play("ballstothewall", 3);
    check(r.cash == 200 && r.newStatus == 0 && r.newVenue.empty(), "three stars: 100 at Hard and Medium; no venue before the encore");
    check(cp::isUnlocked(ui, "18andlife") && !cp::unlockedAt(ui, p, 0, "18andlife"), "the encore unlocked (never at Easy)");
    r = play("18andlife", 3);
    check(r.newVenue == "small1" && r.newStatus == 1 && r.statusAward == 200,
          "the encore completes battle: small1, status 1, $100 at Hard + $100 at Medium (got " + std::to_string(r.statusAward) + ")");
    check(p.cash == 50 + 900 + 2 * 200 + 200 + 200 + 200 && cp::status(ui, p, 2) == 1, "the band's cash and status");
    check(cp::isUnlocked(ui, "wegotthebeat") && !cp::isUnlocked(ui, "explorer") && !cp::isUnlocked(ui, "grim") && cp::isUnlocked(ui, "lespaul") &&
              !cp::isUnlocked(ui, "fish_reward"),
          "the store's items are locked, the others not");
    const int cash = p.cash;
    cp::buyItem(ui, "explorer", 700);
    cp::buyItem(ui, "grim", 4000);
    check(cp::isUnlocked(ui, "explorer") && p.cash == cash - 700 && p.cashSpent == 700 && !cp::isUnlocked(ui, "grim"),
          "a guitar bought, a character too dear");
    // The newest venue's songs only show once that venue is chosen (0x822AA3F8 [C 360]).
    const auto career = cp::careerSongs(ui, false);
    s.venue = "small1";
    const auto career1 = cp::careerSongs(ui, false);
    s.venue = "battle";
    check(std::find(career.begin(), career.end(), "noonelikeyou") == career.end() &&
              std::find(career1.begin(), career1.end(), "noonelikeyou") != career1.end() &&
              std::find(career1.begin(), career1.end(), "holydiver") == career1.end(),
          "the career list: the unlocked tiers' songs, the newest venue's when chosen");
    s.mode = "quickplay";
    const auto qp = cp::quickplaySongs(ui);
    check(std::find(qp.begin(), qp.end(), "noonelikeyou") != qp.end() && std::find(qp.begin(), qp.end(), "holydiver") == qp.end(),
          "quick play: the first two tiers");
    // The save keeps it.
    Career back;
    back.fromJson(ui.career().toJson());
    const auto& e = back.profiles[0].entries[2];
    check(back.profiles[0].cashSpent == 700 && e.at("battle").completed && e.at("small1").unlocked && e.at("bangyourhead").stars == 5 &&
              back.profiles[0].items.count("explorer") && back.profiles[0].entries[1].at("bangyourhead").maxStars == 5,
          "career.json keeps the tour");
    // Co-op (docs/multiplayer.md): the song's instruments from the mode's
    // song_block, set_track's track type, the record and its bass awards.
    Interp& in = ui.interp();
    // This disc has no song_coop blocks or rhythm parts: co-op plays the
    // song's guitar and bass.
    s.mode = "multi_coop", s.cfgSet.clear(), s.song = "18andlife";
    const std::string inst = in.send("song_provider", "get_instrument", {Value::symbol("18andlife"), Value::integer(1)}).str();
    check(inst == "bass" && in.send("song_provider", "has_instrument", {Value::symbol("18andlife"), Value::symbol("bass")}).truthy() &&
              !in.send("song_provider", "has_instrument", {Value::symbol("18andlife"), Value::symbol("rhythm")}).truthy(),
          "co-op's instruments: the song's (18andlife: bass, no rhythm; got " + inst + ")");
    in.send("gamecfg", "set_track", {Value::integer(1), Value::integer(1)});
    const std::string trackSym = in.send("gamecfg", "get_track_sym", {Value::integer(1)}).str();
    check(s.players[1].trackType == 0 && trackSym == "bass", "set_track 1 1: the song's second instrument, bass (got " + trackSym + ")");
    check(!in.send("game", "multiplayer_vs", {}).truthy() && in.send("game", "multiplayer", {}).truthy(), "co-op is multiplayer, not multiplayer_vs");
    ui.career().coop.clear(), ui.career().coopAwards.clear();
    check(!cp::isUnlocked(ui, "bass_sg_mahogany") && cp::isUnlocked(ui, "bass_musicman"), "co-op's basses locked until awarded");
    cp::finishCoopSong(ui, 1000, 2, 90, 90, 3, 3);
    check(s.guitarAwards.empty() && ui.career().coop["18andlife"].score == 1000, "two stars: a record, no bass");
    cp::finishCoopSong(ui, 900, 4, 100, 100, 3, 3);
    check(ui.career().coop["18andlife"].score == 1000 && ui.career().coop["18andlife"].stars == 2 && ui.career().coop["18andlife"].perfect,
          "a lower score keeps the record; 100 % on Expert by both: gold");
    cp::finishCoopSong(ui, 2000, 3, 50, 50, 1, 1);
    check(s.guitarAwards.size() == 1 && s.guitarAwards[0] == "bass_sg_mahogany" && cp::isUnlocked(ui, "bass_sg_mahogany"),
          "three stars on one song: bass_sg_mahogany (songs 1, stars 3)");
    s.mode = "multi_vs";
    check(in.send("game", "multiplayer_vs", {}).truthy(), "face-off is multiplayer_vs");
    // GD03-090: The Glitches (the port's band) has everything unlocked: an
    // older save's band (GH2's defaults only, read back from career.json)
    // is upgraded when the save is read, once (idempotent).
    {
        Career old;
        old.profiles = {};
        old.glitchesLook = ui.career().glitchesLook;
        old.ensureGlitches();
        ui.career().fromJson(old.toJson());
        ui.career().needsSave = false;
        const bool upgraded = cp::unlockGlitches(ui) && ui.career().needsSave;
        check(upgraded && !cp::unlockGlitches(ui), "an older Glitches band upgraded once");
        int slot = -1;
        for (int i = 0; i < Career::kProfiles; ++i)
            if (cp::isGlitches(ui.career().profiles[size_t(i)])) slot = i;
        s.profileSlot = slot, s.mode = "career", s.venue = "battle", s.allAccess = false;
        const cp::Config& cfg = cp::config(ui);
        int locked = 0;
        std::string first;
        for (int d = 0; d < Career::kDifficulties && slot >= 0; ++d) {
            s.players[0].difficulty = d;
            const auto list = cp::careerSongs(ui, false);
            const auto miss = [&](const std::string& sym, bool inList) {
                const bool ok = cp::isUnlocked(ui, sym) && (!inList || std::find(list.begin(), list.end(), sym) != list.end());
                if (!ok && first.empty()) first = sym + " at difficulty " + std::to_string(d);
                locked += ok ? 0 : 1;
            };
            for (int v = 0; v < cp::numTiers(ui, d); ++v) {
                miss(cfg.tiers[size_t(v)].first, false);
                for (const std::string& song : cfg.tiers[size_t(v)].second) miss(song, true);
            }
            for (const std::string& song : cfg.storeSongs) miss(song, true);
            for (const auto& [item, sold] : cfg.tableItems)
                if (!cp::isCoopReward(ui, item)) miss(item, false);  // co-op's basses: the save's, not a band's
            if (cp::status(ui, ui.career().profiles[size_t(slot)], d) < cp::numTiers(ui, d)) locked += 1, first = "the tour not won";
        }
        check(slot >= 0 && locked == 0 && cp::isUnlocked(ui, "multi_fo"), "The Glitches has everything unlocked (first locked: " + first + ")");
        s.mode = "quickplay";
        const auto all = cp::quickplaySongs(ui);
        check(std::find(all.begin(), all.end(), "holydiver") != all.end() && std::find(all.begin(), all.end(), "playwithme") != all.end(),
              "quick play offers The Glitches' songs");
    }
    std::printf("uitest: campaign: %d checks, %d failed\n", n, fails);
    ui.career() = saved;
    s = keep;
    return fails;
}

int runUiTest(const std::string& dataPath) {
    std::unique_ptr<GameData> data;
    std::vector<SongInfo> songs;
    try {
        data = std::make_unique<GameData>(dataPath);
        songs = loadSongs(*data);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
    NullHost host(songs);
    Canvas canvas;  // never initialised: headless
    Input input;
    ui::UiManager ui(*data, canvas, host, input);
    std::string error;
    if (!ui.init(error, true)) {
        std::printf("uitest: init failed: %s\n", error.c_str());
        return 1;
    }
    Interp& in = ui.interp();
    int failures = 0;
    std::map<std::string, int> classes;
    for (ScriptObject* o : ui.madeObjects()) ++classes[o->className()];
    std::printf("uitest: %zu objects made by new:", ui.madeObjects().size());
    for (const auto& [c, n] : classes) std::printf(" %s %d", c.c_str(), n);
    std::printf("\n");

    // Every panel's milo, and the components its own script names.
    scene::MiloLibrary lib(*data);
    std::set<std::string> sends;
    if (ui.bootTree()) collectSends(*ui.bootTree(), sends);
    int unreachable = 0;
    int panels = 0, loaded = 0, checked = 0, dynamic = 0;
    for (ScriptObject* o : ui.madeObjects()) {
        auto* p = dynamic_cast<ui::PanelObj*>(o);
        if (!p) continue;
        ++panels;
        std::string file;
        try {
            file = p->fileName();
        } catch (const std::exception& e) {
            std::printf("  %s: (file ...) failed: %s\n", p->name().c_str(), e.what());
        }
        if (file.empty()) continue;
        const std::string path = scene::miloArkPath(scene::resolveMiloRelative("ui/panel.milo", file));
        if (path.rfind("world/", 0) == 0 || path.rfind("hud/", 0) == 0 || path.rfind("track/", 0) == 0) continue;  // the program's
        if (!data->exists(path)) {
            // A file chosen at run time ({sprint "win_" difficulty ".milo"}): not checked here.
            ++dynamic;
            std::printf("  %s: %s not on the disc (a run-time choice)\n", p->name().c_str(), path.c_str());
            continue;
        }
        std::set<std::string> names;
        try {
            miloNames(*data, lib, path, names);
            // A file chosen by the script ({if_else {gamecfg multiplayer} a.milo b.milo}): any of them.
            std::vector<std::string> choices;
            if (const DataNode* f = p->handler("file")) fileChoices(*f, choices);
            for (const std::string& c : choices)
                miloNames(*data, lib, scene::miloArkPath(scene::resolveMiloRelative("ui/panel.milo", c)), names);
        } catch (const std::exception& e) {
            std::printf("  FAIL %s: %s: %s\n", p->name().c_str(), path.c_str(), e.what());
            ++failures;
            continue;
        }
        ++loaded;
        // The handlers in effect (a later definition of a message replaces
        // the macro's: multi_compete_coop_panel's own update_info).
        std::set<std::string> refs, seen, own;
        if (const DataNode* def = p->typeDef())
            for (size_t k = 3; k < def->children.size(); ++k)
                if (def->children[k].isList() && !def->children[k].children.empty() && def->children[k].children[0].isText())
                    if (const DataNode* eff = p->handler(def->children[k].children[0].s); eff == &def->children[k]) collectSends(*eff, own);
        if (const DataNode* def = p->typeDef())
            for (size_t k = 3; k < def->children.size(); ++k) {
                const DataNode& h = def->children[k];
                if (!h.isList() || h.children.empty() || !h.children[0].isText() || !seen.insert(h.children[0].s).second) continue;
                if (h.children[0].s == "file") continue;
                const bool code = std::any_of(h.children.begin() + 1, h.children.end(), [](const DataNode& c) { return c.type == DataNode::Type::Command; });
                if (code && !reachable(h.children[0].s, p->name(), sends, own)) {
                    ++unreachable;
                    continue;  // e.g. multi_compete_coop_panel's update_winner_info (from a shared macro, never called)
                }
                if (const DataNode* eff = p->handler(h.children[0].s)) collectNames(*eff, p->name(), refs);
            }
        for (const std::string& r : refs) {
            ++checked;
            if (!names.count(r)) {
                std::printf("  UNKNOWN component %s of %s (%s)\n", r.c_str(), p->name().c_str(), path.c_str());
                in.onUnknown("component " + p->name() + " " + r);
            }
        }
    }
    std::printf("uitest: %d panels, %d milos loaded, %d component names checked, %d run-time milos skipped, %d handlers nothing calls\n",
                panels, loaded, checked, dynamic, unreachable);
    failures += campaignChecks(ui);
    for (const auto& [k, n] : in.unknownCounts()) std::printf("  UNKNOWN %s (%d)\n", k.c_str(), n), ++failures;
    for (const auto& [k, n] : in.unhandledCounts()) std::printf("  UNHANDLED %s (%d)\n", k.c_str(), n), ++failures;
    for (const auto& [k, n] : in.stubCounts()) std::printf("  STUB %s (%d)\n", k.c_str(), n);
    for (const std::string& e : in.errors()) std::printf("  script error: %s\n", e.c_str()), ++failures;
    std::printf("uitest: %s\n", failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
