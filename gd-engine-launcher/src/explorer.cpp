// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The Explore models screen: the disc's models of one game, grouped and
// searchable, on the left; the chosen one in the 3D viewer on the right,
// which turns (drag, right stick, arrows), zooms (wheel, triggers, + / -),
// pans (right or middle drag, shoulder + right stick) and plays the model's
// animations. The launcher draws the screen and drives the camera; the game
// module lists and draws the models with its own loaders and renderer
// (gd_game_explore, sdk/gd_host.h), so the launcher knows nothing of formats.
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>

#include "launcher.h"
#include "module_host.h"
#include "palette.h"
#include "platform.h"

using gfx::Color;
using gfx::FontId;
using gfx::Rect;
using gfx::rgb;
using gfx::withAlpha;
using namespace palette;

namespace {

constexpr float kPi = 3.14159265f;
const std::string kDot = " \xC2\xB7 ";  // " · "

// Design layout (1280 x 720 units, like the list).
constexpr float kMargin = 24;
constexpr float kTop = 92;
constexpr float kPanelW = 384;
constexpr float kFooterH = 58;
constexpr float kSearchH = 44;
constexpr float kGroupH = 34;
constexpr float kItemH = 50;
// The viewer's backdrop (the module clears to it).
const Color kViewBg = rgb(0x17141a);

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

float approach(float v, float target, float dt, float rate) { return target + (v - target) * std::exp(-rate * dt); }

}  // namespace

struct Launcher::Explore {
    std::string gameId, title;
    ExploreSession session;

    struct Item {
        std::string group, name, detail, key;  // key: what the search matches (lower case)
    };
    std::vector<Item> items;
    // The list as shown: an item index, or -1 - n for the header of group n.
    std::vector<int> rows;
    std::vector<std::string> groups;
    std::string search;
    bool searching = false;
    int cursor = -1;  // a row (always an item's)
    float scroll = 0, scrollTarget = 0;
    bool mouseScrolled = false;

    // The model shown and the one to load next.
    int shown = -1, pending = -1;
    double pendingAt = 0;
    bool loadingDrawn = false;  // the "loading" frame was presented: load now
    bool ok = false;
    std::string info;
    std::vector<std::string> clips;
    int clip = -1;  // -1: the rest pose (a T-pose for GH's characters), the default
    // "Show instrument" (a character's guitar, bass or drum kit): the module has it, its state.
    bool hasInstrument = false, showInstrument = true;
    Rect instrumentBox;  // the checkbox, inside the viewer (window pixels; empty: not shown)
    double shownAt = 0;

    // The camera: targets and the smoothed values drawn.
    float yaw = 0, pitch = 0.22f, zoom = 1, panX = 0, panY = 0;
    float cYaw = 0, cPitch = 0.22f, cZoom = 1, cPanX = 0, cPanY = 0;
    bool turntable = true;
    // Mouse dragging: 1 turn, 2 pan.
    int drag = 0;
    float lastX = 0, lastY = 0;
    double lastClick = -1;
    // Gamepad: right stick, triggers, right shoulder held (pan).
    float rx = 0, ry = 0, lt = 0, rt = 0;
    bool panHeld = false;
    double lastFrame = 0;
    std::vector<Hit> hits;  // list rows (row = list row), the search box (row -2), the footer's buttons (row -3, col: the fret)
    // The module's last frame of the viewer (read back from its context).
    gfx::Texture frameTex;
    gfx::Rect frameRect;
    std::vector<uint8_t> frame;
    std::vector<bool> collapsed;  // per group: its models hidden (not while searching)
};

Launcher::Launcher() = default;
Launcher::~Launcher() = default;

// ---- Opening and closing ----

void Launcher::exploreOpen(Game& g) {
    log_.add("explore models of " + g.id + " (" + g.title() + " " + g.version + ") with " + g.iso);
    drawLoading(g, "Opening the models of");
    auto ex = std::make_unique<Explore>();
    ex->gameId = g.id;
    ex->title = g.title();
    std::string error;
    if (!ex->session.open(window_, context_, g, catalog_.launcherVersion(), log_, error)) {
        restoreWindow();
        log_.add("explore models of " + g.id + " failed: " + error);
        showDialog("We can't rock this! Sorry...", "The models of " + g.title() + " cannot be shown: " + error + ".",
                   {{"OK", Fret::Green, nullptr}});
        return;
    }
    restoreWindow();
    const GdExplorer* api = ex->session.api();
    const int n = api->item_count(api->self);
    for (int i = 0; i < n; ++i) {
        GdExploreItem it{};
        if (api->item(api->self, i, &it) != 0) continue;
        Explore::Item item;
        item.group = it.group ? it.group : "Models";
        item.name = it.name ? it.name : "?";
        item.detail = it.detail ? it.detail : "";
        item.key = lower(item.group + " " + item.name + " " + item.detail);
        ex->items.push_back(std::move(item));
    }
    for (const Explore::Item& it : ex->items)
        if (std::find(ex->groups.begin(), ex->groups.end(), it.group) == ex->groups.end()) ex->groups.push_back(it.group);
    ex->collapsed.assign(ex->groups.size(), false);
    log_.add("explore models of " + g.id + ": " + std::to_string(ex->items.size()) + " models in " +
             std::to_string(ex->groups.size()) + " groups");
    explore_ = std::move(ex);
    explore_->lastFrame = now_;
    exploreFilter();
    // Start on the first model, shown at once.
    for (size_t r = 0; r < explore_->rows.size(); ++r)
        if (explore_->rows[r] >= 0) {
            exploreCursor(int(r));
            explore_->pendingAt = now_;
            break;
        }
}

void Launcher::exploreClose() {
    if (!explore_) return;
    const std::string id = explore_->gameId;
    gfx_.release(explore_->frameTex);
    const bool unloaded = explore_->session.close();
    explore_.reset();
    restoreWindow();
    SDL_StopTextInput(window_);
    log_.add("explore models of " + id + ": closed" + (unloaded ? " (module unloaded)" : " (module stayed loaded)"));
}

// ---- The list ----

void Launcher::exploreFilter() {
    Explore& ex = *explore_;
    const int current = ex.cursor >= 0 && ex.cursor < int(ex.rows.size()) ? ex.rows[size_t(ex.cursor)] : -1000000;
    ex.rows.clear();
    // Every word of the search must match.
    std::vector<std::string> words;
    {
        std::string w;
        for (char c : lower(ex.search) + " ") {
            if (c == ' ') {
                if (!w.empty()) words.push_back(w), w.clear();
            } else {
                w += c;
            }
        }
    }
    // A collapsed group shows its header only (a search shows every match).
    for (size_t gi = 0; gi < ex.groups.size(); ++gi) {
        bool header = false;
        const bool hide = ex.collapsed[gi] && words.empty();
        for (size_t i = 0; i < ex.items.size(); ++i) {
            const Explore::Item& it = ex.items[i];
            if (it.group != ex.groups[gi]) continue;
            bool match = true;
            for (const std::string& w : words) match = match && it.key.find(w) != std::string::npos;
            if (!match) continue;
            if (!header) ex.rows.push_back(-1 - int(gi)), header = true;
            if (!hide) ex.rows.push_back(int(i));
        }
    }
    // Keep the same row (model or group) under the cursor when it is still
    // listed; else its group's header; else the first model.
    ex.cursor = -1;
    int groupOf = current >= 0 ? -1 - int(std::find(ex.groups.begin(), ex.groups.end(), ex.items[size_t(current)].group) - ex.groups.begin()) : current;
    for (int pass = 0; pass < 3 && ex.cursor < 0; ++pass)
        for (size_t r = 0; r < ex.rows.size(); ++r)
            if ((pass == 0 && ex.rows[r] == current && current != -1000000) || (pass == 1 && ex.rows[r] == groupOf) || (pass == 2 && ex.rows[r] >= 0)) {
                ex.cursor = int(r);
                break;
            }
    if (ex.cursor < 0 && !ex.rows.empty()) ex.cursor = 0;
    ex.mouseScrolled = false;
}

// Moves the cursor to list row `row` (a model, or a group's header); a
// model loads after a short pause.
void Launcher::exploreCursor(int row) {
    Explore& ex = *explore_;
    if (row < 0 || row >= int(ex.rows.size())) return;
    if (row != ex.cursor) sounds_.play(UiSound::Move);
    ex.cursor = row;
    ex.mouseScrolled = false;
    const int item = ex.rows[size_t(row)];
    if (item < 0) {  // a group's header: the model on show stays
        ex.pending = -1;
        return;
    }
    if (item != ex.shown) {
        ex.pending = item;
        ex.pendingAt = now_ + 0.3;
        ex.loadingDrawn = false;
    } else {
        ex.pending = -1;
    }
}

// Collapses (1), expands (0) or toggles (-1) group `g`; -2: every group.
void Launcher::exploreGroup(int g, int state) {
    Explore& ex = *explore_;
    if (g == -2) {
        bool all = true;
        for (bool c : ex.collapsed) all = all && c;
        for (size_t i = 0; i < ex.collapsed.size(); ++i) ex.collapsed[i] = state < 0 ? !all : state == 1;
    } else if (g >= 0 && g < int(ex.collapsed.size())) {
        const bool to = state < 0 ? !ex.collapsed[size_t(g)] : state == 1;
        if (to == ex.collapsed[size_t(g)]) return;
        ex.collapsed[size_t(g)] = to;
    } else {
        return;
    }
    sounds_.play(UiSound::Select);
    // The cursor goes to the group's header, so it stays where the eye is.
    if (g >= 0)
        for (size_t r = 0; r < ex.rows.size(); ++r)
            if (ex.rows[r] == -1 - g) ex.cursor = int(r);
    exploreFilter();
}

void Launcher::exploreMove(int dRow, int dCol) {
    Explore& ex = *explore_;
    mouseUsed_ = false;
    const int onRow = ex.cursor >= 0 && ex.cursor < int(ex.rows.size()) ? ex.rows[size_t(ex.cursor)] : 0;
    if (dCol && onRow < 0 && !dRow) {  // on a group's header: left collapses, right expands
        exploreGroup(-1 - onRow, dCol < 0 ? 1 : 0);
        return;
    }
    if (dCol) {  // left / right: turn the model by 15 degrees
        ex.yaw += float(dCol) * kPi / 12;
        ex.turntable = false;
    }
    if (!dRow || ex.rows.empty()) return;
    // Group headers are rows too (green toggles them); Home / End land on the first / last row.
    const int r = std::clamp((ex.cursor < 0 ? 0 : ex.cursor) + dRow, 0, int(ex.rows.size()) - 1);
    exploreCursor(r);
}

void Launcher::explorePress(Fret f) {
    Explore& ex = *explore_;
    switch (f) {
    case Fret::Green:  // show the model under the cursor now (a group's header: open / close it)
        if (ex.searching) {
            ex.searching = false;
            SDL_StopTextInput(window_);
            sounds_.play(UiSound::Select);
        } else if (ex.cursor >= 0 && ex.cursor < int(ex.rows.size()) && ex.rows[size_t(ex.cursor)] < 0) {
            exploreGroup(-1 - ex.rows[size_t(ex.cursor)], -1);
        } else if (ex.pending >= 0) {
            ex.pendingAt = now_;
            sounds_.play(UiSound::Select);
        }
        break;
    case Fret::Red:
        sounds_.play(UiSound::Back);
        if (ex.searching || !ex.search.empty()) {  // first leave the search
            ex.searching = false;
            SDL_StopTextInput(window_);
            if (!ex.search.empty()) ex.search.clear(), exploreFilter();
        } else {
            exploreClose();
        }
        break;
    case Fret::Yellow:  // reset the view
        sounds_.play(UiSound::Select);
        ex.yaw = 0, ex.pitch = 0.22f, ex.zoom = 1, ex.panX = ex.panY = 0;
        ex.turntable = true;
        // The shortest way back to the front.
        ex.cYaw = std::remainder(ex.cYaw, 2 * kPi);
        break;
    case Fret::Blue:  // the next animation: the rest pose, then each clip, then the rest pose again
        if (!ex.clips.empty()) {
            ex.clip = ex.clip + 1 >= int(ex.clips.size()) ? -1 : ex.clip + 1;
            ex.shownAt = now_;
            sounds_.play(UiSound::Select);
        } else {
            sounds_.play(UiSound::Error);
            toast("This model has no animations.");
        }
        break;
    case Fret::Orange:
        sounds_.play(UiSound::Select);
        ex.turntable = !ex.turntable;
        break;
    }
}

// The "Show instrument" checkbox (I, the left stick's click, a click on it).
void Launcher::exploreToggleInstrument() {
    Explore& ex = *explore_;
    const GdExplorer* api = ex.session.api();
    if (!ex.hasInstrument || !api || !GD_EXPLORER_HAS(api, show_instrument)) {
        sounds_.play(UiSound::Error);
        toast("This model has no instrument.");
        return;
    }
    ex.showInstrument = !ex.showInstrument;
    sounds_.play(UiSound::Select);
    api->show_instrument(api->self, ex.showInstrument ? 1 : 0);
    SDL_GL_MakeCurrent(window_, context_);
    log_.add("explore " + ex.gameId + ": instrument " + (ex.showInstrument ? "shown" : "hidden"));
}

Rect Launcher::exploreViewRect() const { return R(kMargin + kPanelW + 16, kTop, W_ - 2 * kMargin - kPanelW - 16, H_ - kFooterH - 14 - kTop); }

// ---- Input ----

bool Launcher::exploreHandle(const SDL_Event& e) {
    Explore& ex = *explore_;
    switch (e.type) {
    case SDL_EVENT_QUIT:  // closing the window leaves the explorer, as it leaves a game
        exploreClose();
        return true;
    case SDL_EVENT_DROP_BEGIN: case SDL_EVENT_DROP_COMPLETE: case SDL_EVENT_DROP_FILE: case SDL_EVENT_DROP_TEXT:
        return true;  // discs are dropped on the list
    case SDL_EVENT_TEXT_INPUT:
        if (ex.searching && e.text.text) {
            ex.search += e.text.text;
            exploreFilter();
        }
        return true;
    case SDL_EVENT_KEY_DOWN: {
        const SDL_Keycode k = e.key.key;
        if (ex.searching) {
            if (k == SDLK_BACKSPACE) {
                if (!ex.search.empty()) {
                    // One UTF-8 character.
                    size_t n = ex.search.size() - 1;
                    while (n > 0 && (static_cast<unsigned char>(ex.search[n]) & 0xC0) == 0x80) --n;
                    ex.search.resize(n);
                    exploreFilter();
                }
                return true;
            }
            if (k == SDLK_ESCAPE || k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_TAB) {
                if (k == SDLK_ESCAPE) ex.search.clear(), exploreFilter();
                ex.searching = false;
                SDL_StopTextInput(window_);
                return true;
            }
            if (k == SDLK_UP || k == SDLK_DOWN || k == SDLK_PAGEUP || k == SDLK_PAGEDOWN) return false;  // the list moves while typing
            return true;  // letters arrive as text
        }
        switch (k) {
        case SDLK_SLASH: case SDLK_F:
            ex.searching = true;
            SDL_StartTextInput(window_);
            return true;
        case SDLK_EQUALS: case SDLK_PLUS: case SDLK_KP_PLUS: ex.zoom = std::max(0.08f, ex.zoom * 0.85f); return true;
        case SDLK_MINUS: case SDLK_KP_MINUS: ex.zoom = std::min(6.f, ex.zoom / 0.85f); return true;
        case SDLK_R: explorePress(Fret::Yellow); return true;
        case SDLK_N: case SDLK_TAB: explorePress(Fret::Blue); return true;
        case SDLK_T: case SDLK_O: explorePress(Fret::Orange); return true;
        case SDLK_I: exploreToggleInstrument(); return true;
        case SDLK_LEFT: case SDLK_A: exploreMove(0, -1); return true;  // also on key repeat
        case SDLK_RIGHT: case SDLK_D: exploreMove(0, 1); return true;
        case SDLK_UP: case SDLK_W: exploreMove(-1, 0); return true;
        case SDLK_DOWN: case SDLK_S: exploreMove(1, 0); return true;
        case SDLK_P: return true;  // no Play here
        default: return false;
        }
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        const float density = SDL_GetWindowPixelDensity(window_);
        const float mx = e.button.x * density, my = e.button.y * density;
        const Rect view = exploreViewRect();
        if (e.button.button == SDL_BUTTON_LEFT && ex.instrumentBox.w > 0 && ex.instrumentBox.contains(mx, my)) {
            exploreToggleInstrument();
            return true;
        }
        if (view.contains(mx, my)) {
            const bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
            ex.drag = e.button.button == SDL_BUTTON_LEFT && !shift ? 1 : 2;
            ex.lastX = mx, ex.lastY = my;
            if (e.button.button == SDL_BUTTON_LEFT && e.button.clicks >= 2) explorePress(Fret::Yellow);
            return true;
        }
        if (e.button.button == SDL_BUTTON_RIGHT) {
            explorePress(Fret::Red);
            return true;
        }
        for (const Hit& h : ex.hits) {
            if (!h.r.contains(mx, my)) continue;
            if (h.row == -2) {
                ex.searching = true;
                SDL_StartTextInput(window_);
                sounds_.play(UiSound::Select);
            } else if (h.row == -3) {  // a footer button: as its fret
                explorePress(Fret(h.col));
            } else if (ex.rows[size_t(h.row)] < 0) {  // a group's header: open / close it
                exploreCursor(h.row);
                exploreGroup(-1 - ex.rows[size_t(h.row)], -1);
            } else {
                exploreCursor(h.row);
                if (ex.pending >= 0) ex.pendingAt = now_, sounds_.play(UiSound::Select);
            }
            return true;
        }
        return false;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP:
        ex.drag = 0;
        return true;
    case SDL_EVENT_MOUSE_MOTION: {
        if (!ex.drag) return true;
        const float density = SDL_GetWindowPixelDensity(window_);
        const float mx = e.motion.x * density, my = e.motion.y * density;
        const float dx = mx - ex.lastX, dy = my - ex.lastY;
        ex.lastX = mx, ex.lastY = my;
        const Rect view = exploreViewRect();
        if (ex.drag == 1) {
            ex.yaw -= dx / std::max(1.f, view.w) * 2 * kPi;
            ex.pitch = std::clamp(ex.pitch + dy / std::max(1.f, view.h) * kPi, -1.4f, 1.4f);
            ex.turntable = false;
        } else {
            ex.panX -= dx / std::max(1.f, view.h) * 2 * ex.zoom;
            ex.panY += dy / std::max(1.f, view.h) * 2 * ex.zoom;
        }
        return true;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        float mx = 0, my = 0;
        SDL_GetMouseState(&mx, &my);
        const float density = SDL_GetWindowPixelDensity(window_);
        if (exploreViewRect().contains(mx * density, my * density)) {
            ex.zoom = std::clamp(ex.zoom * std::pow(0.88f, e.wheel.y), 0.08f, 6.f);
        } else {
            ex.scrollTarget -= e.wheel.y * 72;
            ex.mouseScrolled = true;
        }
        return true;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        const float v = float(e.gaxis.value) / 32767.f;
        switch (e.gaxis.axis) {
        case SDL_GAMEPAD_AXIS_RIGHTX: ex.rx = v; return true;
        case SDL_GAMEPAD_AXIS_RIGHTY: ex.ry = v; return true;
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: ex.lt = v; return true;
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: ex.rt = v; return true;
        default: return false;  // the left stick moves in the list
        }
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        switch (e.gbutton.button) {
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: explorePress(Fret::Orange); return true;  // a guitar's orange fret
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: ex.panHeld = true; return true;
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK: explorePress(Fret::Yellow); return true;
        case SDL_GAMEPAD_BUTTON_LEFT_STICK: exploreToggleInstrument(); return true;
        default: return false;
        }
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        if (e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) ex.panHeld = false;
        return false;
    default:
        return false;
    }
}

// Development aid (GDUL_SCRIPT): explore=<game id> opens a game's explorer;
// while it is open orbit=<yaw>,<pitch> (degrees), zoom=<factor>, pan=<x>,<y>
// (model radii), search=<text>, pick=<text> (the first model whose name or
// path has it, shown at once), clip=next, reset, turntable=0|1,
// instrument=0|1 (the "Show instrument" checkbox).
bool Launcher::exploreScript(const std::string& cmd, const std::string& arg) {
    if (cmd == "explore") {
        auto& games = catalog_.games();
        for (size_t i = 0; i < games.size(); ++i)
            if (games[i].id == arg) focusRow_ = int(i), focusCol_ = kExplore, activate(int(i), kExplore);
        return true;
    }
    if (!explore_) return false;
    Explore& ex = *explore_;
    if (cmd == "orbit") {
        float y = 0, p = 0;
        SDL_sscanf(arg.c_str(), "%f,%f", &y, &p);
        ex.yaw += y * kPi / 180, ex.pitch = std::clamp(ex.pitch + p * kPi / 180, -1.4f, 1.4f);
        ex.turntable = false;
    } else if (cmd == "zoom") {
        ex.zoom = std::clamp(ex.zoom * float(std::atof(arg.c_str())), 0.08f, 6.f);
    } else if (cmd == "pan") {
        float x = 0, y = 0;
        SDL_sscanf(arg.c_str(), "%f,%f", &x, &y);
        ex.panX += x, ex.panY += y;
    } else if (cmd == "search") {
        ex.search = arg;
        exploreFilter();
    } else if (cmd == "pick") {
        const std::string want = lower(arg);
        for (const Explore::Item& it : ex.items)  // its group open first
            if (it.key.find(want) != std::string::npos) {
                const int gi = int(std::find(ex.groups.begin(), ex.groups.end(), it.group) - ex.groups.begin());
                if (ex.collapsed[size_t(gi)]) ex.collapsed[size_t(gi)] = false, exploreFilter();
                break;
            }
        for (size_t r = 0; r < ex.rows.size(); ++r)
            if (ex.rows[r] >= 0 && ex.items[size_t(ex.rows[r])].key.find(want) != std::string::npos) {
                exploreCursor(int(r));
                ex.pendingAt = now_;
                break;
            }
    } else if (cmd == "clip") {
        explorePress(Fret::Blue);
    } else if (cmd == "reset") {
        explorePress(Fret::Yellow);
    } else if (cmd == "instrument") {
        if ((arg != "0") != ex.showInstrument) exploreToggleInstrument();
    } else if (cmd == "turntable") {
        ex.turntable = arg != "0";
    } else if (cmd == "collapse" || cmd == "expand") {  // a group (its name has <arg>), or "all"
        const std::string want = lower(arg);
        for (size_t gi = 0; gi < ex.groups.size(); ++gi)
            if (want == "all" || lower(ex.groups[gi]).find(want) != std::string::npos) {
                if (want == "all") exploreGroup(-2, cmd == "collapse" ? 1 : 0);
                else exploreGroup(int(gi), cmd == "collapse" ? 1 : 0);
                break;
            }
    } else {
        return false;
    }
    return true;
}

// ---- A frame ----

void Launcher::exploreLoad() {
    Explore& ex = *explore_;
    const GdExplorer* api = ex.session.api();
    const int item = ex.pending;
    ex.pending = -1;
    char info[512] = {};
    const Uint64 t0 = SDL_GetTicksNS();
    ex.ok = api->select(api->self, item, info, int(sizeof info)) == 0;
    SDL_GL_MakeCurrent(window_, context_);
    ex.shown = item;
    ex.info = info;
    ex.clips.clear();
    if (ex.ok && api->clip_count && api->clip_name) {
        const int n = api->clip_count(api->self);
        for (int i = 0; i < n; ++i) {
            const char* c = api->clip_name(api->self, i);
            ex.clips.push_back(c ? c : "");
        }
    }
    ex.clip = -1;  // the rest pose first: arms out, nothing through the body (Next animation plays the clips)
    ex.hasInstrument = ex.ok && GD_EXPLORER_HAS(api, has_instrument) && GD_EXPLORER_HAS(api, show_instrument) &&
                       api->has_instrument(api->self) != 0;
    ex.shownAt = double(SDL_GetTicksNS() - startNs_) / 1e9;
    const Explore::Item& it = ex.items[size_t(item)];
    char secs[32];
    SDL_snprintf(secs, sizeof secs, "%.2f", double(SDL_GetTicksNS() - t0) / 1e9);
    log_.add("explore " + ex.gameId + ": " + it.group + " / " + it.name + (ex.ok ? " shown (" : " failed (") + ex.info + ", " + secs + " s)");
}

void Launcher::exploreFrame() {
    Explore& ex = *explore_;
    const float dt = float(std::clamp(now_ - ex.lastFrame, 0.0, 0.1));
    ex.lastFrame = now_;
    // A model waiting to load: one frame says so, the next loads it.
    if (ex.pending >= 0 && now_ >= ex.pendingAt) {
        if (ex.loadingDrawn) {
            exploreLoad();
        } else {
            ex.loadingDrawn = true;
        }
    }
    // The gamepad's right stick turns (pans with the right shoulder), the triggers zoom.
    auto dead = [](float v) { return std::abs(v) < 0.18f ? 0.f : (v - std::copysign(0.18f, v)) / 0.82f; };
    const float sx = dead(ex.rx), sy = dead(ex.ry);
    if (sx != 0 || sy != 0) {
        if (ex.panHeld) {
            ex.panX += sx * dt * 1.5f * ex.zoom, ex.panY -= sy * dt * 1.5f * ex.zoom;
        } else {
            ex.yaw -= sx * dt * 2.5f;
            ex.pitch = std::clamp(ex.pitch - sy * dt * 1.8f, -1.4f, 1.4f);
            ex.turntable = false;
        }
    }
    if (ex.lt > 0.05f || ex.rt > 0.05f) ex.zoom = std::clamp(ex.zoom * std::exp((ex.lt - ex.rt) * dt * 1.6f), 0.08f, 6.f);
    if (ex.turntable) ex.yaw += dt * 0.35f;
    ex.cYaw = approach(ex.cYaw, ex.yaw, dt, 14);
    ex.cPitch = approach(ex.cPitch, ex.pitch, dt, 14);
    ex.cZoom = approach(ex.cZoom, ex.zoom, dt, 12);
    ex.cPanX = approach(ex.cPanX, ex.panX, dt, 14);
    ex.cPanY = approach(ex.cPanY, ex.panY, dt, 14);

    SDL_GetWindowSizeInPixels(window_, &pw_, &ph_);
    s_ = std::max(0.25f, std::min(float(pw_) / 1280.f, float(ph_) / 720.f));
    W_ = float(pw_) / s_, H_ = float(ph_) / s_;
    // The module draws the model with its own GL context, centred in the
    // viewer. Its frame is read back from that context (the viewer's part of
    // its back buffer) and drawn by the launcher's context, which alone
    // presents: one context presents every frame, cleared, on every system.
    // (Two contexts presenting or drawing over each other's back buffer
    // flickered on macOS, where each context has its own drawable surface
    // and a back buffer's content is undefined after a swap.)
    const bool model = ex.shown >= 0 && ex.ok;
    const Rect v = exploreViewRect();
    bool frame = false;
    if (model) {
        GdExploreView view{};
        view.x = int(v.x), view.y = int(v.y), view.w = int(v.w), view.h = int(v.h);
        view.yaw = ex.cYaw, view.pitch = ex.cPitch, view.zoom = ex.cZoom, view.pan_x = ex.cPanX, view.pan_y = ex.cPanY;
        view.seconds = float(now_ - ex.shownAt);
        view.clip = ex.clip;
        view.background[0] = kViewBg.r, view.background[1] = kViewBg.g, view.background[2] = kViewBg.b;
        const GdExplorer* api = ex.session.api();
        api->draw(api->self, &view);
        SDL_GLContext drawn = SDL_GL_GetCurrentContext();
        if (drawn && drawn != context_) {
            const int x0 = std::clamp(int(v.x), 0, pw_), y0 = std::clamp(int(v.y), 0, ph_);
            const int x1 = std::clamp(int(v.x + v.w), 0, pw_), y1 = std::clamp(int(v.y + v.h), 0, ph_);
            frame = gfx::readBackBuffer(x0, ph_ - y1, x1 - x0, y1 - y0, ex.frame);
            ex.frameRect = {float(x0), float(y0), float(x1 - x0), float(y1 - y0)};
        }
        SDL_GL_MakeCurrent(window_, context_);
        if (frame) gfx_.upload(ex.frameTex, int(ex.frameRect.w), int(ex.frameRect.h), ex.frame.data());
    }
    hits_.clear();
    gfx_.begin(pw_, ph_);
    drawBackground();
    if (model && ex.frameTex.id) gfx_.image(ex.frameTex, ex.frameRect, {}, true, 16 * s_);  // this frame's, else the last one
    else gfx_.rect(v, withAlpha(kViewBg, 0.55f), 16 * s_);
    exploreDraw();
    drawToast();
    gfx_.end();
    if (!pendingShot_.empty()) {
        screenshot(pendingShot_);
        pendingShot_.clear();
    }
    SDL_GL_SwapWindow(window_);
}

// ---- Drawing ----

void Launcher::exploreDraw() {
    Explore& ex = *explore_;
    // The header: a dark fade over the 3D view, the badge, the title and the game.
    gfx_.rect(R(0, 0, W_, kTop + 10), rgb(0x0b0a0d, 0.92f), rgb(0x0b0a0d, 0));
    gfx_.glow(R(34, 22, 48, 48), withAlpha(kOxblood, 0.35f), 24 * s_, 60 * s_);
    gfx_.image(gfx_.icon(), R(kMargin, 12, 68, 68));
    text(FontId::Title, 40, 104, 14, "Explore models", kCream);
    const float subW = text(FontId::SemiBold, 15, 106, 62, ex.gameId + "/" + ex.title, kSand, 1.5f);
    gfx_.rect(R(106, 84, std::min(140.f, subW), 3), kOxblood, 1.5f * s_);
    const std::string count = std::to_string(ex.items.size()) + " models" + kDot + std::to_string(ex.groups.size()) + " groups";
    text(FontId::SemiBold, 18, W_ - kMargin - measure(FontId::SemiBold, 18, count), 22, count, kCream);
    const std::string from = "straight from your disc image";
    text(FontId::Regular, 15, W_ - kMargin - measure(FontId::Regular, 15, from), 48, from, kDim);

    exploreDrawList(R(kMargin, kTop, kPanelW, H_ - kFooterH - 14 - kTop));
    exploreDrawViewer(exploreViewRect());
    exploreDrawFooter();
}

void Launcher::exploreDrawList(const Rect& panelPx) {
    Explore& ex = *explore_;
    ex.hits.clear();
    const float x = kMargin, w = kPanelW, top = kTop, h = panelPx.h / s_;
    gfx_.glow(panelPx, rgb(0x000000, 0.55f), 16 * s_, 26 * s_);
    gfx_.rect(panelPx, rgb(0x1d1a20, 0.94f), rgb(0x131115, 0.96f), 16 * s_);
    gfx_.ring(panelPx, withAlpha(kCream, 0.08f), 16 * s_, 1 * s_);

    // The search box.
    const Rect box = R(x + 12, top + 12, w - 24, kSearchH);
    gfx_.rect(box, rgb(0x0e0d10, 0.9f), 12 * s_);
    gfx_.ring(box, ex.searching ? withAlpha(fretColor(Fret::Blue), 0.9f) : withAlpha(kCream, 0.12f), 12 * s_, (ex.searching ? 2.f : 1.f) * s_);
    if (ex.searching) gfx_.glow(box, withAlpha(fretColor(Fret::Blue), 0.18f), 12 * s_, 12 * s_);
    // A magnifier: a ring and its handle.
    const float mx = x + 34, my = top + 12 + kSearchH / 2 - 2;
    gfx_.ring(R(mx - 7, my - 7, 14, 14), withAlpha(kSand, 0.8f), 7 * s_, 2 * s_);
    gfx_.rect(R(mx + 4.5f, my + 4.5f, 7, 2.6f), withAlpha(kSand, 0.8f), 1.3f * s_);
    const float tx = x + 54;
    if (ex.search.empty() && !ex.searching) {
        text(FontId::Regular, 17, tx, top + 12 + 11, "Search models  ( / )", kMuted);
    } else {
        const std::string shown = gfx_.elide(FontId::Regular, 17 * s_, ex.search, (w - 100) * s_);
        const float sw = text(FontId::Regular, 17, tx, top + 12 + 11, shown, kCream);
        if (ex.searching && std::fmod(now_, 1.0) < 0.6) gfx_.rect(R(tx + sw + 2, top + 12 + 12, 2, 20), kCream);
    }
    int matches = 0;
    for (int r : ex.rows) matches += r >= 0 ? 1 : 0;
    if (!ex.search.empty()) {
        const std::string m = std::to_string(matches);
        text(FontId::SemiBold, 14, x + w - 24 - measure(FontId::SemiBold, 14, m), top + 12 + 13, m, kSand);
    }
    ex.hits.push_back({box, -2, -1});

    // The rows.
    const float listTop = top + 12 + kSearchH + 10, listBottom = top + h - 10;
    const float view = listBottom - listTop;
    std::vector<float> ys(ex.rows.size());
    float y = 0;
    for (size_t r = 0; r < ex.rows.size(); ++r) {
        ys[r] = y;
        y += ex.rows[r] < 0 ? kGroupH : kItemH;
    }
    const float content = y;
    if (!ex.mouseScrolled && ex.cursor >= 0 && ex.cursor < int(ex.rows.size())) {
        const float cy = ys[size_t(ex.cursor)];
        const float ch = ex.rows[size_t(ex.cursor)] < 0 ? kGroupH : kItemH;
        // Keep the cursor's row (and its group's header when it is the first) in view.
        const float want = ex.cursor > 0 && ex.rows[size_t(ex.cursor - 1)] < 0 ? cy - kGroupH : cy;
        if (want < ex.scrollTarget) ex.scrollTarget = want;
        if (cy + ch > ex.scrollTarget + view) ex.scrollTarget = cy + ch - view;
    }
    ex.scrollTarget = std::clamp(ex.scrollTarget, 0.f, std::max(0.f, content - view));
    ex.scroll += (ex.scrollTarget - ex.scroll) * 0.3f;
    if (std::abs(ex.scrollTarget - ex.scroll) < 0.5f) ex.scroll = ex.scrollTarget;

    const Rect clip = R(x, listTop, w, view);
    gfx_.clip(&clip);
    for (size_t r = 0; r < ex.rows.size(); ++r) {
        const float ry = listTop + ys[r] - ex.scroll;
        const float rh = ex.rows[r] < 0 ? kGroupH : kItemH;
        if (ry + rh < listTop || ry > listBottom) continue;
        if (ex.rows[r] < 0) {
            // A group's header: click (or green) opens / closes it.
            const int gi = -1 - ex.rows[r];
            const std::string& g = ex.groups[size_t(gi)];
            const bool closed = ex.collapsed[size_t(gi)] && ex.search.empty();
            const bool cur = int(r) == ex.cursor;
            int n = 0;
            for (const Explore::Item& it : ex.items) n += it.group == g ? 1 : 0;
            if (!ex.search.empty()) {
                n = 0;
                for (size_t k = r + 1; k < ex.rows.size() && ex.rows[k] >= 0; ++k) ++n;
            }
            const Rect hr = R(x + 10, ry + 2, w - 20, kGroupH - 4);
            if (cur) {
                gfx_.rect(hr, rgb(0x2c2731, 0.98f), rgb(0x232027, 0.98f), 8 * s_);
                gfx_.ring(hr, withAlpha(kCream, 0.6f), 8 * s_, 1.5f * s_);
            }
            // The open / closed mark: a small box with a minus or a plus.
            const float bx = x + 20, by = ry + kGroupH / 2 - 7;
            gfx_.rect(R(bx, by, 14, 14), closed ? withAlpha(kOxblood, 0.9f) : rgb(0x2a252e), 4 * s_);
            gfx_.ring(R(bx, by, 14, 14), withAlpha(kSand, closed ? 0.7f : 0.45f), 4 * s_, 1 * s_);
            gfx_.rect(R(bx + 3.5f, by + 6.25f, 7, 1.5f), kCream);
            if (closed) gfx_.rect(R(bx + 6.25f, by + 3.5f, 1.5f, 7), kCream);
            std::string up = g;
            for (char& c : up) c = char(std::toupper(static_cast<unsigned char>(c)));
            const float gw = text(FontId::SemiBold, 13, x + 44, ry + 9, up, cur ? kCream : kSand, 2.2f);
            text(FontId::SemiBold, 13, x + 44 + gw + 8, ry + 9, std::to_string(n), cur ? kSand : kMuted);
            if (!cur) gfx_.rect(R(x + 22, ry + kGroupH - 3, w - 44, 1), withAlpha(kCream, 0.07f));
            ex.hits.push_back({hr, int(r), -1});
            continue;
        }
        const Explore::Item& it = ex.items[size_t(ex.rows[r])];
        const bool cur = int(r) == ex.cursor;
        const bool showing = ex.rows[r] == ex.shown;
        const Rect row = R(x + 10, ry + 3, w - 20, kItemH - 6);
        if (cur) {
            gfx_.rect(row, rgb(0x2c2731, 0.98f), rgb(0x232027, 0.98f), 10 * s_);
            gfx_.ring(row, withAlpha(kCream, 0.6f), 10 * s_, 1.5f * s_);
        }
        // The fret gem: blue for the model on show.
        const Color gem = showing ? fretColor(Fret::Blue) : withAlpha(kMuted, 0.7f);
        if (showing) gfx_.glow(R(x + 22, ry + kItemH / 2 - 5, 10, 10), withAlpha(gem, 0.5f), 5 * s_, 8 * s_);
        gfx_.circle((x + 27) * s_, (ry + kItemH / 2) * s_, 4.5f * s_, gem);
        const std::string name = gfx_.elide(FontId::SemiBold, 18 * s_, it.name, (w - 70) * s_);
        text(FontId::SemiBold, 18, x + 42, ry + 6, name, cur || showing ? kCream : rgb(0xd6cab3));
        const std::string detail = gfx_.elide(FontId::Regular, 13 * s_, it.detail, (w - 70) * s_);
        text(FontId::Regular, 13, x + 42, ry + 28, detail, cur ? kSand : kDim);
        ex.hits.push_back({row, int(r), -1});
    }
    if (ex.rows.empty()) {
        const std::string none = "No model matches \xE2\x80\x9C" + ex.search + "\xE2\x80\x9D";
        text(FontId::Regular, 17, x + 24, listTop + 16, gfx_.elide(FontId::Regular, 17 * s_, none, (w - 48) * s_), kDim);
    }
    gfx_.clip(nullptr);
    // Clicks outside the list's view do not count.
    ex.hits.erase(std::remove_if(ex.hits.begin(), ex.hits.end(),
                                 [&](const Hit& hh) { return hh.row >= 0 && (hh.r.y + hh.r.h < listTop * s_ || hh.r.y > listBottom * s_); }),
                  ex.hits.end());
    if (ex.scroll > 1) gfx_.rect(R(x + 1, listTop, w - 2, 26), rgb(0x1c191f, 1), rgb(0x1c191f, 0));
    if (ex.scroll < content - view - 1) gfx_.rect(R(x + 1, listBottom - 18, w - 2, 18), rgb(0x141216, 0), rgb(0x141216, 0.95f));
}

void Launcher::exploreDrawViewer(const Rect& viewPx) {
    Explore& ex = *explore_;
    const float vx = viewPx.x / s_, vy = viewPx.y / s_, vw = viewPx.w / s_, vh = viewPx.h / s_;
    gfx_.ring(viewPx, withAlpha(kCream, 0.07f), 16 * s_, 1 * s_);

    const bool loading = ex.pending >= 0 && ex.loadingDrawn;
    if (ex.shown >= 0) {
        const Explore::Item& it = ex.items[size_t(ex.shown)];
        // The model's card, top left of the viewer, on a soft shade (bright venues).
        if (ex.ok) gfx_.rect(R(vx + 1, vy + 1, vw - 2, 120), rgb(0x0b0a0d, 0.6f), rgb(0x0b0a0d, 0), 16 * s_);
        const std::string name = gfx_.elide(FontId::Title, 38 * s_, it.name, (vw - 48) * s_);
        text(FontId::Title, 38, vx + 24, vy + 14, name, kCream);
        const std::string sub = gfx_.elide(FontId::SemiBold, 15 * s_, it.group + kDot + it.detail, (vw - 48) * s_);
        text(FontId::SemiBold, 15, vx + 26, vy + 64, sub, kSand);
        if (ex.ok) {
            const std::string inf = gfx_.elide(FontId::Regular, 14 * s_, ex.info, (vw - 48) * s_);
            text(FontId::Regular, 14, vx + 26, vy + 86, inf, kDim);
        }
    }
    // "Show instrument": a checkbox (an on/off choice, guidelines/universal_launcher.md),
    // under the model's card, for a character with a guitar, bass or drum kit.
    ex.instrumentBox = {};
    if (ex.shown >= 0 && ex.ok && ex.hasInstrument && !loading) {
        float mx = -1, my = -1;
        SDL_GetMouseState(&mx, &my);
        const float density = SDL_GetWindowPixelDensity(window_);
        const std::string label = "Show instrument";
        const float bx = vx + 26, by = vy + 112, lw = measure(FontId::SemiBold, 16, label);
        const Rect hit = R(bx - 6, by - 6, 22 + 10 + lw + 12, 34);
        const bool hover = hit.contains(mx * density, my * density);
        const Color accent = fretColor(Fret::Green);
        gfx_.rect(hit, rgb(0x0e0d10, hover ? 0.9f : 0.7f), 10 * s_);
        const Rect box = R(bx, by, 22, 22);
        gfx_.rect(box, ex.showInstrument ? accent : rgb(0x1f1b23, 0.95f), 5 * s_);
        gfx_.ring(box, ex.showInstrument ? withAlpha(accent, 0.95f) : withAlpha(kCream, hover ? 0.6f : 0.35f), 5 * s_, 1.5f * s_);
        if (ex.showInstrument) {  // the tick: a short and a long stroke
            for (int i = 0; i < 5; ++i) gfx_.circle((bx + 5 + float(i) * 1.3f) * s_, (by + 11 + float(i) * 1.3f) * s_, 1.6f * s_, kInk);
            for (int i = 0; i < 9; ++i) gfx_.circle((bx + 10.2f + float(i) * 1.25f) * s_, (by + 16.2f - float(i) * 1.45f) * s_, 1.6f * s_, kInk);
        }
        text(FontId::SemiBold, 16, bx + 32, by + 1, label, hover ? kCream : kSand);
        ex.instrumentBox = hit;
    }
    if (ex.shown >= 0 && !ex.ok && !loading) {
        // What went wrong, in the middle.
        const float cw = std::min(520.f, vw - 60);
        const std::vector<std::string> lines = gfx_.wrap(FontId::Regular, 17 * s_, ex.info, (cw - 48) * s_);
        const float ch = 86 + float(lines.size()) * 24;
        const Rect card = R(vx + (vw - cw) / 2, vy + (vh - ch) / 2, cw, ch);
        gfx_.glow(card, rgb(0x000000, 0.6f), 14 * s_, 30 * s_);
        gfx_.rect(card, rgb(0x241e26, 0.97f), rgb(0x18151a, 0.97f), 14 * s_);
        gfx_.ring(card, withAlpha(fretColor(Fret::Red), 0.5f), 14 * s_, 1.5f * s_);
        text(FontId::SemiBold, 22, vx + (vw - cw) / 2 + 24, vy + (vh - ch) / 2 + 20, "This one won't rock here", kCream);
        float ly = vy + (vh - ch) / 2 + 58;
        for (const std::string& l : lines) text(FontId::Regular, 17, vx + (vw - cw) / 2 + 24, ly, l, rgb(0xd9cfbd)), ly += 24;
    }
    if (ex.shown < 0 && !loading) {
        const std::string t = ex.rows.empty() ? "Nothing to show" : "Pick a model";
        text(FontId::Title, 44, vx + (vw - measure(FontId::Title, 44, t)) / 2, vy + vh / 2 - 40, t, withAlpha(kCream, 0.85f));
    }
    if (loading) {
        // A loading card with a spinning ring of fret gems.
        const Explore::Item& it = ex.items[size_t(ex.pending)];
        const std::string t = "Loading " + it.name + "\xE2\x80\xA6";
        const float tw = std::min(vw - 60, measure(FontId::SemiBold, 20, t) + 110);
        const Rect card = R(vx + (vw - tw) / 2, vy + vh / 2 - 34, tw, 68);
        gfx_.glow(card, rgb(0x000000, 0.6f), 34 * s_, 30 * s_);
        gfx_.rect(card, rgb(0x2b2530, 0.97f), rgb(0x1f1b23, 0.97f), 34 * s_);
        gfx_.ring(card, withAlpha(kSand, 0.45f), 34 * s_, 1.2f * s_);
        const float cx = vx + (vw - tw) / 2 + 40, cy = vy + vh / 2;
        const Fret frets[5] = {Fret::Green, Fret::Red, Fret::Yellow, Fret::Blue, Fret::Orange};
        for (int i = 0; i < 5; ++i) {
            const float a = float(now_) * 4 + float(i) * 2 * kPi / 5;
            gfx_.circle((cx + std::cos(a) * 13) * s_, (cy + std::sin(a) * 13) * s_, 3.6f * s_, fretColor(frets[i]));
        }
        text(FontId::SemiBold, 20, cx + 30, cy - 13, gfx_.elide(FontId::SemiBold, 20 * s_, t, (tw - 90) * s_), kCream);
    }
    if (ex.shown >= 0 && ex.ok && !ex.clips.empty()) {
        // The animation, bottom centre of the viewer.
        const std::string label = ex.clip < 0 ? std::string("Rest pose") : ex.clips[size_t(ex.clip)];
        const std::string pos = ex.clip < 0 ? std::to_string(ex.clips.size()) + " animations"
                                            : std::to_string(ex.clip + 1) + "/" + std::to_string(ex.clips.size());
        const float lw = std::min(vw - 60, measure(FontId::SemiBold, 16, label) + measure(FontId::Regular, 14, pos) +
                                               measure(FontId::SemiBold, 13, "ANIMATION", 1.8f) + 90);
        const Rect pill = R(vx + (vw - lw) / 2, vy + vh - 56, lw, 38);
        gfx_.rect(pill, rgb(0x0e0d10, 0.82f), 19 * s_);
        gfx_.ring(pill, withAlpha(fretColor(Fret::Blue), 0.55f), 19 * s_, 1.2f * s_);
        float px = vx + (vw - lw) / 2 + 18;
        gfx_.circle((px + 6) * s_, (vy + vh - 37) * s_, 6 * s_, fretColor(Fret::Blue));
        px += 20;
        px += text(FontId::SemiBold, 13, px, vy + vh - 47, "ANIMATION", kSand, 1.8f) + 10;
        const std::string shown = gfx_.elide(FontId::SemiBold, 16 * s_, label,
                                             (lw - (px - (vx + (vw - lw) / 2)) - measure(FontId::Regular, 14, pos) - 28) * s_);
        px += text(FontId::SemiBold, 16, px, vy + vh - 48, shown, kCream) + 10;
        text(FontId::Regular, 14, px, vy + vh - 46, pos, kDim);
    }
}

void Launcher::exploreDrawFooter() {
    Explore& ex = *explore_;
    const float y = H_ - kFooterH;
    gfx_.rect(R(0, y, W_, kFooterH), rgb(0x0d0b0f, 0.92f), rgb(0x080709, 0.98f));
    gfx_.rect(R(0, y, W_, 1), withAlpha(kCream, 0.1f));
    float x = kMargin;
    const float cy = y + kFooterH / 2;
    float mx = -1, my = -1;
    SDL_GetMouseState(&mx, &my);
    const float density = SDL_GetWindowPixelDensity(window_);
    mx *= density, my *= density;
    // The actions as buttons in their fret's colour: click one, or press its fret.
    auto button = [&](Fret f, const std::string& label, bool on) {
        const Color fc = fretColor(f);
        const float bw = measure(FontId::SemiBold, 16, label) + 30;
        const Rect r = R(x, cy - 17, bw, 34);
        const bool hover = on && r.contains(mx, my);
        if (hover) gfx_.glow(r, withAlpha(fc, 0.35f), 17 * s_, 12 * s_);
        gfx_.rect(r, hover ? gfx::mix(fc, kInk, 0.55f) : rgb(0x1f1b23, on ? 0.95f : 0.6f), hover ? gfx::mix(fc, kInk, 0.7f) : rgb(0x18151b, on ? 0.95f : 0.6f), 17 * s_);
        gfx_.ring(r, on ? withAlpha(fc, hover ? 0.95f : 0.6f) : withAlpha(kCream, 0.08f), 17 * s_, (hover ? 1.6f : 1.2f) * s_);
        text(FontId::SemiBold, 16, x + 15, cy - 10, label, on ? (hover ? kCream : kSand) : kMuted);
        if (on) ex.hits.push_back({r, -3, int(f)});
        x += bw + 10;
    };
    const bool onHeader = ex.cursor >= 0 && ex.cursor < int(ex.rows.size()) && ex.rows[size_t(ex.cursor)] < 0;
    button(Fret::Green, onHeader ? "Open / close" : "Show", true);
    button(Fret::Red, ex.searching || !ex.search.empty() ? "Clear search" : "Back", true);
    button(Fret::Yellow, "Reset view", true);
    button(Fret::Blue, "Next animation", !ex.clips.empty());
    button(Fret::Orange, ex.turntable ? "Stop turning" : "Turntable", true);
    const std::string mouse = "Drag to turn" + kDot + "right-drag to pan" + kDot + "wheel to zoom";
    if (x + measure(FontId::Regular, 15, mouse) + 20 < W_ - kMargin)
        text(FontId::Regular, 15, W_ - kMargin - measure(FontId::Regular, 15, mouse), cy - 10, mouse, kDim);
}
