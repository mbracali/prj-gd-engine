// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "launcher.h"

#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "module_host.h"
#include "palette.h"
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
constexpr const char* kWebsite = "https://guitardreamengine.com";
// Hits that are not a row's buttons or a dialog's.
constexpr int kHitWebsite = -10, kHitDiscs = -11, kHitUpdates = -12, kHitFullscreen = -13, kHitDreamForge = -14;
const std::string kDot = " \xC2\xB7 ";  // " · "

using namespace palette;

// Design layout (1280 x 720 units).
constexpr float kMargin = 40;
constexpr float kListTop = 184;
constexpr float kRowH = 68;
constexpr float kRowGap = 8;
constexpr float kFooterH = 58;

// Icon accent colours (owner 11-22 §2).
const gfx::Color kWhiteIcon = gfx::rgb(0xf4f0e6);
const gfx::Color kNeonYellowC = gfx::rgb(0xe9e21f);
const gfx::Color kNeonGreenC = gfx::rgb(0x45e66b);
const gfx::Color kGoldC = gfx::rgb(0xffcf4d);
const gfx::Color kNeonRedC = gfx::rgb(0xff4b57);
const gfx::Color kNeonPurpleC = gfx::rgb(0xb463ff);

struct PctIcon {
    uint32_t cp;
    gfx::Color c;
};
PctIcon pctIcon(double v) {  // the playability % icon and its colour (owner 11-22 §2)
    const int p = int(std::lround(v * 100));
    if (p <= 0) return {gfx::icon::kCircle, kWhiteIcon};
    if (p <= 10) return {gfx::icon::kClock10, kNeonYellowC};
    if (p <= 20) return {gfx::icon::kClock20, kNeonYellowC};
    if (p <= 40) return {gfx::icon::kClock40, kNeonYellowC};
    if (p <= 60) return {gfx::icon::kClock60, kNeonGreenC};
    if (p <= 80) return {gfx::icon::kClock80, kNeonGreenC};
    if (p < 95) return {gfx::icon::kClock90, kNeonGreenC};
    if (p < 100) return {gfx::icon::kStrokeFull, kNeonGreenC};  // 95%+: a full stroke in the same green (owner 11-23)
    return {gfx::icon::kStars, kGoldC};
}

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
    // Steam Deck Gaming Mode (and Steam Input on desktops): Steam hides the
    // real controller behind its virtual one; SDL ignores that one unless told
    // (read from the environment, as gd01 and gd02 set it).
    SDL_setenv_unsafe("SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD", "1", 0);
    // The Deck's own controls when Steam does not own them (Desktop Mode
    // without Steam, a terminal): SDL's Steam Deck HIDAPI driver.
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_STEAMDECK, "1");
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
    // Android: OpenGL ES 3.0, as gd02 asks for there (gfx.cpp's shaders follow).
#if defined(__ANDROID__) || defined(GDUL_GLES)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    // Development aid: GDUL_WINDOW=<w>x<h> sets the starting window size.
    int winW = 1280, winH = 720;
    if (const char* ws = SDL_getenv("GDUL_WINDOW")) SDL_sscanf(ws, "%dx%d", &winW, &winH);
    window_ = makeWindow(GD_WINDOW_API_OPENGL, winW, winH, int(SDL_WINDOWPOS_UNDEFINED), int(SDL_WINDOWPOS_UNDEFINED), 0);
    if (!window_) {
        SDL_Log("gdul: cannot open the window: %s", SDL_GetError());
        return false;
    }
#if !defined(__ANDROID__)
    SDL_SetWindowMinimumSize(window_, 960, 540);
#endif
    int iw = 0, ih = 0, n = 0;
    if (unsigned char* px = stbi_load_from_memory(kGdulIconPng, int(kGdulIconPngSize), &iw, &ih, &n, 4)) {
        if (SDL_Surface* s = SDL_CreateSurfaceFrom(iw, ih, SDL_PIXELFORMAT_RGBA32, px, iw * 4)) {
            icon_ = SDL_DuplicateSurface(s);
            SDL_DestroySurface(s);
        }
        stbi_image_free(px);
    }
    std::string error;
    if (!startGl(error)) {
        SDL_Log("gdul: cannot start OpenGL: %s", error.c_str());
        return false;
    }
    restoreWindow();
    rememberWindow();
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
             std::to_string(catalog_.games().size()) + " games, " + std::to_string(catalog_.discsFound()) + " disc images found" +
             (platform::isSteamDeck() ? ", Steam Deck: the games get their handheld settings" : "") +
             (platform::bigPicture() ? ", RecompLauncher Big Picture: full screen" : "") + ")");
    for (const Catalog::DiscDir& d : catalog_.discDirInfo())
        log_.add("disc folder " + d.path + " (" + d.what + "): " +
                 (d.problem.empty() ? std::to_string(d.isos) + " .iso, " + std::to_string(d.known) + " known" : d.problem));
    if (catalog_.translocated())
        log_.add("macOS runs the app from a translocated copy: " + catalog_.runningApp() +
                 (catalog_.originalApp().empty() ? std::string(" (its real place is unknown)") : " (its real place: " + catalog_.originalApp() + ")"));
    // A playable game whose module the build has but the files do not: an
    // incomplete download, said at once (and logged), not only on Play.
    for (const Game& g : catalog_.games())
        if (g.moduleMissing) {
            SDL_Log("gdul: %s", noModuleText(g).c_str());
            log_.add("incomplete download: " + g.id + "'s module is missing (" + g.modulePath + ")");
            missingModules_ += (missingModules_.empty() ? "" : ", ") + g.id;
        }
    // macOS, an app still where the download put it, and no disc found: say why and what to do.
    startupNotice_ = catalog_.translocated() && catalog_.discsFound() == 0;
    sounds_.setLogging(SDL_getenv("GDUL_SCRIPT") != nullptr || SDL_getenv("GDUL_SOUND_LOG") != nullptr);
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
    // toast=<text>, updates (the Updates menu), updatecheck (Check for updates), quit.
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
    updateInit();
    return true;
}

void Launcher::shutdown() {
    updater_.shutdown();
    exploreClose();
    sounds_.close();
    closeGamepads();
    gfx_.shutdown();
    if (context_) SDL_GL_DestroyContext(context_);
    if (window_) SDL_DestroyWindow(window_);
    if (icon_) SDL_DestroySurface(icon_);
    SDL_Quit();
}

// The launcher's window for a graphics API: OpenGL (its own, and every game
// module's at first), or what a module asked for with request_window. `keep`:
// the full screen / borderless state carried over; x, y: the place
// (SDL_WINDOWPOS_UNDEFINED: the system chooses).
SDL_Window* Launcher::makeWindow(uint32_t api, int w, int h, int x, int y, SDL_WindowFlags keep) {
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | keep;
    switch (api) {
    case GD_WINDOW_API_OPENGL: flags |= SDL_WINDOW_OPENGL; break;
    case GD_WINDOW_API_VULKAN: flags |= SDL_WINDOW_VULKAN; break;
    case GD_WINDOW_API_METAL: flags |= SDL_WINDOW_METAL; break;
    default: break;  // SDL_GPU: SDL_ClaimWindowForGPUDevice sets the window up itself
    }
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, kTitle);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, w);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, h);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, x);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, y);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, Sint64(flags));
    SDL_Window* win = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (!win) return nullptr;
#if !defined(__ANDROID__)
    SDL_SetWindowMinimumSize(win, 960, 540);
#endif
    if (icon_) SDL_SetWindowIcon(win, icon_);
    return win;
}

// The launcher's GL context and 2D renderer on window_ (OpenGL window).
bool Launcher::startGl(std::string& error) {
    context_ = SDL_GL_CreateContext(window_);
    if (!context_ || !SDL_GL_MakeCurrent(window_, context_)) {
        error = SDL_GetError();
        if (context_) SDL_GL_DestroyContext(context_);
        context_ = nullptr;
        return false;
    }
    SDL_GL_SetSwapInterval(1);
    if (!gfx_.init(error)) {
        gfx_.shutdown();
        SDL_GL_MakeCurrent(window_, nullptr);
        SDL_GL_DestroyContext(context_);
        context_ = nullptr;
        return false;
    }
    return true;
}

void Launcher::stopGl() {
    if (!context_) return;
    SDL_GL_MakeCurrent(window_, context_);
    gfx_.shutdown();
    SDL_GL_MakeCurrent(window_, nullptr);
    SDL_GL_DestroyContext(context_);
    context_ = nullptr;
}

// request_window (sdk/gd_host.h): the window made again for `api`, at the
// size, place, full screen state and title it has now (the game's). A window
// carries one graphics API: an OpenGL context and a GPU swapchain cannot
// share it, Vulkan and Metal surfaces want it made for them, and SDL cannot
// turn a window back into an OpenGL one. The launcher's GL objects go with
// its context (gfx_ makes them again from the built-in resources).
bool Launcher::switchWindowApi(uint32_t api, std::string& error) {
    if (api == windowApi_ && window_) return true;
    int w = 1280, h = 720, x = int(SDL_WINDOWPOS_UNDEFINED), y = int(SDL_WINDOWPOS_UNDEFINED);
    SDL_GetWindowSize(window_, &w, &h);
    SDL_GetWindowPosition(window_, &x, &y);
    const SDL_WindowFlags was = SDL_GetWindowFlags(window_);
    const SDL_WindowFlags keep = was & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP);
    const SDL_DisplayMode* fsMode = SDL_GetWindowFullscreenMode(window_);
    SDL_DisplayMode mode{};
    if (fsMode) mode = *fsMode;
    const std::string title = SDL_GetWindowTitle(window_);
    const uint32_t oldApi = windowApi_;
    SDL_Window* old = window_;
    stopGl();
#if defined(__ANDROID__)
    // One window at a time: the old one goes first.
    SDL_DestroyWindow(old);
    old = nullptr;
#endif
    SDL_Window* win = makeWindow(api, w, h, x, y, keep);
    if (!win) {
        error = std::string("the window cannot be made: ") + SDL_GetError();
        // As it was: the old window (or a new one like it, on Android) and its GL context.
        if (!old) old = makeWindow(oldApi, w, h, x, y, keep);
        window_ = old;
        if (window_ && oldApi == GD_WINDOW_API_OPENGL) {
            std::string e2;
            if (!startGl(e2)) SDL_Log("gdul: the OpenGL context cannot be made again: %s", e2.c_str());
        }
        return false;
    }
    if (old) SDL_DestroyWindow(old);
    window_ = win;
    windowApi_ = api;
    SDL_SetWindowTitle(window_, title.c_str());
    if (fsMode) SDL_SetWindowFullscreenMode(window_, &mode);
    SDL_ShowWindow(window_);
    SDL_RaiseWindow(window_);
    if (api == GD_WINDOW_API_OPENGL && !startGl(error)) {
        error = "no OpenGL context on the new window: " + error;
        return false;
    }
    SDL_GetWindowSizeInPixels(window_, &pw_, &ph_);
    log_.add(std::string("window: made again for ") +
             (api == GD_WINDOW_API_OPENGL ? "OpenGL" : api == GD_WINDOW_API_SDL_GPU ? "SDL_GPU" : api == GD_WINDOW_API_VULKAN ? "Vulkan" : "Metal") +
             " (" + std::to_string(w) + "x" + std::to_string(h) + ((keep & SDL_WINDOW_FULLSCREEN) ? ", full screen" : "") + ")");
    return true;
}

// The launcher's window as it is (windowed), to put back after a game.
void Launcher::rememberWindow() {
#if !defined(__ANDROID__)  // Android has no window mode: its own settings stay as they are
    const SDL_WindowFlags f = SDL_GetWindowFlags(window_);
    if (f & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MINIMIZED)) return;
    saved_.maximized = (f & SDL_WINDOW_MAXIMIZED) != 0;
    if (!saved_.maximized) {
        SDL_GetWindowPosition(window_, &saved_.x, &saved_.y);
        SDL_GetWindowSize(window_, &saved_.w, &saved_.h);
    }
    saved_.valid = true;
#endif
}

// After a game (and at start): the launcher's title, icon and app id again,
// and always a window, never full screen, at the size and place it had. In
// RecompLauncher's Big Picture mode (platform::bigPicture) full screen
// instead: the desktop's own mode, no border.
void Launcher::restoreWindow() {
    SDL_SetAppMetadata(kTitle, catalog_.launcherVersion().c_str(), "io.github.mbracali.gdul");
    SDL_SetWindowTitle(window_, kTitle);
    if (icon_) SDL_SetWindowIcon(window_, icon_);
    SDL_GL_SetSwapInterval(1);
#if !defined(__ANDROID__)  // Android has no window mode: its settings are left alone
    if (platform::bigPicture()) {
        SDL_SetWindowFullscreenMode(window_, nullptr);  // no exclusive mode left over from a game
        if (!(SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN)) {
            SDL_SetWindowFullscreen(window_, true);
            SDL_SyncWindow(window_);
        }
    } else {
        leaveFullscreen();
    }
    SDL_SetWindowBordered(window_, true);
    SDL_SetWindowResizable(window_, true);
    SDL_SetWindowAlwaysOnTop(window_, false);
    SDL_SetWindowMouseGrab(window_, false);
    SDL_SetWindowKeyboardGrab(window_, false);
    SDL_SetWindowRelativeMouseMode(window_, false);
    SDL_SetWindowMinimumSize(window_, 960, 540);
    SDL_ShowCursor();
    const SDL_WindowFlags f = SDL_GetWindowFlags(window_);
    if (f & SDL_WINDOW_MINIMIZED) SDL_RestoreWindow(window_);
    if (saved_.valid) {
        if (saved_.maximized) {
            if (!(f & SDL_WINDOW_MAXIMIZED)) SDL_MaximizeWindow(window_);
        } else {
            if (f & SDL_WINDOW_MAXIMIZED) SDL_RestoreWindow(window_);
            SDL_SetWindowSize(window_, saved_.w, saved_.h);
            SDL_SetWindowPosition(window_, saved_.x, saved_.y);
        }
    }
    SDL_SyncWindow(window_);
    SDL_ShowWindow(window_);
    SDL_RaiseWindow(window_);
#else
    SDL_ShowCursor();
#endif
    // The transitions above came as events: the launcher's input starts clean.
    SDL_FlushEvents(SDL_EVENT_WINDOW_FIRST, SDL_EVENT_WINDOW_LAST);
    SDL_FlushEvents(SDL_EVENT_KEY_DOWN, SDL_EVENT_MOUSE_REMOVED);
    SDL_GetWindowSizeInPixels(window_, &pw_, &ph_);
    SDL_Log("gdul: window %s %dx%d", (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) ? "fullscreen" : "windowed", pw_, ph_);
}

// Out of full screen (a game's, or the window manager's), waiting for it: on
// Windows and macOS the change is asynchronous.
void Launcher::leaveFullscreen() {
    SDL_SetWindowFullscreenMode(window_, nullptr);  // no exclusive mode left over from a game
    if (!(SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN)) return;
    SDL_SetWindowFullscreen(window_, false);
    if (!SDL_SyncWindow(window_)) {
        // Timed out (SDL waits a while): pump until the window says it left, at most 2 s.
        for (const Uint64 end = SDL_GetTicks() + 2000; SDL_GetTicks() < end;) {
            SDL_PumpEvents();
            if (!(SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN)) break;
            SDL_Delay(10);
        }
    }
    log_.add(std::string("window: left full screen") +
             ((SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) ? " (the system did not confirm it)" : ""));
}

// The header's full-screen / windowed button (and F11): desktop only. Big
// Picture is always full screen; Android has no window mode.
void Launcher::toggleFullscreen() {
#if !defined(__ANDROID__)
    if (platform::bigPicture()) return;
    userFullscreen_ = !userFullscreen_;
    if (userFullscreen_) {
        rememberWindow();  // capture the windowed size / position to come back to
        SDL_SetWindowFullscreenMode(window_, nullptr);  // borderless desktop full screen, not a video mode
        SDL_SetWindowFullscreen(window_, true);
        SDL_SyncWindow(window_);
        log_.add("window: full screen (toggle)");
    } else {
        leaveFullscreen();
        if (saved_.valid) {  // restore the windowed size / position
            if (saved_.maximized) {
                SDL_MaximizeWindow(window_);
            } else {
                SDL_SetWindowSize(window_, saved_.w, saved_.h);
                SDL_SetWindowPosition(window_, saved_.x, saved_.y);
            }
            SDL_SyncWindow(window_);
        }
    }
    SDL_GetWindowSizeInPixels(window_, &pw_, &ph_);
#endif
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

// One press seen from two controllers at once acts once: a Steam Deck in
// Gaming Mode is both its own controller and Steam's virtual one (as gd01's
// menus filter it: the same button from another device within 80 ms).
bool Launcher::duplicatePress(const SDL_Event& e) {
    constexpr Uint64 kSameNs = 80000000;
    if (e.type != SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.type != SDL_EVENT_GAMEPAD_BUTTON_UP) return false;
    const int b = e.gbutton.button;
    if (b < 0 || b >= SDL_GAMEPAD_BUTTON_COUNT) return false;
    const std::pair<SDL_JoystickID, int> key{e.gbutton.which, b};
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_UP) return dropped_.erase(key) > 0;
    RecentPress& r = recentPress_[size_t(b)];
    if (r.ns && e.gbutton.timestamp >= r.ns && e.gbutton.timestamp - r.ns < kSameNs && r.device != e.gbutton.which) {
        dropped_.insert(key);
        return true;
    }
    r = {e.gbutton.timestamp, e.gbutton.which};
    return false;
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
    return exitCode_;
}

// ---- The command line (main.cpp): --play / --explore <game> [--iso <disc>] [--exit-after] ----

void Launcher::cliFail(const std::string& why) {
    std::fprintf(stderr, "Guitar Dream Universal Launcher: %s\n", why.c_str());
    std::fflush(stderr);
    log_.add("command line: " + why);
    if (cli_.exitAfter) {
        exitCode_ = 1;
        running_ = false;
        return;
    }
    sounds_.play(UiSound::Error);
    showDialog("We can't rock this! Sorry...", why, {{"OK", Fret::Green, nullptr}});
}

void Launcher::runCommandLine() {
    cliPending_ = false;
    const bool explore = cli_.play.empty();
    const std::string id = explore ? cli_.explore : cli_.play;
    log_.add("command line: " + std::string(explore ? "explore " : "play ") + id + (cli_.iso.empty() ? "" : " with " + cli_.iso) +
             (cli_.exitAfter ? ", then exit" : ""));
    Game* g = catalog_.find(id);
    if (!g) {
        std::string ids;
        for (const Game& x : catalog_.games()) ids += (ids.empty() ? "" : ", ") + x.id;
        cliFail("There is no game " + id + " (the games: " + ids + "; --list shows them).");
        return;
    }
    for (size_t i = 0; i < catalog_.games().size(); ++i)
        if (&catalog_.games()[i] == g) focusRow_ = int(i), focusCol_ = explore ? kExplore : kPlay;
    if (!cli_.iso.empty()) {
        if (!platform::isFile(cli_.iso)) {
            cliFail("The disc image " + cli_.iso + " is not there.");
            return;
        }
        g->iso = cli_.iso;  // for this run only (Load ISO saves a disc)
        g->isoName = platform::displayName(cli_.iso);
    }
    if (!g->ready) return cliFail(g->title() + " (" + g->id + ") is not playable yet. Coming soon!");
    if (!g->hasModule) return cliFail(noModuleText(*g));
    if (g->iso.empty())
        return cliFail("No " + g->title() + " disc image: give one with --iso <file>, or put it in original_isos/ next to the launcher.");
    if (explore) {
        exploreOpen(*g);
        cliExplore_ = cli_.exitAfter && explore_ != nullptr;
        if (cli_.exitAfter && !explore_) exitCode_ = 1, running_ = false;
        return;
    }
    play(*g);
    if (cli_.exitAfter) {
        exitCode_ = lastExit_;
        running_ = false;
    }
}

void Launcher::frame() {
    // Answers of the system file dialog.
    std::vector<std::pair<std::string, std::string>> picked;
    {
        std::lock_guard<std::mutex> g(pickedLock_);
        picked.swap(picked_);
    }
    for (auto& [id, path] : picked) {
        if (path.empty()) {
            log_.add("load disc for " + id + ": cancelled");
        } else if (path[0] == '\x01') {
            log_.add("load disc for " + id + ": the file dialog failed: " + path.substr(1));
            sounds_.play(UiSound::Error);
            toast("The file dialog did not open: " + path.substr(1));
        } else {
            handleDisc(path, id);
        }
    }
    // Gamepad direction repeat.
    if (heldDir_ && now_ >= repeatAt_) {
        repeatAt_ = now_ + 0.09;
        move(heldDir_ == 1 ? -1 : heldDir_ == 2 ? 1 : 0, heldDir_ == 3 ? -1 : heldDir_ == 4 ? 1 : 0);
    }
    runScript();
    if (!running_) return;
    // The menu sounds, once the window shows (they take a moment to read).
    if (!soundsTried_ && ++framesDrawn_ > 2) loadSounds();
    if (!missingModules_.empty() && framesDrawn_ > 2) {
        showDialog("This download is incomplete",
                   "The module of " + missingModules_ + " is missing from this download, so " +
                       (missingModules_.find(',') == std::string::npos ? "that game" : "those games") +
                       " cannot start. Please download the launcher again from guitardreamengine.com.",
                   {{"OK", Fret::Green, nullptr}});
        missingModules_.clear();
        startupNotice_ = false;  // one dialog at a time
    }
    if (startupNotice_ && framesDrawn_ > 2 && !cliPending_) {
        startupNotice_ = false;
        showDiscFolders("Where are your discs?");
    }
    if (cliPending_ && framesDrawn_ > 2) {
        runCommandLine();
        if (!running_) return;
    }
    updateFrame();
    if (!running_) return;
    if (cliExplore_ && !explore_) {  // --explore --exit-after: the explorer closed
        running_ = false;
        return;
    }
    if (additive_) {
        additiveFrame();
        return;
    }
    if (songs_) {
        songsFrame();
        return;
    }
    if (explore_) {
        exploreFrame();
        return;
    }
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
    if (exploreScript(st.cmd, st.arg)) return;
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
    else if (st.cmd == "folders") showDiscFolders("Where we look for discs");
    else if (st.cmd == "quit") running_ = false;
    else if (st.cmd == "updates") updateMenu();
    else if (st.cmd == "updatecheck") updater_.startCheck(true);
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
    } else if (st.cmd == "songs") {  // dev aid: open the song player for a game (no explorer needed)
        for (Game& g : catalog_.games())
            if (g.id == st.arg) { songsOpen(g); break; }
    } else if (st.cmd == "songplay") {  // dev aid: play row <arg> in the open song player
        if (songs_) songsPlay(std::atoi(st.arg.c_str()));
    } else if (st.cmd == "songpart") {  // dev aid: toggle stem <arg> of the playing song
        if (songs_) songsTogglePart(std::atoi(st.arg.c_str()));
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
    if (duplicatePress(e)) return;
    if (songs_ && songsHandle(e)) return;
    if (explore_ && exploreHandle(e)) return;
    if (additive_ && additiveHandle(e)) return;
    switch (e.type) {
    case SDL_EVENT_QUIT:
        running_ = false;
        break;
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:  // the launcher is a window unless Big Picture or the user's toggle (desktops; Android has no window mode)
#if !defined(__ANDROID__)
        if (!platform::bigPicture() && !userFullscreen_) SDL_SetWindowFullscreen(window_, false);  // Big Picture / toggle: stay full screen
#endif
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
        case SDLK_U: if (!e.key.repeat && !dialog_.open) updateMenu(); break;
        case SDLK_F11: if (!e.key.repeat) toggleFullscreen(); break;
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
        // Scrollbar thumbs: start a drag, or follow one in progress.
        if (vbarDrag_ || hbarDrag_) {
            if (e.type == SDL_EVENT_MOUSE_MOTION) {
                if (vbarDrag_ && vTrackH_ > vThumbH_)
                    scrollTarget_ = vRange_ * std::clamp((my - barDragOff_ - vTrackY_) / (vTrackH_ - vThumbH_), 0.f, 1.f);
                if (hbarDrag_ && hTrackW_ > hThumbW_)
                    hscrollTarget_ = hRange_ * std::clamp((mx - barDragOff_ - hTrackX_) / (hTrackW_ - hThumbW_), 0.f, 1.f);
                mouseUsed_ = true;
            }
            break;
        }
        if (click && !dialog_.open) {
            if (vThumb_.w > 0 && vThumb_.contains(mx, my)) {
                vbarDrag_ = true, barDragOff_ = my - vThumb_.y, mouseUsed_ = true;
                break;
            }
            if (hThumb_.w > 0 && hThumb_.contains(mx, my)) {
                hbarDrag_ = true, barDragOff_ = mx - hThumb_.x, mouseUsed_ = true;
                break;
            }
        }
        for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
            if (!it->r.contains(mx, my)) continue;
            mouseUsed_ = true;
            if (it->row == kHitWebsite || it->row == kHitDiscs || it->row == kHitUpdates || it->row == kHitFullscreen ||
                it->row == kHitDreamForge) {
                if (click && !dialog_.open) {
                    if (it->row == kHitWebsite) openWebsite();
                    else if (it->row == kHitUpdates) updateMenu();
                    else if (it->row == kHitFullscreen) toggleFullscreen();
                    else if (it->row == kHitDiscs) showDiscFolders("Where we look for discs");
                    // kHitDreamForge: disabled, no action (tooltip "Coming soon!")
                }
            } else if (it->row < 0) {
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
    case SDL_EVENT_MOUSE_BUTTON_UP:
        vbarDrag_ = hbarDrag_ = false;
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if (!dialog_.open) {
            const float content = float(catalog_.games().size()) * (kRowH + kRowGap);
            const float view = H_ - kFooterH - 12 - kListTop;
            const bool shift = SDL_GetModState() & SDL_KMOD_SHIFT;
            if (shift || e.wheel.x != 0) {  // horizontal scroll (shift+wheel, or a horizontal wheel)
                const float d = (e.wheel.x != 0 ? e.wheel.x : e.wheel.y) * 72;
                hscrollTarget_ = std::clamp(hscrollTarget_ - d, 0.f, std::max(0.f, tableW_ - (lListRight_ - kMargin)));
            } else {
                scrollTarget_ = std::clamp(scrollTarget_ - e.wheel.y * 72, 0.f, std::max(0.f, content - view));
            }
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
    case kLoad: return true;  // always usable; its colour is handled in drawRow (yellow when no disc)
    case kAdditive: return g.playable();           // the additive-music tool opens when the game is ready
    case kMod: return false;                        // coming soon (disabled placeholder)
    case kExplore: return !g.iso.empty() && g.hasModule;  // the module shows the disc's models
    default: return false;
    }
}

void Launcher::move(int dRow, int dCol) {
    if (explore_ && !dialog_.open) {
        exploreMove(dRow, dCol);
        return;
    }
    if (dialog_.open) {
        const int n = int(dialog_.buttons.size());
        const int was = dialog_.focus;
        if (n && dCol) dialog_.focus = std::clamp(dialog_.focus + dCol, 0, n - 1);
        if (dialog_.focus != was) sounds_.play(UiSound::Move);
        return;
    }
    mouseUsed_ = false;
    const int rows = int(catalog_.games().size());
    if (rows == 0) return;
    const int wasRow = focusRow_, wasCol = focusCol_;
    focusRow_ = std::clamp(focusRow_ + dRow, 0, rows - 1);
    if (dCol) {
        focusCol_ = std::clamp(focusCol_ + dCol, 0, kColumns - 1);
        if (focusCol_ == kMod) focusCol_ = kMod + (dCol > 0 ? 1 : -1);  // skip the disabled Mod column (interior)
    }
    if (focusRow_ != wasRow || focusCol_ != wasCol) sounds_.play(UiSound::Move);
}

void Launcher::press(Fret f) {
    if (dialog_.open) {
        if (f == Fret::Green) {
            Button b = dialog_.buttons.empty() ? Button{} : dialog_.buttons[size_t(dialog_.focus)];
            dialog_.open = false;
            sounds_.play(b.fret == Fret::Red ? UiSound::Back : UiSound::Select);
            if (b.action) b.action();
        } else if (f == Fret::Red) {
            // Red: the dialog's red button if it has one, else close it.
            dialog_.open = false;
            sounds_.play(UiSound::Back);
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
                    sounds_.play(UiSound::Select);
                    if (copy.action) copy.action();
                    break;
                }
        }
        return;
    }
    if (explore_) {
        explorePress(f);
        return;
    }
    switch (f) {
    case Fret::Green: activate(focusRow_, focusCol_); break;
    case Fret::Red:
        sounds_.play(UiSound::Back);
        askQuit();
        break;
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
        else if (!g.ready) sounds_.play(UiSound::Error), toast(g.title() + " is not playable yet. Coming soon!");
        else if (!g.hasModule) sounds_.play(UiSound::Error), toast(noModuleText(g));
        else {  // ready, module built, but no disc yet: open Load ISO (owner 11-22)
            sounds_.play(UiSound::Select);
            pickDisc(g);
        }
        break;
    case kLoad:
        sounds_.play(UiSound::Select);
        pickDisc(g);
        break;
    case kAdditive:
        if (g.playable()) {
            sounds_.play(UiSound::Select);
            additiveOpen(&g);
        } else if (!g.ready) {
            sounds_.play(UiSound::Error), toast(g.title() + " is not playable yet. Coming soon!");
        } else if (!g.hasModule) {
            sounds_.play(UiSound::Error), toast(noModuleText(g));
        } else {
            sounds_.play(UiSound::Error), toast("Load a " + g.title() + " disc image to manage its additive music.");
        }
        break;
    case kMod:
        sounds_.play(UiSound::Error);
        toast("The Mod menu is coming soon.");
        break;
    case kExplore:
        if (g.iso.empty()) sounds_.play(UiSound::Error), toast("Load a " + g.title() + " disc image to explore its models.");
        else if (!g.hasModule)
            sounds_.play(UiSound::Error),
                toast(g.ready ? noModuleText(g) : g.title() + " is not playable yet. Coming soon!");
        else sounds_.play(UiSound::Select), exploreOpen(g);
        break;
    default: break;
    }
}

void Launcher::play(Game& g) {
    log_.add("play " + g.id + " (" + g.title() + " " + g.version + ") with " + g.iso);
    const Uint64 t0 = SDL_GetTicks();
    sounds_.play(UiSound::Play);
    drawLoading(g);
    // The game's own audio device: the launcher's is released first (after
    // its "play" sound had a moment to ring).
    if (sounds_.count() > 0) {
        const Uint64 spent = SDL_GetTicks() - t0;
        if (spent < 450) SDL_Delay(Uint32(450 - spent));
    }
    sounds_.close();
    closeGamepads();
    rememberWindow();
    // Hand the game the discs of other games the player exposed (additive songs).
    const auto shared = exposedDiscsFor(g);
    setExtraDiscs(shared);
    if (!shared.empty()) {
        std::string names;
        for (const auto& [id, p] : shared) names += (names.empty() ? "" : ", ") + id;
        log_.add("additive: " + g.id + " also gets " + std::to_string(shared.size()) + " shared disc(s): " + names);
    }
    GameRun r = runGameModule(window_, context_, g, catalog_.launcherVersion(), log_,
                              [this](uint32_t api, SDL_Window*& w, SDL_GLContext& c, std::string& error) {
                                  const bool ok = switchWindowApi(api, error);
                                  w = window_, c = context_;
                                  return ok;
                              });
    if (r.windowLost || !window_ || !context_) {
        // Nothing to draw the launcher with: say so and stop (never a black window).
        std::fprintf(stderr, "Guitar Dream Universal Launcher: %s\n", r.error.c_str());
        log_.add("launcher stopped: " + r.error);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kTitle, ("The launcher cannot draw its window again: " + r.error).c_str(), nullptr);
        lastExit_ = r.started ? r.exitCode : 1;
        exitCode_ = 1;
        running_ = false;
        return;
    }
    restoreWindow();
    openGamepads();
    sounds_.open();
    dragging_ = false;
    lastExit_ = r.started ? r.exitCode : 1;
    if (!r.started) {
        std::fprintf(stderr, "Guitar Dream Universal Launcher: %s did not start: %s\n", g.id.c_str(), r.error.c_str());
        sounds_.play(UiSound::Error);
        log_.add("play " + g.id + " failed: " + r.error);
        showDialog("We can't rock this! Sorry...", g.title() + " did not start: " + r.error, {{"OK", Fret::Green, nullptr}});
        return;
    }
    char secs[32];
    SDL_snprintf(secs, sizeof secs, "%.1f", r.seconds);
    log_.add("back from " + g.id + ": exit code " + std::to_string(r.exitCode) + " after " + secs + " s" +
             (r.unloaded ? " (module unloaded)" : " (module stayed loaded)"));
    if (r.exitCode == GD_GAME_EXIT_RESTART) {  // Restart Now (GDUL-028): a new Play of the same game
        log_.add("restart " + g.id + " (Restart Now)");
        play(g);
        return;
    }
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
    // "" cancelled; a failed dialog as "\x01<SDL's error>".
    std::string path = !files ? std::string("\x01") + SDL_GetError() : files[0] ? files[0] : "";
    {
        std::lock_guard<std::mutex> g(*req->lock);
        req->out->emplace_back(req->id, path);
    }
    delete req;
}
}  // namespace

void Launcher::pickDisc(Game& g) {
    log_.add("load disc for " + g.id + ": file dialog opened");
    static const SDL_DialogFileFilter kFilters[] = {{"Disc images (*.iso, *.chd)", "iso;ISO;chd;CHD"}, {"All files", "*"}};
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
    const std::string file = platform::displayName(path);
    log_.add("identify " + path + ": " + disc.describe() + " -> " + (g ? g->id : std::string("unknown")));
    Game* wanted = wantedId.empty() ? nullptr : catalog_.find(wantedId);
    if (!g) {
        std::vector<Button> buttons;
        if (wanted && !disc.format.empty()) {
            std::string id = wanted->id;
            buttons.push_back({"Use it anyway", Fret::Yellow, [this, id, path] {
                                   catalog_.saveIso(id, path);
                                   log_.add("disc saved for " + id + " (unrecognised): " + path);
                                   discsChanged();
                                   toast("Disc saved for " + id + ".");
                               }});
        }
        buttons.push_back({"OK", Fret::Green, nullptr});
        sounds_.play(UiSound::Error);
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
        discsChanged();
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
        body += g->ready ? " (" + noModuleText(*g) + ")" : " (It is not playable yet; the launcher will remember it.)";
        buttons.push_back({"Save", Fret::Green, [this, save, id] {
                               save();
                               toast("Disc saved for " + id + ".");
                           }});
    }
    buttons.push_back({"Cancel", Fret::Red, [this, id] { log_.add("disc not saved for " + id); }});
    showDialog(title, body, buttons);
}

void Launcher::openWebsite() {
    log_.add(std::string("website opened: ") + kWebsite);
    sounds_.play(UiSound::Select);
    if (SDL_OpenURL(kWebsite)) toast("Opening guitardreamengine.com in your browser\xE2\x80\xA6");
    else toast("Visit guitardreamengine.com");
}

// The folders where disc images are looked for, what each had, and (macOS)
// what to do when the app runs from a translocated copy.
void Launcher::showDiscFolders(const std::string& title) {
    sounds_.play(UiSound::Select);
    std::string body;
    for (const Catalog::DiscDir& d : catalog_.discDirInfo()) {
        std::string state = !d.problem.empty() ? d.problem
                            : d.isos == 0     ? "no .iso files"
                                              : std::to_string(d.isos) + " .iso, " + std::to_string(d.known) + " known";
        body += "\xE2\x80\xA2 " + d.what + " (" + state + "):\n" + d.path + "\n";
    }
    if (catalog_.translocated()) {
        const std::string app = catalog_.originalApp().empty() ? std::string("Guitar Dream Universal Launcher.app") : catalog_.originalApp();
        body += "\nmacOS is running the app from a temporary copy (it still has the download's quarantine mark), "
                "so the original_isos folder next to it may not be seen. Move the app to another folder with the "
                "Finder and open it again, or run in Terminal:\nxattr -dr com.apple.quarantine \"" + app + "\"\n";
    }
#ifdef __ANDROID__
    // No file manager can open the app's own folders (an app's private
    // folder cannot be shown to another app: "Cannot open
    // /data/data/io.github.mbracali.gdul/files/original_isos"): Load ISO
    // reads a disc from anywhere on the device instead, where it is.
    body += "\nUse Load ISO to choose your disc image anywhere on this device (Downloads, an SD card...): "
            "the launcher reads it where it is, nothing is copied.";
    std::vector<Button> buttons;
    buttons.push_back({"Load ISO", Fret::Yellow, [this] {
                           if (focusRow_ >= 0 && focusRow_ < int(catalog_.games().size())) pickDisc(catalog_.games()[size_t(focusRow_)]);
                       }});
#else
    body += "\nPut your .iso files in one of these folders, use Load ISO, or drop one on this window.";
    std::vector<Button> buttons;
    const std::string mine = platform::join(platform::saveDir(), "original_isos");
    buttons.push_back({"Open folder", Fret::Yellow, [this, mine] {
                           SDL_CreateDirectory(mine.c_str());
                           std::string url = "file://" + mine;
#ifdef _WIN32
                           url = "file:///" + mine;
#endif
                           if (!SDL_OpenURL(url.c_str())) toast("Cannot open " + mine);
                           log_.add("disc folder opened: " + mine);
                       }});
#endif
    buttons.push_back({"Look again", Fret::Blue, [this] {
                           catalog_.rescanDiscs();
                           log_.add("disc folders scanned again: " + std::to_string(catalog_.discsFound()) + " disc images found");
                           discsChanged();
                           toast(std::to_string(catalog_.discsFound()) + (catalog_.discsFound() == 1 ? " disc image found." : " disc images found."));
                       }});
    buttons.push_back({"OK", Fret::Green, nullptr});
    showDialog(title, body, buttons);
    dialog_.focus = 2;
}

// The menu sounds: the first game whose module gives them (Guitar Hero's,
// from its disc). Silent while no such disc is known.
void Launcher::loadSounds() {
    soundsTried_ = true;
    if (sounds_.count() > 0 && !sounds_.builtIn()) return;
    sounds_.clear();
    struct Sink {
        UiSounds* sounds;
    } sink{&sounds_};
    auto add = [](void* user, const char* role, const float* stereo, int frames, int rate) {
        if (role) static_cast<Sink*>(user)->sounds->add(role, stereo, frames, rate);
    };
    // Development aid: GDUL_BUILTIN_SOUNDS=1 uses the built-in ones even with a disc.
    const bool builtInOnly = SDL_getenv("GDUL_BUILTIN_SOUNDS") != nullptr;
    for (const Game& g : catalog_.games()) {
        if (builtInOnly || !g.hasModule || g.iso.empty()) continue;
        std::string error;
        const Uint64 t0 = SDL_GetTicks();
        const int n = fetchUiSounds(window_, context_, g, catalog_.launcherVersion(), log_, add, &sink, error);
        if (n > 0) {
            log_.add("menu sounds: " + std::to_string(sounds_.count()) + " from " + g.id + " (" + g.title() + ", " +
                     g.isoName + ", " + std::to_string(SDL_GetTicks() - t0) + " ms)");
            sounds_.open();
            return;
        }
        SDL_Log("gdul: menu sounds from %s: %s", g.id.c_str(), error.c_str());
    }
    // No such disc: the launcher's own (synthesized, nothing from a game).
    sounds_.addBuiltIn();
    sounds_.open();
    log_.add("menu sounds: " + std::to_string(sounds_.count()) + " built-in" +
             (builtInOnly ? " (GDUL_BUILTIN_SOUNDS)" : " (no disc of a game that has them, such as Guitar Hero)"));
}

void Launcher::discsChanged() {
    if (sounds_.count() == 0 || sounds_.builtIn()) soundsTried_ = false;
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

std::string Launcher::noModuleText(const Game& g) {
    if (g.moduleMissing) return "This download is incomplete: " + g.id + "'s module is missing. Please download the launcher again.";
    return "This launcher build has no " + g.id + " module.";
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
    tooltip_.clear();
    computeLayout();
    gfx_.begin(pw_, ph_);
    drawBackground();
    drawList();
    drawHeader();
    drawFooter();
    drawToast();
    if (dialog_.open) drawDialog();
    if (dragging_) drawDropOverlay();
    if (!tooltip_.empty() && !dialog_.open) drawTooltip();
    gfx_.end();
}

void Launcher::computeLayout() {
    lScrollX_ = W_ - kMargin - 9;      // vertical scrollbar band (~18 wide)
    lListRight_ = lScrollX_ - 12;      // rows end left of the scrollbar
    lPlayX_ = kMargin + 12;
    lPlayW_ = 76;                      // a wide, prominent Play button (owner 11-23)
    lPlayH_ = 52;
    lNameX_ = lPlayX_ + lPlayW_ + 18;
    lColW_ = 84;                       // right-column width — the action buttons sit closer together (owner 11-23)
    const float blockRight = lListRight_ - 6;
    const float blockLeft = blockRight - kColumns * lColW_;
    for (int i = 0; i < kColumns; ++i) lColCx_[i] = blockLeft + (float(i) + 0.5f) * lColW_;
    tableW_ = blockRight - kMargin;    // natural table width, for the horizontal scrollbar
}

// One Material icon, centred on (cx, cy) in design units. `px` is the glyph
// pixel size; the Icons atlas is separate from the text fonts.
void Launcher::icon(uint32_t cp, float cx, float cy, float px, gfx::Color c) {
    const std::string s = gfx::icon::u8(cp);
    const float spx = px * s_;
    const float w = gfx_.measure(gfx::FontId::Icons, spx, s);
    const float lh = gfx_.lineHeight(gfx::FontId::Icons, spx);
    gfx_.text(gfx::FontId::Icons, spx, cx * s_ - w / 2, cy * s_ - lh / 2, s, c);
}

void Launcher::drawTooltip() {
    const float pad = 12, h = 30;
    const float w = measure(FontId::SemiBold, 15, tooltip_) + 2 * pad;
    float x = (tooltipAt_.x + tooltipAt_.w / 2) / s_ - w / 2;
    float y = tooltipAt_.y / s_ - h - 8;
    x = std::clamp(x, kMargin, W_ - kMargin - w);
    if (y < kListTop) y = tooltipAt_.y / s_ + tooltipAt_.h / s_ + 8;
    const Rect r = R(x, y, w, h);
    gfx_.glow(r, withAlpha(kInk, 0.7f), 10 * s_, 16 * s_);
    gfx_.rect(r, rgb(0x2b2530, 0.98f), rgb(0x1f1b23, 0.98f), 8 * s_);
    gfx_.ring(r, withAlpha(kSand, 0.5f), 8 * s_, 1.2f * s_);
    text(FontId::SemiBold, 15, x + pad, y + (h - gfx_.lineHeight(FontId::SemiBold, 15 * s_) / s_) / 2, tooltip_, kCream);
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
    text(FontId::Title, 60, 150, 20, "Guitar Dream Engine", kCream);
    const float subW = text(FontId::SemiBold, 17, 153, 90, "UNIVERSAL LAUNCHER", kSand, 4.5f);
    const std::string ver = "v" + catalog_.launcherVersion();
    const float vw = measure(FontId::SemiBold, 14, ver) + 16;
    gfx_.rect(R(153 + subW + 12, 89, vw, 22), withAlpha(kOxblood, 0.85f), 11 * s_);
    text(FontId::SemiBold, 14, 153 + subW + 20, 91, ver, kCream);
    hits_.push_back({R(153 + subW + 12, 89, vw, 22), kHitUpdates, -1});  // the Updates menu (GDUL-031)

    float mx = -1, my = -1;
    SDL_GetMouseState(&mx, &my);
    const float density = SDL_GetWindowPixelDensity(window_);
    mx *= density, my *= density;
    const bool modal = dialog_.open || dragging_;

    // The project's website: a pill in the badge's oxblood, with a globe.
    const std::string label = "OFFICIAL SITE", url = "guitardreamengine.com", arrow = "\xC2\xBB";
    const float lw = measure(FontId::SemiBold, 12, label, 2.f), uw = measure(FontId::SemiBold, 17, url), aw = measure(FontId::SemiBold, 17, arrow);
    const float pw = 16 + 20 + 10 + lw + 12 + uw + 8 + aw + 16, px = W_ - kMargin - pw, py = 18;
    const Rect pill = R(px, py, pw, 36);
    const bool hover = !modal && pill.contains(mx, my);
    const float pulse = 0.5f + 0.5f * float(std::sin(now_ * 2.2));
    gfx_.glow(pill, withAlpha(kOxblood, hover ? 0.7f : 0.28f + 0.12f * pulse), 18 * s_, (hover ? 22.f : 16.f) * s_);
    gfx_.rect(pill, hover ? rgb(0x8a2a2c) : rgb(0x5a1d20, 0.95f), hover ? rgb(0x4a1416) : rgb(0x2c1013, 0.95f), 18 * s_);
    gfx_.ring(pill, withAlpha(hover ? kCream : kSand, hover ? 0.85f : 0.45f), 18 * s_, 1.2f * s_);
    // The globe: a ring, its equator and a meridian.
    const float gx = px + 16, gy = py + 8;
    const Color gc = hover ? kCream : kSand;
    gfx_.ring(R(gx, gy, 20, 20), gc, 10 * s_, 1.6f * s_);
    gfx_.ring(R(gx + 5.5f, gy, 9, 20), withAlpha(gc, 0.85f), 4.5f * s_, 1.3f * s_);
    gfx_.rect(R(gx + 1, gy + 9.3f, 18, 1.4f), withAlpha(gc, 0.85f));
    float tx = gx + 20 + 10;
    tx += text(FontId::SemiBold, 12, tx, py + 11, label, withAlpha(kSand, hover ? 1.f : 0.85f), 2.f) + 12;
    tx += text(FontId::SemiBold, 17, tx, py + 7, url, kCream) + 8;
    text(FontId::SemiBold, 17, tx + (hover ? 3.f : 0.f), py + 7, arrow, hover ? kCream : kSand);
    hits_.push_back({pill, kHitWebsite, -1});

    // Full-screen / windowed toggle: a small square button left of the site
    // pill (desktop only; Big Picture is always full screen, Android has no
    // window mode).
#if !defined(__ANDROID__)
    if (!platform::bigPicture()) {
        const float fs = 36, fx = px - 12 - fs, fy = py, cx = fx + fs / 2, cy = fy + fs / 2;
        const Rect fb = R(fx, fy, fs, fs);
        const bool fhover = !modal && fb.contains(mx, my);
        gfx_.rect(fb, fhover ? rgb(0x3a3a44) : rgb(0x2a2a30, 0.95f), fhover ? rgb(0x202026) : rgb(0x161619, 0.95f), 10 * s_);
        gfx_.ring(fb, withAlpha(fhover ? kCream : kSand, fhover ? 0.85f : 0.45f), 10 * s_, 1.2f * s_);
        const Color ic = fhover ? kCream : kSand;
        if (userFullscreen_) {  // in full screen now: the icon is a window (go windowed)
            gfx_.ring(R(cx - 7, cy - 5, 14, 10), ic, 3 * s_, 1.4f * s_);
            gfx_.rect(R(cx - 7, cy - 5, 14, 3), ic, ic, 1.5f * s_);  // its title bar
        } else {  // windowed now: the icon is a full frame (go full screen)
            gfx_.ring(R(cx - 8, cy - 6, 16, 12), ic, 3 * s_, 1.4f * s_);
        }
        hits_.push_back({fb, kHitFullscreen, -1});
    }
#endif

    int highlighted = 0;
    for (const Game& g : games) highlighted += g.playable() ? 1 : 0;
    const std::string l1 = std::to_string(highlighted) + " of " + std::to_string(games.size()) + " games ready to rock";
    text(FontId::SemiBold, 20, W_ - kMargin - measure(FontId::SemiBold, 20, l1), 64, l1, kCream);
    // How many discs were found, and a link to where the launcher looked.
    const int discs = catalog_.discsFound();
    const std::string found = discs == 0 ? "No disc images found" : std::to_string(discs) + (discs == 1 ? " disc image found" : " disc images found");
    const std::string where = discs == 0 ? "where we looked" : "folders";
    const float fw = measure(FontId::Regular, 16, found + kDot), ww = measure(FontId::SemiBold, 16, where);
    const float lx = W_ - kMargin - fw - ww;
    const Rect link = R(lx - 4, 88, fw + ww + 8, 24);
    const bool hl = !modal && link.contains(mx, my);
    text(FontId::Regular, 16, lx, 90, found + kDot, discs == 0 ? fretColor(Fret::Yellow) : kDim);
    text(FontId::SemiBold, 16, lx + fw, 90, where, hl ? kCream : kSand);
    gfx_.rect(R(lx + fw, 110, ww, 1.2f), withAlpha(hl ? kCream : kSand, hl ? 0.9f : 0.5f));
    hits_.push_back({link, kHitDiscs, -1});

    drawDreamForge();
    drawColumnHeaders();
}

// The disabled "Dream forge engine" button (header, right-aligned). Hovering it
// shows a "Coming soon!" tooltip; it is never selectable.
void Launcher::drawDreamForge() {
    const std::string label = "DREAM FORGE ENGINE";
    const float px = 14, lw = measure(FontId::SemiBold, px, label, 1.5f);
    const float w = 18 + 22 + 10 + lw + 18, x = W_ - kMargin - w, y = 118, h = 34;
    const Rect r = R(x, y, w, h);
    const bool hov = hovering(r);
    gfx_.rect(r, rgb(0x1c1a20, 0.9f), rgb(0x161418, 0.9f), 10 * s_);
    gfx_.ring(r, withAlpha(kMuted, hov ? 0.6f : 0.4f), 10 * s_, 1.2f * s_);
    const Color ic = withAlpha(fretColor(Fret::Yellow), hov ? 0.75f : 0.5f);  // yellow(handyman), dimmed (disabled)
    icon(gfx::icon::kHandyman, x + 18 + 11, y + h / 2, 22, ic);
    text(FontId::SemiBold, px, x + 18 + 22 + 10, y + (h - gfx_.lineHeight(FontId::SemiBold, px * s_) / s_) / 2, label,
         withAlpha(kSand, hov ? 0.8f : 0.55f), 1.5f);
    if (hov) tooltip_ = "Coming soon!", tooltipAt_ = r;
    hits_.push_back({r, kHitDreamForge, -1});
}

// The table's column headers and the rule under the header (owner 11-22 §2).
void Launcher::drawColumnHeaders() {
    const float hy = 158;
    text(FontId::SemiBold, 13, lPlayX_, hy, "GAME NAME", kMuted, 2.0f);
    static const char* kHeads[kColumns] = {"% PLAY", "ADDITIVE MUSICS", "EXPLORE FILES", "MOD MENU", "LOAD ISO"};
    for (int i = 0; i < kColumns; ++i) {
        const float w = measure(FontId::SemiBold, 11, kHeads[i], 1.2f);
        text(FontId::SemiBold, 11, lColCx_[i] - hscroll_ - w / 2, hy + 1, kHeads[i], kMuted, 1.2f);
    }
    gfx_.rect(R(kMargin, 176, lListRight_ - kMargin, 1), withAlpha(kCream, 0.12f));
    gfx_.rect(R(kMargin, 175, 140, 3), kOxblood, 1.5f * s_);
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
    // Horizontal scroll (engages only when the table is wider than its viewport).
    const float hView = lListRight_ - kMargin;
    hscrollTarget_ = std::clamp(hscrollTarget_, 0.f, std::max(0.f, tableW_ - hView));
    hscroll_ += (hscrollTarget_ - hscroll_) * 0.25f;
    if (std::abs(hscrollTarget_ - hscroll_) < 0.5f) hscroll_ = hscrollTarget_;

    const Rect clip = R(kMargin - 2, top - 10, lListRight_ - kMargin + 4, view + 20);
    gfx_.clip(&clip);
    for (size_t i = 0; i < games.size(); ++i) {
        const float y = top + float(i) * (kRowH + kRowGap) - scroll_;
        if (y + kRowH < top - 20 || y > bottom + 20) continue;
        drawRow(int(i), y);
    }
    gfx_.clip(nullptr);
    // Hits outside the list's view do not count.
    hits_.erase(std::remove_if(hits_.begin(), hits_.end(),
                               [&](const Hit& h) {
                                   return h.row >= 0 && (h.r.y + h.r.h < top * s_ || h.r.y > bottom * s_);
                               }),
                hits_.end());
    // Scroll hints: soft fades where rows continue.
    if (scroll_ > 1) gfx_.rect(R(kMargin, top - 10, lListRight_ - kMargin, 22), rgb(0x100d12, 0.9f), rgb(0x100d12, 0));
    if (scroll_ < content - view - 1) gfx_.rect(R(kMargin, bottom - 12, lListRight_ - kMargin, 22), rgb(0x0a090b, 0), rgb(0x0a090b, 0.9f));
    drawScrollbars(top, bottom, view, content);
}

// Sticky vertical and horizontal scrollbars (owner 11-22 §2). The vertical one
// shows whenever the list overflows; the horizontal one only when the table is
// wider than its viewport.
void Launcher::drawScrollbars(float top, float bottom, float view, float content) {
    vRange_ = std::max(0.f, content - view);
    if (vRange_ > 1) {
        const float trackTop = top, trackH = bottom - top;
        const float thumbH = std::max(40.f, trackH * view / content);
        const float thumbY = trackTop + (trackH - thumbH) * (content > view ? scroll_ / (content - view) : 0);
        const Rect track = R(lScrollX_ - 5, trackTop, 10, trackH);
        gfx_.rect(track, withAlpha(kCream, 0.05f), 5 * s_);
        vThumb_ = R(lScrollX_ - 5, thumbY, 10, thumbH);
        const bool hov = hovering(vThumb_) || vbarDrag_;
        gfx_.rect(vThumb_, withAlpha(kSand, hov ? 0.85f : 0.5f), 5 * s_);
        vTrackY_ = trackTop * s_, vTrackH_ = trackH * s_, vThumbH_ = thumbH * s_;
    } else {
        vThumb_ = {};
    }
    const float hView = lListRight_ - kMargin;
    hRange_ = std::max(0.f, tableW_ - hView);
    const float barY = bottom + 2;
    if (hRange_ > 1) {
        const float trackW = hView, trackX = kMargin;
        const float thumbW = std::max(40.f, trackW * hView / tableW_);
        const float thumbX = trackX + (trackW - thumbW) * (hscroll_ / hRange_);
        gfx_.rect(R(trackX, barY, trackW, 8), withAlpha(kCream, 0.05f), 4 * s_);
        hThumb_ = R(thumbX, barY, thumbW, 8);
        const bool hov = hovering(hThumb_) || hbarDrag_;
        gfx_.rect(hThumb_, withAlpha(kSand, hov ? 0.85f : 0.5f), 4 * s_);
        hTrackX_ = trackX * s_, hTrackW_ = trackW * s_, hThumbW_ = thumbW * s_;
    } else {
        hThumb_ = {};
    }
}

void Launcher::drawRow(int index, float y) {
    const Game& g = catalog_.games()[size_t(index)];
    const bool focused = index == focusRow_ && !dialog_.open;
    const bool lit = g.highlighted();
    const float dx = -hscroll_;  // horizontal scroll offset (usually 0)
    const Rect row = R(kMargin, y, lListRight_ - kMargin, kRowH);
    hits_.push_back({row, index, -1});

    // The card.
    if (focused) gfx_.glow(row, withAlpha(lit ? fretColor(Fret::Green) : kSand, 0.22f), 12 * s_, 22 * s_);
    if (lit) gfx_.rect(row, rgb(0x1e2a21, 0.96f), rgb(0x141b16, 0.96f), 12 * s_);
    else if (g.ready) gfx_.rect(row, rgb(0x1d1a20, 0.94f), rgb(0x151318, 0.94f), 12 * s_);
    else gfx_.rect(row, rgb(0x141216, 0.9f), rgb(0x100e12, 0.9f), 12 * s_);
    gfx_.ring(row, focused ? withAlpha(kCream, 0.75f) : withAlpha(kCream, lit ? 0.14f : 0.06f), 12 * s_, (focused ? 2.f : 1.f) * s_);

    const float cy = y + kRowH / 2;
    // Play, before the name — a wide, prominent button (owner 11-23).
    drawIconButton(R(lPlayX_ + dx, cy - lPlayH_ / 2, lPlayW_, lPlayH_), gfx::icon::kPlayCircle, fretColor(Fret::Green),
                   focused && focusCol_ == kPlay, enabled(index, kPlay), index, kPlay);

    // Status light + name + details.
    const float nx = lNameX_ + dx;
    const Color led = lit && g.hasModule ? fretColor(Fret::Green) : g.ready ? fretColor(Fret::Yellow) : kMuted;
    gfx_.circle((nx + 4) * s_, (y + 24) * s_, 4.5f * s_, led);
    const Color nameColor = lit ? kCream : g.ready ? rgb(0xd6cab3) : rgb(0x8a8174);
    const float idW = text(FontId::SemiBold, 16, nx + 14, y + 13, g.id + "/", lit ? kSand : kDim);
    text(FontId::SemiBold, 23, nx + 14 + idW, y + 7, g.title(), nameColor);
    std::string details = g.basedOn.empty() ? g.platforms : g.basedOn;
    details += kDot + year(g.launch) + kDot + "v" + g.version;
    if (!g.iso.empty())
        details += kDot + g.isoName + (g.isoSource == Game::IsoSource::Saved ? " (saved)" : " (found)");
    else if (g.ready) details += kDot + "no disc yet: load one or drop it here";
    else details += kDot + "coming soon";
    if (g.ready && !g.hasModule) details += kDot + "module not built";
    const float nameMax = lColCx_[0] - lColW_ / 2 - (nx + 14) - 12;  // up to the first column
    const std::string shown = gfx_.elide(FontId::Regular, 14 * s_, details, nameMax * s_);
    text(FontId::Regular, 14, nx + 14, y + 40, shown, lit ? rgb(0xa9b8a6) : kDim);

    // Column 0: the playability % icon (owner 11-22 §2), with the number below.
    const PctIcon pi = pctIcon(g.completeness);
    const float c0 = lColCx_[0] + dx;
    icon(pi.cp, c0, cy - 7, 30, g.ready ? pi.c : withAlpha(pi.c, 0.4f));
    text(FontId::SemiBold, 12, c0 - measure(FontId::SemiBold, 12, percent(g.completeness)) / 2, cy + 12,
         percent(g.completeness), g.ready ? kSand : kDim);

    // The right-hand action buttons, in the order the enum lists them: Additive,
    // Explore, Mod, Load ISO (owner 11-23). Each draws at its own column index.
    const bool hasIso = !g.iso.empty();

    // Additive musics (neon red when the game is playable).
    drawIconButton(R(lColCx_[kAdditive] + dx - 23, cy - 23, 46, 46), gfx::icon::kMusicNoteAdd, kNeonRedC,
                   focused && focusCol_ == kAdditive, enabled(index, kAdditive), index, kAdditive);

    // Explore files.
    drawIconButton(R(lColCx_[kExplore] + dx - 23, cy - 23, 46, 46), gfx::icon::kDatabaseSearch, fretColor(Fret::Blue),
                   focused && focusCol_ == kExplore, enabled(index, kExplore), index, kExplore);

    // Mod menu — disabled placeholder; hovering shows "Coming soon".
    const Rect modR = R(lColCx_[kMod] + dx - 23, cy - 23, 46, 46);
    drawIconButton(modR, gfx::icon::kAddShoppingCart, kNeonPurpleC, focused && focusCol_ == kMod, false, index, kMod);
    if (hovering(modR)) tooltip_ = "Coming soon", tooltipAt_ = modR;

    // Load ISO — album when a disc is set (grey), disc_full when not (yellow for
    // every not-found game).
    const Rect loadR = R(lColCx_[kLoad] + dx - 23, cy - 23, 46, 46);
    drawIconButton(loadR, hasIso ? gfx::icon::kAlbum : gfx::icon::kDiscFull, fretColor(Fret::Yellow),
                   focused && focusCol_ == kLoad, !hasIso /* coloured when no disc */, index, kLoad);
}

// An icon-only button for a row column. `tint` is its colour when enabled.
void Launcher::drawIconButton(const Rect& r, uint32_t cp, Color tint, bool focused, bool on, int row, int col) {
    const float rad = 11 * s_;
    Color bg1, bg2, ring, ic;
    if (focused && on) {
        gfx_.glow(r, withAlpha(tint, 0.4f), rad, 12 * s_);
        bg1 = gfx::mix(tint, kCream, 0.2f), bg2 = gfx::mix(tint, kInk, 0.2f);
        ring = withAlpha(kCream, 0.7f), ic = kInk;
    } else if (focused) {
        bg1 = rgb(0x2a262d), bg2 = rgb(0x211e24), ring = withAlpha(kCream, 0.5f), ic = withAlpha(kCream, 0.85f);
    } else if (on) {
        bg1 = rgb(0x26222a, 0.95f), bg2 = rgb(0x1c191f, 0.95f), ring = withAlpha(tint, 0.5f), ic = tint;
    } else {
        bg1 = rgb(0x1a171d, 0.75f), bg2 = rgb(0x161418, 0.75f), ring = withAlpha(kCream, 0.07f), ic = withAlpha(kMuted, 0.6f);
    }
    gfx_.rect(r, bg1, bg2, rad);
    gfx_.ring(r, ring, rad, (focused ? 1.6f : 1.2f) * s_);
    icon(cp, (r.x + r.w / 2) / s_, (r.y + r.h / 2) / s_, 26, ic);
    hits_.push_back({r, row, col});
}

bool Launcher::hovering(const Rect& rPx) const {
    if (dialog_.open || dragging_) return false;
    if (!(SDL_GetWindowFlags(window_) & SDL_WINDOW_MOUSE_FOCUS)) return false;
    float mx = -1, my = -1;
    SDL_GetMouseState(&mx, &my);
    const float d = SDL_GetWindowPixelDensity(window_);
    return rPx.contains(mx * d, my * d);
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
    const float cy = y + kFooterH / 2;
    // Hints only: everything is clicked (or reached with the keys, a gamepad or a guitar).
    const std::string drop = "Drop a disc image anywhere to identify it";
    text(FontId::Regular, 15, kMargin, cy - 10, drop, kDim);
    const std::string keys = "Mouse" + kDot + "keyboard" + kDot + "gamepad" + kDot + "guitar";
    text(FontId::Regular, 15, W_ - kMargin - measure(FontId::Regular, 15, keys), cy - 10, keys, kMuted);
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

void Launcher::drawLoading(const Game& g, const std::string& what) {
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
        const std::string t = what + " " + g.title() + "\xE2\x80\xA6";
        text(FontId::Title, 52, (W_ - measure(FontId::Title, 52, t)) / 2, H_ / 2 + 40, t, kCream);
        const std::string sub = g.id + kDot + g.isoName;
        text(FontId::Regular, 18, (W_ - measure(FontId::Regular, 18, sub)) / 2, H_ / 2 + 112, sub, kSand);
        gfx_.end();
        SDL_GL_SwapWindow(window_);
    }
}
