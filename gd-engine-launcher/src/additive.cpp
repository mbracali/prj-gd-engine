// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The Additive Music management tool (GDUL-036, owner 11-22 §5): a launcher
// screen reached from each game's Additive-music button. On the left, a table
// of the playable games with per-game "additive" and "custom songs" toggles;
// on the right, a folder tree for the selected game's root custom-songs folder
// (create / rename / delete folders, drop songs, open in the OS file manager,
// quick-validate a song or all of them). The per-game flags persist to
// additive_music.json for the per-game waves (the engine song library) to read.
// Nothing is extracted from a disc here; this tool only manages the settings
// and the custom-songs folders.
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <vector>

#include "additive.h"
#include "launcher.h"
#include "module_host.h"
#include "palette.h"
#include "platform.h"
#include "songs.h"

using gfx::Color;
using gfx::FontId;
using gfx::Rect;
using gfx::rgb;
using gfx::withAlpha;
using namespace palette;
namespace fs = std::filesystem;

namespace {
constexpr float kMargin = 24;
constexpr float kTop = 104;
constexpr float kFooterH = 58;
constexpr float kRowH = 46;
constexpr float kTreeH = 40;

// Hit codes inside the tool's own hits vector.
constexpr int kHitTree = -100;     // tree node: row = kHitTree - index
constexpr int kHitSave = -3, kHitBack = -4, kHitRefresh = -5, kHitOpen = -6, kHitEvalAll = -7, kHitNewFolder = -8;
constexpr int kHitCtx = -200;      // context-menu item: row = kHitCtx - item
constexpr int kHitPromptOk = -9, kHitPromptCancel = -11;
// A game-row hit uses row = game index (>= 0) and col: 0 select, 1 additive, 2 custom.

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

// Prefer a user-visible folder next to the launcher; fall back to the save
// folder (Android, or a read-only install).
std::string customRootBase() {
    const std::string base = platform::baseDir();  // "" on Android (APK assets)
    std::string root = base.empty() ? platform::join(platform::saveDir(), "custom_songs")
                                    : platform::join(base, "custom_songs");
    std::error_code ec;
    fs::create_directories(fs::u8path(root), ec);
    if (ec && !base.empty()) {  // the program folder is read-only: use the save folder
        root = platform::join(platform::saveDir(), "custom_songs");
        fs::create_directories(fs::u8path(root), ec);
    }
    return root;
}

// Quick validation: which custom-song format a folder looks like (owner §3).
std::string classify(const fs::path& dir) {
    bool songIni = false, chart = false, mid = false, dta = false, sng = false, rba = false, con = false, fsb = false,
         qb = false, mogg = false, hasSub = false;
    std::error_code ec;
    for (const auto& de : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (de.is_directory()) {
            hasSub = true;
            continue;
        }
        const std::string n = lower(de.path().filename().u8string());
        auto ends = [&](const char* s) { return n.size() >= std::strlen(s) && n.rfind(s) == n.size() - std::strlen(s); };
        if (n == "song.ini") songIni = true;
        else if (n == "notes.chart") chart = true;
        else if (n == "notes.mid" || n == "notes.midi") mid = true;
        else if (n == "songs.dta" || n == "song.dta") dta = true;
        else if (ends(".sng")) sng = true;
        else if (ends(".rba")) rba = true;
        else if (ends("_rb3con") || ends("_rbcon") || n == "songs") con = true;
        else if (ends(".fsb") || n.find(".fsb.") != std::string::npos) fsb = true;
        else if (ends(".qb") || n.find(".qb.") != std::string::npos) qb = true;
        else if (ends(".mogg")) mogg = true;
    }
    if (sng) return "yarg";                                  // packed CH / YARG
    if ((chart || mid) && songIni) return "clone-hero";      // Clone Hero / YARG folder
    if (mid && (dta || con)) return "rock-band";
    if (rba || con || (dta && mogg)) return "rock-band";
    if (fsb || qb) return "gh-dlc";
    if (chart || mid) return "clone-hero";
    if (hasSub && !songIni && !chart && !mid) return "folder";
    return "invalid";
}

// Does a folder hold a song (any chart/metadata), or is it an organiser?
bool looksLikeSong(const fs::path& dir) {
    std::error_code ec;
    for (const auto& de : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (de.is_directory()) continue;
        const std::string n = lower(de.path().filename().u8string());
        if (n == "song.ini" || n == "notes.chart" || n == "notes.mid" || n == "notes.midi" || n == "songs.dta")
            return true;
        auto ends = [&](const char* s) { return n.size() >= std::strlen(s) && n.rfind(s) == n.size() - std::strlen(s); };
        if (ends(".sng") || ends(".rba") || ends(".fsb") || ends(".qb")) return true;
    }
    return false;
}
}  // namespace

// ---- Opening and closing ----

void Launcher::additiveOpen(Game* focus) {
    log_.add(std::string("additive music tool opened") + (focus ? " from " + focus->id : ""));
    auto a = std::make_unique<Additive>();
    // Build the games table: playable games first, in the catalog's order.
    int focusRow = 0;
    for (const Game& g : catalog_.games()) {
        if (!g.playable()) continue;
        Additive::Row r;
        r.id = g.id;
        r.title = g.title();
        r.isoName = g.isoName;
        if (focus && g.id == focus->id) focusRow = int(a->rows.size());
        a->rows.push_back(std::move(r));
    }
    // Load the saved per-game flags.
    const std::string path = platform::join(platform::saveDir(), "additive_music.json");
    std::ifstream f(fs::u8path(path));
    if (f) {
        std::stringstream ss;
        ss << f.rdbuf();
        const std::string j = ss.str();
        for (Additive::Row& r : a->rows) {
            const size_t k = j.find("\"" + r.id + "\"");
            if (k == std::string::npos) continue;
            const size_t end = j.find('}', k);
            const std::string blk = j.substr(k, end == std::string::npos ? std::string::npos : end - k);
            r.additive = blk.find("\"additive\": true") != std::string::npos || blk.find("\"additive\":true") != std::string::npos;
            r.custom = blk.find("\"custom\": true") != std::string::npos || blk.find("\"custom\":true") != std::string::npos;
            // Use the cached count only when the disc has not changed.
            const size_t ik = blk.find("\"iso\":");
            std::string cachedIso;
            if (ik != std::string::npos) {
                const size_t q1 = blk.find('"', ik + 6), q2 = q1 == std::string::npos ? q1 : blk.find('"', q1 + 1);
                if (q2 != std::string::npos) cachedIso = blk.substr(q1 + 1, q2 - q1 - 1);
            }
            const size_t ck = blk.find("\"count\":");
            if (ck != std::string::npos && cachedIso == r.isoName)
                r.songCount = std::atoi(blk.c_str() + ck + 8);
        }
    }
    a->gameSel = a->rows.empty() ? -1 : std::min(focusRow, int(a->rows.size()) - 1);
    a->lastFrame = now_;
    additive_ = std::move(a);
    // Always show every game's song count: count any game not already cached
    // (one module at a time, with a loading card), then persist the counts.
    bool counted = false;
    for (Additive::Row& r : additive_->rows) {
        if (r.songCount >= 0) continue;
        for (Game& g : catalog_.games())
            if (g.id == r.id) {
                r.songCount = additiveCountSongs(g);
                counted = true;
                break;
            }
    }
    if (additive_->gameSel >= 0) additiveSelect(additive_->gameSel);
    if (counted) additiveWriteFile();  // cache the counts for next time
    sounds_.play(UiSound::Select);
}

void Launcher::additiveClose() {
    if (!additive_) return;
    if (additive_->dirty) additiveSave();
    additive_.reset();
    sounds_.play(UiSound::Back);
    log_.add("additive music tool closed");
}

// ---- Selection, counting, the folder tree ----

int Launcher::additiveCountSongs(const Game& g) {
    // Reuse the "Hear the songs" session to count the disc's songs. One module
    // at a time, driven by the user's selection (never all at once).
    drawLoading(g, "Counting the songs of");
    SongSession session;
    std::string error;
    int n = -1;
    if (session.open(window_, context_, g, catalog_.launcherVersion(), log_, error)) {
        const GdSongs* api = session.api();
        if (api) n = api->song_count(api->self);
        session.close();
    } else {
        log_.add("additive: count " + g.id + " failed: " + error);
    }
    SDL_GL_MakeCurrent(window_, context_);
    restoreWindow();
    return n;
}

void Launcher::additiveSelect(int row) {
    Additive& a = *additive_;
    if (row < 0 || row >= int(a.rows.size())) return;
    a.gameSel = row;
    Additive::Row& r = a.rows[size_t(row)];
    if (r.songCount < 0) {  // count once, lazily
        for (Game& g : catalog_.games())
            if (g.id == r.id) {
                r.songCount = additiveCountSongs(g);
                break;
            }
    }
    a.customRoot = platform::join(customRootBase(), r.id);
    std::error_code ec;
    fs::create_directories(fs::u8path(a.customRoot), ec);
    a.treeSel = 0;
    a.treeScroll = a.treeScrollTarget = 0;
    additiveBuildTree();
}

void Launcher::additiveBuildTree() {
    Additive& a = *additive_;
    // Keep which folders were expanded (by path) across a rebuild.
    std::set<std::string> wasExpanded;
    for (const Additive::Node& n : a.tree)
        if (!n.isSong && n.expanded) wasExpanded.insert(n.path);

    a.tree.clear();
    std::function<void(const fs::path&, int, int)> walk = [&](const fs::path& dir, int depth, int parent) {
        if (depth > 3) return;  // the root plus two sub-levels (owner §5)
        std::error_code ec;
        std::vector<fs::path> dirs;
        for (const auto& de : fs::directory_iterator(dir, ec)) {
            if (ec) break;
            if (de.is_directory()) dirs.push_back(de.path());
        }
        std::sort(dirs.begin(), dirs.end(),
                  [](const fs::path& x, const fs::path& y) { return lower(x.filename().u8string()) < lower(y.filename().u8string()); });
        for (const fs::path& p : dirs) {
            Additive::Node n;
            n.path = p.u8string();
            n.name = p.filename().u8string();
            n.isSong = looksLikeSong(p);
            n.depth = depth;
            n.parent = parent;
            n.expanded = wasExpanded.count(n.path) != 0;
            const int me = int(a.tree.size());
            a.tree.push_back(std::move(n));
            if (!a.tree[size_t(me)].isSong) walk(p, depth + 1, me);
        }
    };
    walk(fs::u8path(a.customRoot), 0, -1);
    // Visibility: a node shows when every ancestor is expanded.
    for (size_t i = 0; i < a.tree.size(); ++i) {
        Additive::Node& n = a.tree[i];
        n.visible = true;
        for (int p = n.parent; p >= 0; p = a.tree[size_t(p)].parent)
            if (!a.tree[size_t(p)].expanded) {
                n.visible = false;
                break;
            }
    }
    a.treeSel = std::clamp(a.treeSel, 0, std::max(0, int(a.tree.size()) - 1));
    // Quick-validate every song now, so the list shows each one's format at a
    // glance (owner §5: evaluate all songs when the folder's content changes).
    for (Additive::Node& n : a.tree) {
        if (n.isSong) {
            n.kind = classify(fs::u8path(n.path));
            n.valid = n.kind != "invalid";
        } else {
            n.kind = "folder";
            n.valid = true;
        }
        n.evaluated = true;
    }
}

void Launcher::additiveEvaluate(int node) {
    Additive& a = *additive_;
    auto one = [&](Additive::Node& n) {
        if (!n.isSong) {
            n.kind = "folder";
            n.evaluated = true;
            n.valid = true;
            return;
        }
        n.kind = classify(fs::u8path(n.path));
        n.valid = n.kind != "invalid";
        n.evaluated = true;
    };
    int done = 0;
    if (node < 0) {
        for (Additive::Node& n : a.tree) {
            one(n);
            if (n.isSong) ++done;
        }
        a.status = "Evaluated " + std::to_string(done) + (done == 1 ? " song" : " songs");
    } else if (node < int(a.tree.size())) {
        one(a.tree[size_t(node)]);
        a.status = a.tree[size_t(node)].name + ": " + a.tree[size_t(node)].kind;
    }
    a.statusAt = now_;
}

bool Launcher::additiveWriteFile() {
    Additive& a = *additive_;
    const std::string path = platform::join(platform::saveDir(), "additive_music.json");
    auto esc = [](const std::string& s) {  // minimal JSON string escaping
        std::string o;
        for (char c : s) {
            if (c == '"' || c == '\\') o += '\\';
            o += c;
        }
        return o;
    };
    std::ostringstream o;
    o << "{\n";
    for (size_t i = 0; i < a.rows.size(); ++i) {
        const Additive::Row& r = a.rows[i];
        o << "  \"" << r.id << "\": {\"additive\": " << (r.additive ? "true" : "false") << ", \"custom\": "
          << (r.custom ? "true" : "false") << ", \"count\": " << r.songCount << ", \"iso\": \"" << esc(r.isoName) << "\"}"
          << (i + 1 < a.rows.size() ? "," : "") << "\n";
    }
    o << "}\n";
    std::ofstream f(fs::u8path(path), std::ios::trunc);
    if (!f) return false;
    f << o.str();
    log_.add("additive music: settings written to " + path);
    return true;
}

void Launcher::additiveSave() {
    Additive& a = *additive_;
    if (additiveWriteFile()) {
        a.dirty = false;
        a.status = "Saved";
    } else {
        a.status = "Could not save";
        sounds_.play(UiSound::Error);
    }
    a.statusAt = now_;
}

// ---- Input ----

bool Launcher::additiveHandle(const SDL_Event& e) {
    Additive& a = *additive_;
    // A folder / file dropped: copy it into the current custom-songs folder and
    // quick-validate (owner §5).
    if (e.type == SDL_EVENT_DROP_FILE) {
        if (a.customRoot.empty()) return true;
        const fs::path src = fs::u8path(e.drop.data ? e.drop.data : "");
        std::error_code ec;
        if (!src.empty() && fs::exists(src, ec)) {
            const fs::path dst = fs::u8path(a.customRoot) / src.filename();
            fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            if (ec) {
                a.status = "Could not add " + src.filename().u8string();
                sounds_.play(UiSound::Error);
            } else {
                additiveBuildTree();
                additiveEvaluate(-1);
                a.status = "Added " + src.filename().u8string();
            }
            a.statusAt = now_;
        }
        return true;
    }
    if (e.type == SDL_EVENT_DROP_BEGIN || e.type == SDL_EVENT_DROP_COMPLETE || e.type == SDL_EVENT_DROP_TEXT) return true;

    // The create / rename text prompt owns the keyboard while open.
    if (a.promptMode) {
        if (e.type == SDL_EVENT_TEXT_INPUT) {
            a.promptText += e.text.text;
            return true;
        }
        if (e.type == SDL_EVENT_KEY_DOWN) {
            if (e.key.key == SDLK_BACKSPACE && !a.promptText.empty()) a.promptText.pop_back();
            else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) additivePromptCommit();
            else if (e.key.key == SDLK_ESCAPE) { a.promptMode = 0; SDL_StopTextInput(window_); }
            return true;
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            const float d = SDL_GetWindowPixelDensity(window_);
            const float mx = e.button.x * d, my = e.button.y * d;
            for (const Hit& h : a.hits)
                if (h.r.contains(mx, my)) {
                    if (h.row == kHitPromptOk) additivePromptCommit();
                    else if (h.row == kHitPromptCancel) { a.promptMode = 0; SDL_StopTextInput(window_); }
                    return true;
                }
            return true;
        }
        return true;
    }

    // A context menu open: a click chooses an item or dismisses it.
    if (a.ctxNode >= 0 && e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        const float d = SDL_GetWindowPixelDensity(window_);
        const float mx = e.button.x * d, my = e.button.y * d;
        for (const Hit& h : a.hits)
            if (h.r.contains(mx, my) && h.row <= kHitCtx) {
                additiveContextAction(kHitCtx - h.row);
                a.ctxNode = -1;
                return true;
            }
        a.ctxNode = -1;  // click elsewhere closes it
        return true;
    }

    switch (e.type) {
    case SDL_EVENT_QUIT:
        additiveClose();
        return true;
    case SDL_EVENT_KEY_DOWN:
        switch (e.key.key) {
        case SDLK_ESCAPE: case SDLK_BACKSPACE:
            if (!e.key.repeat) additiveClose();
            return true;
        case SDLK_UP: case SDLK_W:
            if (a.focus == 1 && !a.tree.empty()) {
                int r = a.treeSel;
                do r = std::max(0, r - 1); while (r > 0 && !a.tree[size_t(r)].visible);
                a.treeSel = r;
            } else if (a.focus == 0) additiveSelect(std::max(0, a.gameSel - 1));
            sounds_.play(UiSound::Move);
            return true;
        case SDLK_DOWN: case SDLK_S:
            if (a.focus == 1 && !a.tree.empty()) {
                int r = a.treeSel;
                do r = std::min(int(a.tree.size()) - 1, r + 1); while (r < int(a.tree.size()) - 1 && !a.tree[size_t(r)].visible);
                a.treeSel = r;
            } else if (a.focus == 0) additiveSelect(std::min(int(a.rows.size()) - 1, a.gameSel + 1));
            sounds_.play(UiSound::Move);
            return true;
        case SDLK_TAB:
            a.focus = (a.focus + 1) % 2;  // games <-> tree
            sounds_.play(UiSound::Move);
            return true;
        case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE:
            if (a.focus == 1 && a.treeSel < int(a.tree.size())) {
                Additive::Node& n = a.tree[size_t(a.treeSel)];
                if (n.isSong) additiveEvaluate(a.treeSel);
                else { n.expanded = !n.expanded; additiveBuildTree(); }
            }
            return true;
        default:
            return true;
        }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        if (e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST || e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) additiveClose();
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        const float d = SDL_GetWindowPixelDensity(window_);
        const float mx = e.button.x * d, my = e.button.y * d;
        const bool right = e.button.button == SDL_BUTTON_RIGHT;
        for (auto it = a.hits.rbegin(); it != a.hits.rend(); ++it) {
            if (!it->r.contains(mx, my)) continue;
            if (it->row >= 0) {  // a game row
                a.focus = 0;
                if (it->col == 1) { a.rows[size_t(it->row)].additive = !a.rows[size_t(it->row)].additive; a.dirty = true; }
                else if (it->col == 2) { a.rows[size_t(it->row)].custom = !a.rows[size_t(it->row)].custom; a.dirty = true; }
                else additiveSelect(it->row);
                sounds_.play(UiSound::Select);
            } else if (it->row <= kHitTree) {
                const int idx = kHitTree - it->row;
                a.focus = 1;
                a.treeSel = idx;
                if (right) { a.ctxNode = idx; a.ctxAt = {mx, my, 0, 0}; }
                else if (idx < int(a.tree.size())) {
                    Additive::Node& n = a.tree[size_t(idx)];
                    if (n.isSong) additiveEvaluate(idx);
                    else { n.expanded = !n.expanded; additiveBuildTree(); }
                }
            } else switch (it->row) {
                case kHitSave: additiveSave(); break;
                case kHitBack: additiveClose(); break;
                case kHitRefresh: additiveSelect(a.gameSel); a.status = "Refreshed"; a.statusAt = now_; break;
                case kHitOpen: additiveOpenInOs(); break;
                case kHitEvalAll: additiveEvaluate(-1); break;
                case kHitNewFolder: additivePromptNewFolder(); break;
                default: break;
            }
            return true;
        }
        if (right) additiveClose();
        return true;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        a.treeScrollTarget -= e.wheel.y * 60;
        return true;
    default:
        return false;
    }
}

void Launcher::additivePromptNewFolder() {
    Additive& a = *additive_;
    a.promptMode = 1;
    a.promptText.clear();
    a.promptNode = -1;
    SDL_StartTextInput(window_);
}

void Launcher::additivePromptCommit() {
    Additive& a = *additive_;
    std::string name = a.promptText;
    // Strip path separators so a name cannot escape the folder.
    name.erase(std::remove_if(name.begin(), name.end(), [](char c) { return c == '/' || c == '\\' || c == ':'; }), name.end());
    while (!name.empty() && (name.front() == ' ' || name.front() == '.')) name.erase(name.begin());
    while (!name.empty() && name.back() == ' ') name.pop_back();
    std::error_code ec;
    if (!name.empty()) {
        if (a.promptMode == 1) {  // new folder under the current root (or the selected folder)
            fs::path parent = fs::u8path(a.customRoot);
            if (a.treeSel < int(a.tree.size()) && !a.tree[size_t(a.treeSel)].isSong && a.tree[size_t(a.treeSel)].depth < 2)
                parent = fs::u8path(a.tree[size_t(a.treeSel)].path);
            fs::create_directories(parent / fs::u8path(name), ec);
            a.status = ec ? "Could not create the folder" : "Created " + name;
        } else if (a.promptMode == 2 && a.promptNode >= 0 && a.promptNode < int(a.tree.size())) {  // rename
            const fs::path p = fs::u8path(a.tree[size_t(a.promptNode)].path);
            fs::rename(p, p.parent_path() / fs::u8path(name), ec);
            a.status = ec ? "Could not rename" : "Renamed to " + name;
        }
        a.statusAt = now_;
        additiveBuildTree();
    }
    a.promptMode = 0;
    SDL_StopTextInput(window_);
}

void Launcher::additiveContextAction(int item) {
    Additive& a = *additive_;
    if (a.ctxNode < 0 || a.ctxNode >= int(a.tree.size())) return;
    Additive::Node& n = a.tree[size_t(a.ctxNode)];
    std::error_code ec;
    switch (item) {
    case 0:  // Remove
        fs::remove_all(fs::u8path(n.path), ec);
        a.status = ec ? "Could not remove" : "Removed " + n.name;
        a.statusAt = now_;
        additiveBuildTree();
        break;
    case 1:  // Quick-evaluate
        additiveEvaluate(a.ctxNode);
        break;
    case 2:  // Rename
        a.promptMode = 2;
        a.promptNode = a.ctxNode;
        a.promptText = n.name;
        SDL_StartTextInput(window_);
        break;
    case 3:  // Open in the OS file manager
        SDL_OpenURL(("file://" + n.path).c_str());
        break;
    default: break;
    }
}

void Launcher::additiveOpenInOs() {
    Additive& a = *additive_;
    if (a.customRoot.empty()) return;
    SDL_OpenURL(("file://" + a.customRoot).c_str());
    a.status = "Opened the folder in your file manager";
    a.statusAt = now_;
}

// ---- A frame ----

void Launcher::additiveFrame() {
    additive_->lastFrame = now_;
    SDL_GetWindowSizeInPixels(window_, &pw_, &ph_);
    s_ = std::max(0.25f, std::min(float(pw_) / 1280.f, float(ph_) / 720.f));
    W_ = float(pw_) / s_, H_ = float(ph_) / s_;
    gfx_.begin(pw_, ph_);
    drawBackground();
    additiveDraw();
    gfx_.end();
    if (!pendingShot_.empty()) {  // development aid: GDUL_SCRIPT shot
        screenshot(pendingShot_);
        pendingShot_.clear();
    }
    SDL_GL_SwapWindow(window_);
}

// ---- Drawing ----

namespace {
// A kind badge's short label and colour.
std::pair<std::string, Color> kindBadge(const std::string& k) {
    if (k == "clone-hero") return {"CH", gfx::rgb(0x45e66b)};
    if (k == "yarg") return {"YARG", gfx::rgb(0x45c7e6)};
    if (k == "rock-band") return {"RB", gfx::rgb(0xff8a3a)};
    if (k == "gh-dlc") return {"DLC", gfx::rgb(0xb463ff)};
    if (k == "invalid") return {"?", gfx::rgb(0xe23a3f)};
    return {"", gfx::rgb(0x91877a)};
}
}  // namespace

void Launcher::additiveDraw() {
    Additive& a = *additive_;
    a.hits.clear();
    float mx = -1, my = -1;
    SDL_GetMouseState(&mx, &my);
    const float density = SDL_GetWindowPixelDensity(window_);
    mx *= density, my *= density;
    const bool modal = a.promptMode || a.ctxNode >= 0;
    auto hover = [&](const Rect& r) { return !modal && r.contains(mx, my); };

    // Header.
    gfx_.image(gfx_.icon(), R(kMargin, 12, 64, 64));
    text(FontId::Title, 38, 98, 14, "Additive Music", kCream);
    text(FontId::SemiBold, 15, 100, 60, "Share songs across games \xC2\xB7 add your own custom songs", kSand, 1.0f);
    gfx_.rect(R(100, 82, 150, 3), kOxblood, 1.5f * s_);

    const float top = kTop, bottom = H_ - kFooterH - 14;
    const float leftX = kMargin, leftW = (W_ - 2 * kMargin) * 0.42f - 8;
    const float rightX = leftX + leftW + 16, rightW = W_ - kMargin - rightX;

    // ---- Left: the games table ----
    const Rect lp = R(leftX, top, leftW, bottom - top);
    gfx_.rect(lp, rgb(0x1d1a20, 0.92f), rgb(0x131115, 0.94f), 14 * s_);
    gfx_.ring(lp, withAlpha(a.focus == 0 ? kSand : kCream, a.focus == 0 ? 0.4f : 0.08f), 14 * s_, (a.focus == 0 ? 1.6f : 1.f) * s_);
    const float colSongs = leftX + leftW - 190, colAdd = leftX + leftW - 120, colCust = leftX + leftW - 46;
    text(FontId::SemiBold, 11, leftX + 16, top + 12, "GAME", kMuted, 1.5f);
    text(FontId::SemiBold, 11, colSongs - measure(FontId::SemiBold, 11, "SONGS") / 2, top + 12, "SONGS", kMuted, 1.0f);
    text(FontId::SemiBold, 11, colAdd - measure(FontId::SemiBold, 11, "EXPOSE") / 2, top + 12, "EXPOSE", kMuted, 1.0f);
    text(FontId::SemiBold, 11, colCust - measure(FontId::SemiBold, 11, "CUSTOM") / 2, top + 12, "CUSTOM", kMuted, 1.0f);
    gfx_.rect(R(leftX + 12, top + 32, leftW - 24, 1), withAlpha(kCream, 0.1f));

    auto checkbox = [&](float cx, float cy, bool on, int grow, int col) {
        const Rect b = R(cx - 11, cy - 11, 22, 22);
        const bool hv = hover(b);
        gfx_.rect(b, on ? withAlpha(fretColor(Fret::Green), 0.22f) : rgb(0x26222a, 0.9f), 5 * s_);
        gfx_.ring(b, on ? fretColor(Fret::Green) : withAlpha(kCream, hv ? 0.5f : 0.22f), 5 * s_, 1.4f * s_);
        if (on) {  // a filled marker (the font has no check glyph)
            gfx_.rect(R(cx - 6, cy - 6, 12, 12), fretColor(Fret::Green), 3 * s_);
            gfx_.rect(R(cx - 3, cy - 3, 3, 3), kInk);
        }
        a.hits.push_back({b, grow, col});
    };

    const Rect lclip = R(leftX, top + 34, leftW, bottom - top - 34);
    gfx_.clip(&lclip);
    for (size_t i = 0; i < a.rows.size(); ++i) {
        const Additive::Row& r = a.rows[i];
        const float ry = top + 40 + float(i) * kRowH - a.listScroll;
        if (ry + kRowH < top + 34 || ry > bottom) continue;
        const bool sel = int(i) == a.gameSel;
        const Rect row = R(leftX + 8, ry, leftW - 16, kRowH - 6);
        if (sel) {
            gfx_.rect(row, rgb(0x2c2731, 0.98f), rgb(0x232027, 0.98f), 9 * s_);
            gfx_.ring(row, withAlpha(kSand, 0.6f), 9 * s_, 1.4f * s_);
        } else if (hover(row)) {
            gfx_.rect(row, rgb(0x241f29, 0.7f), 9 * s_);
        }
        const float cyr = ry + (kRowH - 6) / 2;
        const float idW = text(FontId::SemiBold, 13, leftX + 18, cyr - 15, r.id + "/", kDim);
        text(FontId::SemiBold, 17, leftX + 18 + idW, cyr - 18, r.title, sel ? kCream : rgb(0xd6cab3));
        const std::string sc = r.songCount < 0 ? "\xE2\x80\x94" : std::to_string(r.songCount);
        text(FontId::SemiBold, 15, colSongs - measure(FontId::SemiBold, 15, sc) / 2, cyr - 9, sc, kSand);
        // Push the row-select hit BEFORE the checkboxes so the reverse-order
        // mouse scan finds the (smaller) checkboxes first.
        a.hits.push_back({row, int(i), 0});
        checkbox(colAdd, cyr, r.additive, int(i), 1);
        checkbox(colCust, cyr, r.custom, int(i), 2);
    }
    gfx_.clip(nullptr);
    a.hits.erase(std::remove_if(a.hits.begin(), a.hits.end(),
                                [&](const Hit& h) { return h.row >= 0 && (h.r.y + h.r.h < (top + 34) * s_ || h.r.y > bottom * s_); }),
                 a.hits.end());

    // ---- Right: the custom-songs folder tree ----
    const Rect rp = R(rightX, top, rightW, bottom - top - 44);
    gfx_.rect(rp, rgb(0x1d1a20, 0.92f), rgb(0x131115, 0.94f), 14 * s_);
    gfx_.ring(rp, withAlpha(a.focus == 1 ? kSand : kCream, a.focus == 1 ? 0.4f : 0.08f), 14 * s_, (a.focus == 1 ? 1.6f : 1.f) * s_);
    const std::string hd = a.gameSel >= 0 ? a.rows[size_t(a.gameSel)].title + " \xE2\x80\x94 custom songs" : "Custom songs";
    text(FontId::SemiBold, 16, rightX + 16, top + 12, hd, kCream);
    const std::string folderPath = gfx_.elide(FontId::Regular, 12 * s_, a.customRoot, (rightW - 32) * s_);
    text(FontId::Regular, 12, rightX + 16, top + 36, folderPath, kDim);
    gfx_.rect(R(rightX + 12, top + 56, rightW - 24, 1), withAlpha(kCream, 0.1f));

    const float treeTop = top + 64, treeBottom = bottom - 44 - 8;
    const float tview = treeBottom - treeTop;
    // Visible rows (only those marked visible), laid out contiguously.
    std::vector<int> vis;
    for (int i = 0; i < int(a.tree.size()); ++i)
        if (a.tree[size_t(i)].visible) vis.push_back(i);
    const float tcontent = float(vis.size()) * kTreeH;
    a.treeScrollTarget = std::clamp(a.treeScrollTarget, 0.f, std::max(0.f, tcontent - tview));
    a.treeScroll += (a.treeScrollTarget - a.treeScroll) * 0.3f;
    const Rect tclip = R(rightX, treeTop, rightW, tview);
    gfx_.clip(&tclip);
    for (size_t vi = 0; vi < vis.size(); ++vi) {
        const int ni = vis[vi];
        const Additive::Node& n = a.tree[size_t(ni)];
        const float ry = treeTop + float(vi) * kTreeH - a.treeScroll;
        if (ry + kTreeH < treeTop || ry > treeBottom) continue;
        const bool sel = ni == a.treeSel && a.focus == 1;
        const Rect row = R(rightX + 8, ry + 3, rightW - 16, kTreeH - 6);
        if (sel) {
            gfx_.rect(row, rgb(0x2c2731, 0.98f), 8 * s_);
            gfx_.ring(row, withAlpha(kSand, 0.55f), 8 * s_, 1.3f * s_);
        } else if (hover(row)) {
            gfx_.rect(row, rgb(0x241f29, 0.6f), 8 * s_);
        }
        const float ix = rightX + 18 + float(n.depth) * 18;
        const float cyr = ry + kTreeH / 2;
        if (n.isSong) {
            icon(gfx::icon::kMusicNoteAdd, ix + 8, cyr, 18, n.evaluated ? (n.valid ? fretColor(Fret::Green) : fretColor(Fret::Red)) : kDim);
        } else {  // a folder: an expand triangle
            const std::string tri = n.expanded ? "\xE2\x96\xBC" : "\xE2\x96\xB6";  // ▼ / ▶
            text(FontId::SemiBold, 15, ix, cyr - 9, tri, kSand);
            icon(gfx::icon::kDatabaseSearch, ix + 20, cyr, 16, withAlpha(kSand, 0.8f));
        }
        text(FontId::SemiBold, 15, ix + (n.isSong ? 22 : 36), cyr - 10, gfx_.elide(FontId::SemiBold, 15 * s_, n.name, (rightW - (ix - rightX) - 120) * s_), sel ? kCream : rgb(0xcfc4ad));
        if (n.isSong && n.evaluated) {
            auto [lab, col] = kindBadge(n.kind);
            if (!lab.empty()) {
                const float bw = measure(FontId::SemiBold, 11, lab) + 14;
                const Rect bb = R(rightX + rightW - 20 - bw, cyr - 9, bw, 18);
                gfx_.rect(bb, withAlpha(col, 0.18f), 5 * s_);
                gfx_.ring(bb, withAlpha(col, 0.6f), 5 * s_, 1.f * s_);
                text(FontId::SemiBold, 11, rightX + rightW - 20 - bw + 7, cyr - 8, lab, col);
            }
        }
        a.hits.push_back({row, kHitTree - ni, 0});
    }
    gfx_.clip(nullptr);
    a.hits.erase(std::remove_if(a.hits.begin(), a.hits.end(),
                                [&](const Hit& h) { return h.row <= kHitTree && h.row > kHitCtx && (h.r.y + h.r.h < treeTop * s_ || h.r.y > treeBottom * s_); }),
                 a.hits.end());
    if (vis.empty())
        text(FontId::Regular, 15, rightX + 20, treeTop + 16, "No custom songs yet. Drop a song folder here, or use New folder.", kDim);

    // Right-panel action bar (under the tree box).
    float bx = rightX;
    const float barY = bottom - 40;
    auto smallBtn = [&](uint32_t cp, const std::string& label, int code, Color tint) {
        const float bw = 28 + measure(FontId::SemiBold, 14, label) + 20;
        const Rect r = R(bx, barY, bw, 34);
        const bool hv = hover(r);
        gfx_.rect(r, hv ? rgb(0x2a262d) : rgb(0x201d24, 0.95f), hv ? rgb(0x211e24) : rgb(0x18151b, 0.95f), 9 * s_);
        gfx_.ring(r, withAlpha(tint, hv ? 0.8f : 0.5f), 9 * s_, 1.2f * s_);
        icon(cp, bx + 16, barY + 17, 18, tint);
        text(FontId::SemiBold, 14, bx + 28, barY + 17 - gfx_.lineHeight(FontId::SemiBold, 14 * s_) / s_ / 2, label, hv ? kCream : kSand);
        a.hits.push_back({r, code, -1});
        bx += bw + 8;
    };
    smallBtn(gfx::icon::kMusicNoteAdd, "New folder", kHitNewFolder, fretColor(Fret::Yellow));
    smallBtn(gfx::icon::kCircle, "Evaluate all", kHitEvalAll, fretColor(Fret::Blue));
    smallBtn(gfx::icon::kDatabaseSearch, "Open folder", kHitOpen, fretColor(Fret::Green));
    if (now_ - a.statusAt < 4.0 && !a.status.empty())
        text(FontId::SemiBold, 14, W_ - kMargin - measure(FontId::SemiBold, 14, a.status), barY + 9, a.status, kSand);

    // ---- Footer: Save / Back / Refresh ----
    const float fy = H_ - kFooterH;
    gfx_.rect(R(0, fy, W_, kFooterH), rgb(0x0d0b0f, 0.92f), rgb(0x080709, 0.98f));
    gfx_.rect(R(0, fy, W_, 1), withAlpha(kCream, 0.1f));
    float fx = kMargin;
    const float fcy = fy + kFooterH / 2;
    auto footBtn = [&](Fret f, const std::string& label, int code, bool strong) {
        const float bw = measure(FontId::SemiBold, 16, label) + 34;
        const Rect r = R(fx, fcy - 17, bw, 34);
        const bool hv = hover(r);
        const Color fc = fretColor(f);
        gfx_.rect(r, hv ? gfx::mix(fc, kInk, 0.5f) : rgb(0x1f1b23, 0.95f), hv ? gfx::mix(fc, kInk, 0.68f) : rgb(0x18151b, 0.95f), 17 * s_);
        gfx_.ring(r, withAlpha(fc, hv ? 0.95f : strong ? 0.7f : 0.5f), 17 * s_, (hv ? 1.6f : 1.2f) * s_);
        text(FontId::SemiBold, 16, fx + 17, fcy - 10, label, hv ? kCream : kSand);
        a.hits.push_back({r, code, -1});
        fx += bw + 10;
    };
    footBtn(Fret::Green, a.dirty ? "Save *" : "Save", kHitSave, a.dirty);
    footBtn(Fret::Red, "Back", kHitBack, false);
    footBtn(Fret::Yellow, "Refresh", kHitRefresh, false);
    const std::string hint = "Tick EXPOSE to share a game's songs \xC2\xB7 CUSTOM to load the custom-songs folder \xC2\xB7 drop songs on the right";
    if (fx + measure(FontId::Regular, 14, hint) + 16 < W_ - kMargin)
        text(FontId::Regular, 14, W_ - kMargin - measure(FontId::Regular, 14, hint), fcy - 9, hint, kDim);

    // ---- Context menu ----
    if (a.ctxNode >= 0) {
        static const char* items[] = {"Remove", "Quick evaluate", "Rename", "Open in files"};
        const float mw = 180, mh = 4 + 4 * 30;
        float cxp = a.ctxAt.x / s_, cyp = a.ctxAt.y / s_;
        cxp = std::min(cxp, W_ - kMargin - mw);
        cyp = std::min(cyp, H_ - kFooterH - mh - 4);
        const Rect menu = R(cxp, cyp, mw, mh);
        gfx_.glow(menu, withAlpha(kInk, 0.7f), 10 * s_, 18 * s_);
        gfx_.rect(menu, rgb(0x221d25), rgb(0x171419), 10 * s_);
        gfx_.ring(menu, withAlpha(kSand, 0.4f), 10 * s_, 1.2f * s_);
        for (int i = 0; i < 4; ++i) {
            const Rect ir = R(cxp + 4, cyp + 4 + float(i) * 30, mw - 8, 28);
            const bool hv = ir.contains(mx, my);
            if (hv) gfx_.rect(ir, rgb(0x2e2833), 7 * s_);
            text(FontId::SemiBold, 15, cxp + 16, cyp + 4 + float(i) * 30 + 5, items[i], i == 0 ? fretColor(Fret::Red) : kCream);
            a.hits.push_back({ir, kHitCtx - i, 0});
        }
    }

    // ---- New-folder / rename prompt ----
    if (a.promptMode) {
        gfx_.rect(R(0, 0, W_, H_), rgb(0x050406, 0.6f));
        const float w = 460, h = 180, x = (W_ - w) / 2, y = (H_ - h) / 2;
        const Rect panel = R(x, y, w, h);
        gfx_.glow(panel, rgb(0x000000, 0.7f), 16 * s_, 36 * s_);
        gfx_.rect(panel, rgb(0x221d25), rgb(0x141116), 16 * s_);
        gfx_.ring(panel, withAlpha(kSand, 0.35f), 16 * s_, 1.5f * s_);
        text(FontId::Title, 30, x + 28, y + 20, a.promptMode == 1 ? "New folder" : "Rename", kCream);
        const Rect field = R(x + 28, y + 72, w - 56, 38);
        gfx_.rect(field, rgb(0x15131a), 8 * s_);
        gfx_.ring(field, withAlpha(kSand, 0.6f), 8 * s_, 1.4f * s_);
        const std::string shown = (a.promptText.empty() ? "type a name" : a.promptText) + (int(now_ * 2) % 2 ? "_" : " ");
        text(FontId::SemiBold, 18, x + 40, y + 80, shown, a.promptText.empty() ? kDim : kCream);
        auto pBtn = [&](const std::string& label, Fret f, int code, float rx) {
            const float bw = measure(FontId::SemiBold, 16, label) + 40;
            const Rect r = R(rx - bw, y + h - 52, bw, 38);
            const bool hv = r.contains(mx, my);
            const Color fc = fretColor(f);
            gfx_.rect(r, hv ? gfx::mix(fc, kInk, 0.5f) : rgb(0x1f1b23), 10 * s_);
            gfx_.ring(r, withAlpha(fc, 0.7f), 10 * s_, 1.3f * s_);
            text(FontId::SemiBold, 16, r.x / s_ + 20, y + h - 52 + 9, label, kCream);
            a.hits.push_back({r, code, -1});
            return bw;
        };
        float rx = x + w - 28;
        rx -= pBtn("Create", Fret::Green, kHitPromptOk, rx) + 12;
        pBtn("Cancel", Fret::Red, kHitPromptCancel, rx);
    }

    drawToast();
}
