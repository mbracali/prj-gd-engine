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
#include "game/song_db.h"
#include "input/input.h"
#include "render/canvas.h"
#include "scene/milo.h"
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
    for (const auto& [k, n] : in.unknownCounts()) std::printf("  UNKNOWN %s (%d)\n", k.c_str(), n), ++failures;
    for (const auto& [k, n] : in.unhandledCounts()) std::printf("  UNHANDLED %s (%d)\n", k.c_str(), n), ++failures;
    for (const auto& [k, n] : in.stubCounts()) std::printf("  STUB %s (%d)\n", k.c_str(), n);
    for (const std::string& e : in.errors()) std::printf("  script error: %s\n", e.c_str()), ++failures;
    std::printf("uitest: %s\n", failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
