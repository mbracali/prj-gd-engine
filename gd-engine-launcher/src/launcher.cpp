// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "launcher.h"

#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "module_host.h"
#include "platform.h"

extern const unsigned char kGdulIconPng[];
extern const std::size_t kGdulIconPngSize;

using gfx::Color;
using gfx::FontId;
using gfx::Rect;
using gfx::rgb;
using gfx::withAlpha;

namespace {

constexpr const char* kTitle = "Guitar Dream Universal Launcher";
const std::string kDot = " \xC2\xB7 ";  // " · "

// The palette: the badge's black, cream and oxblood, and the five frets.
const Color kInk = rgb(0x0b0a0d);
const Color kCream = rgb(0xf1e7d3);
const Color kSand = rgb(0xc9b892);
const Color kDim = rgb(0x91877a);
const Color kMuted = rgb(0x5f584f);
const Color kOxblood = rgb(0xa3242b);

// Design layout (1280 x 720 units).
constexpr float kMargin = 40;
constexpr float kListTop = 160;
constexpr float kRowH = 64;
constexpr float kRowGap = 8;
constexpr float kFooterH = 58;

std::string percent(double v) { return std::to_string(int(std::lround(v * 100))) + "%"; }

std::string year(const std::string& date) { return date.size() >= 4 ? date.substr(0, 4) : date; }

}  // namespace

Color Launcher::fretColor(Fret f) {
    switch (f) {
    case Fret::Green: return rgb(0x32c94f);
    case Fret::Red: return rgb(0xe23a3f);
    case Fret::Yellow: return rgb(0xf3c02c);
    case Fret::Blue: return rgb(0x2f8ff0);
    default: return rgb(0xf2832c);
    }
}

float Launcher::text(FontId f, float px, float x, float y, const std::string& t, Color c, float tracking) {
    return gfx_.text(f, px * s_, x * s_, y * s_, t, c, tracking * s_) / s_;
}

float Launcher::measure(FontId f, float px, const std::string& t, float tracking) {
    return gfx_.measure(f, px * s_, t, tracking * s_) / s_;
}

// ---- Life cycle ----

bool Launcher::init() {
    SDL_SetAppMetadata(kTitle, "", "io.github.mbracali.gdul");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
        SDL_Log("gdul: cannot start SDL: %s", SDL_GetError());
        return false;
    }
    SDL_CreateDirectory(platform::saveDir().c_str());
    log_.open(platform::join(platform::saveDir(), "gdul.log"));
    catalog_.load();
    SDL_SetAppMetadata(kTitle, catalog_.launcherVersion().c_str(), "io.github.mbracali.gdul");

    // The same OpenGL setup as the games (3.3 core, 24-bit depth), so their
    // contexts fit this window.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    // Development aid: GDUL_WINDOW=<w>x<h> sets the starting window size.
    int winW = 1280, winH = 720;
    if (const char* ws = SDL_getenv("GDUL_WINDOW")) SDL_sscanf(ws, "%dx%d", &winW, &winH);
    window_ = SDL_CreateWindow(kTitle, winW, winH, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window_) {
        SDL_Log("gdul: cannot open the window: %s", SDL_GetError());
        return false;
    }
    SDL_SetWindowMinimumSize(window_, 960, 540);
    int iw = 0, ih = 0, n = 0;
    if (unsigned char* px = stbi_load_from_memory(kGdulIconPng, int(kGdulIconPngSize), &iw, &ih, &n, 4)) {
        if (SDL_Surface* s = SDL_CreateSurfaceFrom(iw, ih, SDL_PIXELFORMAT_RGBA32, px, iw * 4)) {
            icon_ = SDL_DuplicateSurface(s);
            SDL_DestroySurface(s);
        }
        stbi_image_free(px);
    }
    context_ = SDL_GL_CreateContext(window_);
    std::string error;
    if (!context_ || !SDL_GL_MakeCurrent(window_, context_) || !gfx_.init(error)) {
        SDL_Log("gdul: cannot start OpenGL: %s", context_ ? error.c_str() : SDL_GetError());
        return false;
    }
    restoreWindow();
    // Development aid: GDUL_VIRTUAL_PAD=<vendor>:<product> (hex, e.g. 045e:028e)
    // plugs in a virtual gamepad, to test the controller hand-over without hardware.
    if (const char* vp = SDL_getenv("GDUL_VIRTUAL_PAD")) {
        unsigned vendor = 0, product = 0;
        if (SDL_sscanf(vp, "%x:%x", &vendor, &product) == 2) {
            SDL_VirtualJoystickDesc desc;
            SDL_INIT_INTERFACE(&desc);
            desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
            desc.vendor_id = Uint16(vendor), desc.product_id = Uint16(product);
            desc.naxes = SDL_GAMEPAD_AXIS_COUNT, desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
            desc.name = "gdul virtual gamepad";
            if (!SDL_AttachVirtualJoystick(&desc)) SDL_Log("gdul: virtual pad: %s", SDL_GetError());
        }
    }
    openGamepads();

    log_.add("launcher started (gdul " + catalog_.launcherVersion() + ", " + platform::kOs + "-" + platform::kArch + ", " +
             std::to_string(catalog_.games().size()) + " games, " + std::to_string(catalog_.discsFound()) + " disc images found)");
    // Start on the first game that can be played.
    for (size_t i = 0; i < catalog_.games().size(); ++i)
        if (catalog_.games()[i].playable()) {
            focusRow_ = int(i);
            break;
        }

    // Development aid: GDUL_SCRIPT="<ms>:<command>[=<argument>];..." runs
    // commands at those times (ms since start): up down left right green red
    // yellow blue, drop=<file> (as if dropped on the window), play=<game id>,
    // shot=<file.bmp> (saves the frame), drag=1|0 (the drop overlay),
    // toast=<text>, quit.
    if (const char* sc = SDL_getenv("GDUL_SCRIPT")) {
        std::string all = sc;
        for (size_t start = 0; start < all.size();) {
            size_t end = all.find(';', start);
            if (end == std::string::npos) end = all.size();
            std::string step = all.substr(start, end - start);
            start = end + 1;
            size_t colon = step.find(':');
            if (colon == std::string::npos) continue;
            ScriptStep s;
            s.ms = std::atof(step.substr(0, colon).c_str());
            std::string rest = step.substr(colon + 1);
            size_t eq = rest.find('=');
            s.cmd = rest.substr(0, eq);
            if (eq != std::string::npos) s.arg = rest.substr(eq + 1);
            script_.push_back(s);
        }
    }
    return true;
}

void Launcher::shutdown() {
    closeGamepads();
    gfx_.shutdown();
    if (context_) SDL_GL_DestroyContext(context_);
    if (window_) SDL_DestroyWindow(window_);
    if (icon_) SDL_DestroySurface(icon_);
    SDL_Quit();
}

// After a game: the launcher's title, icon and app id again.
void Launcher::restoreWindow() {
    SDL_SetAppMetadata(kTitle, catalog_.launcherVersion().c_str(), "io.github.mbracali.gdul");
    SDL_SetWindowTitle(window_, kTitle);
    if (icon_) SDL_SetWindowIcon(window_, icon_);
    SDL_GL_SetSwapInterval(1);
}

void Launcher::openGamepads() {
    int n = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&n)) {
        for (int i = 0; i < n; ++i)
            if (!pads_.count(ids[i]))
                if (SDL_Gamepad* p = SDL_OpenGamepad(ids[i])) pads_[ids[i]] = p;
        SDL_free(ids);
    }
}

void Launcher::closeGamepads() {
    for (auto& [id, p] : pads_) SDL_CloseGamepad(p);
    pads_.clear();
    heldDir_ = 0;
}

int Launcher::run() {
    if (!init()) {
        shutdown();
        return 1;
    }
    startNs_ = SDL_GetTicksNS();
    while (running_) {
        const Uint64 frameStart = SDL_GetTicksNS();
        now_ = double(frameStart - startNs_) / 1e9;
        SDL_Event e;
        while (SDL_PollEvent(&e)) handle(e);
        frame();
        // Without vsync (e.g. SDL_VIDEO_DRIVER=offscreen) about 60 frames a second.
        const Uint64 spent = SDL_GetTicksNS() - frameStart;
        if (spent < 16000000) SDL_DelayNS(16000000 - spent);
    }
    log_.add("launcher closed");
    shutdown();
    return 0;
}

void Launcher::frame() {
    // Answers of the system file dialog.
    std::vector<std::pair<std::string, std::string>> picked;
    {
        std::lock_guard<std::mutex> g(pickedLock_);
        picked.swap(picked_);
    }
    for (auto& [id, path] : picked) {
        if (path.empty()) log_.add("load disc for " + id + ": cancelled");
        else handleDisc(path, id);
    }
    // Gamepad direction repeat.
    if (heldDir_ && now_ >= repeatAt_) {
        repeatAt_ = now_ + 0.09;
        move(heldDir_ == 1 ? -1 : heldDir_ == 2 ? 1 : 0, heldDir_ == 3 ? -1 : heldDir_ == 4 ? 1 : 0);
    }
    runScript();
    if (!running_) return;
    draw();
    if (!pendingShot_.empty()) {
        screenshot(pendingShot_);
        pendingShot_.clear();
    }
    SDL_GL_SwapWindow(window_);
}

void Launcher::runScript() {
    if (scriptPos_ >= script_.size() || now_ * 1000 < script_[scriptPos_].ms) return;
    const ScriptStep st = script_[scriptPos_++];
    SDL_Log("gdul script: %s %s", st.cmd.c_str(), st.arg.c_str());
    if (st.cmd == "up") move(-1, 0);
    else if (st.cmd == "down") move(1, 0);
    else if (st.cmd == "left") move(0, -1);
    else if (st.cmd == "right") move(0, 1);
    else if (st.cmd == "green") press(Fret::Green);
    else if (st.cmd == "red") press(Fret::Red);
    else if (st.cmd == "yellow") press(Fret::Yellow);
    else if (st.cmd == "blue") press(Fret::Blue);
    else if (st.cmd == "shot") pendingShot_ = st.arg;
    else if (st.cmd == "drag") dragging_ = st.arg != "0";
    else if (st.cmd == "toast") toast(st.arg);
    else if (st.cmd == "quit") running_ = false;
    else if (st.cmd == "drop") {
        static std::string dropped;  // SDL_EVENT_DROP_FILE's data must outlive the event queue
        dropped = st.arg;
        SDL_Event e{};
        e.type = SDL_EVENT_DROP_FILE;
        e.drop.windowID = SDL_GetWindowID(window_);
        e.drop.data = dropped.c_str();
        SDL_PushEvent(&e);
    } else if (st.cmd == "play") {
        auto& games = catalog_.games();
        for (size_t i = 0; i < games.size(); ++i)
            if (games[i].id == st.arg) focusRow_ = int(i), focusCol_ = kPlay, activate(int(i), kPlay);
    }
}

void Launcher::screenshot(const std::string& path) {
    std::vector<uint8_t> px = gfx_.readPixels(pw_, ph_);
    if (SDL_Surface* s = SDL_CreateSurfaceFrom(pw_, ph_, SDL_PIXELFORMAT_RGBA32, px.data(), pw_ * 4)) {
        if (!SDL_SaveBMP(s, path.c_str())) SDL_Log("gdul: screenshot %s: %s", path.c_str(), SDL_GetError());
        SDL_DestroySurface(s);
    }
}

// ---- Input ----

void Launcher::handle(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_QUIT:
        running_ = false;
        break;
    case SDL_EVENT_DROP_BEGIN:
        dragging_ = true;
        break;
    case SDL_EVENT_DROP_COMPLETE:
        dragging_ = false;
        break;
    case SDL_EVENT_DROP_FILE:
        dragging_ = false;
        if (e.drop.data) {
            log_.add(std::string("disc dropped: ") + e.drop.data);
            handleDisc(e.drop.data, "");
        }
        break;
    case SDL_EVENT_KEY_DOWN:
        switch (e.key.key) {
        case SDLK_UP: case SDLK_W: move(-1, 0); break;
        case SDLK_DOWN: case SDLK_S: move(1, 0); break;
        case SDLK_LEFT: case SDLK_A: move(0, -1); break;
        case SDLK_RIGHT: case SDLK_D: move(0, 1); break;
        case SDLK_PAGEUP: move(-6, 0); break;
        case SDLK_PAGEDOWN: move(6, 0); break;
        case SDLK_HOME: move(-1000, 0); break;
        case SDLK_END: move(1000, 0); break;
        case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE:
            if (!e.key.repeat) press(Fret::Green);
            break;
        case SDLK_ESCAPE: case SDLK_BACKSPACE:
            if (!e.key.repeat) press(Fret::Red);
            break;
        case SDLK_L: if (!e.key.repeat) press(Fret::Yellow); break;
        case SDLK_E: if (!e.key.repeat) press(Fret::Blue); break;
        case SDLK_P: if (!e.key.repeat && !dialog_.open) activate(focusRow_, kPlay); break;
        default: break;
        }
        break;
    case SDL_EVENT_GAMEPAD_ADDED:
        if (!pads_.count(e.gdevice.which))
            if (SDL_Gamepad* p = SDL_OpenGamepad(e.gdevice.which)) pads_[e.gdevice.which] = p;
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        if (auto it = pads_.find(e.gdevice.which); it != pads_.end()) {
            SDL_CloseGamepad(it->second);
            pads_.erase(it);
        }
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
        // Guitars as gamepads: the frets are A B Y X LB, the strum bar the D-pad.
        int dir = 0;
        switch (e.gbutton.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP: dir = 1; break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: dir = 2; break;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: dir = 3; break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: dir = 4; break;
        case SDL_GAMEPAD_BUTTON_SOUTH: case SDL_GAMEPAD_BUTTON_START: press(Fret::Green); break;
        case SDL_GAMEPAD_BUTTON_EAST: case SDL_GAMEPAD_BUTTON_BACK: press(Fret::Red); break;
        case SDL_GAMEPAD_BUTTON_NORTH: press(Fret::Yellow); break;
        case SDL_GAMEPAD_BUTTON_WEST: press(Fret::Blue); break;
        default: break;
        }
        if (dir) {
            heldDir_ = dir, repeatAt_ = now_ + 0.4;
            move(dir == 1 ? -1 : dir == 2 ? 1 : 0, dir == 3 ? -1 : dir == 4 ? 1 : 0);
        }
        break;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        if (e.gbutton.button >= SDL_GAMEPAD_BUTTON_DPAD_UP && e.gbutton.button <= SDL_GAMEPAD_BUTTON_DPAD_RIGHT) heldDir_ = 0;
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        // The left stick like the D-pad.
        if (e.gaxis.axis != SDL_GAMEPAD_AXIS_LEFTX && e.gaxis.axis != SDL_GAMEPAD_AXIS_LEFTY) break;
        const bool y = e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY;
        const int v = e.gaxis.value;
        int dir = v < -20000 ? (y ? 1 : 3) : v > 20000 ? (y ? 2 : 4) : 0;
        const bool mine = y ? (heldDir_ == 1 || heldDir_ == 2) : (heldDir_ == 3 || heldDir_ == 4);
        if (dir && dir != heldDir_) {
            heldDir_ = dir, repeatAt_ = now_ + 0.4;
            move(dir == 1 ? -1 : dir == 2 ? 1 : 0, dir == 3 ? -1 : dir == 4 ? 1 : 0);
        } else if (!dir && mine && std::abs(v) < 12000) {
            heldDir_ = 0;
        }
        break;
    }
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        const float density = SDL_GetWindowPixelDensity(window_);
        const float mx = (e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.x : e.button.x) * density;
        const float my = (e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.y : e.button.y) * density;
        const bool click = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT;
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT) {
            press(Fret::Red);
            break;
        }
        for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
            if (!it->r.contains(mx, my)) continue;
            mouseUsed_ = true;
            if (it->row < 0) {
                dialog_.focus = it->col;
                if (click) press(Fret::Green);
            } else if (!dialog_.open) {
                focusRow_ = it->row;
                if (it->col >= 0) focusCol_ = it->col;
                if (click && it->col >= 0) activate(it->row, it->col);
            }
            break;
        }
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        if (!dialog_.open) {
            const float content = float(catalog_.games().size()) * (kRowH + kRowGap);
            const float view = H_ - kFooterH - 12 - kListTop;
            scrollTarget_ = std::clamp(scrollTarget_ - e.wheel.y * 72, 0.f, std::max(0.f, content - view));
        }
        break;
    default:
        break;
    }
}

bool Launcher::enabled(int row, int col) const {
    if (row < 0 || row >= int(catalog_.games().size())) return false;
    const Game& g = catalog_.games()[size_t(row)];
    switch (col) {
    case kPlay: return g.playable();
    case kLoad: return true;
    default: return !g.iso.empty();  // Explore models: the disc's models (coming soon)
    }
}

void Launcher::move(int dRow, int dCol) {
    if (dialog_.open) {
        const int n = int(dialog_.buttons.size());
        if (n && dCol) dialog_.focus = std::clamp(dialog_.focus + dCol, 0, n - 1);
        return;
    }
    mouseUsed_ = false;
    const int rows = int(catalog_.games().size());
    if (rows == 0) return;
    focusRow_ = std::clamp(focusRow_ + dRow, 0, rows - 1);
    if (dCol) focusCol_ = std::clamp(focusCol_ + dCol, 0, kColumns - 1);
}

void Launcher::press(Fret f) {
    if (dialog_.open) {
        if (f == Fret::Green) {
            Button b = dialog_.buttons.empty() ? Button{} : dialog_.buttons[size_t(dialog_.focus)];
            dialog_.open = false;
            if (b.action) b.action();
        } else if (f == Fret::Red) {
            // Red: the dialog's red button if it has one, else close it.
            dialog_.open = false;
            for (const Button& b : dialog_.buttons)
                if (b.fret == Fret::Red && b.action) {
                    Button copy = b;
                    copy.action();
                    break;
                }
        } else {
            for (const Button& b : dialog_.buttons)
                if (b.fret == f) {
                    Button copy = b;
                    dialog_.open = false;
                    if (copy.action) copy.action();
                    break;
                }
        }
        return;
    }
    switch (f) {
    case Fret::Green: activate(focusRow_, focusCol_); break;
    case Fret::Red: askQuit(); break;
    case Fret::Yellow: activate(focusRow_, kLoad); break;
    case Fret::Blue: activate(focusRow_, kExplore); break;
    default: break;
    }
}

// ---- Actions ----

void Launcher::activate(int row, int col) {
    if (row < 0 || row >= int(catalog_.games().size())) return;
    Game& g = catalog_.games()[size_t(row)];
    focusRow_ = row;
    switch (col) {
    case kPlay:
        if (g.playable()) play(g);
        else if (!g.ready) toast(g.title() + " is not playable yet. Coming soon!");
        else if (!g.hasModule) toast("This launcher build has no " + g.id + " module.");
        else toast("Load a " + g.title() + " disc image first (yellow).");
        break;
    case kLoad: pickDisc(g); break;
    case kExplore:
        if (g.iso.empty()) toast("Load a " + g.title() + " disc image to explore its models.");
        else exploreModels(g);
        break;
    default: break;
    }
}

void Launcher::play(Game& g) {
    log_.add("play " + g.id + " (" + g.title() + " " + g.version + ") with " + g.iso);
    drawLoading(g);
    closeGamepads();
    GameRun r = runGameModule(window_, context_, g, catalog_.launcherVersion(), log_);
    restoreWindow();
    openGamepads();
    dragging_ = false;
    if (!r.started) {
        log_.add("play " + g.id + " failed: " + r.error);
        showDialog("We can't rock this! Sorry...", g.title() + " did not start: " + r.error, {{"OK", Fret::Green, nullptr}});
        return;
    }
    char secs[32];
    SDL_snprintf(secs, sizeof secs, "%.1f", r.seconds);
    log_.add("back from " + g.id + ": exit code " + std::to_string(r.exitCode) + " after " + secs + " s" +
             (r.unloaded ? " (module unloaded)" : " (module stayed loaded)"));
    toast(r.exitCode == 0 ? "Welcome back from " + g.title() + "!" : g.title() + " ended with code " + std::to_string(r.exitCode) + ".");
}

namespace {
struct PickRequest {
    std::mutex* lock;
    std::vector<std::pair<std::string, std::string>>* out;
    std::string id;
};
void SDLCALL onPicked(void* user, const char* const* files, int) {
    auto* req = static_cast<PickRequest*>(user);
    std::string path = files && files[0] ? files[0] : "";
    {
        std::lock_guard<std::mutex> g(*req->lock);
        req->out->emplace_back(req->id, path);
    }
    delete req;
}
}  // namespace

void Launcher::pickDisc(Game& g) {
    log_.add("load disc for " + g.id + ": file dialog opened");
    static const SDL_DialogFileFilter kFilters[] = {{"Disc images (*.iso)", "iso;ISO"}, {"All files", "*"}};
    std::string start;
    if (!g.iso.empty()) start = platform::parentDir(g.iso);
    for (const std::string& d : catalog_.discDirs())
        if (start.empty() && platform::isDir(d)) start = d;
    auto* req = new PickRequest{&pickedLock_, &picked_, g.id};
    SDL_ShowOpenFileDialog(&onPicked, req, window_, kFilters, 2, start.empty() ? nullptr : start.c_str(), false);
}

void Launcher::handleDisc(const std::string& path, const std::string& wantedId) {
    const DiscInfo disc = probeDisc(path);
    Game* g = catalog_.identify(disc);
    const std::string file = platform::fileName(path);
    log_.add("identify " + path + ": " + disc.describe() + " -> " + (g ? g->id : std::string("unknown")));
    Game* wanted = wantedId.empty() ? nullptr : catalog_.find(wantedId);
    if (!g) {
        std::vector<Button> buttons;
        if (wanted && !disc.format.empty()) {
            std::string id = wanted->id;
            buttons.push_back({"Use it anyway", Fret::Yellow, [this, id, path] {
                                   catalog_.saveIso(id, path);
                                   log_.add("disc saved for " + id + " (unrecognised): " + path);
                                   toast("Disc saved for " + id + ".");
                               }});
        }
        buttons.push_back({"OK", Fret::Green, nullptr});
        showDialog("We can't rock this! Sorry...",
                   file + " (" + disc.describe() + ") is not a disc of any game the launcher knows.", buttons);
        return;
    }
    for (size_t i = 0; i < catalog_.games().size(); ++i)
        if (&catalog_.games()[i] == g) focusRow_ = int(i);
    const std::string id = g->id;
    auto save = [this, id, path] {
        catalog_.saveIso(id, path);
        log_.add("disc saved for " + id + ": " + path);
    };
    const bool canPlay = g->ready && g->hasModule;
    std::string title = g->title() + " detected!";
    std::string body = file + " is " + g->name + " (" + disc.describe() + ").";
    if (wanted && wanted != g) {
        title = "That's " + g->title() + "!";
        body += " It is not a " + wanted->title() + " disc.";
    }
    if (g->iso == path) {
        body += " It is already the disc of " + g->id + ".";
        std::vector<Button> buttons;
        if (g->playable()) buttons.push_back({"Play", Fret::Green, [this, id] {
                                                  if (Game* gg = catalog_.find(id)) play(*gg);
                                              }});
        buttons.push_back({"Close", Fret::Red, nullptr});
        showDialog(title, body, buttons);
        return;
    }
    std::vector<Button> buttons;
    if (canPlay) {
        body += " Save it as the disc of " + id + " and play?";
        buttons.push_back({"Save & play", Fret::Green, [this, save, id] {
                               save();
                               if (Game* gg = catalog_.find(id); gg && gg->playable()) play(*gg);
                           }});
        buttons.push_back({"Save only", Fret::Yellow, [this, save, id] {
                               save();
                               toast("Disc saved for " + id + ".");
                           }});
    } else {
        body += " Save it as the disc of " + id + "?";
        body += g->ready ? " (This launcher build has no " + id + " module.)" : " (It is not playable yet; the launcher will remember it.)";
        buttons.push_back({"Save", Fret::Green, [this, save, id] {
                               save();
                               toast("Disc saved for " + id + ".");
                           }});
    }
    buttons.push_back({"Cancel", Fret::Red, [this, id] { log_.add("disc not saved for " + id); }});
    showDialog(title, body, buttons);
}

void Launcher::exploreModels(Game& g) {
    log_.add("explore models of " + g.id + ": coming soon");
    showDialog("Explore models", "Coming soon: browse " + g.title() +
                                     "'s 3D models straight from your disc image, then rotate and zoom them.",
               {{"OK", Fret::Green, nullptr}});
}

void Launcher::askQuit() {
    showDialog("Leaving so soon?", "Close the Guitar Dream Universal Launcher?",
               {{"Quit", Fret::Green, [this] { running_ = false; }}, {"Keep rocking", Fret::Red, nullptr}});
    dialog_.focus = 1;
}

void Launcher::showDialog(const std::string& title, const std::string& body, std::vector<Button> buttons) {
    dialog_.title = title;
    dialog_.body = body;
    dialog_.buttons = std::move(buttons);
    dialog_.focus = 0;
    dialog_.open = true;
}

void Launcher::toast(const std::string& t) {
    toast_ = t;
    toastAt_ = double(SDL_GetTicksNS() - startNs_) / 1e9;  // now: a game may have run since the frame began
}

// ---- Drawing ----

void Launcher::draw() {
    SDL_GetWindowSizeInPixels(window_, &pw_, &ph_);
    s_ = std::max(0.25f, std::min(float(pw_) / 1280.f, float(ph_) / 720.f));
    W_ = float(pw_) / s_, H_ = float(ph_) / s_;
    hits_.clear();
    gfx_.begin(pw_, ph_);
    drawBackground();
    drawList();
    drawHeader();
    drawFooter();
    drawToast();
    if (dialog_.open) drawDialog();
    if (dragging_) drawDropOverlay();
    gfx_.end();
}

void Launcher::drawBackground() {
    gfx_.rect(R(0, 0, W_, H_), rgb(0x141116), rgb(0x070608));
    // The badge, large and faint, on the right.
    const float size = H_ * 1.18f;
    gfx_.image(gfx_.backdrop(), R(W_ - size * 0.62f, (H_ - size) * 0.5f + 20, size, size), {1, 1, 1, 0.075f});
    // An oxblood glow behind the header badge, as in the logo.
    gfx_.glow(R(60, 40, 60, 60), withAlpha(kOxblood, 0.35f), 30 * s_, 90 * s_);
}

void Launcher::drawHeader() {
    const std::vector<Game>& games = catalog_.games();
    gfx_.image(gfx_.icon(), R(kMargin - 6, 16, 104, 104));
    text(FontId::Title, 60, 150, 20, "Guitar Dream", kCream);
    const float subW = text(FontId::SemiBold, 17, 153, 90, "UNIVERSAL LAUNCHER", kSand, 4.5f);
    const std::string ver = "v" + catalog_.launcherVersion();
    const float vw = measure(FontId::SemiBold, 14, ver) + 16;
    gfx_.rect(R(153 + subW + 12, 89, vw, 22), withAlpha(kOxblood, 0.85f), 11 * s_);
    text(FontId::SemiBold, 14, 153 + subW + 20, 91, ver, kCream);

    int highlighted = 0;
    for (const Game& g : games) highlighted += g.playable() ? 1 : 0;
    const std::string l1 = std::to_string(highlighted) + " of " + std::to_string(games.size()) + " games ready to rock";
    const int discs = catalog_.discsFound();
    const std::string l2 = std::to_string(discs) + (discs == 1 ? " disc image" : " disc images") + " found in original_isos";
    text(FontId::SemiBold, 20, W_ - kMargin - measure(FontId::SemiBold, 20, l1), 40, l1, kCream);
    text(FontId::Regular, 16, W_ - kMargin - measure(FontId::Regular, 16, l2), 68, l2, kDim);

    // Column titles and the rule under the header.
    const float right = W_ - kMargin - 16;
    const float buttonsW = 104 + 10 + 120 + 10 + 160;
    text(FontId::SemiBold, 13, 66, 136, "GAME", kMuted, 2.5f);
    text(FontId::SemiBold, 13, right - buttonsW - 24 - 132, 136, "COMPLETE", kMuted, 2.5f);
    gfx_.rect(R(kMargin, 126, W_ - 2 * kMargin, 1), withAlpha(kCream, 0.12f));
    gfx_.rect(R(kMargin, 125, 140, 3), kOxblood, 1.5f * s_);
}

void Launcher::drawList() {
    const std::vector<Game>& games = catalog_.games();
    const float top = kListTop, bottom = H_ - kFooterH - 12;
    const float view = bottom - top;
    const float content = float(games.size()) * (kRowH + kRowGap);
    // Keep the focused row in view (not while the mouse scrolls).
    if (!mouseUsed_) {
        const float fy = float(focusRow_) * (kRowH + kRowGap);
        if (fy < scrollTarget_) scrollTarget_ = fy;
        if (fy + kRowH > scrollTarget_ + view) scrollTarget_ = fy + kRowH - view;
    }
    scrollTarget_ = std::clamp(scrollTarget_, 0.f, std::max(0.f, content - view));
    scroll_ += (scrollTarget_ - scroll_) * 0.25f;
    if (std::abs(scrollTarget_ - scroll_) < 0.5f) scroll_ = scrollTarget_;

    const Rect clip = R(0, top - 10, W_, view + 20);
    gfx_.clip(&clip);
    for (size_t i = 0; i < games.size(); ++i) {
        const float y = top + float(i) * (kRowH + kRowGap) - scroll_;
        if (y + kRowH < top - 20 || y > bottom + 20) continue;
        drawRow(int(i), y);
    }
    gfx_.clip(nullptr);
    // Hits outside the list's view do not count.
    hits_.erase(std::remove_if(hits_.begin(), hits_.end(),
                               [&](const Hit& h) { return h.r.y + h.r.h < top * s_ || h.r.y > bottom * s_; }),
                hits_.end());
    // Scroll hints: soft fades where rows continue.
    if (scroll_ > 1) gfx_.rect(R(0, top - 10, W_, 22), rgb(0x100d12, 0.9f), rgb(0x100d12, 0));
    if (scroll_ < content - view - 1) gfx_.rect(R(0, bottom - 12, W_, 22), rgb(0x0a090b, 0), rgb(0x0a090b, 0.9f));
}

void Launcher::drawRow(int index, float y) {
    const Game& g = catalog_.games()[size_t(index)];
    const bool focused = index == focusRow_ && !dialog_.open;
    const bool lit = g.highlighted();
    const Rect row = R(kMargin, y, W_ - 2 * kMargin, kRowH);
    hits_.push_back({row, index, -1});

    // The card.
    if (focused) gfx_.glow(row, withAlpha(lit ? fretColor(Fret::Green) : kSand, 0.22f), 12 * s_, 22 * s_);
    if (lit) gfx_.rect(row, rgb(0x1e2a21, 0.96f), rgb(0x141b16, 0.96f), 12 * s_);
    else if (g.ready) gfx_.rect(row, rgb(0x1d1a20, 0.94f), rgb(0x151318, 0.94f), 12 * s_);
    else gfx_.rect(row, rgb(0x141216, 0.9f), rgb(0x100e12, 0.9f), 12 * s_);
    gfx_.ring(row, focused ? withAlpha(kCream, 0.75f) : withAlpha(kCream, lit ? 0.14f : 0.06f), 12 * s_, (focused ? 2.f : 1.f) * s_);
    // The status light: green ready to rock, yellow needs a disc, grey coming soon.
    const Color led = lit && g.hasModule ? fretColor(Fret::Green) : g.ready ? fretColor(Fret::Yellow) : kMuted;
    if (g.ready) gfx_.glow(R(kMargin + 16, y + 26, 12, 12), withAlpha(led, 0.5f), 6 * s_, 10 * s_);
    gfx_.circle((kMargin + 22) * s_, (y + 32) * s_, 5.5f * s_, led);

    // Name and details.
    const float x = kMargin + 40;
    const Color nameColor = lit ? kCream : g.ready ? rgb(0xd6cab3) : rgb(0x8a8174);
    const float idW = text(FontId::SemiBold, 17, x, y + 11, g.id + "/", lit ? kSand : kDim);
    text(FontId::SemiBold, 25, x + idW, y + 4, g.title(), nameColor);
    std::string details = g.basedOn.empty() ? g.platforms : g.basedOn;
    details += kDot + year(g.launch) + kDot + "v" + g.version;
    if (!g.iso.empty())
        details += kDot + platform::fileName(g.iso) + (g.isoSource == Game::IsoSource::Saved ? " (saved)" : " (found)");
    else if (g.ready) details += kDot + "no disc yet: load one or drop it here";
    else details += kDot + "coming soon";
    if (g.ready && !g.hasModule) details += kDot + "module not built";

    const float right = W_ - kMargin - 16;
    const float buttonsW = 104 + 10 + 120 + 10 + 160;
    const float compX = right - buttonsW - 24 - 132;
    const std::string shown = gfx_.elide(FontId::Regular, 15 * s_, details, (compX - x - 20) * s_);
    text(FontId::Regular, 15, x, y + 37, shown, lit ? rgb(0xa9b8a6) : kDim);

    // Completeness: the percentage over a bar.
    const std::string pc = percent(g.completeness);
    text(FontId::SemiBold, 20, compX + 120 - measure(FontId::SemiBold, 20, pc), y + 10, pc, g.ready ? kCream : kDim);
    gfx_.rect(R(compX, y + 40, 120, 6), withAlpha(kCream, 0.08f), 3 * s_);
    const Color barA = g.completeness >= 0.9 ? fretColor(Fret::Green) : g.completeness >= 0.5 ? fretColor(Fret::Yellow) : fretColor(Fret::Orange);
    if (g.completeness > 0) gfx_.rect(R(compX, y + 40, float(120 * g.completeness), 6), barA, withAlpha(barA, 0.8f), 3 * s_);

    // The buttons: Play, Load ISO, Explore models.
    float bx = right - buttonsW;
    const float by = y + (kRowH - 38) / 2;
    drawButton(R(bx, by, 104, 38), "Play", Fret::Green, focused && focusCol_ == kPlay, enabled(index, kPlay), index, kPlay);
    bx += 114;
    drawButton(R(bx, by, 120, 38), g.iso.empty() ? "Load ISO" : "Change ISO", Fret::Yellow, focused && focusCol_ == kLoad,
               enabled(index, kLoad), index, kLoad);
    bx += 130;
    drawButton(R(bx, by, 160, 38), "Explore models", Fret::Blue, focused && focusCol_ == kExplore, enabled(index, kExplore),
               index, kExplore);
}

void Launcher::drawButton(const Rect& r, const std::string& label, Fret fret, bool focused, bool on, int row, int col) {
    const Color fc = fretColor(fret);
    const float rad = 9 * s_;
    if (focused && on) {
        gfx_.glow(r, withAlpha(fc, 0.45f), rad, 14 * s_);
        gfx_.rect(r, gfx::mix(fc, kCream, 0.18f), gfx::mix(fc, kInk, 0.18f), rad);
    } else if (focused) {
        gfx_.rect(r, rgb(0x2a262d), rgb(0x211e24), rad);
        gfx_.ring(r, withAlpha(kCream, 0.5f), rad, 1.5f * s_);
    } else {
        gfx_.rect(r, on ? rgb(0x26222a, 0.95f) : rgb(0x1a171d, 0.8f), on ? rgb(0x1c191f, 0.95f) : rgb(0x161418, 0.8f), rad);
        gfx_.ring(r, on ? withAlpha(fc, 0.55f) : withAlpha(kCream, 0.07f), rad, 1.2f * s_);
    }
    // The fret gem, then the label.
    const float px = 16 * s_;
    const float tw = gfx_.measure(FontId::SemiBold, px, label);
    const float gem = 6 * s_;
    const float total = gem * 2 + 8 * s_ + tw;
    const float x0 = r.x + (r.w - total) / 2;
    const float cy = r.y + r.h / 2;
    const Color gemColor = !on ? withAlpha(kMuted, 0.6f) : focused ? kInk : fc;
    gfx_.circle(x0 + gem, cy, gem, gemColor);
    if (on && !focused) gfx_.circle(x0 + gem - gem * 0.3f, cy - gem * 0.3f, gem * 0.35f, withAlpha(kCream, 0.55f));
    const Color tc = !on ? rgb(0x5d564e) : focused ? rgb(0x101010) : kCream;
    gfx_.text(FontId::SemiBold, px, x0 + gem * 2 + 8 * s_, cy - gfx_.lineHeight(FontId::SemiBold, px) / 2, label, tc);
    hits_.push_back({r, row, col});
}

void Launcher::drawFooter() {
    const float y = H_ - kFooterH;
    gfx_.rect(R(0, y, W_, kFooterH), rgb(0x0d0b0f, 0.92f), rgb(0x080709, 0.98f));
    gfx_.rect(R(0, y, W_, 1), withAlpha(kCream, 0.1f));
    float x = kMargin;
    const float cy = y + kFooterH / 2;
    auto hint = [&](Fret f, const std::string& label) {
        gfx_.circle((x + 8) * s_, cy * s_, 8 * s_, fretColor(f));
        gfx_.circle((x + 6) * s_, (cy - 2.5f) * s_, 2.8f * s_, withAlpha(kCream, 0.6f));
        x += 22;
        x += text(FontId::SemiBold, 16, x, cy - 10, label, kSand) + 24;
    };
    hint(Fret::Green, "Select");
    hint(Fret::Red, "Back");
    hint(Fret::Yellow, "Load ISO");
    hint(Fret::Blue, "Explore models");
    const std::string drop = "Drop a disc image anywhere to identify it";
    text(FontId::Regular, 15, W_ - kMargin - measure(FontId::Regular, 15, drop), cy - 10, drop, kDim);
}

void Launcher::drawToast() {
    const double age = now_ - toastAt_;
    if (toast_.empty() || age > 4.0) return;
    const float a = float(std::min(1.0, std::min(age / 0.15, (4.0 - age) / 0.4)));
    const float tw = measure(FontId::SemiBold, 18, toast_) + 48;
    const Rect r = R((W_ - tw) / 2, H_ - kFooterH - 64, tw, 44);
    gfx_.glow(r, withAlpha(kInk, 0.8f * a), 22 * s_, 24 * s_);
    gfx_.rect(r, rgb(0x2b2530, 0.97f * a), rgb(0x1f1b23, 0.97f * a), 22 * s_);
    gfx_.ring(r, withAlpha(kSand, 0.5f * a), 22 * s_, 1.2f * s_);
    text(FontId::SemiBold, 18, (W_ - tw) / 2 + 24, H_ - kFooterH - 64 + 10, toast_, withAlpha(kCream, a));
}

void Launcher::drawDialog() {
    gfx_.rect(R(0, 0, W_, H_), rgb(0x050406, 0.72f));
    const float w = 660;
    const std::vector<std::string> lines = gfx_.wrap(FontId::Regular, 19 * s_, dialog_.body, (w - 64) * s_);
    const float h = 150 + float(lines.size()) * 26 + 60;
    const float x = (W_ - w) / 2, y = (H_ - h) / 2;
    const Rect panel = R(x, y, w, h);
    gfx_.glow(panel, rgb(0x000000, 0.7f), 18 * s_, 40 * s_);
    gfx_.rect(panel, rgb(0x221d25), rgb(0x141116), 18 * s_);
    gfx_.ring(panel, withAlpha(kSand, 0.35f), 18 * s_, 1.5f * s_);
    gfx_.rect(R(x + 32, y + 84, 120, 3), kOxblood, 1.5f * s_);
    gfx_.image(gfx_.icon(), R(x + w - 96, y + 18, 70, 70), {1, 1, 1, 0.9f});
    const std::string title = gfx_.elide(FontId::Title, 40 * s_, dialog_.title, (w - 140) * s_);
    text(FontId::Title, 40, x + 32, y + 24, title, kCream);
    float ly = y + 104;
    for (const std::string& l : lines) {
        text(FontId::Regular, 19, x + 32, ly, l, rgb(0xd9cfbd));
        ly += 26;
    }
    // Buttons, right-aligned.
    float bx = x + w - 32;
    const float by = y + h - 32 - 42;
    for (int i = int(dialog_.buttons.size()) - 1; i >= 0; --i) {
        const Button& b = dialog_.buttons[size_t(i)];
        const float bw = std::max(120.f, measure(FontId::SemiBold, 16, b.label) + 60);
        bx -= bw;
        drawButton(R(bx, by, bw, 42), b.label, b.fret, i == dialog_.focus, true, -1, i);
        bx -= 12;
    }
}

void Launcher::drawDropOverlay() {
    gfx_.rect(R(0, 0, W_, H_), rgb(0x050406, 0.6f));
    const Rect r = R(24, 24, W_ - 48, H_ - 48);
    const float pulse = 0.55f + 0.25f * float(std::sin(now_ * 5));
    gfx_.glow(r, withAlpha(fretColor(Fret::Green), 0.25f * pulse), 24 * s_, 30 * s_);
    gfx_.ring(r, withAlpha(fretColor(Fret::Green), pulse), 24 * s_, 3 * s_);
    const std::string t = "Drop your disc to rock!";
    text(FontId::Title, 56, (W_ - measure(FontId::Title, 56, t)) / 2, H_ / 2 - 50, t, kCream);
    const std::string sub = "The launcher reads the disc and finds its game.";
    text(FontId::Regular, 19, (W_ - measure(FontId::Regular, 19, sub)) / 2, H_ / 2 + 22, sub, kSand);
}

void Launcher::drawLoading(const Game& g) {
    // Two frames (both buffers) of the loading card before the game takes the window.
    for (int i = 0; i < 2; ++i) {
        SDL_GetWindowSizeInPixels(window_, &pw_, &ph_);
        s_ = std::max(0.25f, std::min(float(pw_) / 1280.f, float(ph_) / 720.f));
        W_ = float(pw_) / s_, H_ = float(ph_) / s_;
        gfx_.begin(pw_, ph_);
        drawBackground();
        const float size = 220;
        gfx_.glow(R((W_ - size) / 2, H_ / 2 - 200, size, size), withAlpha(kOxblood, 0.4f), size / 2 * s_, 60 * s_);
        gfx_.image(gfx_.icon(), R((W_ - size) / 2, H_ / 2 - 200, size, size));
        const std::string t = "Loading " + g.title() + "\xE2\x80\xA6";
        text(FontId::Title, 52, (W_ - measure(FontId::Title, 52, t)) / 2, H_ / 2 + 40, t, kCream);
        const std::string sub = g.id + kDot + platform::fileName(g.iso);
        text(FontId::Regular, 18, (W_ - measure(FontId::Regular, 18, sub)) / 2, H_ / 2 + 112, sub, kSand);
        gfx_.end();
        SDL_GL_SwapWindow(window_);
    }
}
