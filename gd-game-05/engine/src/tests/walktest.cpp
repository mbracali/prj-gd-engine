// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/walktest.h"

#include <set>
#include <SDL3/SDL.h>
#if defined(__linux__)
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <sstream>

#include "data/game_data.h"
#include "data/json.h"
#include "render/canvas.h"
#include "render/device.h"
#include "scene/bitmap.h"
#include "game/player.h"
#include "game/song_db.h"
#include "input/input.h"
#include "render/scene_gpu.h"
#include "render/ui_view.h"
#include "scene/milo.h"
#include "scene/ui_objects.h"
#include "script/tasks.h"
#include "ui/campaign.h"
#include "ui/char_select.h"
#include "app.h"
#include "ui/enhanced_panel.h"
#include "ui/natives.h"
#include "platform/paths.h"
#include "ui/rb_meta.h"
#include "ui/ui_objects.h"
#include "ui/ui_host.h"
#include "ui/ui_manager.h"

extern const unsigned char kUiStubsTxt[];
extern const std::size_t kUiStubsTxtSize;

namespace {

constexpr int kX = ui::kPad_X, kTri = ui::kPad_Tri, kStart = ui::kPad_Start, kUp = ui::kPad_DUp, kDown = ui::kPad_DDown;
constexpr int kLeft = ui::kPad_DLeft, kRight = ui::kPad_DRight;
// long_cheats.dta's frets (HX_XBOX): G_YELLOW, G_BLUE, G_ORANGE.
constexpr int kYellow = ui::kPad_Circle, kBlue = ui::kPad_Square, kOrange = ui::kPad_L1;

// The STUB whitelist (tests/ui_stubs.txt): prefixes, `*` at the end matches anything.
std::vector<std::string> whitelist() {
    std::vector<std::string> out;
    std::istringstream in(std::string(reinterpret_cast<const char*>(kUiStubsTxt), kUiStubsTxtSize));
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty() && line[0] != '#') out.push_back(line);
    }
    return out;
}

bool whitelisted(const std::string& key, const std::vector<std::string>& wl) {
    for (const std::string& w : wl) {
        if (!w.empty() && w.back() == '*') {
            if (key.compare(0, w.size() - 1, w, 0, w.size() - 1) == 0) return true;
        } else if (key == w) {
            return true;
        }
    }
    return false;
}

// GD05-061: the device walks' controller as a kernel input device
// (GD05_WALK_EVDEV): "uinput" makes one that is the owner's Harmonix Rock
// Band Guitar as the kernel shows it (gd-game-05/tools/uinput_guitar.c:
// name, 1bad:0002 v0103, 11 buttons, 6 axes, a hat), a /dev/input/eventN
// path writes into that device (the real guitar's own node: evdev passes
// written events to every reader as the device's). Either way SDL reads it
// as the real controller (evdev, its real GUID), in the launcher too;
// without it the walks use an SDL virtual joystick.
struct EvdevOut {
    int fd = -1;
    bool created = false;
    std::string node;  // /dev/input/eventN the game reads
    ~EvdevOut() { close(); }
    void close() {
#if defined(__linux__)
        if (fd >= 0) {
            if (created) ioctl(fd, UI_DEV_DESTROY);
            ::close(fd);
        }
#endif
        fd = -1, created = false, node.clear();
    }
};
EvdevOut g_evdev;

#if defined(__linux__)
void evdevEmit(int type, int code, int value) {
    input_event e[2];
    std::memset(e, 0, sizeof e);
    e[0].type = Uint16(type), e[0].code = Uint16(code), e[0].value = value;
    e[1].type = EV_SYN, e[1].code = SYN_REPORT;
    if (write(g_evdev.fd, e, sizeof e) != ssize_t(sizeof e)) SDL_Log("walk: evdev write: %s", std::strerror(errno));
}
// SDL's evdev numbering: buttons and axes in code order.
constexpr int kEvdevButtons[11] = {BTN_A, BTN_B, BTN_X, BTN_Y, BTN_TL, BTN_TR, BTN_SELECT, BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR};
constexpr int kEvdevAxes[6] = {ABS_X, ABS_Y, ABS_Z, ABS_RX, ABS_RY, ABS_RZ};
#endif

// Opens GD05_WALK_EVDEV; "" when the walk is to use a virtual joystick.
std::string evdevOpen(const std::string& spec) {
#if defined(__linux__)
    g_evdev.close();
    if (spec != "uinput") {
        g_evdev.fd = ::open(spec.c_str(), O_WRONLY | O_CLOEXEC);
        if (g_evdev.fd < 0) return "cannot open " + spec + ": " + std::strerror(errno);
        g_evdev.node = spec;
    } else {
        g_evdev.fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        if (g_evdev.fd < 0) return std::string("cannot open /dev/uinput: ") + std::strerror(errno);
        ioctl(g_evdev.fd, UI_SET_EVBIT, EV_KEY);
        ioctl(g_evdev.fd, UI_SET_EVBIT, EV_ABS);
        for (const int b : kEvdevButtons) ioctl(g_evdev.fd, UI_SET_KEYBIT, b);
        // The owner's guitar's ranges and rest values (EVIOCGABS of his event9).
        uinput_abs_setup abs[] = {
            {ABS_X, {0, -32768, 32767, 16, 128, 0}},        {ABS_Y, {0, -32768, 32767, 16, 128, 0}},
            {ABS_Z, {229, 0, 255, 0, 0, 0}},                {ABS_RX, {-32768, -32768, 32767, 16, 128, 0}},
            {ABS_RY, {-32768, -32768, 32767, 16, 128, 0}},  {ABS_RZ, {0, 0, 255, 0, 0, 0}},
            {ABS_HAT0X, {0, -1, 1, 0, 0, 0}},               {ABS_HAT0Y, {0, -1, 1, 0, 0, 0}},
        };
        for (const uinput_abs_setup& a : abs) ioctl(g_evdev.fd, UI_SET_ABSBIT, a.code), ioctl(g_evdev.fd, UI_ABS_SETUP, &a);
        uinput_setup setup;
        std::memset(&setup, 0, sizeof setup);
        setup.id.bustype = BUS_USB, setup.id.vendor = 0x1bad, setup.id.product = 0x0002, setup.id.version = 0x0103;
        std::snprintf(setup.name, sizeof setup.name, "Harmonix Rock Band Guitar");
        if (ioctl(g_evdev.fd, UI_DEV_SETUP, &setup) < 0 || ioctl(g_evdev.fd, UI_DEV_CREATE) < 0) {
            const std::string why = std::strerror(errno);
            g_evdev.close();
            return "uinput: " + why;
        }
        g_evdev.created = true;
        char sys[64] = "";
        ioctl(g_evdev.fd, UI_GET_SYSNAME(sizeof sys), sys);
        std::error_code ec;
        for (const auto& d : std::filesystem::directory_iterator(std::string("/sys/devices/virtual/input/") + sys, ec))
            if (d.path().filename().string().rfind("event", 0) == 0) g_evdev.node = "/dev/input/" + d.path().filename().string();
        if (g_evdev.node.empty()) return std::string("uinput: no event node under ") + sys;
    }
    return "";
#else
    (void)spec;
    return "GD05_WALK_EVDEV is Linux only";
#endif
}

}  // namespace

// The walk's controller: the SDL virtual joystick, or the kernel device.
static void devButton(SDL_Joystick* j, int button, bool on) {
#if defined(__linux__)
    if (g_evdev.fd >= 0) {
        if (button >= 0 && button < 11) evdevEmit(EV_KEY, kEvdevButtons[button], on ? 1 : 0);
        return;
    }
#endif
    SDL_SetJoystickVirtualButton(j, button, on);
}
static void devHat(SDL_Joystick* j, int hat, Uint8 v) {
#if defined(__linux__)
    if (g_evdev.fd >= 0) {
        evdevEmit(EV_ABS, ABS_HAT0X, (v & SDL_HAT_RIGHT) ? 1 : (v & SDL_HAT_LEFT) ? -1 : 0);
        evdevEmit(EV_ABS, ABS_HAT0Y, (v & SDL_HAT_DOWN) ? 1 : (v & SDL_HAT_UP) ? -1 : 0);
        return;
    }
#endif
    SDL_SetJoystickVirtualHat(j, hat, v);
}
static void devAxis(SDL_Joystick* j, int axis, Sint16 v) {
#if defined(__linux__)
    if (g_evdev.fd >= 0) {
        if (axis < 0 || axis >= 6) return;
        const bool trigger = axis == 2 || axis == 5;  // 0..255 on this device
        evdevEmit(EV_ABS, kEvdevAxes[axis], trigger ? (int(v) + 32768) * 255 / 65535 : int(v));
        return;
    }
#endif
    SDL_SetJoystickVirtualAxis(j, axis, v);
}

static void measureTextColors(ui::UiManager& ui, const std::string& tag, std::vector<std::string>& bad);
static void measureRefColors(ui::UiManager& ui, const std::string& tag, std::vector<std::string>& bad);

WalkDriver::WalkDriver(const std::string& scenario) : scenario_(scenario) {
    textColors_ = scenario == "rb_text_colors" || SDL_getenv("GD05_TEXTCOLORS");
    using S = Step;
    auto screen = [&](const char* s, double timeout = 20000) { steps_.push_back({S::Screen, s, 0, timeout}); };
    auto press = [&](int b) { steps_.push_back({S::Press, "", b, 0}); };
    auto pressP = [&](int b, int player) { steps_.push_back({S::Press, "", b, 0, player}); };
    auto wait = [&](double ms) { steps_.push_back({S::Wait, "", 0, ms}); };
    // Waits measured from a mark, not from the step before: presses on a
    // beat that do not drift by a frame or two each (the calibration).
    auto mark = [&] { steps_.push_back({S::Wait, "mark", 0, 0}); };
    auto until = [&](double ms) { steps_.push_back({S::Wait, "until", 0, ms}); };
    // A real key press, through SDL's events and the input layer (as GD05_KEYS' kReturn).
    auto key = [&](SDL_Keycode k) { steps_.push_back({S::Key, "", int(k), 0}); };
    // A key held down (down) or let go (!down) on its own.
    auto keyHold = [&](SDL_Keycode k, bool down) {
        Step st{S::Key, "", int(k), 0};
        st.x = down ? 1.0f : 2.0f;
        steps_.push_back(st);
    };
    auto check = [&](const char* what, double timeout = 0) { steps_.push_back({S::Check, what, 0, timeout}); };
    // The mouse (UiManager::mouse): at a point, or on a component of the focus panel.
    auto mouseAt = [&](float x, float y, int button) {
        Step st{S::Mouse, "", button, 0};
        st.x = x, st.y = y;
        steps_.push_back(st);
    };
    auto mouseOn = [&](const char* component, int button) { steps_.push_back({S::Mouse, component, button, 0}); };
    // Each screen: wait until it is up and its transition is over, then a moment.
    auto at = [&](const char* s, double timeout = 20000) {
        screen(s, timeout);
        wait(400);
    };
    auto quickplaySong = [&](int downs) {
        at("main_screen");
        press(kX);  // QUICK PLAY (main_quickspin.btn, the focus)
        at("qp_selsong_screen");
        check("qp_footer");
        for (int i = 0; i < downs; ++i) press(kDown), wait(150);
        press(kX);
        at("qp_diff_screen");
        press(kUp);  // Medium (the last difficulty) -> Easy
        wait(150);
        press(kX);
        at("loading_screen");
        at("game_screen", 30000);
    };
    // Rock Band's way into a quick play song (ui/main.dta, selsong.dta,
    // seldiff.dta, loading.dta) [C data]: the intro movie, the title, Solo,
    // Quickplay, the song list's first song (random_song), the difficulty
    // the slot focuses, then the loading screens.
    auto rbQuickplay = [&] {
        // The walks skip the intro movie (GD05_SKIP_MOVIES): straight to the title.
        at("splash_screen", 30000);
        // Every song listed (the long cheat): the walks were written against
        // the whole list; a new profile has ten (rb_qp_locked, GD05-073).
        check("unlock_all_songs");
        press(kX);
        at("main_screen");
        press(kX);  // SOLO (solo.btn, the focus)
        at("main_solomode_screen");
        press(kX);  // QUICKPLAY
        at("qp_selsong_screen");
        wait(500);
        press(kX);  // the first entry
        at("qp_seldiff_solo_screen");
        press(kX);  // the guitar slot's part (guitar.btn): RB asks guitar or bass first
        wait(1500);  // the button's selecting animation
        press(kX);  // the focused difficulty: ready
        at("game_screen", 60000);  // through loading_screen and the loading tips
    };
    if (scenario == "rb_boot") {
        // Rock Band's boot (ui/main.dta, docs/frontend.md): the intro movie,
        // the title over the menus' background video (movie_panel,
        // rb_video*.bik), the main menu over it.
        // The walks skip the intro movie (GD05_SKIP_MOVIES): straight to the title.
        at("splash_screen", 30000);
        check("probe_black");
        wait(1500);
        check("counter:menu_movie_frames:20");
        press(kX);
        at("main_screen");
        wait(1500);
        check("counter:menu_movie_frames:60");
        check("counter_max:menu_venue_sounds:0");
        press(kX);  // SOLO
        at("main_solomode_screen");
        press(kX);  // QUICKPLAY
        at("qp_selsong_screen");
        wait(800);
        check("click_helpbar_back");
        at("main_solomode_screen");
        wait(1000);
        check("no_black");  // title > main > solo > song list > back: no black frame
    } else if (scenario == "rb_songlist_text") {
        // GD05-063, owner report: "the list of musics have wrong sizes for
        // the letters". Every text the song list draws (into its RenderDir's
        // texture) against the size its label's data gives through RB's
        // fit (songlist_text): scrolled, so headings and long names show.
        at("splash_screen", 30000);
        check("unlock_all_songs");  // the list longer than the ten a new profile has (rb_qp_locked)
        wait(1500);
        press(kX);
        at("main_screen");
        wait(1500);
        press(kX);  // SOLO
        at("main_solomode_screen");
        wait(800);
        press(kX);  // QUICKPLAY
        at("qp_selsong_screen");
        wait(2500);
        check("songlist_text:top");
        for (int i = 0; i < 9; ++i) press(kDown), wait(250);
        wait(1200);
        check("songlist_text:down9");
        for (int i = 0; i < 20; ++i) press(kDown), wait(150);
        wait(1200);
        check("songlist_text:down29");
    } else if (scenario == "rb_songlist_highlight") {
        // GD05-073, owner (G06 6): "the song list's yellow selector
        // highlights more than one song". At every step down the list
        // (headings, the scroll, the end) the highlight's quad covers the
        // selected row's name and no other row's name or heading
        // (songlist_highlight: the drawn texts' middles against the quad).
        // GD05_SONGLIST_LEGACY=1 (rows without RB's song_select_gap) fails.
        at("splash_screen", 30000);
        check("unlock_all_songs");  // the list longer than the ten a new profile has (rb_qp_locked)
        press(kX);
        at("main_screen");
        press(kX);  // SOLO
        at("main_solomode_screen");
        press(kX);  // QUICKPLAY
        at("qp_selsong_screen");
        wait(1500);
        check("songlist_highlight");
        for (int i = 0; i < 24; ++i) {
            press(kDown), wait(300);
            check("songlist_highlight");
        }
        for (int i = 0; i < 6; ++i) {
            press(kUp), wait(300);
            check("songlist_highlight");
        }
    } else if (scenario == "rb_stutter") {
        // GD05-073, owner (G06 6): "micro-stutters when a song preview
        // starts and when a song starts". Each preview the song list asks
        // for while it scrolls (the window of 30 frames after each) and the
        // song's first 240 frames against the run's median frame (ratios:
        // load on the machine moves both; the median of the previews'
        // windows), and no texture or mesh sent to the GPU once the song has
        // begun (each was a stall on the frame that first drew it).
        // GD05_MUSIC_LEGACY=1 / GD05_WARM_LEGACY=1 run the code before.
        endMs_ = 25000;
        at("splash_screen", 30000);
        check("unlock_all_songs");  // the list longer than the ten a new profile has (rb_qp_locked)
        press(kX);
        at("main_screen");
        press(kX);  // SOLO
        at("main_solomode_screen");
        press(kX);  // QUICKPLAY
        at("qp_selsong_screen");
        wait(1500);
        for (int i = 0; i < 16; ++i) press(kDown), wait(400);
        wait(1500);
        check("counter:frame_marks:preview:12");
        check("counter:music_plays:8");
        check("counter_max:frame_spike:preview:30:40");  // the previews' windows: median of their longest frame <= 4.0 x the median frame
        press(kX);
        at("qp_seldiff_solo_screen");
        press(kX);  // the guitar slot's part
        wait(1500);
        press(kX);  // the focused difficulty: ready
        at("game_screen", 60000);
        wait(12000);
        check("counter:frame_marks:song_begin:1");
        check("counter_max:song_gpu_uploads:0");
        check("counter_max:frame_spike:song_begin!:240:150");  // the song's first 240 frames: longest <= 15 x the median frame
    } else if (scenario == "rb_qp_locked") {
        // GD05-073, owner (G06 6): "Quickplay: clicking a random song selects
        // songs that are not unlocked". The quick play list holds the songs
        // RB's rules make available on a new profile and nothing else
        // (qp_locked: modes.dta's unlock_song_initial_num first songs by the
        // rank of the mode's unlock_song_instrument, no (unlockable TRUE)
        // song), Random Song draws only from them, and the song it starts
        // is one of them. GD05_SONGLIST_LEGACY=1 (every song listed) fails.
        at("splash_screen", 30000);
        press(kX);
        at("main_screen");
        press(kX);  // SOLO
        at("main_solomode_screen");
        press(kX);  // QUICKPLAY
        at("qp_selsong_screen");
        wait(1500);
        check("qp_locked");
        press(kX);  // Random Song (the first row)
        at("qp_seldiff_solo_screen");
        press(kX);  // the guitar slot's part
        wait(1500);
        press(kX);  // the focused difficulty: ready
        at("game_screen", 60000);
        check("qp_locked_song");
    } else if (scenario == "rb_od_keys" || scenario == "rb_od_pad" || scenario == "rb_od_guitar" || scenario == "rb_od_drums" || scenario == "rb_od_vocals") {
        // GD05-073, owner (G06 6): "Overdrive activation may not work:
        // double check and fix". Each way RB deploys it, through the
        // device's own SDL events: the keyboard's Tab (the port's Select /
        // tilt key), an Xbox pad's Back (force_mercury kPad_Select [C]), the
        // owner's Rock Band guitar (1bad:0002) by tilt (axis 4) and by Back,
        // the drummer by the fill's crash (crash_fill_ready_hits, RB
        // 0x824C1530 [C]; the drummer autoplays), the singer by a shout in a
        // gap between phrases (RB 0x82458F70 [C]). Each first with the meter
        // at 45 % (refused: RB deploys at ready_level / deploy_threshold
        // 0.5 [C data]), then at 60 % (deployed), then the drain against
        // band_energy deploy_beats (32 a full meter, RB 0x82445188 [C]).
        const std::string path = scenario.substr(6);
        autoplay_ = path == "drums" || path == "vocals";  // the guitar paths play nothing: the input is theirs
        if (path == "drums" || path == "vocals") SDL_setenv_unsafe("GD05_INSTRUMENT", path == "drums" ? "drum" : "vocals", 1);
        endMs_ = path == "drums" || path == "vocals" ? 120000 : 40000;
        auto checkS = [&](const std::string& what, double timeout = 0) { steps_.push_back({S::Check, what, 0, timeout}); };
        if (path == "pad") checkS("dev_attach 045e:028e", 10000);
        if (path == "guitar") checkS("dev_attach 1bad:0002:rbguitar", 10000);
        at("splash_screen", 30000);
        wait(500);
        key(SDLK_RETURN);
        at("main_screen");
        key(SDLK_RETURN);  // SOLO
        at("main_solomode_screen");
        key(SDLK_RETURN);  // QUICKPLAY
        at("qp_selsong_screen");
        wait(500);
        key(SDLK_RETURN);  // Random Song
        at("qp_seldiff_solo_screen");
        if (path != "drums" && path != "vocals") key(SDLK_RETURN), wait(1500);  // the guitar part
        else wait(1500);
        key(SDLK_RETURN);  // the difficulty
        at("game_screen", 60000);
        wait(4000);  // the intro: the track is out
        if (path == "drums" || path == "vocals") {
            checkS("od_auto:" + path + ":45", 60000);  // refused under half
            checkS("od_auto:" + path + ":60", 90000);  // deployed
            wait(4000);
            check("od_drain");
        } else {
            // The device's press (held 120 ms, as a player taps it).
            auto deployPress = [&](const std::string& how) {
                if (how == "keys") keyHold(SDLK_TAB, true), wait(120), keyHold(SDLK_TAB, false);
                else if (how == "pad") checkS("dev_set b4 1"), wait(120), checkS("dev_set b4 0");  // SDL_GAMEPAD_BUTTON_BACK
                else if (how == "tilt") checkS("dev_axis 4 32767"), wait(120), checkS("dev_axis 4 -32768");  // up, then level (its report's -32768)
                else checkS("dev_set b6 1"), wait(120), checkS("dev_set b6 0");  // the guitar's Back (BTN_SELECT)
            };
            const std::vector<std::string> hows = path == "guitar" ? std::vector<std::string>{"tilt", "select"} : std::vector<std::string>{path};
            for (const std::string& how : hows) {
                check("od_set:45"), wait(300);
                deployPress(how), wait(300);
                check(("od_state:0:" + how).c_str());
                check("od_set:60"), wait(300);
                deployPress(how), wait(300);
                check(("od_state:1:" + how).c_str());
                wait(3000);
                check("od_drain");
                check("od_set:0"), wait(400);
            }
            // crowd_meter.dta's deploy / stop_deploy (band_energy_deploy.trig,
            // the icon's deploy.trig ...) fired with each deploy and its end.
            check("counter:rb_crowd_fx_triggers:2");
        }
    } else if (scenario == "rb_esc3") {
        // Wave 05 (GD05-033, universal-behaviors.md): Esc three times in a
        // row on the main menu quits the game.
        at("splash_screen", 30000);
        wait(1500);
        press(kX);
        at("main_screen");
        wait(1500);
        check("expect_quit");
        for (int i = 0; i < 3; ++i) key(SDLK_ESCAPE), wait(250);
        wait(3000);
        check("quit_missed");
    } else if (scenario == "rb_bright") {
        // GD05 0.0.6 owner report: text and icons darker than RB's. The
        // menus, options, the song list, the difficulty card, the loading
        // screen (LOADING, the band icons) and the pause screen.
        at("splash_screen", 30000);
        wait(1500);
        press(kX);
        at("main_screen");
        wait(1500);
        check("bright:main");
        press(kDown), wait(300), press(kDown), wait(300), press(kDown), wait(300);  // OPTIONS
        press(kX);
        at("options_screen");
        wait(1000);
        check("bright:options");
        press(kX);  // AUDIO SETTINGS: the four sliders (the INSTRUMENTS and BACKGROUND MUSIC ones drew near black)
        at("options_audio_screen");
        wait(1000);
        check("bright:audio");
        press(kTri);
        at("options_screen");
        wait(1000);
        // The other options screens: GAMEPLAY, CHARACTER DATA, CALIBRATE.
        const char* subs[3][2] = {{"options_gameplay_screen", "bright:gameplay"}, {"options_savedata_screen", "bright:savedata"},
                                  {"calibrate_av_chooser_screen", "bright:calibrate"}};
        for (int i = 0; i < 3; ++i) {
            press(kDown), wait(300);
            press(kX);
            at(subs[i][0]);
            wait(1000);
            check(subs[i][1]);
            press(kTri);
            at("options_screen");
            wait(1000);
        }
        press(kTri);
        at("main_screen");
        wait(1500);
        for (int i = 0; i < 3; ++i) press(kUp), wait(300);  // back to SOLO
        press(kX);
        at("main_solomode_screen");
        wait(800);
        check("bright:solo");
        press(kX);  // QUICKPLAY
        at("qp_selsong_screen");
        wait(1500);
        check("bright:songlist");
        press(kX);
        at("qp_seldiff_solo_screen");
        wait(800);
        check("bright:seldiff");
        press(kX);
        wait(1500);
        check("bright:seldiff2");
        press(kX);
        check("bright:loading:loading_panel");  // the loading screens hold their transition: wait on the panel
        at("game_screen", 60000);
        wait(3000);
        key(SDLK_ESCAPE);
        at("pause_screen", 10000);
        wait(800);
        check("bright:pause");
    } else if (scenario == "rb_text_colors") {
        // GD05 owner report (0.0.7): menu text darker than RB's. Every text
        // of every screen on the way (measured once each has rested, plus the
        // textcolors:<tag> states) against RB's colour from the data
        // (measureTextColors): the main menu and its sub-menus, every
        // options screen, the song list, the difficulty, loading, the song's
        // pause. GD05_TEXTCOLORS=1 measures any other walk's screens the same
        // way (band_tour: Create a Band; creator; rock_shop; credits;
        // tutorials; rb_lose: the fail screen; quickplay: the results).
        at("splash_screen", 30000);
        wait(1500);
        press(kX);
        at("main_screen");
        wait(1500);
        check("textcolors:main_solo");
        const char* subs[3][2] = {{"main_multimode_screen", "multi"}, {"training_menu_screen", "training"}, {"options_screen", "options"}};
        for (int i = 0; i < 3; ++i) {
            press(kDown), wait(300);
            check((std::string("textcolors:main_") + subs[i][1]).c_str());
        }
        for (int i = 0; i < 3; ++i) press(kUp), wait(300);
        for (int i = 0; i < 2; ++i) {
            press(kDown), wait(300);
            press(kX);
            at(subs[i][0]);
            wait(1000);
            press(kDown), wait(300);
            check((std::string("textcolors:") + subs[i][1] + "_2nd").c_str());
            press(kTri);
            at("main_screen");
            wait(1000);
        }
        press(kDown), wait(300);  // OPTIONS
        press(kX);
        at("options_screen");
        wait(1000);
        const char* opts[5] = {"options_audio_screen", "options_gameplay_screen", "options_savedata_screen", "calibrate_av_chooser_screen", ""};
        for (int i = 0; i < 4; ++i) {
            if (i) press(kDown), wait(300);
            check((std::string("textcolors:options_") + std::to_string(i)).c_str());
            press(kX);
            at(opts[i]);
            wait(1000);
            press(kDown), wait(300);
            check((std::string("textcolors:") + opts[i] + "_2nd").c_str());
            press(kTri);
            at("options_screen");
            wait(1000);
        }
        press(kTri);
        at("main_screen");
        wait(1500);
        for (int i = 0; i < 3; ++i) press(kUp), wait(300);  // SOLO
        press(kX);
        at("main_solomode_screen");
        wait(800);
        check("refcolors:solo");  // the owner's capture of RB's screen (GD05-062)
        press(kDown), wait(300);
        check("textcolors:solo_2nd");
        check("refcolors:solo_2nd");
        press(kUp), wait(300);
        press(kX);  // QUICKPLAY
        at("qp_selsong_screen");
        wait(2500);
        check("textcolors:songlist");
        press(kX);
        at("qp_seldiff_solo_screen");
        wait(800);
        press(kX);
        wait(1500);
        check("textcolors:seldiff_diff");
        press(kX);
        check("textcolors:loading:loading_panel");
        at("game_screen", 60000);
        wait(3000);
        key(SDLK_ESCAPE);
        at("pause_screen", 10000);
        wait(1000);
        press(kDown), wait(300);
        check("textcolors:pause_2nd");
    } else if (scenario == "rb_mouse") {
        // Wave 05 (GD05-036 / 037): the port's GAMEPADS button (bottom left)
        // stays on the main menu and every main menu button opens its menu
        // on a click, also after coming back.
        at("splash_screen", 30000);
        wait(1500);
        press(kX);
        at("main_screen");
        wait(1500);
        check("gamepads_button");
        mouseOn("solo.btn", 0), wait(200);
        mouseOn("solo.btn", 1);
        at("main_solomode_screen");
        wait(800);
        check("gamepads_button");  // GD05-048: on every sub-menu too
        press(kTri);
        at("main_screen");
        wait(1500);
        check("gamepads_button");
        mouseOn("multi.btn", 0), wait(200);
        mouseOn("multi.btn", 1);
        at("main_multimode_screen");
        wait(1500);
        check("gamepads_button");
        // From the sub-menu, a click on another main menu button opens its menu.
        mouseOn("tutorial.btn", 1);
        at("training_menu_screen");
        wait(800);
        check("gamepads_button");
        press(kTri);
        at("main_screen");
        wait(1500);
        mouseOn("options.btn", 0), wait(200);
        mouseOn("options.btn", 1);
        at("options_screen");
        wait(800);
        check("gamepads_button");
        mouseOn("port_enhanced.btn", 1);
        at("enhanced_options_screen");
        wait(800);
        press(kTri);
        at("options_screen");
        wait(800);
        press(kTri);
        at("main_screen");
        wait(1500);
        check("gamepads_button");
        check("click_gamepads_button");
        at("gamepads_screen", 3000);
        wait(800);
        // GD05-048: GAMEPADS from a sub-menu, back to that sub-menu.
        press(kTri);
        at("main_screen");
        wait(1500);
        mouseOn("solo.btn", 1);
        at("main_solomode_screen");
        wait(800);
        check("click_gamepads_button");
        at("gamepads_screen", 3000);
        wait(800);
        press(kTri);
        at("main_solomode_screen");
        wait(800);
        expect_ = {"main_screen", "main_solomode_screen", "main_screen", "main_multimode_screen", "training_menu_screen", "main_screen", "options_screen", "enhanced_options_screen", "options_screen", "main_screen", "gamepads_screen",
                   "main_screen", "main_solomode_screen", "gamepads_screen", "main_solomode_screen"};
    } else if (scenario == "rb_enhanced") {
        // Enhanced Options > ACCESSIBILITY > Easy Chords (GDALL-039): off on a
        // fresh save; on (saved), exported (GD05_DIALOG: the walk's folder);
        // turned off again, Load User Settings brings it back on.
        at("splash_screen", 30000);
        wait(1500);
        press(kX);
        at("main_screen");
        wait(1500);
        mouseOn("options.btn", 0), wait(200);
        mouseOn("options.btn", 1);
        at("options_screen");
        wait(800);
        mouseOn("port_enhanced.btn", 1);
        at("enhanced_options_screen");
        wait(800);
        check("enhanced_easy_chords_default");
        // MODES > Steam Deck down to Easy Chords (the desktop's rows; Android has Shake for Whammy too).
#ifdef __ANDROID__
        for (int i = 0; i < 41; ++i) press(kDown), wait(100);
#else
        for (int i = 0; i < 40; ++i) press(kDown), wait(100);
#endif
        wait(300);
        check("shot_enhanced_accessibility_focus");
        press(kX), wait(300);
        check("enhanced_easy_chords");
        check("shot_enhanced_accessibility");
        press(kDown), wait(120), press(kDown), wait(120), press(kX), wait(300);  // CHEATS & ENHANCERS > Export User Files
        check("enhanced_easy_chords_exported");
        press(kUp), wait(120), press(kUp), wait(120), press(kX), wait(300);
        check("enhanced_easy_chords_off");
        press(kDown), wait(120), press(kDown), wait(120), press(kDown), wait(120), press(kX), wait(300);  // Load User Settings
        check("enhanced_easy_chords_loaded");
        press(kTri);
        at("options_screen");
        wait(800);
        press(kTri);
        at("main_screen");
        expect_ = {"main_screen", "options_screen", "enhanced_options_screen", "options_screen", "main_screen"};
    } else if (scenario == "rb_owner_bugs") {
        // Wave 03's owner bugs. GD05-023: nothing of a venue plays before a
        // song (the Enhanced Options backdrop is silent). GD05-024: the red
        // fret (keyboard 2) and Esc's back before and during the song do
        // not leave game_screen nor load the song again (RB's UIScreen goes
        // back only to its own (back ...), 0x8225AEE8).
        endMs_ = 40000;
        // The walks skip the intro movie (GD05_SKIP_MOVIES): straight to the title.
        at("splash_screen", 30000);
        press(kX);
        at("main_screen");
        press(kX);  // SOLO
        at("main_solomode_screen");
        press(kX);  // QUICKPLAY
        at("qp_selsong_screen");
        wait(800);
        check("song_list_layout");
        check("rb_helpbar");
        press(kX);  // the first entry
        at("qp_seldiff_solo_screen");
        press(kX);
        wait(1500);
        press(kX);
        at("game_screen", 60000);
        check("loading_icons");  // the loading panel stays loaded
        for (int i = 0; i < 8; ++i) key(SDLK_2), wait(250);
        check("counter_max:song_loads:1");
        wait(2000);
        check("counter_max:song_loads:1");
        screen("game_screen");
        check("counter_max:menu_venue_sounds:0");
    } else if (scenario == "rb_pause_quit") {
        // Wave 04 (GD05-028): the pause screen's QUIT (pause.dta pause_quit
        // -> confirm_quit_screen, no.btn focused), yes, back to the main menu
        // through meta_loading_quit_early_screen; the song stays loaded once.
        rbQuickplay();
        wait(3000);
        key(SDLK_ESCAPE);
        at("pause_screen", 10000);
        wait(800);
        for (int i = 0; i < 4; ++i) press(kDown), wait(250);  // resume, restart, lefty, new song, QUIT
        check("print_focus");
        press(kX);  // QUIT
        at("confirm_quit_screen", 10000);
        wait(800);
        check("print_focus");
        press(kUp), wait(300);  // no.btn -> yes.btn
        press(kX);  // yes
        at("main_screen", 40000);
        check("counter_max:song_loads:1");
    } else if (scenario == "rb_lose_quit") {
        // RB's lose screen's QUIT (lose.dta: confirm_goto meta_loading_main_screen) back to the main menu.
        failMs_ = 8000;
        rbQuickplay();
        at("lose_screen", 40000);
        wait(1500);
        for (int i = 0; i < 4; ++i) press(kDown), wait(250);  // restart, difficulty, new song, practice, QUIT
        check("print_focus");
        press(kX);  // QUIT
        wait(2000);
        press(kUp), wait(300);  // no.btn (the focus) -> yes.btn
        check("print_focus");
        press(kX);  // yes
        wait(2000);
        check("print_focus");
        at("main_screen", 40000);
    } else if (scenario == "rb_lose" || scenario == "credits") {
        // RB's lose screen (ui/lose.dta: retry / new song / quit) after a
        // failed Quickplay song; the credits from the options.
        if (scenario == "rb_lose") {
            failMs_ = 8000;
            rbQuickplay();
            at("lose_screen", 40000);
            wait(1500);
            check("print_focus");
            press(kX);  // the focused choice (retry)
            at("loading_screen|game_screen", 30000);
        } else {
            // The walks skip the intro movie (GD05_SKIP_MOVIES): straight to the title.
            at("splash_screen", 30000);
            press(kX);
            at("main_screen");
            for (int i = 0; i < 3; ++i) press(kDown), wait(200);
            press(kX);  // OPTIONS
            at("options_screen");
            for (int i = 0; i < 8; ++i) {
                wait(400);
                check("print_focus");
                press(kDown);
            }
        }
    } else if (scenario == "creator") {
        // RB's character creator (ui/charcreator.dta): Solo Tour > new
        // character > the maker > PHYSIQUE (the appearance panel: hair and
        // head lists through OutfitPanel) > a hair chosen.
        // The walks skip the intro movie (GD05_SKIP_MOVIES): straight to the title.
        at("splash_screen", 30000);
        press(kX);
        at("main_screen");
        press(kX);
        at("main_solomode_screen");
        press(kDown), wait(200);
        press(kX);
        at("tour_solo_char_screen", 30000);
        wait(800);
        press(kX), wait(800), press(kX);
        at("charcreator_maker_screen", 20000);
        wait(1500);
        press(kX);  // the name
        wait(800);
        for (int i = 0; i < 3; ++i) press(kDown), wait(200);
        press(kX);  // PHYSIQUE
        at("charcreator_appearance_screen", 20000);
        wait(1200);
        check("print_focus");
        for (int i = 0; i < 6; ++i) {
            press(kDown), wait(300);
            check("print_focus");
        }
        mouseOn("hair.btn", 1);
        wait(800);
        check("print_focus");
        press(kDown), wait(300), press(kDown), wait(300);
        check("print_focus");
        press(kX);
        wait(1200);
        check("print_focus");
        check("creator_hair");
    } else if (scenario == "rock_shop") {
        // RB's Rock Shop (ui/shop.dta) from the Solo Tour's hub: a new
        // character, the shop, clothing, a store, a shirt bought and worn.
        // The walks skip the intro movie (GD05_SKIP_MOVIES): straight to the title.
        at("splash_screen", 30000);
        press(kX);
        at("main_screen");
        press(kX);  // SOLO
        at("main_solomode_screen");
        press(kDown), wait(200);
        press(kX);  // TOUR
        at("tour_solo_char_screen", 30000);
        wait(800);
        press(kX), wait(800), press(kX);  // create new, male
        at("charcreator_maker_screen", 20000);
        wait(1500);
        press(kX);
        wait(800);
        for (int i = 0; i < 4; ++i) press(kDown), wait(200);
        press(kX);
        at("charcreator_confirm_screen", 10000);
        wait(1500);
        press(kUp), wait(400), press(kX);
        at("tour_solo_hub_screen", 30000);
        wait(1000);
        press(kDown), wait(300);
        check("print_focus");
        press(kX);  // ROKK SHOPPE
        at("shop_screen", 20000);
        wait(1000);
        press(kX);  // CLOTHING
        at("shop_clothing_screen", 20000);
        wait(1500);
        press(kX);  // the first store (punk)
        at("shop_clothing_genre_screen", 20000);
        wait(1500);
        press(kX);  // TORSO: its list
        wait(1200);
        for (int i = 0; i < 5; ++i) press(kDown), wait(250);  // flannelcoat_checked, 99: affordable with the 100 a new character has
        check("print_focus");
        press(kX);  // buy: the confirm screen
        wait(1500);
        check("print_focus");
        press(kX);  // yes
        wait(1500);
        check("print_focus");
        check("counter:shop_bought:1");
    } else if (scenario == "rb_career") {
        // GD05-072: the Solo Tour end to end through RB's own menus. A new
        // character (the creator: skin and hair chosen), the Rock Shop (a
        // shirt bought: the cash drops by the catalog's price, the shirt
        // worn), a gig played on autoplay (the character on stage in the
        // shirt; the cash rises by the event's reward_per_star x the stars,
        // the event's outfit won), then the game restarts (runApp) and
        // rb_career_restarted finds it all in the save.
        endMs_ = 30000;
        at("splash_screen", 30000);
        press(kX);
        at("main_screen");
        press(kX);  // SOLO
        at("main_solomode_screen");
        press(kDown), wait(200);
        press(kX);  // TOUR
        at("tour_solo_char_screen", 30000);
        wait(800);
        press(kX), wait(800), press(kX);  // create new, male
        at("charcreator_maker_screen", 20000);
        wait(1500);
        press(kX);  // NAME (the offered name is taken)
        wait(800);
        check("focus_to attitude.btn", 5000);
        press(kX), wait(600);  // attitude.lst (rocker, the default)
        press(kDown), wait(400);  // spazz
        press(kX), wait(600);  // confirm
        check("rb_creator_attitude");
        check("focus_to appearance.btn", 5000);
        press(kX);  // PHYSIQUE
        at("charcreator_appearance_screen", 20000);
        wait(1200);
        check("rb_creator_before");
        check("focus_to skin.btn", 5000);
        press(kX), wait(500);  // skin.lst
        press(kDown), wait(300), press(kDown), wait(300);
        check("print_focus");
        press(kX), wait(500);  // confirm the third skin colour
        check("focus_to hair.btn", 5000);
        press(kX), wait(600);  // hair.lst
        press(kDown), wait(300), press(kDown), wait(300);
        check("print_focus");
        press(kX), wait(1200);  // confirm the hair
        check("rb_creator_look");
        check("focus_to done.btn", 5000);
        press(kX);
        at("charcreator_maker_screen", 20000);
        wait(1000);
        check("focus_to create.btn", 5000);
        press(kX);  // CREATE
        at("charcreator_confirm_screen", 10000);
        wait(1500);
        press(kUp), wait(400), press(kX);  // yes
        at("tour_solo_hub_screen", 30000);
        wait(1000);
        check("rb_career_cash0");
        check("focus_to rokkshoppe.btn", 5000);
        press(kX);  // ROKK SHOPPE
        at("shop_screen", 20000);
        wait(1000);
        press(kX);  // CLOTHING
        at("shop_clothing_screen", 20000);
        wait(1500);
        press(kX);  // the first store (punk)
        at("shop_clothing_genre_screen", 20000);
        wait(1500);
        press(kX);  // TORSO: its list
        wait(1200);
        for (int i = 0; i < 5; ++i) press(kDown), wait(250);  // flannelcoat_checked
        check("print_focus");
        press(kX);  // buy: the confirm screen
        wait(1500);
        press(kX);  // yes
        wait(1500);
        check("rb_shop_bought");
        for (int i = 0; i < 3; ++i) press(kTri), wait(1200);  // the store, the clothing, the shop
        at("tour_solo_hub_screen", 30000);
        wait(1000);
        check("focus_up_to gigguide.btn", 5000);
        press(kX);  // GIG GUIDE
        at("tour_solo_diff_screen");
        press(kX);  // the difficulty
        at("tour_solo_guitar_welcome_screen|tour_solo_event_screen", 20000);
        wait(800);
        press(kX);
        at("tour_solo_event_screen", 20000);
        wait(1000);
        press(kX);  // the first event
        at("game_screen", 60000);
        wait(3000);
        check("rb_band_wears");
        at("tour_solo_endgame_screen", 120000);
        check("rb_gig_stars");
        wait(1000);
        press(kX);
        at("tour_solo_gigcomplete_screen", 20000);
        wait(1000);
        check("rb_gig_cash");
        press(kX);
        at("tour_solo_event_screen|tour_solo_hub_screen|tour_solo_newtown_screen", 40000);
        wait(1500);
        check("restart_app");
    } else if (scenario == "rb_career_restarted") {
        // rb_career's second half, after the restart: the character from
        // the save (row 0 of the Solo Tour's list), its cash, look, clothes
        // and the gig's stars as the first half left them.
        at("splash_screen", 30000);
        press(kX);
        at("main_screen");
        press(kX);  // SOLO
        at("main_solomode_screen");
        press(kDown), wait(200);
        press(kX);  // TOUR
        at("tour_solo_char_screen", 30000);
        wait(1000);
        press(kX);  // the saved character
        at("tour_solo_hub_screen", 30000);
        wait(1000);
        check("rb_career_reloaded");
    } else if (scenario == "rb_career_band") {
        // GD05-072: the Band World Tour pays its members' characters. A
        // guitarist made in the Solo Tour's creator, the game restarted,
        // then Multiplayer > Band World Tour with a drummer (the lobby
        // stand-in takes the profile's guitarist), a new band, its first gig on
        // autoplay: the character is paid the event's reward_per_star x the
        // band's stars and the band's money is its members'; restarted
        // again, the band and the money are still there.
        // Two players (modes.dta tour_world min_num_participants 2): the
        // guitarist made here and a drummer without a character (the lobby
        // stand-in's premade one: not a member, not paid).
        SDL_setenv_unsafe("GD05_INSTRUMENT", "guitar,drum,vocals,guitar", 1);
        SDL_setenv_unsafe("GD05_BAND_PLAYERS", "2", 1);
        at("splash_screen", 30000);
        press(kX);
        at("main_screen");
        press(kX);  // SOLO
        at("main_solomode_screen");
        press(kDown), wait(200);
        press(kX);  // TOUR
        at("tour_solo_char_screen", 30000);
        wait(800);
        press(kX), wait(800), press(kX);  // create new, male
        at("charcreator_maker_screen", 20000);
        wait(1500);
        press(kX);  // NAME (the offered name is taken)
        wait(800);
        check("focus_to create.btn", 5000);
        press(kX);  // CREATE
        at("charcreator_confirm_screen", 10000);
        wait(1500);
        press(kUp), wait(400), press(kX);  // yes
        at("tour_solo_hub_screen", 30000);
        wait(1000);
        check("rb_career_cash0");
        check("restart_app");
    } else if (scenario == "rb_career_band_restarted") {
        endMs_ = 90000;  // long enough for the band's first star on autoplay
        at("splash_screen", 30000);
        press(kX);
        at("main_screen");
        press(kDown), wait(200);
        press(kX);  // MULTIPLAYER
        at("main_multimode_screen");
        press(kDown), wait(200);
        press(kX);  // BAND WORLD TOUR
        at("tour_band_sel_screen", 20000);
        wait(800);
        press(kX);  // a new band
        at("tour_band_create_screen", 20000);
        wait(1200);
        press(kX);  // NAME (the offered name is taken)
        wait(800);
        for (int i = 0; i < 2; ++i) press(kDown), wait(200);
        press(kX);  // ACCEPT
        at("tour_band_hub_screen", 30000);
        wait(1000);
        check("rb_band_cash0");
        press(kX);  // GIG GUIDE
        for (int i = 0; i < 6; ++i) {  // the first-time hints, each continued with X
            wait(2500);
            press(kX);
        }
        at("game_screen", 60000);
        wait(3000);
        check("rb_band_member_on_stage");
        at("tour_band_endgame_screen", 300000);
        wait(1000);
        press(kX);
        at("tour_band_gigcomplete_screen", 20000);
        wait(1000);
        check("rb_band_cash");
        press(kX);
        at("tour_band_event_screen|tour_band_hub_screen|tour_band_firstgig_screen|tour_band_firstgig_checkmarks_screen|tour_band_newvan_screen|tour_band_newitem_screen|tour_band_firstvenue_screen", 40000);
        wait(1500);
        check("restart_app");
    } else if (scenario == "rb_career_band_restarted_restarted") {
        at("splash_screen", 30000);
        press(kX);
        at("main_screen");
        press(kDown), wait(200);
        press(kX);  // MULTIPLAYER
        at("main_multimode_screen");
        press(kDown), wait(200);
        press(kX);  // BAND WORLD TOUR
        at("tour_band_sel_screen", 20000);
        wait(1000);
        press(kX);  // the saved band (row 0)
        at("tour_band_hub_screen", 30000);
        wait(1000);
        check("rb_band_reloaded");
    } else if (scenario == "rb_shop_items") {
        // GD05-072: the Rock Shop's instruments (char/instruments.dta: the
        // models and their prices). A new guitarist (100) cannot afford the
        // first guitar (shop_error_money_screen, the cash kept); with RB's
        // cheat_add_money 2000 (cheats_funcs.dta) it is bought: the cash
        // drops by the model's price, its default finish is played on stage;
        // after a restart it is still the character's.
        endMs_ = 15000;
        at("splash_screen", 30000);
        press(kX);
        at("main_screen");
        press(kX);  // SOLO
        at("main_solomode_screen");
        press(kDown), wait(200);
        press(kX);  // TOUR
        at("tour_solo_char_screen", 30000);
        wait(800);
        press(kX), wait(800), press(kX);  // create new, male
        at("charcreator_maker_screen", 20000);
        wait(1500);
        press(kX);  // NAME
        wait(800);
        check("focus_to create.btn", 5000);
        press(kX);  // CREATE
        at("charcreator_confirm_screen", 10000);
        wait(1500);
        press(kUp), wait(400), press(kX);  // yes
        at("tour_solo_hub_screen", 30000);
        wait(1000);
        check("rb_career_cash0");
        check("focus_to rokkshoppe.btn", 5000);
        press(kX);  // ROKK SHOPPE
        at("shop_screen", 20000);
        wait(1000);
        check("focus_to instruments.btn", 5000);
        press(kX);  // INSTRUMENTS
        at("shop_guitarbass_screen", 20000);
        wait(1000);
        press(kX);  // GUITAR
        at("shop_instrument_screen", 20000);
        wait(1500);
        check("print_focus");
        press(kX);  // the first model: too dear
        at("shop_error_money_screen", 10000);
        check("rb_shop_too_dear");
        press(kX);  // OK
        at("shop_instrument_screen", 10000);
        wait(1000);
        check("rb_shop_cheat_money");
        press(kX);  // the first model again: the confirm screen
        at("shop_confirm_screen", 10000);
        wait(1000);
        press(kX);  // yes
        wait(1500);
        check("rb_shop_guitar");
        at("shop_guitarbass_screen", 10000);  // the confirm screen's pop (the port's: [I])
        press(kTri);
        at("shop_screen", 10000);
        press(kTri);
        at("tour_solo_hub_screen", 30000);
        wait(1000);
        check("focus_up_to gigguide.btn", 5000);
        press(kX);  // GIG GUIDE
        at("tour_solo_diff_screen");
        press(kX);
        at("tour_solo_guitar_welcome_screen|tour_solo_event_screen", 20000);
        wait(800);
        press(kX);
        at("tour_solo_event_screen", 20000);
        wait(1000);
        press(kX);  // the first event
        at("game_screen", 60000);
        wait(3000);
        check("rb_shop_guitar_on_stage");
        at("tour_solo_endgame_screen", 120000);
        wait(1000);
        press(kX);
        at("tour_solo_gigcomplete_screen", 20000);
        wait(1000);
        press(kX);
        at("tour_solo_event_screen|tour_solo_hub_screen|tour_solo_newtown_screen", 40000);
        wait(1500);
        check("restart_app");
    } else if (scenario == "rb_shop_items_restarted") {
        at("splash_screen", 30000);
        press(kX);
        at("main_screen");
        press(kX);  // SOLO
        at("main_solomode_screen");
        press(kDown), wait(200);
        press(kX);  // TOUR
        at("tour_solo_char_screen", 30000);
        wait(1000);
        press(kX);  // the saved character
        at("tour_solo_hub_screen", 30000);
        wait(1000);
        check("rb_shop_guitar_reloaded");
    } else if (scenario == "solo_tour") {
        // RB's Solo Tour (ui/tour_solo.dta): Solo > Tour, a character (made
        // in the creator the first time), the hub, the gig guide, a
        // difficulty, the first event, its song, the results, the gig's end.
        endMs_ = 20000;
        // The walks skip the intro movie (GD05_SKIP_MOVIES): straight to the title.
        at("splash_screen", 30000);
        press(kX);
        at("main_screen");
        press(kX);  // SOLO
        at("main_solomode_screen");
        press(kDown), wait(200);
        press(kX);  // TOUR
        at("tour_solo_char_screen", 30000);
        wait(800);
        press(kX);  // create new
        wait(800);
        press(kX);  // male
        at("charcreator_maker_screen", 20000);
        wait(1500);
        press(kX);  // NAME (the offered name is taken)
        wait(800);
        for (int i = 0; i < 4; ++i) press(kDown), wait(200);
        press(kX);  // CREATE
        at("charcreator_confirm_screen", 10000);
        wait(1500);
        check("print_focus");
        press(kUp), wait(400);
        check("print_focus");
        press(kX);  // yes
        at("tour_solo_hub_screen", 30000);
        wait(1000);
        press(kX);  // GIG GUIDE (gigguide.btn, the focus)
        at("tour_solo_diff_screen");
        press(kX);  // the difficulty
        at("tour_solo_guitar_welcome_screen|tour_solo_event_screen", 20000);
        wait(800);
        press(kX);
        at("tour_solo_event_screen", 20000);
        wait(1000);
        press(kX);  // the first event
        at("game_screen", 60000);
        wait(2000);
        at("tour_solo_endgame_screen", 90000);
        wait(1000);
        press(kX);
        at("tour_solo_gigcomplete_screen", 20000);
        wait(1000);
        press(kX);
        at("tour_solo_event_screen|tour_solo_hub_screen|tour_solo_newtown_screen", 40000);
    } else if (scenario == "screens") {
        // The screen sweep (runCheck "sweep"): every screen from the main menu.
        // The walks skip the intro movie (GD05_SKIP_MOVIES): straight to the title.
        at("splash_screen", 30000);
        press(kX);
        at("main_screen");
        steps_.push_back({S::Check, "sweep", 0, 0});
    } else if (scenario == "band_tour") {
        // RB's Band World Tour (ui/tour_band_sel.dta, tour_band.dta):
        // Multiplayer > Band World Tour, a new band (the lobby's stand-in
        // joins two players), its name, the hub, the gig guide, the hints, a
        // gig, its song, the results.
        SDL_setenv_unsafe("GD05_INSTRUMENT", "guitar,drum,vocals,guitar", 1);
        SDL_setenv_unsafe("GD05_BAND_PLAYERS", "2", 1);
        endMs_ = 20000;
        // The walks skip the intro movie (GD05_SKIP_MOVIES): straight to the title.
        at("splash_screen", 30000);
        press(kX);
        at("main_screen");
        press(kDown), wait(200);
        press(kX);  // MULTIPLAYER
        at("main_multimode_screen");
        press(kDown), wait(200);
        press(kX);  // BAND WORLD TOUR
        at("tour_band_sel_screen", 20000);
        wait(800);
        press(kX);  // a new band
        at("tour_band_create_screen", 20000);
        wait(1200);
        check("shot_band_create");
        press(kX);  // NAME (the offered name is taken)
        wait(800);
        check("shot_band_naming");
        for (int i = 0; i < 2; ++i) press(kDown), wait(200);
        press(kX);  // ACCEPT
        at("tour_band_hub_screen", 30000);
        wait(1000);
        press(kX);  // GIG GUIDE
        // The first-time hints (welcome, first gig ...), each continued with X.
        for (int i = 0; i < 6; ++i) {
            wait(2500);
            check("print_focus");
            press(kX);
        }
        at("game_screen", 60000);
        wait(2000);
        check("counter:band_players:2");
        at("tour_band_endgame_screen", 300000);  // through the gig's set list
        wait(1000);
        press(kX);
        at("tour_band_gigcomplete_screen", 20000);
        wait(1000);
        press(kX);
        at("tour_band_event_screen|tour_band_hub_screen|tour_band_firstgig_screen|tour_band_firstgig_checkmarks_screen|tour_band_newvan_screen|tour_band_newitem_screen|tour_band_firstvenue_screen", 40000);
    } else if (scenario == "practice" || scenario == "tutorials" || scenario == "drum_trainer") {
        // RB's Training menu (ui/training.dta): Tutorials, Practice, the Drum Trainer.
        if (scenario == "drum_trainer") SDL_setenv_unsafe("GD05_INSTRUMENT", "drum", 1);
        endMs_ = 20000;
        // The walks skip the intro movie (GD05_SKIP_MOVIES): straight to the title.
        at("splash_screen", 30000);
        press(kX);
        at("main_screen");
        press(kDown), wait(200), press(kDown), wait(200);
        press(kX);  // TRAINING
        at("training_menu_screen", 20000);
        wait(800);
        if (scenario == "practice") press(kDown), wait(200);
        if (scenario == "drum_trainer") press(kDown), wait(200), press(kDown), wait(200);
        press(kX);
        for (int i = 0; i < 6; ++i) {
            wait(2500);
            check("print_focus");
            press(kX);
        }
        wait(8000);
        check("print_focus");
    } else if (scenario == "rb_practice") {
        // RB's Practice (ui/practice.dta): a song, a difficulty, the third
        // section of the list, the second speed; the practice clock starts
        // a measure before that section ("practice:" log line).
        endMs_ = 60000;
        // The walks skip the intro movie (GD05_SKIP_MOVIES): straight to the title.
        at("splash_screen", 30000);
        press(kX);
        at("main_screen");
        press(kDown), wait(200), press(kDown), wait(200);
        press(kX);  // TRAINING
        at("training_menu_screen", 20000);
        wait(800);
        press(kDown), wait(200);
        press(kX);  // PRACTICE
        at("practice_selsong_screen", 20000);
        wait(1500);
        press(kX);
        at("qp_seldiff_solo_screen", 20000);
        wait(2500);
        press(kX);
        wait(2500);
        press(kX);  // the difficulty, then ready
        at("practice_sel_section_screen", 30000);
        wait(1500);
        press(kDown), wait(300), press(kDown), wait(300);
        check("print_focus");
        press(kX);  // the start section
        wait(1500);
        press(kDown), wait(300);
        press(kX);  // the end section, one further
        at("practice_sel_speed_screen", 20000);
        wait(1000);
        press(kDown), wait(300);
        check("print_focus");
        press(kX);
        at("practice_game_screen", 40000);
        wait(12000);
        check("print_focus");
    } else if (scenario == "score_duel" || scenario == "tug_of_war") {
        // RB's head to head (ui/main.dta): Multiplayer > Head to Head >
        // Tug of War (headtohead.btn) or Score Duel (headtohead_pro.btn) >
        // Local; the lobby's stand-in joins two players (DECISIONS D10).
        const bool tow = scenario == "tug_of_war";
        SDL_setenv_unsafe("GD05_INSTRUMENT", "guitar,guitar", 1);
        endMs_ = 25000;
        // The walks skip the intro movie (GD05_SKIP_MOVIES): straight to the title.
        at("splash_screen", 30000);
        press(kX);
        at("main_screen");
        press(kDown), wait(200);
        press(kX);  // MULTIPLAYER
        at("main_multimode_screen");
        for (int i = 0; i < (tow ? 2 : 3); ++i) press(kDown), wait(200);
        press(kX);
        at(tow ? "main_hth_split_screen" : "main_hth_pro_screen", 20000);
        press(kX);  // LOCAL
        at("qp_selsong_screen", 30000);
        wait(800);
        press(kX);
        wait(3000);
        press(kX), wait(1500), press(kX), wait(1500);
        pressP(kX, 1), wait(1500), pressP(kX, 1), wait(1500);
        at("game_screen|hth_game_screen", 60000);
        wait(3000);
        check("counter:band_players:2");
        at("qp_hth_endgame_screen|qp_hth_tow_endgame_screen|endgame_waiting_screen", 90000);
    } else if (scenario == "band_quickplay") {
        // RB's Band Quickplay (ui/main.dta, selsong / seldiff): Multiplayer >
        // Band Quickplay > Local; the band lobby joins four players (the
        // port's stand-in, DECISIONS D10: GD05_BAND_PLAYERS, the instruments
        // from GD05_INSTRUMENT; players without a controller autoplay), the
        // song, the leader's difficulty, the song with four tracks.
        SDL_setenv_unsafe("GD05_INSTRUMENT", "guitar,drum,vocals,guitar", 1);
        SDL_setenv_unsafe("GD05_BAND_PLAYERS", "4", 1);
        endMs_ = 30000;
        // The walks skip the intro movie (GD05_SKIP_MOVIES): straight to the title.
        at("splash_screen", 30000);
        check("unlock_all_songs");  // the songs the walk was written for (rb_qp_locked, GD05-073)
        press(kX);
        at("main_screen");
        press(kDown), wait(200);
        press(kX);  // MULTIPLAYER
        at("main_multimode_screen");
        press(kX);  // BAND QUICKPLAY (bandqp.btn, the focus)
        at("main_bandcoop_screen");
        press(kX);  // LOCAL
        at("qp_selsong_screen", 30000);
        wait(800);
        press(kX);  // the first entry
        at("qp_seldiff_screen|qp_seldiff_solo_screen", 20000);
        wait(1500);
        press(kX), wait(1500), press(kX), wait(1500);
        at("game_screen", 60000);
        wait(3000);
        check("counter:band_players:4");
        check("counter:rb_track_frames:30");
        // GD05-087: the first member's Overdrive ready (be_ready.trig) when
        // the HUD probe draws its meter alone, whatever the first song gives
        // (Wave of Mutilation's guitar had it at 15 s: the box 26..33 px off).
        check("od_set:60");
        at("endgame_waiting_screen", 90000);
        // GD05-058: RB's own HUD (render/rb_hud_view.h): each element drawn alone
        // lands within 4 px of the box its milo data gives; the score, the
        // multiplier, the Overdrive and crowd meters show the players' values.
        check("counter:rb_hud_probes:1");
        check("counter:rb_hud_boxes:9");
        check("counter_max:rb_hud_box_err_px:4");
        check("counter_max:rb_hud_value_errors:0");
        // GD05-064: a gem in each lane at the now bar shows; the HUD covers
        // none of it (0.0.6: the Overdrive meter and the multiplier sat
        // across the smashers).
        check("counter:rb_nowbar_gem_px:200");
        check("counter_max:rb_nowbar_cover_pct:2");
        at("qp_endgame_screen", 30000);
    } else if (scenario == "quickplay_vocals" || scenario == "quickplay_drums") {
        // Rock Band's instrument per player (wave 02): the controller type
        // decides the part (seldiff.dta: a drum or a vocals controller
        // skips the guitar / bass choice). The singer autoplays (the walk's
        // song keys sing the target) or, with GD05_VOCALS_FILE, sings a file.
        const bool vocals = scenario == "quickplay_vocals";
        SDL_setenv_unsafe("GD05_INSTRUMENT", vocals ? "vocals" : "drum", 1);
        endMs_ = vocals ? 95000 : 30000;
        // The walks skip the intro movie (GD05_SKIP_MOVIES): straight to the title.
        at("splash_screen", 30000);
        press(kX);
        at("main_screen");
        press(kX);  // SOLO
        at("main_solomode_screen");
        press(kX);  // QUICKPLAY
        at("qp_selsong_screen");
        wait(500);
        press(kX);  // the first entry (random_song)
        at("qp_seldiff_solo_screen");
        wait(1500);
        press(kX);  // the focused difficulty: ready
        at("game_screen", 60000);
        wait(3000);
        check(vocals ? "counter:part_vocals:1" : "counter:part_drum:1");
        check("counter:crowd_drawn:0");  // reported (the audience: GD05-007)
        if (!vocals) check("counter:rb_track_frames:30");  // RB's own track through the menus, not the debug highway
        check("counter:band_members:4");
        check("counter:band_meshes:20");
        if (vocals) {
            wait(85000);  // into the song's first phrases
            check("counter:vocal_notes_drawn:1");
            check("counter:vocal_phrases_rated:1");
            check("counter:vocal_score:1");
        }
        at("endgame_waiting_screen", 150000);
        at("qp_endgame_screen", 20000);
        wait(1000);
    } else if (scenario == "rb_title_back") {
        // GD05-059: Press Start -> main menu -> Back to Press Start, with
        // screenshots at fixed moments both ways (GD05_WALK_SHOTS:
        // title_<tag>.bmp) and the fx panel's fade / the panels' transition
        // state printed at each.
        at("splash_screen", 30000);
        wait(1500);
        check("tshot:splash_rest");
        key(SDLK_RETURN);
        for (const char* t : {"fwd_0033", "fwd_0100", "fwd_0250", "fwd_0500", "fwd_0750", "fwd_1000", "fwd_1500", "fwd_2500"}) {
            static double last = 0;
            const double ms = std::atof(t + 4);
            wait(ms - last), last = ms;
            steps_.push_back({S::Check, std::string("tshot:") + t, 0, 0});
        }
        at("main_screen");
        wait(1500);
        check("tshot:main_rest");
        key(SDLK_ESCAPE);
        for (const char* t : {"back_0033", "back_0100", "back_0250", "back_0500", "back_0750", "back_1000", "back_1500", "back_2500"}) {
            static double last = 0;
            const double ms = std::atof(t + 5);
            wait(ms - last), last = ms;
            steps_.push_back({S::Check, std::string("tshot:") + t, 0, 0});
        }
        at("splash_screen", 20000);
        wait(1500);
        check("tshot:splash_again");
        expect_ = {"splash_screen", "main_screen", "splash_screen"};
    } else if (scenario == "rb_band_start") {
        // GD05-054: the song's first seconds (the band's entrance, the intro
        // shot, the first shots and the track) draw nothing without its
        // shader: no draw without its program or pipeline, no standard
        // variant replaced by a simpler one, no band / venue mesh without its
        // material, no RB shot left without a PostProc (render/device.h
        // shaderaudit; GD05_SHADERLOG=1 names them). GD05_WALK_SHOTS: a shot
        // every 500 ms (band_start_NN.bmp).
        endMs_ = 20000;
        rbQuickplay();
        for (int i = 0; i < 30; ++i) wait(500), check("band_start_shot");
        check("counter_max:missing_shaders:0");
        at("endgame_waiting_screen", 90000);
    } else if (scenario == "rb_highway") {
        // GD05-070 (owner: "the yellow Overdrive bar flickers", "the
        // multiplier is drawn on top of other elements", "the hit area looks
        // wrong", "Overdrive colour on the sustains' tails"): App::rbProbes
        // under every frame. The Overdrive meter (star_power_01) alone over
        // magenta: its luma may jump at most once a beat and never straight
        // back (be_ready.trig's be_color_pulse.anim loops 0..1 at k1_fpb);
        // the track's draw order against static.grp's and the smasher
        // plate's setup_draworder; no HUD directory draws a drawable before
        // one of a lower draw order (RndDir's sort, RB 0x821B3A48); every
        // star tail drawn with tail_bonus.mat, its pixels near white
        // (chroma under 20%; the slot colours are over 90%).
        // GD05_HWY_LEGACY=1 shows the checks failing (the code before).
        if (!SDL_getenv("GD05_VENUE")) SDL_setenv_unsafe("GD05_VENUE", "arena_01", 1);
        SDL_setenv_unsafe("GD05_METER_PROBE", "1", 1);
        SDL_setenv_unsafe("GD05_TAIL_PROBE", "1", 1);
        SDL_setenv_unsafe("GD05_TAILTEST", "1", 1);  // every gem a tail: the star ones too
        endMs_ = 75000;
        rbQuickplay();
        wait(4000);
        check("od_meter:0.6");  // be_ready: the pulse
        wait(8000);
        check("counter:meter_frames:200");
        check("counter_max:meter_flickers:0");
        check("counter:rb_track_order_parts:8");
        check("counter_max:rb_track_order_errors:0");
        check("counter:rb_hud_order_dirs:5");
        check("counter_max:rb_hud_order_inversions:0");
        wait(45000);  // into the song's Overdrive phrases
        check("counter:rb_star_tails:1");
        check("counter_max:rb_star_tails_wrong:0");
        check("counter:star_tail_px:50");
        check("counter_max:star_tail_chroma_pct:20");
        check("counter_max:meter_flickers:0");
        at("endgame_waiting_screen", 90000);
    } else if (scenario == "rb_crowd") {
        // GD05-071 (owner: "no crowd visible in any venue"): a shot with a
        // crowd (GD05_FORCE_SHOT #crowdN, default the first), the audience
        // alone through its camera every 30th frame (App::rbProbes): its
        // pixels, the archetypes' clips started (crowd.dta intro_start /
        // crowd_update -> main.drv play_group_flags). Before GD05-071 no clip
        // played and every archetype lay on the floor in its bind pose: 0 px.
        // GD05_VENUE picks the venue (default arena_01); GD05_CROWD_LEGACY=1
        // shows the check failing.
        if (!SDL_getenv("GD05_VENUE")) SDL_setenv_unsafe("GD05_VENUE", "arena_01", 1);
        if (!SDL_getenv("GD05_FORCE_SHOT")) SDL_setenv_unsafe("GD05_FORCE_SHOT", "#crowd0", 1);
        SDL_setenv_unsafe("GD05_CROWD_PROBE", "1", 1);
        endMs_ = 20000;
        rbQuickplay();
        wait(5000);
        check("counter:crowd_probes:3");
        check("counter:crowd_clips:8");
        check("counter:crowd_px_max:20000");  // 1% of the 1920 x 1080 picture
        at("endgame_waiting_screen", 90000);
    } else if (scenario == "rb_look") {
        // GD05-069 (owner: "everything is overbright", "the original is
        // darker with a stronger film grain", "the pause background stays
        // too bright"): the world alone (no track or HUD) in 30 fixed shots
        // (the venue's BandCamShots by name, every 7th, 1.5 s each) from 12 s
        // into a song, each frame's luma mean, contrast (its
        // standard deviation), saturation and grain (the mean |luma - its 3x3
        // box| where luma > 20) at 640x360, against the disc's own gameplay
        // movies (videos/entersandman, tomsawyer, learntofly, gowiththeflow,
        // wanteddeadoralive .bik, one frame every 2 s, title cards out: 289
        // frames, through the same display curve): luma 69.3, contrast 55.4,
        // grain 1.97; fails outside 15% (grain 40%: Bink smooths it). Then
        // the pause: pause.milo's black_bg.mesh (black_bg.mat: black, alpha
        // 0.7, kBlendSrcAlpha over the whole screen) leaves the world at 0.3
        // of its luma: the outer strips, before and while paused.
        // GD05_VENUE picks the venue (default small_club_02); the second song.
        if (!SDL_getenv("GD05_VENUE")) SDL_setenv_unsafe("GD05_VENUE", "small_club_02", 1);
        SDL_setenv_unsafe("GD05_TRACK", "0", 1);
        SDL_setenv_unsafe("GD05_HUD", "0", 1);
        endMs_ = 90000;
        at("splash_screen", 30000);
        press(kX);
        at("main_screen");
        press(kX);  // SOLO
        at("main_solomode_screen");
        press(kX);  // QUICKPLAY
        at("qp_selsong_screen");
        wait(500);
        press(kDown), wait(400);  // the first song (past Random Song)
        press(kX);
        at("qp_seldiff_solo_screen");
        press(kX);
        wait(1500);
        press(kX);
        at("game_screen", 60000);
        wait(12000);
        // Fixed shots: the venue's BandCamShots by name, every 7th (#0, #7,
        // ... GD05_FORCE_SHOT), 1.5 s into each.
        for (int i = 0; i < 30; ++i) {
            const std::string next = "look:shot:" + std::to_string(i * 7);
            check(next.c_str());
            wait(1500), check("look:frame");
        }
        check("look:prepause");
        key(SDLK_ESCAPE);
        at("pause_screen", 10000);
        wait(1500);
        check("look:pause");
        check("look:shot:");
        check("look:eval");
    } else if (scenario == "quickplay") {
        endMs_ = 30000;  // the song ends early (the walk's end time; GD05_WALK_END overrides)
        if (const char* e = SDL_getenv("GD05_WALK_END")) endMs_ = float(std::atof(e));
        rbQuickplay();
        wait(3000);
        check("counter:rb_track_frames:30");
        check("counter:band_members:4");
        at("endgame_waiting_screen", 90000);  // modes.dta win_screen (the default mode)
        // GD05-058: RB's own HUD (render/rb_hud_view.h): each element drawn alone
        // lands within 4 px of the box its milo data gives; the score, the
        // multiplier, the Overdrive and crowd meters show the players' values.
        check("counter:rb_hud_probes:1");
        check("counter:rb_hud_boxes:5");
        check("counter_max:rb_hud_box_err_px:4");
        check("counter_max:rb_hud_value_errors:0");
        // GD05-064: a gem in each lane at the now bar shows; the HUD covers
        // none of it (0.0.6: the Overdrive meter and the multiplier sat
        // across the smashers).
        check("counter:rb_nowbar_gem_px:200");
        check("counter_max:rb_nowbar_cover_pct:2");
        at("qp_endgame_screen", 20000);       // the mode's endgame_screen once the stats are in
        wait(1000);
        press(kX);  // continue (SELECT_START_MSG: ui sync_screen qp_complete_screen)
        at("qp_complete_screen");
        press(kX);  // the focused choice: pick another song
        at("qp_selsong_screen", 30000);  // through meta_loading_continue_screen
        wait(1000);
    } else if (scenario == "keys") {
        // The keyboard through SDL's events (as a player without a
        // controller): Enter starts at the title and selects in the menus,
        // the arrows move [I: the port's keyboard map, docs/controls.md].
        // The walks skip the intro movie (GD05_SKIP_MOVIES): straight to the title.
        at("splash_screen", 30000);
        key(SDLK_RETURN);
        at("main_screen");
        key(SDLK_RETURN);  // SOLO
        at("main_solomode_screen");
        key(SDLK_RETURN);  // QUICKPLAY
        at("qp_selsong_screen");
        wait(500);
        key(SDLK_DOWN);
        wait(300);
        key(SDLK_RETURN);
        at("qp_seldiff_solo_screen");
        key(SDLK_RETURN);  // guitar
        wait(1500);
        key(SDLK_RETURN);  // the difficulty
        at("game_screen", 60000);
        wait(4000);
        key(SDLK_ESCAPE);  // pause (Start)
        at("pause_screen", 10000);
        key(SDLK_RETURN);  // RESUME (the focus)
        at("game_screen", 10000);
        wait(1000);
    } else if (scenario == "rb_keys_play" || scenario == "rb_keys_rebind") {
        // GD05-050: a song played with the keyboard alone, through SDL's
        // key events and the input layer (docs/guidelines/
        // universal_mechanisms.md, the keyboard table): frets 1..5, strum on
        // Down, Overdrive on Tab, pause on Enter. rb_keys_rebind first binds
        // the green fret to Q as GAMEPADS' Customize inputs saves it, and
        // plays green with Q (1 no longer plays it).
        autoplay_ = false;
        // rb_keys_play: a guitar left plugged in holds player 1 (the owner's
        // setup): the keyboard still plays the one-player song and pauses it.
        if (scenario == "rb_keys_play") check("kb_guitar_attach");
        at("splash_screen", 30000);
        key(SDLK_RETURN);
        at("main_screen");
        wait(1500);
        if (scenario == "rb_keys_rebind") check("kb_bind_q"), wait(300), check("kb_save_q");
        key(SDLK_RETURN);  // SOLO
        at("main_solomode_screen");
        key(SDLK_RETURN);  // QUICKPLAY
        at("qp_selsong_screen");
        wait(500);
        key(SDLK_DOWN);
        wait(300);
        key(SDLK_RETURN);
        at("qp_seldiff_solo_screen");
        key(SDLK_RETURN);  // guitar
        wait(1500);
        key(SDLK_RETURN);  // the difficulty
        at("game_screen", 60000);
        check("kb_play 30", 90000);
        check("kb_hits");
        key(SDLK_RETURN);  // pause (Start)
        at("pause_screen", 10000);
        wait(500);
        key(SDLK_RETURN);  // RESUME
        at("game_screen", 10000);
        check("kb_play 20", 90000);
        check("kb_hits");
        if (scenario == "rb_keys_play") {
            check("kb_play od", 150000);  // on to the first Overdrive deployed with Tab
            check("kb_hits");
        }
        expect_ = {"main_screen", "main_solomode_screen", "qp_selsong_screen", "qp_seldiff_solo_screen", "game_screen", "pause_screen", "game_screen"};
    } else if (scenario == "rb_guitar_play" || scenario == "rb_guitar_none" || scenario == "rb_pad_play" || scenario == "rb_owner_guitar") {
        // GD05-056: a song played with a device's own SDL events (a virtual
        // joystick that is the owner's Harmonix Rock Band Guitar 1bad:0002:
        // 11 buttons, 6 axes, a hat, his SDL mapping; or a generic Xbox 360
        // pad): frets on its buttons, strum on the hat, whammy on axis 3,
        // Overdrive by tilt (axis 4) / the pad's Back, pause on Start.
        // rb_guitar_play works the menus with the guitar too; rb_guitar_none
        // is the owner's controllers.json (the keyboard player 1, the guitar
        // None): the one-player song still takes the guitar (D34).
        autoplay_ = false;
        // rb_owner_guitar (GD05-058): the guitar with the owner's GUID and his
        // controllers.json of 2026-11-11 17:36 (keyboard player 1, the guitar
        // None, his keyboard bindings), imported as GAMEPADS' Load does.
        const bool owner = scenario == "rb_owner_guitar";
        const bool pad = scenario == "rb_pad_play", none = scenario == "rb_guitar_none" || owner;
        auto checkS = [&](const std::string& what, double timeout = 0) { steps_.push_back({S::Check, what, 0, timeout}); };
        // A button press on the device (SDL button / hat numbers of the device).
        auto dev = [&](const std::string& what) { checkS("dev_set " + what + " 1"), wait(90), checkS("dev_set " + what + " 0"), wait(220); };
        const std::string green = "b0", strumDown = "hat4", start = pad ? "b6" : "b7";
        if (owner) check("dev_owner_guid");
        checkS(pad ? "dev_attach 045e:028e" : "dev_attach 1bad:0002:rbguitar", 10000);
        if (owner) check("dev_owner_json");
        at("splash_screen", 30000);
        check("unlock_all_songs");  // the songs the walk was written for (rb_qp_locked, GD05-073)
        wait(500);  // the device settles (its first 300 ms are its rest values)
        if (pad || none) key(SDLK_RETURN); else dev(start);
        at("main_screen");
        wait(1500);
        if (none && !owner) checkS("dev_owner_players");
        checkS("dev_players");
        if (pad || none) {
            key(SDLK_RETURN), at("main_solomode_screen"), key(SDLK_RETURN), at("qp_selsong_screen"), wait(500);
            key(SDLK_DOWN), wait(300), key(SDLK_RETURN), at("qp_seldiff_solo_screen");
            key(SDLK_RETURN), wait(1500), key(SDLK_RETURN);
        } else {
            dev(green), at("main_solomode_screen"), dev(green), at("qp_selsong_screen"), wait(500);
            dev(strumDown), wait(300), dev(green), at("qp_seldiff_solo_screen");
            dev(green), wait(1500), dev(green);  // guitar, then the difficulty
        }
        at("game_screen", 60000);
        // The owner's session: the window made again for Vulkan without the
        // keyboard focus (SDL then drops every joystick event unless
        // background events are allowed): the song played with no focus.
        if (owner) check("dev_unfocus");
        checkS(pad ? "dev_play pad 30" : "dev_play guitar 30", 90000);
        check("dev_hits");
        if (owner) {
            // The rest (pause, sustain, Overdrive, a miss) is rb_guitar_none's
            // with the same controllers.json; a hidden window's song does not
            // go on after a pause (offscreen).
            check("dev_trace");
            check("dev_refocus");
            expect_ = {"main_screen", "main_solomode_screen", "qp_selsong_screen", "qp_seldiff_solo_screen", "game_screen"};
        } else {
        dev(start);  // pause
        at("pause_screen", 10000);
        wait(500);
        if (pad) dev("b0"); else dev(green);  // RESUME (the focus)
        at("game_screen", 10000);
        checkS(pad ? "dev_play pad sustain" : "dev_play guitar sustain", 120000);  // held through a sustain, whammied
        check("dev_hits");
        checkS(pad ? "dev_play pad od" : "dev_play guitar od", 150000);  // Overdrive deployed by tilt / Back
        check("dev_hits");
        // GD05-045: the fret board's effects from RB's track milo: every
        // hit launched its smashers' flames, the frets held lit their
        // smashers, a sustain burned, the deploy showed its spotlight.
        check("counter:rb_fx_hits:20");
        check("counter_max:rb_fx_hits_no_flame:0");
        check("counter:rb_fx_glows:1");
        check("counter:rb_fx_burns:1");
        check("counter:rb_fx_deploys:1");
        check("counter_max:rb_fx_deploys_no_visual:0");
        // GD05-058: RB's own HUD (render/rb_hud_view.h): each element drawn alone
        // lands within 4 px of the box its milo data gives; the score, the
        // multiplier, the Overdrive and crowd meters show the players' values.
        check("counter:rb_hud_probes:1");
        check("counter:rb_hud_boxes:5");
        check("counter_max:rb_hud_box_err_px:4");
        check("counter_max:rb_hud_value_errors:0");
        // GD05-064: a gem in each lane at the now bar shows; the HUD covers
        // none of it (0.0.6: the Overdrive meter and the multiplier sat
        // across the smashers).
        check("counter:rb_nowbar_gem_px:200");
        check("counter_max:rb_nowbar_cover_pct:2");
        if (!pad) {
            // Gems let pass and a wrong strum: the miss sound, the guitar
            // stem muted; the next hits bring it back.
            check("dev_play guitar miss", 60000);
            check("dev_play guitar 8", 60000);
            check("dev_audible");
        }
        if (!pad) check("dev_trace");
        expect_ = {"main_screen", "main_solomode_screen", "qp_selsong_screen", "qp_seldiff_solo_screen", "game_screen", "pause_screen", "game_screen"};
        }
    } else if (scenario == "rb_fuzz") {
        // GD05-052 (GD05-032): menus disappearing. Seeded random input on the
        // main menu and its sub-menus (GD05_FUZZ_SEED, GD05_FUZZ_STEPS):
        // mouse hovers and clicks on the buttons drawn, arrows, Enter, Esc
        // (never a third on the main menu: that quits) and the frets, fast
        // and slow, also during transitions; every few steps the menus
        // settle and each panel of the screen must be up, shown and have
        // its buttons drawn on screen, with a focused panel.
        at("splash_screen", 30000);
        press(kX);
        at("main_screen");
        wait(1500);
        check("fuzz_baseline");
        check("fuzz", 3.6e6);
    } else if (scenario == "dev_cheats") {
        // GD02-045: Enhanced > Developer Keyboard Cheats on, then a real 'l'
        // key (SDL event, through the app's bound-key filter) runs cheats.dta's
        // "Automatically Lose" ({handle (player0 lose)}): the lose screen. A
        // bound key ('1', green) runs no cheat.
        at("splash_screen", 30000);
        press(kX);
        at("startup_create_save_screen");
        press(kX);
        at("saving_screen");
        at("guitar_help_screen");
        press(kX);
        quickplaySong(3);
        wait(3000);
        check("dev_cheat_on");
        key(SDLK_1);  // bound to green: no cheat
        wait(1500);
        check("dev_cheat_bound");
        key(SDLK_L);  // Automatically Lose
        at("lose_screen", 20000);
    } else if (scenario == "lose") {
        autoplay_ = false;
        failMs_ = 6000;  // past hush's first big note (PART GUITAR beat 8), its gem passed unhit
        songKeys_ = "1000:strum,2000:strum";  // two overstrums: GH2's miss sound each
        quickplaySong(19);  // hush (big, the 20th quick play song)
        check("miss_sound", 30000);
        at("lose_screen", 30000);
        check("big_note_missed");
        press(kDown), wait(150), press(kDown), wait(150);  // QUIT (lose_quit_normal.btn)
        press(kX);
        at("meta_loading_screen");
        at("main_screen");
        check("menu_sounds");
        // After a song, Enhanced Options' backdrop still shows the band (GD02-074).
        wait(2000);  // the main menu's entrance (a press during it is lost)
        for (int i = 0; i < 4; ++i) press(kDown), wait(150);  // OPTIONS
        press(kX);
        at("options_screen");
        press(kDown), wait(150), press(kDown), wait(150);
        press(kX);
        at("enhanced_options_screen");
        wait(300);  // after a song the backdrop loads on the screen's second frame (and fades in)
        check("backdrop_band");
        press(kTri);
        at("options_screen");
        press(kTri);
        at("main_screen");
        expect_ = {"main_screen", "qp_selsong_screen", "qp_diff_screen", "loading_screen", "game_screen", "lose_screen",
                   "meta_loading_screen", "main_screen", "options_screen", "enhanced_options_screen", "options_screen", "main_screen"};
    } else if (scenario == "whammy_tail") {
        // A held sustain's tail swells with the whammy bar and goes back when
        // it is let go (0x8269EAD0, Owner 10-30): the analog bar (the value
        // fully down) from 1 s, up at 16 s, then a digital whammy (a key or
        // button) held from 26 s to 42 s, autoplay holding the sustains.
        songKeys_ = "1000:whammy=1,16000:whammy=0,26000:+whammykey,42000:-whammykey";
        quickplaySong(0);
        check("whammy_tail_fat");
        check("whammy_tail_back");
        check("whammy_tail_key");
        check("whammy_tail_key_back");
        press(kStart);
        at("pause_screen");
        press(kUp);  // QUIT (up from RESUME)
        wait(150);
        press(kX);
        at("pause_confirm_quit_screen");
        press(kX);  // quit.btn
        at("meta_loading_screen");
        at("qp_selsong_screen");
        expect_ = {"main_screen", "qp_selsong_screen", "qp_diff_screen", "loading_screen", "game_screen", "pause_screen",
                   "pause_confirm_quit_screen", "meta_loading_screen", "qp_selsong_screen"};
    } else if (scenario == "career_glitches") {
        // The Glitches (GD02-090, every song passed at every difficulty):
        // the career difficulty screen shows each difficulty's songs
        // passed (campaign get_status_progress 0x82284F18, GD02-149).
        at("splash_screen", 30000);
        press(kX);
        at("startup_create_save_screen");
        press(kX);
        at("saving_screen");
        at("guitar_help_screen");
        press(kX);
        at("main_screen");
        press(kUp);  // CAREER
        wait(150);
        press(kX);
        at("chooseprof_screen");
        wait(800);
        check("focus_glitches");
        press(kX);
        at("sel_difficulty_screen");
        wait(1200);
        check("difficulty_status all");
        expect_ = {"main_screen", "chooseprof_screen", "sel_difficulty_screen"};
    } else if (scenario == "pause") {
        quickplaySong(1);
        wait(2500);
        press(kStart);
        at("pause_screen");
        press(kStart);  // Start again resumes
        at("game_screen");
        wait(1500);
        press(kStart);
        at("pause_screen");
        press(kDown);  // RESTART
        wait(150);
        press(kX);
        at("pause_confirm_restart_screen");
        press(kX);  // restart.btn
        at("game_screen");
        wait(2500);
        check("game_sinks");  // track_panel and hud still hear the game after RESTART
        press(kStart);
        at("pause_screen");
        press(kUp);  // QUIT (up from RESUME)
        wait(150);
        press(kX);
        at("pause_confirm_quit_screen");
        press(kX);  // quit.btn
        at("meta_loading_screen");
        at("qp_selsong_screen");
        check("menu_sounds");
        expect_ = {"main_screen", "qp_selsong_screen", "qp_diff_screen", "loading_screen", "game_screen", "pause_screen",
                   "game_screen", "pause_screen", "pause_confirm_restart_screen", "game_screen", "pause_screen",
                   "pause_confirm_quit_screen", "meta_loading_screen", "qp_selsong_screen"};
    } else if (scenario == "career" || scenario == "career_full" || scenario == "career_song" || scenario == "career_encore" ||
               scenario == "career_win" || scenario == "career_win_medium" || scenario == "career_practice" ||
               scenario == "career_venues" || scenario == "career_venues_input") {
        at("splash_screen", 30000);
        press(kX);
        at("startup_create_save_screen");
        press(kX);
        at("saving_screen");
        at("guitar_help_screen");
        press(kX);
        at("main_screen");
        if (scenario == "career_song" && SDL_getenv("GD05_WALK_ENHANCED")) {
            // Enhanced Options first (its backdrop loads a venue, then frees it).
            for (int i = 0; i < 4; ++i) press(kDown), wait(150);  // OPTIONS
            press(kX);
            at("options_screen");
            press(kDown), wait(150), press(kDown), wait(150);
            press(kX);
            at("enhanced_options_screen");
            wait(1000);
            press(kTri);
            at("options_screen");
            press(kTri);
            at("main_screen");
            wait(2000);  // the main menu's entrance (a press during it is lost)
            for (int i = 0; i < 6; ++i) press(kUp), wait(400);  // OPTIONS -> QUICK PLAY
        }
        press(kUp);  // CAREER (main_career.btn)
        wait(150);
        press(kX);
        at("chooseprof_screen");  // a save always has a band (The Glitches, the last slot)
        press(kX);  // cp_band0.btn: NEW BAND
        at("nameprof_screen");
        wait(600);
        press(kX);  // the band's name: "A"
        wait(300);
        check("name_entry_1");
        press(kX);  // "AA": the name grows to the right of where it was
        wait(300);
        check("name_entry_2");
        press(kTri);  // back to "A"
        wait(300);
        key(SDLK_RETURN);  // the keyboard's Enter is Start here: finishes the name (adds no letter)
        at("sel_difficulty_screen");
        check("band_named_A");
        if (scenario == "career") {
            // Back to Choose Band (ui_exit_back: meta_light_off) and the band
            // again: the difficulty screen comes back lit, as GH2 reloads
            // meta_bricks once no panel holds it (GD02-077 / GD02-081).
            wait(800);
            check("bricks_record");
            press(kTri);
            at("chooseprof_screen");
            press(kX);  // cp_band0.btn: the band "A"
            at("sel_difficulty_screen");
            wait(800);
            check("bricks_same");
        }
        if (scenario == "career_win") press(kUp), wait(150);  // Medium -> Easy
        if (scenario == "career_song") {
            // GD05_WALK_DIFF=0..3 (default Medium, the focus).
            const char* d = SDL_getenv("GD05_WALK_DIFF");
            const int diff = d ? std::clamp(std::atoi(d), 0, 3) : 1;
            for (int i = diff; i < 1; ++i) press(kUp), wait(150);
            for (int i = 1; i < diff; ++i) press(kDown), wait(150);
        }
        press(kX);  // Medium (the profile's last difficulty)
        at("sel_character_new_screen");
        check("locked_portraits");
        wait(1500);  // the character opens the door
        if ((scenario == "career_song" && SDL_getenv("GD05_WALK_CHAR")) || scenario == "career_venues" || scenario == "career_venues_input") {
            // GD05_WALK_CHAR=<downs> from Punk, GD05_WALK_GUITAR=<downs> from
            // the Les Paul; the first outfit / skin when there are two.
            const char* g = SDL_getenv("GD05_WALK_GUITAR");
            for (int i = SDL_getenv("GD05_WALK_CHAR") ? std::atoi(SDL_getenv("GD05_WALK_CHAR")) : 0; i > 0; --i) press(kDown), wait(300);
            press(kX);
            wait(1200);
            check("confirm_on sel_character_new_screen");
            at("sel_guitar_new_screen");
            for (int i = g ? std::atoi(g) : 0; i > 0; --i) press(kDown), wait(300);
            press(kX);
            wait(900);
            check("confirm_on sel_guitar_new_screen");
        } else {
        press(kDown);  // Punk -> Alterna (Judy Nails)
        wait(300);
        press(kX);  // two outfits (alterna2 bought): pick one
        wait(800);
        press(kDown);  // outfit1.btn -> outfit2.btn (alterna2)
        wait(800);
        press(kX);
        at("sel_guitar_new_screen");
        press(kDown);  // Les Paul -> SG
        wait(300);
        press(kX);  // two skins (sg_white bought): pick one
        wait(600);
        press(kDown);  // sg_cherry -> sg_white
        wait(300);
        press(kX);
        }
        at("tour_start_screen");
        press(kX);
        at("career_screen");
        expect_ = {"main_screen", "chooseprof_screen", "nameprof_screen", "sel_difficulty_screen", "sel_character_new_screen", "sel_guitar_new_screen",
                   "tour_start_screen", "saving_screen", "career_screen"};
        if (scenario == "career")
            expect_ = {"main_screen",          "chooseprof_screen",       "nameprof_screen",       "sel_difficulty_screen",
                       "chooseprof_screen",    "sel_difficulty_screen",   "sel_character_new_screen", "sel_guitar_new_screen",
                       "tour_start_screen",    "saving_screen",           "career_screen"};
        if (scenario == "career_song") {
            // One career song (GD05_WALK_SONG, default strutter) at
            // GD05_WALK_DIFF: every tier song before it passed with GH2's
            // cheat_beat_song (its venue unlocked), set_career_venue /
            // set_career_song offer it, it is played (autoplay; GD05_WALK_END
            // <song ms> ends it early) and its endgame screens followed back
            // towards the career menu.
            if (const char* e = SDL_getenv("GD05_WALK_END")) endMs_ = float(std::atof(e));
            check("career_target");
            press(kX);  // LET'S ROCK
            at("sel_venue_screen");
            // The newest unlocked venue has the focus (set_career_venue); on
            // Easy a tier opens after 4 of its songs, so the target can be
            // in an older one, and not the first left in the list (GD02-088).
            wait(600);
            check("career_pick_venue", 20000);
            press(kX);
            at("sel_song_screen");
            wait(600);
            check("career_pick_song", 20000);
            check("career_song_target");
            wait(600);  // the list still scrolling takes no select
            press(kX);
            at("loading_screen");
            at("game_screen", 30000);
            if (const char* f = SDL_getenv("GD05_WALK_FAIL")) {
                // GD05_WALK_FAIL=<song ms>: the song fails then; RESTART (the
                // lose screen's focus) plays it again.
                failMs_ = float(std::atof(f));
                autoplay_ = false;
                at("lose_screen", 900000);
                press(kX);
                at("game_screen", 30000);
                wait(8000);
            } else {
                at("endgame_screen|endgame_encore_screen|win_game_screen", 900000);  // win_game: the tour's last song
                for (int i = 0; i < 6; ++i) wait(2500), press(kX);
                wait(4000);
            }
            expect_.insert(expect_.end(), {"sel_venue_screen", "sel_song_screen", "loading_screen", "game_screen"});
        }
        if (scenario == "career_venues") {
            // The venue select follows the focus (GD02-144): every venue
            // unlocked (stone's songs offered), then down from stone to the
            // battle and back up: at each press the focus, the game's venue,
            // the map (venue_anims.grp scrolled to the venue's frame,
            // career.dta's FOCUS_MSG) and the focused poster's state.
            winTarget_ = "beastandtheharlot";
            check("career_target");
            press(kX);  // LET'S ROCK
            at("sel_venue_screen");
            wait(1500);
            check("venue_follow");
            for (int i = 0; i < 7; ++i) press(kDown), wait(900), check("venue_follow");
            for (int i = 0; i < 3; ++i) press(kUp), wait(900), check("venue_follow");
            // A career song in that venue (big), quit from the pause; then
            // quick play plays in its song's venue (set_quickplay
            // 0x822B86D0), not the career's last.
            press(kX);
            at("sel_song_screen");
            wait(800);
            press(kX);
            at("loading_screen");
            at("game_screen", 30000);
            check("career_venue_loaded big");
            wait(3000);
            press(kStart);
            at("pause_screen");
            press(kUp);  // QUIT
            wait(150);
            press(kX);
            at("pause_confirm_quit_screen");
            press(kX);
            at("meta_loading_screen");
            at("sel_song_screen", 30000);
            wait(800);
            press(kTri);
            at("sel_venue_screen");
            wait(800);
            press(kTri);
            at("career_screen");
            press(kTri);
            at("sel_difficulty_screen");
            wait(1200);
            check("difficulty_status");  // the band's songs passed, per difficulty
            press(kTri);
            at("chooseprof_screen");
            press(kTri);
            quickplaySong(0);
            check("quickplay_venue");
            expect_.insert(expect_.end(), {"sel_venue_screen", "sel_song_screen", "loading_screen", "game_screen", "pause_screen",
                                           "pause_confirm_quit_screen", "meta_loading_screen", "sel_song_screen", "sel_venue_screen",
                                           "career_screen", "sel_difficulty_screen", "chooseprof_screen", "main_screen",
                                           "qp_selsong_screen", "qp_diff_screen", "loading_screen", "game_screen"});
        }
        if (scenario == "career_venues_input") {
            // The venue select with every way of moving (GD02-144): quick
            // presses (one every 100 ms), a press while the map still
            // scrolls (checked mid-way), the help bar's UP/DOWN pill clicked
            // (lower half down, upper half up), and back to the top.
            winTarget_ = "beastandtheharlot";
            check("career_target");
            press(kX);  // LET'S ROCK
            at("sel_venue_screen");
            wait(1500);
            check("venue_follow");
            for (int i = 0; i < 3; ++i) press(kDown), wait(100);
            wait(900);
            check("venue_follow");  // three venues down: theatre
            press(kDown);
            wait(120);
            check("venue_scrolling");  // between theatre (5) and big (4)
            press(kDown);
            wait(900);
            check("venue_follow");  // small2
            check("click_updown down");
            wait(900);
            check("venue_follow");  // small1
            check("click_updown up");
            wait(900);
            check("venue_follow");  // small2
            for (int i = 0; i < 7; ++i) press(kUp), wait(250);
            wait(900);
            check("venue_follow");  // stone (the top; up from it stays)
            expect_.insert(expect_.end(), {"sel_venue_screen"});
        }
        if (scenario == "career_win" || scenario == "career_win_medium") {
            // The tour won (GD02-023): every tier song before the tour's last
            // passed with cheat_beat_song, the last one offered and played
            // (cut short, a win). At Easy the tour ends with arena (numTiers
            // 0x822A2BC8: one tier less) and Easy has no encores, so arena's
            // fifth song (required 5) psychobilly completes it; above Easy
            // stone needs all six: hangar18 played, then its encore freebird
            // accepted and played [C]. win_game_screen: beat_easy /
            // beat_other and the crowd's crowd_win, then the results.
            const bool easy = scenario == "career_win";
            winTarget_ = easy ? "psychobilly" : "hangar18";
            endMs_ = 30000;
            check("career_target");
            press(kX);  // LET'S ROCK
            at("sel_venue_screen");
            press(kX);
            at("sel_song_screen");
            check("career_song_target");
            press(kX);
            at("loading_screen");
            at("game_screen", 30000);
            if (!easy) {
                at("endgame_encore_screen", 90000);
                wait(1500);
                // Free Bird is the tour's last song: the letter is
                // endgame_encore_freebird1 and YES asks three times
                // (yes_freebird1..3.btn, freebird1..3.grp) before it starts.
                check("freebird 0");
                for (int i = 1; i <= 3; ++i) {
                    press(kX);
                    wait(800);
                    check(("freebird " + std::to_string(i)).c_str());
                }
                press(kX);  // yes_freebird3.btn: START_ENCORE
                at("loading_screen", 30000);
                at("game_screen", 30000);
            }
            at("win_game_screen", 120000);
            wait(1500);
            check(easy ? "tour_won easy" : "tour_won other");
            wait(5500);  // WIN_GAME_INPUT_DELAY_SEC (6 s from the screen's entrance)
            press(kX);
            // (The save's The Glitches has pro face-off already, so
            // unlock_pfo_screen, only for a new unlock, does not come.)
            at("endgame_screen", 20000);
            expect_.insert(expect_.end(), {"sel_venue_screen", "sel_song_screen", "loading_screen", "game_screen", "post_show_screen",
                                           "win_game_screen", "endgame_screen"});
        }
        if (scenario == "career_practice") {
            // Practice from a lost career song (lose.dta goto_practice_mode:
            // practice_sel_section_screen's career_mode): the lose screen's
            // career buttons, the section at 100%, the pause's career buttons
            // (pause.dta: resume_career, change_speed_career, quit_career)
            // and back to the career menu (resume_from_practice).
            failMs_ = 8000;
            autoplay_ = false;
            press(kX);  // LET'S ROCK
            at("sel_venue_screen");
            press(kX);
            at("sel_song_screen");
            press(kX);
            at("loading_screen");
            at("game_screen", 30000);
            at("lose_screen", 60000);
            wait(800);
            check("focus_to lose_practice_career.btn", 5000);
            press(kX);
            at("meta_loading_screen");
            at("practice_sel_section_screen", 30000);
            wait(1000);
            press(kDown), wait(300);
            press(kX), wait(600), press(kX);
            at("practice_sel_speed_screen");
            press(kX);
            at("practice_loading_screen", 30000);
            at("practice_game_screen", 30000);
            wait(3000);
            press(kStart);
            at("pract_pause_screen");
            check("focus_to resume_career.btn", 3000);
            check("focus_to change_speed_career.btn", 5000);
            press(kX);
            at("practice_change_speed_screen");
            press(kTri);  // back to the pause
            at("pract_pause_screen");
            check("focus_to quit_career.btn", 5000);
            press(kX);
            at("meta_loading_screen");
            at("career_screen", 30000);
            check("practice_career_back");
            expect_.insert(expect_.end(), {"sel_venue_screen", "sel_song_screen", "loading_screen", "game_screen", "lose_screen",
                                           "meta_loading_screen", "practice_sel_section_screen", "practice_sel_speed_screen",
                                           "practice_loading_screen", "practice_game_screen", "pract_pause_screen",
                                           "practice_change_speed_screen", "pract_pause_screen", "meta_loading_screen", "career_screen"});
        }
        if (scenario == "career_encore") {
            // career_full's tier to its encore offer (salvation played), declined:
            // the venue's encore_no (endgame.dta no.btn), then the results.
            endMs_ = 35000;
            check("career_seed");
            press(kX);  // LET'S ROCK
            at("sel_venue_screen");
            press(kX);
            at("sel_song_screen");
            wait(900);
            check("career_song_salvation");
            press(kX);
            at("loading_screen");
            at("game_screen", 30000);
            at("endgame_encore_screen", 90000);
            wait(1500);
            check("encore_decline");  // down to NO
            wait(400);
            check("encore_decline");  // select it
            at("endgame_screen", 20000);
            wait(500);
            check("encore_declined");
            expect_.insert(expect_.end(), {"sel_venue_screen", "sel_song_screen", "loading_screen", "game_screen", "post_show_screen",
                                           "endgame_encore_screen", "endgame_screen"});
        }
        if (scenario == "career_full") {
            // The first tier at Medium: three songs passed with GH2's own
            // debug cheat (campaign cheat_beat_song), the fourth played (it
            // unlocks the encore), the encore played (the tier complete, the
            // next venue unlocked), then the store.
            endMs_ = 35000;
            check("career_seed");
            press(kX);  // LET'S ROCK
            at("sel_venue_screen");
            wait(900);  // (its screenshot)
            press(kX);  // the venue (set_career_venue: the newest unlocked)
            at("sel_song_screen");
            wait(900);
            check("career_song_salvation");
            check("setlist_encore");
            press(kX);
            at("loading_screen");
            at("game_screen", 30000);
            at("endgame_encore_screen", 90000);
            check("career_encore_offered");
            wait(1500);
            press(kX);  // yes.btn: the encore
            at("loading_screen", 30000);
            at("game_screen", 30000);
            at("endgame_screen", 90000);
            wait(1500);
            press(kX);  // CONTINUE
            at("status_screen");  // the tier complete: the next sponsorship
            check("career_tier");
            wait(1000);
            press(kX);
            at("unlock_venue_screen");  // the next venue (its 3 s animation, then on)
            wait(1600);
            check("shot_unlock_venue");
            at("complete_screen", 20000);
            wait(900);
            press(kX);  // CONTINUE: the career menu
            at("career_screen", 30000);
            for (int i = 0; i < 3; ++i) press(kDown), wait(150);  // LET'S ROCK -> ... -> STORE
            press(kX);
            at("store_screen");
            wait(1000);
            press(kDown);  // GUITARS -> FINISHES
            wait(300);
            press(kX);
            wait(1500);
            check("shot_store_item");
            press(kX);  // buy the first finish
            at("store_bought_screen");
            wait(900);
            press(kX);
            at("store_screen");
            check("career_bought");
            press(kTri);  // the categories
            wait(800);
            press(kDown), wait(200), press(kDown), wait(200);  // FINISHES -> SONGS -> CHARACTERS
            press(kX);
            wait(3000);  // the character loads and walks in
            check("shot_store_character");
            press(kTri);
            wait(800);
            press(kTri);  // the career menu (autosave)
            at("career_screen", 30000);
            expect_.insert(expect_.end(), {"sel_venue_screen", "sel_song_screen", "loading_screen", "game_screen", "post_show_screen",
                                           "endgame_encore_screen", "loading_screen", "game_screen", "post_show_screen", "endgame_screen",
                                           "status_screen", "unlock_venue_screen", "complete_screen", "career_screen", "store_screen",
                                           "store_bought_screen", "store_screen", "career_screen"});
        }
    } else if (scenario == "back") {
        at("main_screen");
        check("camera_record");
        for (int i = 0; i < 4; ++i) press(kDown), wait(150);  // QUICK PLAY -> MULTIPLAYER -> TRAINING -> LEADERBOARDS -> OPTIONS
        press(kX);
        at("options_screen");
        wait(800);
        check("bricks_record");
        press(kX);  // Audio Settings (the focus)
        at("audio_settings_screen");
        press(kTri);
        at("options_screen");
        press(kTri);
        at("main_screen");
        check("camera_same");
        // The options again: the bricks' light as the first time (meta_bricks
        // is one directory: main's ui_enter_back turns it on again for every
        // panel showing it after options' ui_exit_back turned it off).
        for (int i = 0; i < 4; ++i) press(kDown), wait(150);
        press(kX);
        at("options_screen");
        wait(800);
        check("bricks_same");
        press(kTri);
        at("main_screen");
        expect_ = {"main_screen", "options_screen", "audio_settings_screen", "options_screen", "main_screen", "options_screen", "main_screen"};
    } else if (scenario == "enhanced") {
        at("main_screen");
        for (int i = 0; i < 4; ++i) press(kDown), wait(150);  // OPTIONS (past MULTIPLAYER)
        press(kX);
        at("options_screen");
        press(kDown), wait(150), press(kDown), wait(150);  // Audio Settings -> Video Settings -> Enhanced Options
        press(kX);
        at("enhanced_options_screen");
        check("enhanced_default");
        check("backdrop_band");
        wait(500);  // a few frames through the default effects
        check("enhanced_fx_default");
        // Each Enhanced Option in turn (GDALL-015, universal_mechanisms.md
        // 1.2.2): toggled or stepped with the pad, saved, and its effect
        // checked. A step: down, then a press.
        auto row = [&](int button) { press(kDown), wait(120), press(button), wait(150); };
        auto again = [&](int button) { press(button), wait(150); };
        // MODES: Steam Deck (the focus), Original Feel (the window and the
        // resolution kept), Good Old Days, then Original Feel again.
        press(kX), wait(400);
        check("enhanced_mode_deck");
        row(kX), wait(300);
        check("enhanced_mode_original");
        for (int i = 0; i < 5; ++i) press(kDown), wait(120);  // Clean, Max HD, Max 4K, Budget, Good Old Days
        press(kX), wait(800);
        check("enhanced_mode_old_days");
        for (int i = 0; i < 5; ++i) press(kUp), wait(120);
        press(kX), wait(300);
        // CONTROLS: past the modes -> Gamepads -> Mouse (kept) -> Touch on
        // (XInput and Rock Band guitars: always on, no rows).
        for (int i = 0; i < 9; ++i) press(kDown), wait(120);
        press(kX), wait(150);
        check("enhanced_controls");
        // Audio / Video Calibration: 8 presses on the ticks (a beat every
        // 750 ms after 1 s, 3 lead-in beats), then 8 on the flashes (1.5 s
        // later), then select closes the result.
        press(kDown), wait(120);
        press(kX), mark();
        for (int i = 0; i < 8; ++i) until(2600 + i * 750.0), press(kX);
        mark();
        for (int i = 0; i < 8; ++i) until(3100 + i * 750.0), press(kX);
        wait(750);
        check("enhanced_calibrated");
        press(kX), wait(200);
        // Reset Calibration, the next row (#47): both delays back to 0 ms.
        press(kDown), wait(120);
        press(kX), wait(300);
        check("enhanced_calibration_reset");
        // PERFORMANCE & GRAPHICS -> Graphics Backend -> Audio Backend (the
        // section's first rows, kept at their defaults) -> Screen Mode -> Resolution;
        // select opens its dropdown, down and select pick the next option.
        for (int i = 0; i < 5; ++i) press(kDown), wait(120);
        press(kX);
        wait(200);
        press(kDown), wait(120);
        press(kX);  // 448p (Good Old Days') -> 480p
        wait(300);
        check("enhanced_changed");
        // From the original look: Sharper Text on and off again, FXAA, FSR 1
        // (Upscaling Quality appears) at Performance, (Max FPS, FPS Counter
        // kept), Bokeh None, Particles None, Bloom 2x, Cinematic Color on,
        // Color Grading Warm, Film Grain Medium, Chromatic Aberration Light,
        // Ambient Occlusion Medium, Sharpening High, Blur on Star Power
        // Medium.
        row(kX), again(kX), row(kRight), row(kRight), row(kRight);
        press(kDown), wait(120), press(kDown), wait(120);
        // HDR (universal_mechanisms.md "HDR"): Emulated, True HDR (the window
        // was made without an HDR surface: the documented fallback to
        // Emulated, with why), None.
        row(kRight), wait(400);
        check("enhanced_hdr_emulated");
        again(kRight), wait(400);
        check("enhanced_hdr_true");
        again(kRight), wait(400);
        check("enhanced_hdr_none");
        // Graphics / Audio Backend (GDCORE-008): still the defaults, Vulkan and
        // miniaudio (the walk never touched them; the backends walk does).
        check("enhanced_backends");
        row(kLeft), again(kLeft), row(kLeft), row(kRight), again(kRight), row(kX), row(kRight), row(kRight), again(kRight), row(kRight),
            row(kRight), again(kRight), row(kRight), again(kRight), row(kRight), again(kRight);
        wait(500);
        check("enhanced_graphics");
        // The venue extras at their defaults (off), then Band Shadows on, More
        // Lights 2x, Dust 1.5x, Shaky Camera off.
        check("enhanced_lights_default");
        row(kX), row(kRight), again(kRight), row(kRight), row(kX);
        wait(400);
        check("enhanced_enhancers");
        // Remove All Effects on: every effect off, GH2's own too; then off again.
        row(kX);
        wait(400);
        check("enhanced_no_effects");
        press(kX), wait(400);
        check("enhanced_effects_back");
        // ACCESSIBILITY > Easy Chords (GDALL-039): off on a fresh save; on,
        // saved, exported; turned off again, Load User Settings brings it back.
        check("enhanced_easy_chords_default");
        press(kDown), wait(120);
        row(kX);
        wait(300);
        check("enhanced_easy_chords");
        // CHEATS & ENHANCERS: Export User Files, Load User Settings, Load
        // User Progress (GD05_DIALOG: the walk's folder).
        press(kDown), wait(120);
        row(kX);
        wait(300);
        check("enhanced_exported");
        press(kUp), wait(120), press(kUp), wait(120), press(kX), wait(300);
        check("enhanced_easy_chords_off");
        press(kDown), wait(120), press(kDown), wait(120);
        row(kX);
        wait(300);
        check("enhanced_settings_loaded");
        row(kX);
        wait(300);
        check("enhanced_progress_loaded");
        // Enable Cut Content (not built: stays off), No Fail Mode, Autoplay,
        // Forgiving Hammer-ons and Pull-offs.
        row(kX), row(kX), row(kX), row(kX);
        check("enhanced_cheats");
        // The port's cheats back off (the walk's own save; tidy).
        press(kUp), wait(120), press(kX), wait(150), press(kUp), wait(120), press(kX), wait(150);
        press(kTri);
        at("options_screen");
        press(kX);  // Enhanced Options again (the focus): the band again
        at("enhanced_options_screen");
        check("backdrop_band");
        press(kTri);
        at("options_screen");
        press(kTri);
        at("main_screen");
        expect_ = {"main_screen", "options_screen", "enhanced_options_screen", "options_screen", "enhanced_options_screen", "options_screen",
                   "main_screen"};
    } else if (scenario == "enhanced_backends") {
        // Enhanced > Graphics / Audio Backend (GDCORE-008, universal-menus.md
        // "Backends"). With GD05_WALK_ENHANCED_SEED holding unknown backends:
        // they are dropped at start (the defaults, Vulkan and miniaudio, D71).
        // The two rows are the first of PERFORMANCE & GRAPHICS; Right steps
        // Graphics to SDL3, then OpenGL3 (each change asks Restart Now /
        // Later: back is Later), the Audio dropdown picks OpenAL (Later
        // again): saved, still the defaults in this run. Then Graphics back
        // to SDL3 and Restart Now: the game starts again
        // (enhanced_backends_restarted).
        at("main_screen");
        for (int i = 0; i < 4; ++i) press(kDown), wait(150);  // OPTIONS
        press(kX);
        at("options_screen");
        press(kDown), wait(150), press(kDown), wait(150);
        press(kX);
        at("enhanced_options_screen");
        check("enhanced_backends_start");
        for (int i = 0; i < 14; ++i) press(kDown), wait(120);  // MODES > Steam Deck down to Graphics Backend
        press(kRight), wait(300);  // SDL3: the pop-up
        press(kTri), wait(300);    // Later
        press(kRight), wait(300);  // OpenGL3
        press(kTri), wait(300);
        press(kDown), wait(120);
        press(kX), wait(200);  // Audio Backend's dropdown
        press(kDown), wait(120);
        press(kX), wait(300);  // OpenAL
        press(kTri), wait(300);
        at("enhanced_options_screen");
        check("enhanced_backends_saved");
        press(kUp), wait(120);
        press(kLeft), wait(300);  // Graphics: SDL3
        press(kX);                // Restart Now (focused)
        expect_ = {"main_screen", "options_screen", "enhanced_options_screen"};
    } else if (scenario == "enhanced_backends_restarted") {
        // The game started again by Restart Now: straight on Enhanced Options,
        // on the saved backends (SDL3 hands over to the next backend where
        // it cannot start).
        at("enhanced_options_screen");
        check("enhanced_backends_restarted");
        expect_ = {"enhanced_options_screen"};
    } else if (scenario == "mouse") {
        at("main_screen");
        wait(300);  // a frame drawn: the components' bounds are known
        mouseOn("main_options.btn", 0);
        wait(200);
        check("focus_main_options");
        mouseOn("main_options.btn", 1);
        at("options_screen");
        wait(300);
        mouseOn("port_enhanced.btn", 1);
        at("enhanced_options_screen");
        wait(300);
        // MODES > Clean Image (the list's 4th row, every section open).
        mouseAt(300, 297, 0);
        mouseAt(300, 297, 1);
        wait(300);
        check("enhanced_mode_clean");
        // MODES collapsed (a click on its title): Graphics Backend is the
        // list's 11th row, hovered; the wheel moves past Audio Backend and
        // Screen Mode to Resolution, scrolled into its place, and the small
        // nudge after it keeps that focus (#51), so select opens its
        // dropdown, upward: 720p 180 units above the row.
        mouseAt(300, 190, 0);
        mouseAt(300, 190, 1);
        wait(300);
        mouseAt(480, 523, 0);
        wait(200);
        for (int i = 0; i < 3; ++i) mouseAt(480, 523, 5), wait(200);
        wait(400);
        mouseAt(483, 525, 0);
        press(kX);  // the focused row's dropdown: Resolution's only if the nudge kept the focus
        wait(200);
        mouseAt(480, 375, 0);
        mouseAt(480, 375, 1);
        wait(300);
        check("enhanced_res_720");
        // The scrollbar's bottom: the list's end, Monkey Head Crowd clicked on, then off.
        mouseAt(66, 572, 1);
        wait(800);
        mouseAt(300, 555, 0);
        mouseAt(300, 555, 1);
        wait(200);
        check("cheat_menu_monkey");
        mouseAt(300, 555, 1);
        wait(200);
        // The Back button under the list.
        mouseAt(150, 608, 0);
        mouseAt(150, 608, 1);
        at("options_screen");
        wait(300);
        check("click_help1");  // the help bar's BACK
        at("main_screen");
        expect_ = {"main_screen", "options_screen", "enhanced_options_screen", "options_screen", "main_screen"};
    } else if (scenario == "gamepads") {
        at("main_screen");
        check("gp_p1_keyboard");  // the keyboard is player 1 (whatever pads this machine has)
        key(SDLK_3);  // the keyboard's yellow fret
        at("gamepads_screen", 3000);
        // The Gamepad menu (universal_mechanisms.md 1.2.1): Player 1 ->
        // Instrument -> Bind to -> Customize inputs, then INPUTS MAPPING.
        for (int i = 0; i < 3; ++i) press(kDown), wait(80);
        press(kX), wait(200);
        check("gp_editing");
        for (int i = 0; i < 13; ++i) press(kDown), wait(60);  // Start .. Whammy (the headings are skipped)
        press(kRight), wait(80);  // Key 2
        press(kX);  // waits for a key
        wait(200);
        key(SDLK_E);
        wait(300);
        check("whammy_e_unsaved");  // works at once, written only on Save
        for (int i = 0; i < 4; ++i) press(kDown), wait(60);  // Gyro, Require Strum, Reset, Save
        press(kX), wait(300);
        check("whammy_is_e");
        press(kTri);
        at("main_screen");
        wait(300);
        check("click_help1");  // the help bar's GAMEPADS
        at("gamepads_screen", 3000);
        expect_ = {"main_screen", "gamepads_screen", "main_screen", "gamepads_screen"};
    } else if (scenario == "gamepads_pad") {
        // A pad plugged in takes player 1 (the keyboard player 2); Player 2's
        // Bind to > Detect, then the pad's A: the pad is player 2.
        at("main_screen");
        check("vpad_attach");
        wait(600);
        key(SDLK_3);
        at("gamepads_screen", 3000);
        for (int i = 0; i < 6; ++i) press(kDown), wait(80);  // Player 2's Bind to
        press(kX), wait(150);  // its dropdown (on the keyboard)
        press(kUp), wait(80), press(kUp), wait(80);  // None, then round to Detect
        press(kX), wait(150);
        check("gp_detecting");
        check("vpad_press");
        wait(300);
        check("pad_is_p2");
        press(kTri);
        at("main_screen");
        expect_ = {"main_screen", "gamepads_screen", "main_screen"};
    } else if (scenario == "band") {
        // Band naming with real keys (universal_mechanisms.md 1.1.2): the
        // arrows change the letter (each one step: menu Up / Down and the
        // strum are one key, no double input), green (1) takes it, Enter
        // (Start) finishes the name.
        at("splash_screen", 30000);
        key(SDLK_RETURN);  // any button
        at("startup_create_save_screen");
        press(kX);
        at("saving_screen");
        at("guitar_help_screen");
        press(kX);
        at("main_screen");
        press(kUp);  // CAREER
        wait(150);
        press(kX);
        at("chooseprof_screen");
        press(kX);  // NEW BAND
        at("nameprof_screen");
        wait(600);
        key(SDLK_DOWN), wait(250), key(SDLK_DOWN), wait(250);  // A -> C
        key(SDLK_1), wait(250);                                // "C"
        key(SDLK_DOWN), wait(250);                             // D
        key(SDLK_1), wait(250);                                // "CD"
        key(SDLK_X), wait(150), key(SDLK_BACKSPACE), wait(150);  // typed (GD02-085): "CDX", "CD"
        key(SDLK_E), wait(150);                                // typed: "CDE"
        key(SDLK_RETURN);
        at("sel_difficulty_screen");
        check("band_named_CDE");
        expect_ = {"splash_screen", "startup_load_finder_screen", "startup_create_save_screen", "saving_screen", "guitar_help_screen",
                   "main_screen", "chooseprof_screen", "nameprof_screen", "sel_difficulty_screen"};
    } else if (scenario == "logo_skip") {
        // Logos and credits skip (universal_mechanisms.md 1.1.1): the boot
        // splashes by the red fret (2), the credits by the green fret (1),
        // then again by a click.
        wait(300);
        key(SDLK_2);
        check("splashes_skipped", 1000);
        at("splash_screen", 30000);
        press(kX);
        at("startup_create_save_screen");
        press(kX);
        at("saving_screen");
        at("guitar_help_screen");
        press(kX);
        at("main_screen");
        for (int i = 0; i < 4; ++i) press(kDown), wait(150);  // OPTIONS
        press(kX);
        at("options_screen");
        wait(300);
        mouseOn("op_credit.btn", 1);
        at("credits_screen");
        wait(1500);
        key(SDLK_1);
        at("options_screen");
        wait(300);
        mouseOn("op_credit.btn", 1);
        at("credits_screen");
        wait(1500);
        mouseAt(640, 360, 1);
        at("options_screen");
        expect_ = {"cut_scene_screen", "splash_screen", "startup_load_finder_screen", "startup_create_save_screen", "saving_screen",
                   "guitar_help_screen", "main_screen", "options_screen", "credits_screen", "options_screen", "credits_screen",
                   "options_screen"};
    } else if (scenario == "stickyaxis") {
        at("main_screen");
        check("adapter_attach");
        wait(2000);
        check("no_sideways");
        check("adapter_move");
        wait(300);
        check("one_right");
        expect_ = {"main_screen"};
    } else if (scenario == "cheats") {
        // GH2's codes at the main menu (config/long_cheats.dta), a press every 120 ms.
        auto code = [&](std::initializer_list<int> buttons) {
            for (int b : buttons) press(b), wait(120);
        };
        at("main_screen");
        code({kYellow, kBlue, kYellow, kOrange, kYellow, kBlue});  // Air Guitar
        check("cheat_air_on");
        code({kBlue, kOrange, kYellow, kOrange, kBlue, kOrange, kYellow, kYellow});  // Hyper Speed
        check("cheat_hyper_on");
        wait(900);  // more than 750 ms: the next press starts afresh
        code({kYellow, kBlue, kYellow, kOrange, kYellow, kBlue});  // Air Guitar again: off
        check("cheat_air_off");
        // The same cheats in Enhanced Options > CHEATS (up from Gamepads
        // wraps to the list's end): Monkey Head Crowd on, Eyeball Head Crowd
        // on (one crowd head at a time), Flaming Head on, Hyper Speed Mode off again.
        wait(900);
        for (int i = 0; i < 4; ++i) press(kDown), wait(150);  // OPTIONS
        press(kX);
        at("options_screen");
        press(kDown), wait(150), press(kDown), wait(150);
        press(kX);
        at("enhanced_options_screen");
        for (int i = 0; i < 2; ++i) press(kUp), wait(120);  // MODES, then round to the list's end
        press(kX), wait(150);
        check("cheat_menu_monkey");
        press(kUp), wait(120), press(kX), wait(150);
        press(kUp), wait(120), press(kX), wait(150);
        press(kUp), wait(120), press(kX), wait(150);
        check("cheat_menu_rest");
        press(kTri);
        at("options_screen");
        press(kTri);
        at("main_screen");
        expect_ = {"main_screen", "options_screen", "enhanced_options_screen", "options_screen", "main_screen"};
    } else if (scenario == "coop" || scenario == "faceoff" || scenario == "proface") {
        endMs_ = 20000;
        at("splash_screen", 30000);
        press(kX);
        at("startup_create_save_screen");
        press(kX);
        at("saving_screen");
        at("guitar_help_screen");
        press(kX);
        at("main_screen");
        if (scenario == "proface") check("proface_seed");
        press(kDown);  // QUICK PLAY -> MULTIPLAYER
        wait(150);
        press(kX);
        at("multi_controller_select_screen");
        pressP(kUp, 0), wait(150), pressP(kX, 0), wait(150);    // pad 0 to P1
        pressP(kDown, 1), wait(150), pressP(kX, 1);              // pad 1 to P2
        at("multi_screen");
        wait(300);
        const int modeDowns = scenario == "coop" ? 0 : scenario == "faceoff" ? 1 : 2;
        for (int i = 0; i < modeDowns; ++i) press(kDown), wait(150);
        press(kX);
        // Both players' characters (the first each: one outfit) and guitars (twice: a
        // second X takes the skin when the guitar has several).
        auto charsAndGuitars = [&]() {
            at("multi_sel_character_screen");
            wait(1500);
            pressP(kDown, 1), wait(600);  // P2 to the next character
            pressP(kX, 0), wait(900);
            check("shot_multi_char_chosen");
            pressP(kX, 0), wait(900);
            pressP(kX, 1), wait(900), pressP(kX, 1);
            at("multi_sel_guitar_screen");
            wait(900);
            check("shot_multi_sel_guitar");
            pressP(kX, 0), wait(300), pressP(kX, 1), wait(600);
            check("multi_guitar_skins");  // a guitar with several skins: its skin too
        };
        auto venueAndSong = [&](const char* venue, const char* song) {
            at(venue);
            press(kX);
            at(song);
            wait(600);
            press(kX);  // the first song: surrender
        };
        if (scenario == "coop") {
            venueAndSong("multi_coop_venue_screen", "multi_coop_selsong_screen");
            at("selpart_screen");
            pressP(kX, 0), wait(700), pressP(kX, 1);  // P1 the guitar (part0), P2 the bass (part1)
            at("multi_coop_seldiff_screen");
            pressP(kUp, 0), wait(150), pressP(kX, 0), wait(700), pressP(kX, 1);  // P1 Easy, P2 Medium
            charsAndGuitars();
        } else if (scenario == "faceoff") {
            charsAndGuitars();
            venueAndSong("multi_vs_venue_screen", "multi_vs_selsong_screen");
            at("multi_vs_seldiff_screen");
            pressP(kUp, 0), wait(150), pressP(kX, 0), wait(700), pressP(kX, 1);  // P1 Easy, P2 Medium
        } else {
            charsAndGuitars();
            venueAndSong("multi_fo_venue_screen", "multi_fo_selsong_screen");
            at("multi_fo_seldiff_screen");
            press(kUp), wait(150), press(kX);  // Easy for both
        }
        at("game_screen", 60000);
        check("multi_setup");
        if (scenario == "faceoff") {
            // Two-player Star Power (hud_panel.dta star_power_ready, player.dta
            // stop_using: play_game_sfx_player's _multi sounds when {game
            // multiplayer}): both meters filled with GH2's fill_star_power
            // (its S debug key, cheats_funcs.dta fill_star_meter) and a
            // deploy rate of a meter per measure, P1 deploys (Select) at 9 s.
            songKeys_ = "9000:starpower";
            check("sp_multi_fill", 30000);
        }
        const char* results = scenario == "coop" ? "multi_compete_coop_screen" : scenario == "faceoff" ? "multi_compete_screen" : "multi_compete_fo_screen";
        at(results, 90000);
        wait(1500);
        if (scenario == "coop") check("coop_record");
        if (scenario == "faceoff") check("sp_multi_sounds");
        press(kX);  // CONTINUE (me_continue.btn)
        if (scenario == "coop") {
            at("multi_unlock_bass_screen");  // surrender's three stars or more: bass_sg_mahogany (songs 1, stars 3)
            wait(1200);
            press(kX);
        }
        at("complete_screen", 20000);
        wait(600);
        press(kX);  // the song list again (comp_selsong.btn): co-op's record autosaved on the way
        if (scenario == "coop") at("saving_screen", 20000);
        const char* again = scenario == "coop" ? "multi_coop_selsong_screen" : scenario == "faceoff" ? "multi_vs_selsong_screen" : "multi_fo_selsong_screen";
        at(again, 30000);
        expect_ = {"main_screen", "multi_controller_select_screen", "multi_screen"};
        if (scenario == "coop")
            expect_.insert(expect_.end(), {"multi_coop_venue_screen", "multi_coop_selsong_screen", "selpart_screen", "multi_coop_seldiff_screen",
                                           "multi_sel_character_screen", "multi_sel_guitar_screen", "loading_screen", "game_screen",
                                           "multi_compete_coop_screen", "multi_unlock_bass_screen", "complete_screen", "saving_screen", "multi_coop_selsong_screen"});
        else if (scenario == "faceoff")
            expect_.insert(expect_.end(), {"multi_sel_character_screen", "multi_sel_guitar_screen", "multi_vs_venue_screen", "multi_vs_selsong_screen",
                                           "multi_vs_seldiff_screen", "loading_screen", "game_screen", "multi_compete_screen", "complete_screen", "multi_vs_selsong_screen"});
        else
            expect_.insert(expect_.end(), {"multi_sel_character_screen", "multi_sel_guitar_screen", "multi_fo_venue_screen", "multi_fo_selsong_screen",
                                           "multi_fo_seldiff_screen", "loading_screen", "game_screen", "multi_compete_fo_screen", "complete_screen", "multi_fo_selsong_screen"});
    } else if (scenario == "practice") {
        // On the bass: an overstrum in the measure before the section (no
        // gem there) is beatmatch.dta's miss on a bass track, miss_bass.
        songKeys_ = "2600:strum";
        at("main_screen");
        wait(2000);  // the main menu's entrance (a press during it is lost)
        press(kDown), wait(150), press(kDown), wait(150);  // QUICK PLAY -> MULTIPLAYER -> TRAINING
        press(kX);
        at("training_screen");
        press(kDown), wait(150);  // TUTORIALS (the focus) -> PRACTICE
        press(kX);
        at("practice_selsong_screen");
        press(kX);  // the first song
        at("practice_selpart_screen");
        press(kDown), wait(150);  // the guitar (part0.btn) -> the bass (part1.btn)
        press(kX);
        at("practice_diff_screen");
        press(kX);
        at("practice_sel_section_screen");
        wait(1000);
        press(kDown), wait(300);  // the full song -> the first section
        press(kX);  // the start section
        wait(600);
        press(kX);  // the end section: the same
        at("practice_sel_speed_screen");
        press(kDown), wait(150), press(kDown), wait(150);  // 100% -> 85% -> 65%
        press(kX);
        at("practice_loading_screen");
        at("practice_game_screen", 30000);
        check("practice_stream");
        wait(500);
        check("track_surface");
        check("practice_clock");  // the song clock's rate against the wall clock, now ...
        wait(4000);
        check("practice_clock");  // ... and 4 s later: the practice speed
        at("practice_end_screen", 120000);
        wait(1000);
        check("practice_end");
        check("practice_click_fader");
        for (int i = 0; i < 4; ++i) press(kDown), wait(150);  // RESTART -> CHANGE SPEED -> CHANGE SECTION -> NEW SONG -> EXIT
        press(kX);  // quit.btn: back to the main menu
        at("meta_loading_screen");
        at("main_screen", 30000);
        check("menu_sounds");
        expect_ = {"main_screen", "training_screen", "practice_selsong_screen", "practice_selpart_screen", "practice_diff_screen",
                   "practice_sel_section_screen", "practice_sel_speed_screen", "practice_loading_screen", "practice_game_screen",
                   "practice_end_screen", "meta_loading_screen", "main_screen"};
    } else if (scenario == "practice_menus") {
        // Practice's pause and its menus (pause.dta pract_pause_screen): a
        // section at 100%, Start pauses (the game paused) and resumes;
        // CHANGE SPEED -> 65% (game_restart: the section again on _p65);
        // CHANGE SECTION -> the second section at 85% (_p85); NEW SONG ->
        // the song list.
        auto toGame = [&] {
            at("practice_loading_screen", 30000);
            at("practice_game_screen", 30000);
            wait(3000);
        };
        auto pause = [&] {
            press(kStart);
            at("pract_pause_screen");
        };
        at("main_screen");
        wait(2000);  // the main menu's entrance (a press during it is lost)
        press(kDown), wait(150), press(kDown), wait(150);  // QUICK PLAY -> MULTIPLAYER -> TRAINING
        press(kX);
        at("training_screen");
        press(kDown), wait(150);  // TUTORIALS -> PRACTICE
        press(kX);
        at("practice_selsong_screen");
        press(kX);  // the first song
        at("practice_selpart_screen");
        press(kX);  // the guitar
        at("practice_diff_screen");
        press(kX);
        at("practice_sel_section_screen");
        wait(1000);
        press(kDown), wait(300);  // the full song -> the first section
        press(kX), wait(600), press(kX);  // start and end: the first section
        at("practice_sel_speed_screen");
        press(kX);  // 100%
        toGame();
        pause();
        check("practice_paused yes");
        press(kStart);  // resumes
        at("practice_game_screen");
        wait(1500);
        check("practice_paused no");
        pause();
        check("focus_to change_speed.btn", 5000);
        press(kX);
        at("practice_change_speed_screen");
        press(kDown), wait(150), press(kDown), wait(150);  // 100% -> 85% -> 65%
        press(kX);  // game_restart
        at("practice_game_screen", 30000);
        wait(3000);
        check("practice_stream 65");
        pause();
        check("focus_to section.btn", 5000);
        press(kX);
        at("meta_loading_screen");
        at("practice_sel_section_screen", 30000);
        wait(1000);
        press(kDown), wait(300), press(kDown), wait(300);  // the second section
        press(kX), wait(600), press(kX);
        at("practice_sel_speed_screen");
        press(kUp), wait(150);  // the speed list keeps its focus (65%): up to 85%
        press(kX);
        toGame();
        check("practice_stream 85");
        pause();
        check("focus_to newsong.btn", 5000);
        press(kX);
        at("meta_loading_screen");
        at("practice_selsong_screen", 30000);
        expect_ = {"main_screen", "training_screen", "practice_selsong_screen", "practice_selpart_screen", "practice_diff_screen",
                   "practice_sel_section_screen", "practice_sel_speed_screen", "practice_loading_screen", "practice_game_screen",
                   "pract_pause_screen", "practice_game_screen", "pract_pause_screen", "practice_change_speed_screen", "practice_game_screen",
                   "pract_pause_screen", "meta_loading_screen", "practice_sel_section_screen", "practice_sel_speed_screen",
                   "practice_loading_screen", "practice_game_screen", "pract_pause_screen", "meta_loading_screen", "practice_selsong_screen"};
    } else if (scenario == "tutorial") {
        at("main_screen");
        wait(2000);  // the main menu's entrance (a press during it is lost)
        press(kDown), wait(150), press(kDown), wait(150);  // QUICK PLAY -> MULTIPLAYER -> TRAINING
        press(kX);
        at("training_screen");
        press(kX);  // TUTORIALS (the focus)
        at("tutorials_screen");
        press(kX);  // the first tutorial (tut_1.btn)
        at("tut_loading_screen|tut_script_screen");
        at("tut_script_screen", 30000);
        // tutorial_11_tuning: the voice-over, then the tuning (each fret
        // held and strummed three times, with the keyboard's frets 1..5
        // and its strum, Down), then the next lesson.
        check("tutorial_tuning", 180000);
        wait(500);
        for (SDL_Keycode f : {SDLK_1, SDLK_2, SDLK_3, SDLK_4, SDLK_5}) {
            keyHold(f, true), wait(150);
            for (int i = 0; i < 3; ++i) key(SDLK_DOWN), wait(300);
            keyHold(f, false), wait(200);
        }
        check("tutorial_tuned", 15000);
        check("tutorial_next_lesson", 60000);
        wait(1000);
        press(kStart);  // tut_script_screen: Start pauses
        at("tut_pause_screen");
        press(kDown), wait(150), press(kDown), wait(150);  // RESUME -> RESTART -> QUIT
        press(kX);
        at("meta_loading_screen", 20000);
        at("main_screen", 30000);
        check("menu_sounds");
        expect_ = {"main_screen", "training_screen", "tutorials_screen", "tut_loading_screen", "tut_script_screen", "tut_pause_screen",
                   "tutorial_quit_screen", "meta_loading_screen", "main_screen"};
    } else if (scenario == "flaming_head") {
        // GD02-043: the Flaming Head code at the main menu, a song: the
        // guitarist's cheat_headflames (char/shared) burns.
        auto code = [&](std::initializer_list<int> buttons) {
            for (int b : buttons) press(b), wait(120);
        };
        at("main_screen");
        wait(2000);
        code({kYellow, kOrange, kYellow, kYellow, kOrange, kYellow, kYellow, kOrange});
        wait(2000);
        quickplaySong(0);
        wait(3000);
        check("band_flames");
        expect_ = {"main_screen", "qp_selsong_screen", "qp_diff_screen", "loading_screen", "game_screen"};
    } else if (scenario == "tutorial1") {
        // The whole first tutorial on autoplay: the tuning, then playing notes
        // (tutorial102), different notes (103), held notes (104), chords,
        // the interface and the wrap-up (105), to its end.
        songKeys_ = "";
        at("main_screen");
        wait(2000);  // the main menu's entrance (a press during it is lost)
        press(kDown), wait(150), press(kDown), wait(150);  // QUICK PLAY -> MULTIPLAYER -> TRAINING
        press(kX);
        at("training_screen");
        press(kX);  // TUTORIALS (the focus)
        at("tutorials_screen");
        press(kX);
        at("tut_loading_screen|tut_script_screen");
        at("tut_script_screen", 30000);
        check("tutorial_tuning", 180000);
        wait(500);
        for (SDL_Keycode f : {SDLK_1, SDLK_2, SDLK_3, SDLK_4, SDLK_5}) {
            keyHold(f, true), wait(150);
            for (int i = 0; i < 3; ++i) key(SDLK_DOWN), wait(300);
            keyHold(f, false), wait(200);
        }
        check("tutorial_tuned", 15000);
        for (const char* l : {"tutorial_lesson playing_notes", "tutorial_lesson diff_notes", "tutorial_lesson held_notes", "tutorial_lesson chords",
                              "tutorial_lesson interface", "tutorial_lesson wrapup"})
            check(l, 400000);
        at("meta_loading_screen", 400000);
        at("main_screen", 30000);
        expect_ = {"main_screen", "training_screen", "tutorials_screen", "tut_loading_screen", "tut_script_screen", "tutorial_end_screen",
                   "meta_loading_screen", "main_screen"};
    } else if (scenario == "tutorial2" || scenario == "tutorial3") {
        // The later tutorials (tutorials.dta tutorials_panel): each opens once
        // the one before has been started (campaign do_tutorial n ->
        // tutorials_done). Played through to their end on autoplay.
        auto toTutorials = [&] {
            at("main_screen");
            wait(2000);  // the main menu's entrance (a press during it is lost)
            press(kDown), wait(150), press(kDown), wait(150);  // QUICK PLAY -> MULTIPLAYER -> TRAINING
            press(kX);
            at("training_screen");
            press(kX);  // TUTORIALS (the focus)
            at("tutorials_screen");
            wait(300);
        };
        if (scenario == "tutorial2") {
            // Star Power (tutorial_21..24): the phrase captured (autoplay), the
            // whammy on its sustains (the bar moved all along) filling the
            // meter, then the tilt (Select, tried every second) deploying it
            // for 8 gems; a gem hit before that is button_error.
            std::string keys;
            for (int t = 0; t < 400000; t += 100) keys += std::to_string(t) + (t % 200 ? ":whammy=1," : ":whammy=0,");
            // From 4.5 s: tutorial204's first gem (tick 3840, 4.0 s) is hit
            // before the first tilt, so it plays button_error.
            for (int t = 4500; t < 400000; t += 1000) keys += std::to_string(t) + ":starpower,";
            songKeys_ = keys;
            toTutorials();
            check("tut_locked 2");
            press(kX);  // the first tutorial: started (do_tutorial 1), tuned, paused in its next lesson, quit
            at("tut_script_screen", 30000);
            // (Paused during its first lesson the tutorial does not stop:
            // tutorial's state is still "intro" until a next_lesson, and
            // tut_pause_screen pauses and takes buttons only at state "".)
            check("tutorial_tuning", 180000);
            wait(500);
            for (SDL_Keycode f : {SDLK_1, SDLK_2, SDLK_3, SDLK_4, SDLK_5}) {
                keyHold(f, true), wait(150);
                for (int i = 0; i < 3; ++i) key(SDLK_DOWN), wait(300);
                keyHold(f, false), wait(200);
            }
            check("tutorial_next_lesson", 60000);
            wait(1000);
            press(kStart);
            at("tut_pause_screen");
            press(kDown), wait(150), press(kDown), wait(150);  // RESUME -> RESTART -> QUIT
            press(kX);
            at("meta_loading_screen", 20000);
            toTutorials();
            check("tut_unlocked 2");
            check("focus_to tut_2.btn", 5000);
            press(kX);
            at("tut_loading_screen|tut_script_screen");
            at("tut_script_screen", 30000);
            check("tutorial_lesson star_combos", 120000);
            check("tutorial_lesson whammy", 400000);
            check("tutorial_lesson wail", 400000);
            at("meta_loading_screen", 400000);  // tutorial_end_screen goes on at once (meta_loading_goto)
            at("main_screen", 30000);
            check("tutorial_done 2");
            expect_ = {"main_screen", "training_screen", "tutorials_screen", "tut_loading_screen", "tut_script_screen", "tut_pause_screen",
                       "tutorial_quit_screen", "meta_loading_screen", "main_screen", "training_screen", "tutorials_screen",
                       "tut_loading_screen", "tut_script_screen", "tutorial_end_screen", "meta_loading_screen", "main_screen"};
        } else {
            // Hammer-ons and pull-offs (tutorial_31..33): the trainer off the
            // track twice (green, red, yellow hammered on; yellow, red, green
            // pulled off; three times each, with the keyboard's frets and its
            // strum, Down), the track's HOPOs on autoplay between.
            check("tutorials_seed");
            toTutorials();
            check("tut_unlocked 3");
            check("focus_to tut_3.btn", 5000);
            press(kX);
            at("tut_loading_screen|tut_script_screen");
            at("tut_script_screen", 30000);
            for (const bool pull : {false, true}) {
                if (pull) check("tutorial_lesson pulloff", 400000);
                check("hopo_training", 400000);
                wait(500);
                for (int i = 0; i < 3; ++i) {
                    if (!pull) {
                        keyHold(SDLK_1, true), wait(100);
                        key(SDLK_DOWN), wait(200);
                        keyHold(SDLK_2, true), wait(200);
                        keyHold(SDLK_3, true), wait(300);
                    } else {
                        keyHold(SDLK_1, true), keyHold(SDLK_2, true), keyHold(SDLK_3, true), wait(100);
                        key(SDLK_DOWN), wait(200);
                        keyHold(SDLK_3, false), wait(200);
                        keyHold(SDLK_2, false), wait(300);
                    }
                    keyHold(SDLK_3, false), keyHold(SDLK_2, false), keyHold(SDLK_1, false), wait(400);
                }
                check(pull ? "hopo_trained tut_34_fret_callback" : "hopo_trained tut_31_callback", 10000);
            }
            at("meta_loading_screen", 400000);  // tutorial_end_screen goes on at once (meta_loading_goto)
            at("main_screen", 30000);
            check("tutorial_done 3");
            expect_ = {"main_screen", "training_screen", "tutorials_screen", "tut_loading_screen", "tut_script_screen",
                       "tutorial_end_screen", "meta_loading_screen", "main_screen"};
        }
    } else if (scenario == "pad_unplugged") {
        // A controller lost mid-song: a pad as player 1, the keyboard given
        // player 2; the pad unplugged: the song pauses on
        // pause_controller_screen with GH2's message, RESUME disabled; the
        // pad back: RESUME plays on.
        autoplay_ = false;
        at("main_screen");
        check("vpad_attach");
        wait(600);
        check("keyboard_p2");
        quickplaySong(3);
        wait(3000);
        check("vpad_detach");
        at("pause_controller_screen", 5000);
        wait(800);
        check("pad_loss_message");
        press(kX);  // ignored while the pad is missing
        wait(500);
        check("still pause_controller_screen");
        check("vpad_attach");
        wait(800);
        press(kX);  // RESUME
        at("game_screen", 5000);
        expect_ = {"main_screen", "qp_selsong_screen", "qp_diff_screen", "loading_screen", "game_screen", "pause_controller_screen", "game_screen"};
    } else if (scenario == "options") {
        // The audio settings (options.dta AUDIO_SETTINGS_PANEL_HANDLERS):
        // each slider selected, moved one step (its sound at the new
        // volume: slider_band / slider_gtr / slider_sfx) and confirmed,
        // then saved on the way back; the lag calibration (lag_panel)
        // from the video settings: its countdown, the clicks
        // (sync_click.cue) struck along, its result, and back.
        at("main_screen");
        wait(2000);  // the main menu's entrance (a press during it is lost)
        for (int i = 0; i < 4; ++i) press(kDown), wait(150);  // OPTIONS
        press(kX);
        at("options_screen");
        press(kX);  // Audio Settings (the focus)
        at("audio_settings_screen");
        check("volumes_record");
        for (int k = 0; k < 3; ++k) {  // gs_band.sld, gs_guitar.sld, gs_sfx.sld
            if (k) press(kDown), wait(200);
            press(kX), wait(200);     // select the slider
            press(kDown), wait(400);  // one step down
            press(kX), wait(300);     // confirm
        }
        check("audio_sliders");
        press(kTri);  // back: set_volumes, autosave
        at("options_screen", 20000);
        check("volumes_saved");
        check("volumes_live");
        press(kDown), wait(150);  // Video Settings
        press(kX);
        at("video_settings_screen");
        check("focus_to calibrate_lag.btn", 5000);
        press(kX);
        at("lag_screen");
        press(kX);  // autocalibrate.btn: the countdown, then 11 clicks 0.733 s apart
        check("lag_strike", 20000);  // a strum (down) on each click as it plays
        check("lag_calibrated", 15000);
        press(kTri);
        at("video_settings_screen");
        press(kTri);
        at("options_screen", 20000);
        press(kTri);
        at("main_screen");
        expect_ = {"main_screen", "options_screen", "audio_settings_screen", "options_screen", "video_settings_screen", "lag_screen",
                   "video_settings_screen", "options_screen", "main_screen"};
    } else if (scenario == "attract") {
        endMs_ = 20000;
        at("splash_screen");
        at("loading_screen", 70000);  // 60 s idle on the title screen
        at("game_screen", 30000);
        wait(3000);
        press(kX);  // leave the attract mode: a 2 s fade, finish_fade, back to the title
        at("splash_screen", 15000);
        expect_ = {"splash_screen", "loading_screen", "game_screen", "finish_fade", "meta_loading_screen", "splash_screen"};
    }
}

std::string WalkDriver::startScreen() const {
    if (scenario_ == "lose" || scenario_ == "pause" || scenario_ == "back" || scenario_ == "enhanced" || scenario_ == "cheats" ||
        scenario_ == "mouse" || scenario_ == "gamepads" || scenario_ == "gamepads_pad" || scenario_ == "stickyaxis" ||
        scenario_ == "whammy_tail" || scenario_ == "practice" || scenario_ == "practice_menus" || scenario_ == "tutorial" || scenario_ == "tutorial1" || scenario_ == "tutorial2" ||
        scenario_ == "tutorial3" || scenario_ == "options" || scenario_ == "flaming_head" || scenario_ == "pad_unplugged" ||
        scenario_ == "enhanced_backends")
        return "main_screen";
    if (scenario_ == "enhanced_backends_restarted") return "enhanced_options_screen";
    if (scenario_ == "attract") return "splash_screen";
    return "";
}

void WalkDriver::poll(ui::UiManager& ui, double nowMs) {
    if (done_) return;
    if (step_ >= steps_.size()) {
        done_ = true;
        return;
    }
    Step& s = steps_[step_];
    if (stepStart_ < 0) stepStart_ = nowMs;
    if (ui.currentScreen() != lastScreen_) {
        if (lastScreen_ == "loading_screen") lastLoadingMs_ = nowMs - lastChangeMs_;
        lastScreen_ = ui.currentScreen(), lastChangeMs_ = nowMs;
    }
    // rb_text_colors / GD05_TEXTCOLORS=1: each screen's texts, once, after
    // 1.5 s out of any transition (the fx panel's fade over).
    if (textColors_) {
        ui::ScreenObj* sc = ui.current();
        if (sc)
            for (ui::PanelObj* p : sc->panels())
                if (p->view) p->view->setTextProbes(true);
        if (!sc || ui.inTransition() || textColorsSince_ < lastChangeMs_) textColorsSince_ = sc && !ui.inTransition() ? nowMs : -1;
        else if (!textColorsSeen_.count(lastScreen_) && nowMs - textColorsSince_ >= 1500) {
            textColorsSeen_.insert(lastScreen_);
            measureTextColors(ui, lastScreen_, textColorsBad_);
        }
    }
    if ((scenario_ == "rb_songlist_text" || scenario_ == "rb_songlist_highlight") && ui.current())  // the drawn texts are measured
        for (ui::PanelObj* p : ui.current()->panels())
            if (p->view) p->view->setTextProbes(true);
    // career: the band has bought Judy Nails' second outfit and the SG's
    // white skin in the store (M10), so both choices offer two.
    if ((scenario_ == "career" || scenario_ == "career_full" || scenario_ == "career_song" || scenario_ == "career_encore" ||
         scenario_ == "career_win" || scenario_ == "career_win_medium" || scenario_ == "career_practice") && !seeded_ && ui.currentScreen() == "sel_difficulty_screen" && ui.session().profileSlot >= 0) {
        seeded_ = true;
        Career::Profile& p = ui.career().profiles[size_t(ui.session().profileSlot)];
        p.items["alterna2"] = true;
        p.items["sg_white"] = true;
    }
    switch (s.kind) {
    case Step::Screen:
        // "a|b": either screen.
        if (!ui.inTransition() && ("|" + s.screen + "|").find("|" + ui.currentScreen() + "|") != std::string::npos) {
            SDL_Log("walk: %s (%.1f s)", s.screen.c_str(), nowMs / 1000);
            ++step_, stepStart_ = -1;
        } else if (nowMs - stepStart_ > s.ms) {
            error_ = "timed out waiting for " + s.screen + " (on " + ui.currentScreen() + ")";
            failed_ = done_ = true;
        }
        return;
    case Step::Press:
        ui.buttonDown(s.button, s.player);
        ui.buttonUp(s.button, s.player);
        ++step_, stepStart_ = -1;
        return;
    case Step::Check:
        runCheck(ui, s, nowMs);
        return;
    case Step::Key:
        for (const bool down : {true, false}) {
            if ((s.x == 1.0f && !down) || (s.x == 2.0f && down)) continue;
            SDL_Event e{};
            e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            e.key.timestamp = SDL_GetTicksNS();
            e.key.key = SDL_Keycode(s.button);
            e.key.scancode = SDL_GetScancodeFromKey(e.key.key, nullptr);
            e.key.down = down;
            SDL_PushEvent(&e);
        }
        ++step_, stepStart_ = -1;
        return;
    case Step::Mouse: {
        float x = s.x, y = s.y;
        if (!s.screen.empty()) {  // a component: the centre of its drawn bounds
            ui::PanelObj* fp = ui.focusPanel();
            bool found = false;
            if (fp && fp->view)
                for (const gfx::UiView::Hit& h : fp->view->hits())
                    if (h.key == s.screen) x = (h.x0 + h.x1) * 0.5f, y = (h.y0 + h.y1) * 0.5f, found = true;
            // Another panel's on the screen (RB's main menu beside its sub-menus).
            if (!found && ui.current())
                for (ui::PanelObj* p : ui.current()->panels())
                    if (p != fp && p->up && p->showing && p->view)
                        for (const gfx::UiView::Hit& h : p->view->hits())
                            if (!found && h.key == s.screen) x = (h.x0 + h.x1) * 0.5f, y = (h.y0 + h.y1) * 0.5f, found = true;
            if (!found) {
                error_ = "the mouse: " + s.screen + " was not drawn (on " + ui.currentScreen() + ")";
                failed_ = done_ = true;
                return;
            }
        }
        if (s.button == 3) ui.buttonDown(ui::kPad_Tri, 0), ui.buttonUp(ui::kPad_Tri, 0);
        else if (s.button >= 4) ui.wheel(s.button == 4 ? -1 : 1);
        else ui.mouse(x, y, s.button == 1);
        ++step_, stepStart_ = -1;
        return;
    }
    case Step::Wait:
    case Step::Hold:
        if (s.kind == Step::Wait && s.screen == "mark") markMs_ = nowMs;
        if (nowMs - (s.kind == Step::Wait && s.screen == "until" ? markMs_ : stepStart_) >= s.ms) ++step_, stepStart_ = -1;
        return;
    }
}

// The main menu's camera and scene: the camera's position plus the sum of
// every transform's world position in its view (the metacam moves the
// camera's rig and the walls with each transition).
// The colour of the bricks' material (meta_light_on.mnm's) on the current
// screen's panels; false without one.
static bool bricksLight(ui::UiManager& ui, std::array<float, 4>& out) {
    ui::ScreenObj* sc = ui.current();
    if (!sc) return false;
    for (ui::PanelObj* p : sc->panels()) {
        if (!p->view) continue;
        const scene::Object* a = p->view->find("meta_light_on.mnm");
        const scene::MatAnim* ma = a ? a->as<scene::MatAnim>() : nullptr;
        const scene::Mat* mat = ma && ma->dir ? ma->dir->resolve<scene::Mat>(ma->mat) : nullptr;
        if (!mat) continue;
        const gfx::ScenePose::MatState* st = p->view->pose().mat(mat);
        out = st ? std::array<float, 4>{st->rgb[0], st->rgb[1], st->rgb[2], st->alpha} : std::array<float, 4>{1, 1, 1, 1};
        return true;
    }
    return false;
}

static bool mainCamera(ui::UiManager& ui, std::array<float, 3>& pos) {
    ui::PanelObj* p = ui.panel("main_panel");
    if (!p || !p->view || !p->view->milo()) return false;
    const gfx::Vec3 c = p->view->camera().position();
    pos = {c.x, c.y, c.z};
    gfx::UiView& v = *p->view;
    auto add = [&](const scene::Object& o) {
        if (!gfx::transOf(&o)) return;
        const scene::Xfm w = v.pose().world(&o);
        for (size_t k = 0; k < 3; ++k) pos[k] += w.pos[k];
    };
    v.milo()->root().forEach(add);
    for (const scene::Milo* m : v.subdirs()) m->root().forEach(add);
    return true;
}

// ---- rb_text_colors (GD05 owner report: menu text darker than RB's) --------
// Every text the current screen's panels drew last frame (UiView text
// probes): the colour RB's shader gives its glyph cores, from the data, and
// the colour on the screen there. Expected, per pixel where the font
// texture's alpha is 1 over the pixel's whole footprint: (sum of the lights
// * gDiffuse + base * gAmbient) * texel * (intensify ? 2 : 1), base the
// vertex colour (prelit) or gDiffuse (ark/xbox_shaders: per vertex and per
// pixel alike, the light sum tinted once), then the console's output
// (outputCurve, D25). Drawn: the screenshot's pixel. A text whose glyph
// cores are more than 8% darker than expected fails.
static void measureTextColors(ui::UiManager& ui, const std::string& tag, std::vector<std::string>& bad) {
    ui::ScreenObj* sc = ui.current();
    if (!sc) return;
    const char* dir = SDL_getenv("GD05_WALK_SHOTS");
    const std::string path = std::string(dir ? dir : SDL_GetBasePath()) + "/tc_" + tag + ".bmp";
    if (!ui.host().saveScreenshot(path)) return;
    SDL_Surface* raw = SDL_LoadBMP(path.c_str());
    SDL_Surface* img = raw ? SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32) : nullptr;
    if (raw) SDL_DestroySurface(raw);
    if (!img) return;
    float gamma = 0.85f;
    if (const char* e = SDL_getenv("GD05_GAMMA")) gamma = float(std::atof(e));
    struct Tex {
        int w = 0, h = 0;
        std::vector<uint8_t> rgba;
    };
    std::map<const scene::Tex*, Tex> texs;
    auto luma = [](const float* c) { return 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2]; };
    std::printf("  text colours %s on %s:\n", tag.c_str(), ui.currentScreen().c_str());
    if (SDL_getenv("GD05_TEXTCOLORS_DEBUG"))
        if (ui::PanelObj* fx = ui.panel("fx"); fx && fx->view) std::printf("    fx up %d showing %d fade.mnm frame %.2f\n", int(fx->up), int(fx->showing), fx->view->frame("fade.mnm"));
    for (ui::PanelObj* panel : sc->panels()) {
        if (SDL_getenv("GD05_TEXTCOLORS_DEBUG"))
            std::printf("    panel %s view %d up %d showing %d texts %d probes %zu\n", panel->name().c_str(), panel->view ? 1 : 0, int(panel->up),
                        int(panel->showing), panel->view ? panel->view->textsDrawn() : -1, panel->view ? panel->view->textProbes().size() : 0);
        if (SDL_getenv("GD05_TEXTCOLORS_DEBUG") && panel->view)
            for (const gfx::UiView::MaskDraw& m : panel->view->masks())
                std::printf("      mask rgba %.2f %.2f %.2f %.2f blend %d rect %.2f %.2f %.2f %.2f\n", m.rgba[0], m.rgba[1], m.rgba[2], m.rgba[3], m.blend, m.rect[0],
                            m.rect[1], m.rect[2], m.rect[3]);
        if (SDL_getenv("GD05_TEXTCOLORS_DEBUG") && panel->view && panel->view->milo()) {
            std::vector<const scene::Milo*> ms{panel->view->milo()};
            for (const scene::Milo* m : panel->view->subdirs()) ms.push_back(m);
            for (const scene::Milo* m : ms)
                for (const scene::Environ* e : m->root().all<scene::Environ>()) {
                    const auto& x = e->colorXfm;
                    std::printf("      environ %s amb %.2f %.2f %.2f lights %zu colour_adjust %d hue %.2f sat %.2f light %.2f contrast %.2f bright %.2f in %.2f..%.2f out %.2f..%.2f rot %.2f %.2f %.2f pos %.2f\n",
                                e->name.c_str(), e->ambientColor[0], e->ambientColor[1], e->ambientColor[2], e->lights.size(), int(e->useColorAdjust), x.hue,
                                x.saturation, x.lightness, x.contrast, x.brightness, x.inLo[0], x.inHi[0], x.outLo[0], x.outHi[0], x.xfm.rot[0], x.xfm.rot[4],
                                x.xfm.rot[8], x.xfm.pos[0]);
                }
        }
        if (!panel->view || !panel->up || !panel->showing) continue;
        for (const gfx::TextProbe& p : panel->view->textProbes()) {
            std::string flags = p.m.prelit ? "prelit" : "lit";
            const int lights = p.m.useEnviron ? p.env.lightCount - p.env.projectedCount : 0;
            if (p.m.useEnviron) flags += " env L" + std::to_string(lights);
            if (p.m.perPixelLit) flags += " pp";
            if (SDL_getenv("GD05_TEXTCOLORS_DEBUG")) {
                std::printf("      %s: colour %.3f %.3f %.3f a %.2f vtx %.3f %.3f %.3f amb %.2f %.2f %.2f envamb %.2f %.2f %.2f spec %.2f blend %d intens %d\n", p.name.c_str(),
                            p.m.color[0], p.m.color[1], p.m.color[2], p.m.alpha, p.verts.empty() ? 0.f : p.verts[0].color[0], p.verts.empty() ? 0.f : p.verts[0].color[1],
                            p.verts.empty() ? 0.f : p.verts[0].color[2], p.env.ambient.x, p.env.ambient.y, p.env.ambient.z, p.env.envAmbient.x, p.env.envAmbient.y,
                            p.env.envAmbient.z, p.m.specularRgb[0], int(p.m.blend), int(p.m.intensify));
                for (int i = 0; i < lights; ++i)
                    std::printf("        light %d colour %.2f %.2f %.2f vec %.1f %.1f %.1f range %.0f start %.0f dir %d\n", i, p.env.lights[i].color.x, p.env.lights[i].color.y,
                                p.env.lights[i].color.z, p.env.lights[i].vec.x, p.env.lights[i].vec.y, p.env.lights[i].vec.z, p.env.lights[i].range,
                                p.env.lights[i].falloffStart, int(p.env.directional || p.env.lights[i].directional));
            }
            auto skip = [&](const char* why) {
                std::printf("    %-22s %-30s %-16s skipped: %s\n", panel->name().c_str(), p.name.c_str(), flags.c_str(), why);
            };
            if (p.indirect) {
                skip("drawn into a 3D RenderDir's texture");
                continue;
            }
            if (p.m.alpha < 0.95f) {
                skip("material alpha < 0.95");
                continue;
            }
            if (p.m.blend != gfx::MatBlend::SrcAlpha && p.m.blend != gfx::MatBlend::Src && p.m.blend != gfx::MatBlend::PreMultAlpha) {
                skip("additive / other blend");
                continue;
            }
            const scene::Tex* t = p.mat && p.mat->dir && !p.mat->diffuseTex.empty() ? p.mat->dir->resolve<scene::Tex>(p.mat->diffuseTex) : nullptr;
            if (!t || t->bitmap.empty() || p.verts.empty()) {
                skip("no font texture");
                continue;
            }
            Tex& tx = texs[t];
            if (!tx.w && scene::decodeBitmapRgba(t->bitmap, 0, tx.rgba)) tx.w = t->bitmap.width, tx.h = t->bitmap.height;
            if (!tx.w) {
                skip("font texture does not decode");
                continue;
            }
            if (SDL_getenv("GD05_TEXTCOLORS_DEBUG")) {
                // The texture's opaque texels: their rgb's spread.
                std::vector<int> l;
                for (size_t i = 0; i + 3 < tx.rgba.size(); i += 4)
                    if (tx.rgba[i + 3] >= 250) l.push_back((299 * tx.rgba[i] + 587 * tx.rgba[i + 1] + 114 * tx.rgba[i + 2]) / 1000);
                std::sort(l.begin(), l.end());
                if (!l.empty())
                    std::printf("      texture %s (%s, fmt %d, %dx%d): opaque texels %zu, luma p10 %d p50 %d p90 %d max %d\n", t->name.c_str(), p.mat->name.c_str(),
                                int(t->bitmap.format), tx.w, tx.h, l.size(), l[l.size() / 10], l[l.size() / 2], l[l.size() * 9 / 10], l.back());
            }
            // RB's colour before the texture, per pixel (RB's text materials
            // are per-pixel lit: a big title's light falls off across it).
            gfx::Vec3 n{p.verts[0].normal[0], p.verts[0].normal[1], p.verts[0].normal[2]};
            {
                const gfx::Vec3 o = p.world.transformPoint({0, 0, 0}), q = p.world.transformPoint(n);
                n = {q.x - o.x, q.y - o.y, q.z - o.z};
                const float l = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
                if (l > 0) n = {n.x / l, n.y / l, n.z / l};
            }
            const float* base = p.m.prelit ? p.verts[0].color : p.m.color;
            const gfx::Vec3 amb = p.m.useEnviron ? p.env.envAmbient : p.env.ambient;
            auto shadeAt = [&](const gfx::Vec3& pos, float* shade) {
                shade[0] = base[0] * amb.x, shade[1] = base[1] * amb.y, shade[2] = base[2] * amb.z;
                for (int i = 0; i < lights; ++i) {
                    const gfx::Light& L = p.env.lights[i];
                    gfx::Vec3 l = L.vec;
                    float fall = 1;
                    if (!p.env.directional && !L.directional) {
                        gfx::Vec3 d{L.vec.x - pos.x, L.vec.y - pos.y, L.vec.z - pos.z};
                        const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                        l = len > 1e-6f ? gfx::Vec3{d.x / len, d.y / len, d.z / len} : d;
                        if (L.range > L.falloffStart) fall = 1 - std::clamp((len - L.falloffStart) / (L.range - L.falloffStart), 0.0f, 1.0f);
                    }
                    const float lam = std::max(0.0f, n.x * l.x + n.y * l.y + n.z * l.z) * fall;
                    shade[0] += L.color.x * lam * p.m.color[0], shade[1] += L.color.y * lam * p.m.color[1], shade[2] += L.color.z * lam * p.m.color[2];
                }
            };
            const float mult = p.m.intensify ? 2.0f : 1.0f;
            // The glyph cores, triangle by triangle.
            const float sx = float(img->w) / 1280.0f, sy = float(img->h) / 720.0f;
            double sumE[3] = {0, 0, 0}, sumD[3] = {0, 0, 0};
            int count = 0;
            for (const auto& f : p.faces) {
                float X[3], Y[3], W[3], U[3], V[3];
                gfx::Vec3 P[3];
                bool ok = true;
                for (int k = 0; k < 3; ++k) {
                    const scene::MeshVertex& v = p.verts[f[size_t(k)]];
                    const gfx::Vec3 w = p.world.transformPoint({v.pos[0], v.pos[1], v.pos[2]});
                    P[k] = w;
                    const gfx::Mat4& m = p.viewProj;
                    const float cx = m.at(0, 0) * w.x + m.at(0, 1) * w.y + m.at(0, 2) * w.z + m.at(0, 3);
                    const float cy = m.at(1, 0) * w.x + m.at(1, 1) * w.y + m.at(1, 2) * w.z + m.at(1, 3);
                    const float cw = m.at(3, 0) * w.x + m.at(3, 1) * w.y + m.at(3, 2) * w.z + m.at(3, 3);
                    if (cw <= 1e-4f) ok = false;
                    X[k] = (p.rect[0] + (cx / cw + 1) * 0.5f * (p.rect[2] - p.rect[0])) * sx;
                    Y[k] = (p.rect[1] + (1 - cy / cw) * 0.5f * (p.rect[3] - p.rect[1])) * sy;
                    W[k] = 1.0f / cw;
                    const gfx::Vec3 uv = p.m.texXfm.transformPoint({v.uv[0], v.uv[1], 0});
                    U[k] = uv.x * float(tx.w), V[k] = uv.y * float(tx.h);
                }
                const float area = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
                if (!ok || std::fabs(area) < 0.5f) continue;
                // Texels per pixel along the edges: the footprint's radius.
                float k = 0;
                for (int e = 0; e < 3; ++e) {
                    const int a = e, b = (e + 1) % 3;
                    const float dp = std::hypot(X[b] - X[a], Y[b] - Y[a]), dt = std::hypot(U[b] - U[a], V[b] - V[a]);
                    if (dp > 0.5f) k = std::max(k, dt / dp);
                }
                const int r = int(std::ceil(0.5f * k)) + 1;
                const int x0 = std::max(0, int(std::floor(std::min({X[0], X[1], X[2]})))), x1 = std::min(img->w - 1, int(std::ceil(std::max({X[0], X[1], X[2]}))));
                const int y0 = std::max(0, int(std::floor(std::min({Y[0], Y[1], Y[2]})))), y1 = std::min(img->h - 1, int(std::ceil(std::max({Y[0], Y[1], Y[2]}))));
                for (int py = y0; py <= y1; ++py)
                    for (int px = x0; px <= x1; ++px) {
                        const float qx = float(px) + 0.5f, qy = float(py) + 0.5f;
                        const float b0 = ((X[1] - qx) * (Y[2] - qy) - (X[2] - qx) * (Y[1] - qy)) / area;
                        const float b1 = ((X[2] - qx) * (Y[0] - qy) - (X[0] - qx) * (Y[2] - qy)) / area;
                        const float b2 = 1 - b0 - b1;
                        if (b0 < 0 || b1 < 0 || b2 < 0) continue;
                        const float iw = b0 * W[0] + b1 * W[1] + b2 * W[2];
                        const float u = (b0 * U[0] * W[0] + b1 * U[1] * W[1] + b2 * U[2] * W[2]) / iw;
                        const float v = (b0 * V[0] * W[0] + b1 * V[1] * W[1] + b2 * V[2] * W[2]) / iw;
                        const int tu = int(std::floor(u)), tv = int(std::floor(v));
                        int minA = 255;
                        for (int dy = -r; dy <= r && minA >= 250; ++dy)
                            for (int dx = -r; dx <= r; ++dx) {
                                const int xx = std::clamp(tu + dx, 0, tx.w - 1), yy = std::clamp(tv + dy, 0, tx.h - 1);
                                minA = std::min(minA, int(tx.rgba[(size_t(yy) * size_t(tx.w) + size_t(xx)) * 4 + 3]));
                            }
                        if (minA < 250) continue;
                        // The texel colour the sampler gives: the texels the pixel covers.
                        const float rc = std::max(0.5f, 0.5f * k);
                        float texel[3] = {0, 0, 0};
                        int nt = 0;
                        for (int yy = int(std::floor(v - rc)); yy <= int(std::floor(v + rc)); ++yy)
                            for (int xx = int(std::floor(u - rc)); xx <= int(std::floor(u + rc)); ++xx) {
                                const uint8_t* q = &tx.rgba[(size_t(std::clamp(yy, 0, tx.h - 1)) * size_t(tx.w) + size_t(std::clamp(xx, 0, tx.w - 1))) * 4];
                                texel[0] += q[0], texel[1] += q[1], texel[2] += q[2];
                                ++nt;
                            }
                        if (!nt) continue;
                        const Uint8* d = static_cast<const Uint8*>(img->pixels) + py * img->pitch + px * 4;
                        float shade[3];
                        {
                            const float a0 = b0 * W[0] / iw, a1 = b1 * W[1] / iw, a2 = b2 * W[2] / iw;
                            shadeAt({a0 * P[0].x + a1 * P[1].x + a2 * P[2].x, a0 * P[0].y + a1 * P[1].y + a2 * P[2].y, a0 * P[0].z + a1 * P[1].z + a2 * P[2].z}, shade);
                        }
                        for (int ch = 0; ch < 3; ++ch) {
                            const float e = std::clamp(shade[ch] * texel[ch] / (255.0f * float(nt)) * mult, 0.0f, 1.0f);
                            sumE[ch] += gfx::outputCurve(e, gamma) * 255.0;
                            sumD[ch] += d[ch];
                        }
                        ++count;
                    }
            }
            if (count < 12) {
                skip(count ? "under 12 glyph-core pixels" : "no glyph-core pixel on the screen");
                continue;
            }
            float E[3], D[3];
            for (int ch = 0; ch < 3; ++ch) E[ch] = float(sumE[ch] / count), D[ch] = float(sumD[ch] / count);
            const float ratio = luma(E) > 1 ? luma(D) / luma(E) : 1;
            const bool dark = ratio < 0.92f;
            std::printf("    %-22s %-30s %-16s exp %3.0f %3.0f %3.0f  drawn %3.0f %3.0f %3.0f  ratio %.2f  n %d%s\n", panel->name().c_str(), p.name.c_str(),
                        flags.c_str(), E[0], E[1], E[2], D[0], D[1], D[2], ratio, count, dark ? "  DARK" : "");
            if (dark) {
                char line[256];
                std::snprintf(line, sizeof line, "%s %s/%s: drawn %.0f %.0f %.0f, expected %.0f %.0f %.0f (%.2f)", tag.c_str(), panel->name().c_str(),
                              p.name.c_str(), D[0], D[1], D[2], E[0], E[1], E[2], ratio);
                bad.push_back(line);
            }
        }
    }
    SDL_DestroySurface(img);
}

// ---- rb_text_colors against RB's own screen (GD05-062) -------------------
// The owner's capture of RB's main menu with SOLO's sub-menu open
// (QUICKPLAY focused on its orange bar, SOLO TOUR normal; 1200 x 675, kept
// out of the repository): the 99th percentile luma of each word's box,
// measured on that image. The port's frame is averaged down to 1200 x 675
// and measured in the same boxes. The sub-menu labels (BandButtons with
// custom_colors 0x707070 / 0xd7d7d7, avantgarde.mat, popup_main_test.env's
// bottom_right.lit) must be within 10% of RB's for their state; the main
// buttons' modelled words (button_main.milo's solo.mesh ...) are printed.
static void measureRefColors(ui::UiManager& ui, const std::string& tag, std::vector<std::string>& bad) {
    struct Box {
        const char* name;
        int x0, y0, x1, y1;
        float focused, normal;  // RB's level in that state (0: not in the capture)
    };
    // Sub-menu rows by position (row 1 QUICKPLAY, row 2 SOLO TOUR): RB's
    // focused QUICKPLAY 188, normal SOLO TOUR 102.
    static const Box sub[2] = {{"quickplay.btn", 565, 256, 700, 284, 188, 102}, {"tour.btn", 565, 304, 700, 332, 188, 102}};
    // Main buttons (SOLO focused in the capture, the others normal).
    static const Box words[5] = {{"solo.btn", 340, 258, 440, 295, 203, 0},
                                 {"multi.btn", 275, 322, 495, 362, 0, 216},
                                 {"tutorial.btn", 310, 392, 465, 432, 0, 220},
                                 {"options.btn", 315, 450, 465, 495, 0, 215},
                                 {"community.btn", 265, 505, 490, 570, 0, 191}};
    ui::PanelObj* subPanel = ui.panel("main_solomode_panel");
    ui::PanelObj* mainPanel = ui.panel("main_panel");
    if (ui.currentScreen() != "main_solomode_screen" || !subPanel || !mainPanel) {
        bad.push_back("refcolors " + tag + ": not on main_solomode_screen (" + ui.currentScreen() + ")");
        return;
    }
    const char* dir = SDL_getenv("GD05_WALK_SHOTS");
    const std::string path = std::string(dir ? dir : SDL_GetBasePath()) + "/rc_" + tag + ".bmp";
    if (!ui.host().saveScreenshot(path)) return;
    SDL_Surface* raw = SDL_LoadBMP(path.c_str());
    SDL_Surface* img = raw ? SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32) : nullptr;
    if (raw) SDL_DestroySurface(raw);
    if (!img) return;
    // The frame averaged down to the capture's 1200 x 675 (each pixel the
    // mean of the source pixels whose centres fall in it), as luma.
    const int W = 1200, H = 675;
    std::vector<double> sum(size_t(W) * H, 0.0);
    std::vector<int> cnt(size_t(W) * H, 0);
    for (int y = 0; y < img->h; ++y)
        for (int x = 0; x < img->w; ++x) {
            const int tx = std::min(W - 1, int((x + 0.5) * W / img->w)), ty = std::min(H - 1, int((y + 0.5) * H / img->h));
            const Uint8* d = static_cast<const Uint8*>(img->pixels) + y * img->pitch + x * 4;
            sum[size_t(ty) * W + size_t(tx)] += 0.299 * d[0] + 0.587 * d[1] + 0.114 * d[2];
            ++cnt[size_t(ty) * W + size_t(tx)];
        }
    SDL_DestroySurface(img);
    auto p99 = [&](const Box& b) {
        std::vector<double> v;
        for (int y = b.y0; y < b.y1; ++y)
            for (int x = b.x0; x < b.x1; ++x)
                if (const int n = cnt[size_t(y) * W + size_t(x)]) v.push_back(sum[size_t(y) * W + size_t(x)] / n);
        if (v.empty()) return 0.0;
        std::sort(v.begin(), v.end());
        return v[size_t(double(v.size()) * 0.99)];
    };
    std::printf("  RB's capture vs the port, %s (99th percentile luma per word box, 1200 x 675):\n", tag.c_str());
    for (int i = 0; i < 2; ++i) {
        const bool focused = subPanel->focus == sub[i].name;
        const float want = focused ? sub[i].focused : sub[i].normal;
        const double got = p99(sub[i]);
        const double ratio = want > 0 ? got / want : 1;
        const bool off = ratio < 0.9 || ratio > 1.1;
        std::printf("    sub-menu row %d %-14s %-7s RB %3.0f  drawn %3.0f  ratio %.2f%s\n", i + 1, sub[i].name, focused ? "focused" : "normal", want, got, ratio,
                    off ? "  OFF" : "");
        if (off) {
            char line[200];
            std::snprintf(line, sizeof line, "refcolors %s: sub-menu %s (%s) drawn %.0f, RB's capture %.0f (%.2f)", tag.c_str(), sub[i].name,
                          focused ? "focused" : "normal", got, want, ratio);
            bad.push_back(line);
        }
    }
    for (const Box& b : words) {
        const bool focused = mainPanel->focus == b.name;
        const float want = focused ? b.focused : b.normal;
        if (want <= 0) continue;  // not in that state in the capture
        const double got = p99(b);
        std::printf("    main word %-14s %-7s RB %3.0f  drawn %3.0f  ratio %.2f (printed only: GD05-062)\n", b.name, focused ? "focused" : "normal", want, got,
                    got / want);
    }
}

void WalkDriver::runCheck(ui::UiManager& ui, const Step& s, double nowMs) {
    auto pass = [&] { ++step_, stepStart_ = -1; };
    // GD05_WALK_KEEP_GOING=1: a failed check is printed and the walk goes
    // on (a diagnostic run: every check of a walk against an older build;
    // its "OK" is no verdict, the FAIL lines are).
    static const bool keepGoing = SDL_getenv("GD05_WALK_KEEP_GOING") != nullptr;
    auto fail = [&](const std::string& e) {
        if (keepGoing) return (void)std::printf("  FAIL (kept going) %s\n", e.c_str()), pass();
        error_ = e, failed_ = done_ = true;
    };
    if (s.screen.rfind("refcolors:", 0) == 0) {
        // rb_text_colors: refcolors:<tag> measures main_solomode_screen's
        // labels against the owner's capture of RB once it has rested 700 ms.
        static double seenMs = -1;
        if (ui.inTransition()) return;
        if (seenMs < 0) seenMs = nowMs;
        if (nowMs - seenMs < 700) return;
        seenMs = -1;
        measureRefColors(ui, s.screen.substr(10), textColorsBad_);
        return pass();
    }
    if (s.screen.rfind("textcolors:", 0) == 0) {
        // rb_text_colors: textcolors:<tag> measures the screen once it has
        // rested 700 ms (the probes on from the first poll).
        // textcolors:<tag>:<panel> waits for that panel instead (the loading
        // screens hold their transition).
        static double seenMs = -1;
        const std::string rest = s.screen.substr(11);
        const size_t c = rest.find(':');
        if (ui::ScreenObj* sc = ui.current())
            for (ui::PanelObj* p : sc->panels())
                if (p->view) p->view->setTextProbes(true);
        if (c == std::string::npos ? ui.inTransition() : !(ui.panel(rest.substr(c + 1)) && ui.panel(rest.substr(c + 1))->up)) return;
        if (seenMs < 0) seenMs = nowMs;
        if (nowMs - seenMs < 700) return;
        seenMs = -1;
        measureTextColors(ui, rest.substr(0, c), textColorsBad_);
        return pass();
    }
    if (s.screen.rfind("bright:", 0) == 0) {
        // rb_bright: bright:<tag>[:<panel>] waits (loading screens: until
        // the panel is up), saves the frame, prints each hit box's 97th
        // percentile luma.
        const std::string rest = s.screen.substr(7);
        const size_t c = rest.find(':');
        const std::string tag = rest.substr(0, c), panelName = c == std::string::npos ? std::string() : rest.substr(c + 1);
        static double seenMs = -1;
        ui::PanelObj* p = panelName.empty() ? ui.focusPanel() : ui.panel(panelName);
        if (!p || !p->view || !p->up) {
            if (stepStart_ >= 0 && nowMs - stepStart_ > 30000) return fail("bright: " + panelName + " never came up");
            return;
        }
        if (seenMs < 0) seenMs = nowMs;
        if (nowMs - seenMs < 600) return;
        seenMs = -1;
        const char* dir = SDL_getenv("GD05_WALK_SHOTS");
        const std::string path = std::string(dir ? dir : SDL_GetBasePath()) + "/bright_" + tag + ".bmp";
        if (!ui.host().saveScreenshot(path)) return fail("bright: no screenshot");
        SDL_Surface* raw = SDL_LoadBMP(path.c_str());
        SDL_Surface* img = raw ? SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32) : nullptr;
        if (raw) SDL_DestroySurface(raw);
        if (!img) return fail("bright: the screenshot does not load");
        auto p97 = [&](float x0, float y0, float x1, float y1) {
            std::vector<int> v;
            const float sx = float(img->w) / 1280.0f, sy = float(img->h) / 720.0f;
            for (int y = std::max(0, int(y0 * sy)); y < std::min(img->h, int(y1 * sy)); ++y)
                for (int x = std::max(0, int(x0 * sx)); x < std::min(img->w, int(x1 * sx)); ++x) {
                    const Uint8* px = static_cast<const Uint8*>(img->pixels) + y * img->pitch + x * 4;
                    v.push_back((299 * px[0] + 587 * px[1] + 114 * px[2]) / 1000);
                }
            if (v.empty()) return -1;
            std::sort(v.begin(), v.end());
            return v[size_t(0.97 * double(v.size() - 1))];
        };
        std::printf("  bright %s on %s (panel %s):\n", tag.c_str(), ui.currentScreen().c_str(), p->name().c_str());
        std::map<std::string, int> best;  // a component's brightest box
        for (const gfx::UiView::Hit& h : p->view->hits()) {
            const int v = p97(h.x0, h.y0, h.x1, h.y1);
            std::printf("    %-28s %4.0f %4.0f %4.0f %4.0f  p97 %d\n", h.key.c_str(), h.x0, h.y0, h.x1, h.y1, v);
            best[h.key] = std::max(best.count(h.key) ? best[h.key] : -1, v);
        }
        if (tag == "loading") {
            // The four band icons (128 x 128 quads in load_icon_*), boxed
            // through the frame's camera.
            int dimmest = 255;
            for (const char* m : {"guitar.mesh", "vox.mesh", "drummer.mesh", "bass.mesh"}) {
                const std::string d = std::string("load_icon_") + (m[0] == 'g' ? "guitar" : m[0] == 'v' ? "vox" : m[0] == 'd' ? "drum" : "bass");
                const scene::Object* o = p->view->findIn(d, m);
                if (!o) return SDL_DestroySurface(img), fail(std::string("bright: no ") + m);
                const float lo[3] = {-64, -64, 0}, hi[3] = {64, 64, 0};
                const size_t n = p->view->hits().size();
                p->view->addHit("icon", lo, hi, gfx::toMat4(p->view->pose().world(o)));
                if (p->view->hits().size() == n) return SDL_DestroySurface(img), fail(std::string("bright: ") + m + " off the screen");
                const gfx::UiView::Hit h = p->view->hits().back();
                const int v = p97(h.x0, h.y0, h.x1, h.y1);
                std::printf("    icon %-23s %4.0f %4.0f %4.0f %4.0f  p97 %d\n", m, h.x0, h.y0, h.x1, h.y1, v);
                dimmest = std::min(dimmest, v);
            }
            best["band_icons"] = dimmest;
            // The lit pixels' (luma over 100) mean luma and blue / red: the
            // word and the icons graded alike (GD05-065, D49).
            auto grade = [&](float x0, float y0, float x1, float y1, double& luma, double& br) {
                const float sx = float(img->w) / 1280.0f, sy = float(img->h) / 720.0f;
                double l = 0, r = 0, b = 0;
                long n = 0;
                for (int y = std::max(0, int(y0 * sy)); y < std::min(img->h, int(y1 * sy)); ++y)
                    for (int x = std::max(0, int(x0 * sx)); x < std::min(img->w, int(x1 * sx)); ++x) {
                        const Uint8* px = static_cast<const Uint8*>(img->pixels) + y * img->pitch + x * 4;
                        const int v = (299 * px[0] + 587 * px[1] + 114 * px[2]) / 1000;
                        if (v <= 100) continue;
                        l += v, r += px[0], b += px[2], ++n;
                    }
                luma = n ? l / double(n) : 0, br = r > 0 ? b / r : 0;
                return n;
            };
            float ix0 = 1e9f, iy0 = 1e9f, ix1 = -1e9f, iy1 = -1e9f;
            for (const gfx::UiView::Hit& h : p->view->hits())
                if (h.key == "icon") ix0 = std::min(ix0, h.x0), iy0 = std::min(iy0, h.y0), ix1 = std::max(ix1, h.x1), iy1 = std::max(iy1, h.y1);
            double iconLuma = 0, iconBr = 0;
            grade(ix0, iy0, ix1, iy1, iconLuma, iconBr);
            // LOADING: its box's top 60% (the gold trim runs under its foot).
            for (const gfx::UiView::Hit& h : p->view->hits())
                if (h.key == "loading.lbl") {
                    best["loading_word"] = p97(h.x0, h.y0, h.x1, h.y0 + 0.6f * (h.y1 - h.y0));
                    double luma = 0, br = 0;
                    const long n = grade(h.x0, h.y0, h.x1, h.y0 + 0.6f * (h.y1 - h.y0), luma, br);
                    std::printf("    loading word                 p97 %d, lit px %ld luma %.0f blue/red %.2f; icons luma %.0f blue/red %.2f\n", best["loading_word"], n, luma,
                                br, iconLuma, iconBr);
                    // 0.0.6 (D44): flat white, blue / red 1.00 against the icons' 0.66.
                    if (n < 100 || std::fabs(br - iconBr) > 0.10 || std::fabs(luma - iconLuma) > 40)
                        return SDL_DestroySurface(img), fail("bright loading: LOADING not graded as the band icons");
                }
        }
        SDL_DestroySurface(img);
        // The floors: 0.0.6's dark spots measured after the fix, less a
        // margin (the loading icons were 44, lit by the wrong environ; 215
        // with intruments.env).
        static const std::map<std::string, std::vector<std::pair<std::string, int>>> floors = {
            {"main", {{"solo.btn", 190}, {"multi.btn", 190}, {"tutorial.btn", 190}, {"options.btn", 190}, {"community.btn", 190}}},
            // The audio sliders were edge-on (2-3 px, 10..118; slider.grp's turn
            // left out); the LOADING word bright from the start, graded as the
            // band icons (D49, replacing D44's flat white; it was 25, unlit
            // until loading.tnm's glint reached it).
            {"audio", {{"instruments.sld", 120}, {"background.sld", 120}, {"crowd.sld", 120}, {"sfx.sld", 120}}},
            {"options", {{"audio.btn", 170}, {"gameplay.btn", 80}, {"characterdata.btn", 80}, {"calibrate_av.btn", 80}, {"port_enhanced.btn", 75}}},
            {"solo", {{"quickplay.btn", 150}, {"tour.btn", 80}}},
            {"songlist", {{"ss_song.lst#0#list.lbl", 120}, {"ss_song.lst#2#list.lbl", 95}}},
            {"seldiff", {{"player.lbl", 190}, {"guitar.lbl", 180}, {"choose_diff.lbl", 150}}},
            {"seldiff2", {{"easy.btn", 115}, {"medium.btn", 125}, {"hard.btn", 120}, {"expert.btn", 120}}},
            {"loading", {{"tip.lbl", 190}, {"band_icons", 150}, {"loading_word", 200}}},
            {"pause", {{"pause_title.lbl", 220}, {"0.btn", 160}, {"4.btn", 110}}},
        };
        if (auto it = floors.find(tag); it != floors.end())
            for (const auto& [key, floor] : it->second) {
                auto b = best.find(key);
                if (b == best.end()) return fail("bright " + tag + ": " + key + " was not drawn");
                if (b->second < floor) return fail("bright " + tag + ": " + key + " p97 " + std::to_string(b->second) + " < " + std::to_string(floor));
            }
        return pass();
    }
    if (s.screen.rfind("freebird ", 0) == 0) {
        // endgame_encore_panel: freebird set by {campaign encore_newly_unlocked
        // $freebird} (its $var by name), the focus on yes.btn, then
        // yes_freebird1..3.btn with freebird1..n.grp shown.
        const int n = std::atoi(s.screen.c_str() + 9);
        ui::PanelObj* fp = ui.focusPanel();
        const std::string f = fp ? fp->focus : std::string();
        const int isFinal = ui.interp().send("endgame_encore_panel", "get", {Value::symbol("freebird")}).integerValue();
        std::printf("  free bird: step %d, freebird %d, focus %s\n", n, isFinal, f.c_str());
        if (!isFinal) return fail("Free Bird was not offered as the tour's last song");
        const std::string want = n == 0 ? "yes.btn" : "yes_freebird" + std::to_string(n) + ".btn";
        if (f != want) return fail("the focus is " + f + ", not " + want);
        for (int g = 1; g <= 3; ++g) {
            const bool shown = fp && fp->view && fp->view->showing("freebird" + std::to_string(g) + ".grp");
            if (shown != (g <= n)) return fail("freebird" + std::to_string(g) + ".grp " + (shown ? "shown" : "hidden"));
        }
        return pass();
    }
    if (s.screen == "creator_hair") {
        const Value c = ui.interp().send("charcreator_panel", "get_tour_char", {});
        const std::string hair = c.object ? ui.interp().send(c.object, "get_outfit", {Value::symbol("hair")}).str() : std::string();
        std::printf("  creator: hair %s\n", hair.c_str());
        if (hair.empty() || hair == "none") return fail("no hair chosen");
        return pass();
    }
    // rb_career_band (GD05-072): the band (tour performer) and player 0's
    // character (its member); the event's reward_per_star from
    // config/tour_band.dta's (tiers (<tier> (<venue> (<event> ...)))).
    if (s.screen.rfind("rb_band_", 0) == 0 && s.screen != "rb_band_wears") {
        Interp& in = ui.interp();
        auto send = [&](ScriptObject* o, const char* msg, std::vector<Value> a = {}) { return o ? in.send(o, msg, a) : Value::integer(0); };
        ScriptObject* band = in.send("tour", "performer", {}).object;
        const Value pc = in.send("game", "get_player_config", {Value::integer(0)});
        ScriptObject* ch = send(pc.object, "get_char").object;
        if (!band || !ch || ch->className() != "TourCharLocal") return fail(s.screen + ": no band or no member character");
        const int bandMoney = send(band, "money").integerValue(), charMoney = send(ch, "money").integerValue();
        const int fame = send(band, "world_fame").integerValue();
        const std::string torso = send(ch, "get_outfit", {Value::symbol("torso")}).str();
        std::printf("  %s: band %s money %d fame %d; member %s money %d torso %s\n", s.screen.c_str(), send(band, "band_name").str().c_str(), bandMoney,
                    fame, send(ch, "get_char_name").str().c_str(), charMoney, torso.c_str());
        if (s.screen == "rb_band_cash0") {
            std::printf("  expected: the band's money is its member's %d; measured %d\n", charMoney, bandMoney);
            if (bandMoney != charMoney) return fail("the band's money is not its member's");
            careerCash_ = charMoney;
            return pass();
        }
        if (s.screen == "rb_band_member_on_stage") {
            const int n = ui.host().gameCounter(0, "band_piece:" + torso);
            std::printf("  expected: the guitarist in %s; measured %d member(s)\n", torso.c_str(), n);
            if (n < 1) return fail("the band's guitarist is not the player's character");
            return pass();
        }
        if (s.screen == "rb_band_cash") {
            ScriptObject* ev = in.send("tour", "event", {}).object;
            const std::string venue = send(ev, "get", {Value::symbol("venue")}).str(), name = send(ev, "get", {Value::symbol("event_name")}).str();
            const DataNode* tour = ui.bootTree() ? ui.bootTree()->find("tour") : nullptr;
            const DataNode* tiers = tour && tour->find("band") ? tour->find("band")->find("tiers") : nullptr;
            int per = -1;
            for (size_t i = 1; tiers && i < tiers->children.size() && per < 0; ++i)
                if (const DataNode* v = tiers->children[i].isList() ? tiers->children[i].find(venue) : nullptr)
                    if (const DataNode* e = v->find(name))
                        if (const DataNode* rp = e->find("reward_per_star")) per = rp->intAt(1);
            const int want = careerCash_ + per * fame;
            std::printf("  expected: member %d + %d x %d stars = %d, the band's money the same; measured: member %d, band %d (event %s/%s)\n",
                        careerCash_, per, fame, want, charMoney, bandMoney, venue.c_str(), name.c_str());
            if (per < 0) return fail("no event " + venue + "/" + name + " in config/tour_band.dta");
            if (fame < 1) return fail("the gig earned no star");
            if (charMoney != want) return fail("the member was paid " + std::to_string(charMoney - careerCash_));
            if (bandMoney != charMoney) return fail("the band's money is not its member's");
            SDL_setenv_unsafe("GD05_WALK_CAREER", (std::to_string(bandMoney) + " " + std::to_string(fame)).c_str(), 1);
            return pass();
        }
        if (s.screen == "rb_band_reloaded") {
            int wMoney = -1, wFame = -1;
            std::istringstream is(SDL_getenv("GD05_WALK_CAREER") ? SDL_getenv("GD05_WALK_CAREER") : "");
            is >> wMoney >> wFame;
            std::printf("  expected (before the restart): money %d, fame %d; measured: band %d, member %d, fame %d\n", wMoney, wFame, bandMoney, charMoney,
                        fame);
            if (wMoney < 0) return fail("no expectations from before the restart");
            if (bandMoney != wMoney || charMoney != wMoney || fame != wFame) return fail("the band changed through the save");
            return pass();
        }
        return fail("unknown check " + s.screen);
    }
    // rb_career (GD05-072): expectations from the data, values measured
    // through the scripts' own objects (tour performer, charcreator_panel).
    if (s.screen.rfind("rb_creator_", 0) == 0 || s.screen.rfind("rb_career_", 0) == 0 || s.screen == "rb_shop_bought" ||
        s.screen == "rb_band_wears" || s.screen.rfind("rb_gig_", 0) == 0) {
        Interp& in = ui.interp();
        const DataNode* tour = ui.bootTree() ? ui.bootTree()->find("tour") : nullptr;
        auto send = [&](ScriptObject* o, const char* msg, std::vector<Value> a = {}) { return o ? in.send(o, msg, a) : Value::integer(0); };
        ScriptObject* creating = in.send("charcreator_panel", "get_tour_char", {}).object;
        ScriptObject* perf = in.send("tour", "performer", {}).object;
        ScriptObject* c = s.screen.rfind("rb_creator_", 0) == 0 ? creating : perf;
        if (!c) return fail(s.screen + ": no character");
        const int money = send(c, "money").integerValue();
        const std::string gender = send(c, "get_gender").str(), inst = send(c, "get_instrument").str();
        const std::string hair = send(c, "get_outfit", {Value::symbol("hair")}).str(), torso = send(c, "get_outfit", {Value::symbol("torso")}).str();
        const int skin = send(c, "skin_color").integerValue();
        auto owns = [&](const std::string& cat, const std::string& o) { return send(c, "has_outfit", {Value::symbol(cat), Value::symbol(o)}).truthy(); };
        std::printf("  %s: %s %s, money %d, skin %d, hair %s, torso %s\n", s.screen.c_str(), gender.c_str(), inst.c_str(), money, skin, hair.c_str(),
                    torso.c_str());
        if (s.screen == "rb_creator_attitude") {
            // attitude.lst's data (attitude_banger attitude_dramatic
            // attitude_rocker attitude_spazz), on kGenreRocker's row: one down
            // is attitude_spazz, kGenreSpazz (charcreator.dta sym_to_attitude).
            const int a = send(c, "get_attitude").integerValue();
            std::printf("  expected: attitude kGenreSpazz (2048); measured %d\n", a);
            if (a != 2048) return fail("the attitude chosen in the creator is " + std::to_string(a));
            return pass();
        }
        if (s.screen == "rb_creator_before") {
            careerHair0_ = hair;
            return pass();
        }
        if (s.screen == "rb_creator_look") {
            // Two downs on skin.lst from the first colour: the third (index 2).
            ScriptObject* sl = ui.panel("charcreator_appearance_panel") ? ui.panel("charcreator_appearance_panel")->component("skin.lst") : nullptr;
            std::printf("  skin.lst: %d colours, at %d\n", send(sl, "num_data").integerValue(), send(sl, "selected_pos").integerValue());
            std::printf("  expected: skin 2, hair not %s; measured: skin %d, hair %s\n", careerHair0_.c_str(), skin, hair.c_str());
            if (skin != 2) return fail("the skin colour chosen in the creator is " + std::to_string(skin) + ", not 2");
            if (hair.empty() || hair == "none" || hair == careerHair0_) return fail("no hair chosen in the creator (" + hair + ")");
            return pass();
        }
        if (s.screen == "rb_career_cash0") {
            const int want = tour && tour->find("default_money") ? tour->find("default_money")->intAt(1) : -1;
            std::printf("  expected cash (config/tour.dta default_money) %d, measured %d\n", want, money);
            if (money != want) return fail("a new character has " + std::to_string(money) + ", not " + std::to_string(want));
            careerCash_ = money;
            return pass();
        }
        if (s.screen == "rb_shop_bought") {
            const int price = ui::rbOutfitCost(ui, gender, "torso", "flannelcoat_checked");
            std::printf("  expected: torso flannelcoat_checked, cash %d - %d = %d; measured: torso %s, cash %d, owned %d\n", careerCash_, price,
                        careerCash_ - price, torso.c_str(), money, int(owns("torso", "flannelcoat_checked")));
            if (price <= 0) return fail("flannelcoat_checked has no price in the catalog");
            if (torso != "flannelcoat_checked") return fail("the bought shirt is not worn (torso " + torso + ")");
            if (money != careerCash_ - price) return fail("the cash after the purchase is " + std::to_string(money));
            careerCash_ = money;
            return pass();
        }
        if (s.screen == "rb_band_wears") {
            const int t = ui.host().gameCounter(0, "band_piece:" + torso), h = ui.host().gameCounter(0, "band_piece:" + hair);
            const int att = send(c, "get_attitude").integerValue(), a = ui.host().gameCounter(0, "band_flags:" + std::to_string(att));
            std::printf("  expected: the guitarist built with %s and %s, playing with attitude %d; measured: %d / %d / %d member(s)\n", torso.c_str(),
                        hair.c_str(), att, t, h, a);
            if (t < 1 || h < 1) return fail("the character on stage does not wear its own clothes");
            if (a < 1) return fail("no member plays with the character's attitude");
            return pass();
        }
        if (s.screen == "rb_gig_stars") {
            // As the scripts read them after the song (endgame.dta complete_song {main_performer num_stars}).
            const UiHost::SongResult res = ui.host().lastResult(0);
            careerStars_ = res.valid ? res.stars : ui.host().gameCounter(0, "stars");
            std::printf("  the gig's stars: %d\n", careerStars_);
            if (careerStars_ < 1) return fail("the gig earned no star");
            return pass();
        }
        // The event in config/tour_solo.dta (towns (<town> (<venue> (<event> ...)))).
        ScriptObject* ev = in.send("tour", "event", {}).object;
        const std::string town = send(ev, "get", {Value::symbol("town")}).str(), venue = send(ev, "get", {Value::symbol("venue")}).str(),
                          name = send(ev, "get", {Value::symbol("event_name")}).str();
        const DataNode* solo = tour ? tour->find("solo") : nullptr;
        const DataNode* towns = solo ? solo->find("towns") : nullptr;
        const DataNode* tn = towns ? towns->find(town) : nullptr;
        const DataNode* vn = tn ? tn->find(venue) : nullptr;
        const DataNode* en = vn ? vn->find(name) : nullptr;
        std::string rcat, rout;  // the event's outfit for the instrument and gender
        if (const DataNode* o = en ? en->find("outfit") : nullptr)
            if (const DataNode* i = o->find(inst == "bass" ? "guitar" : inst))
                if (const DataNode* g = i->find(gender)) rcat = g->textAt(1), rout = g->textAt(2);
        if (s.screen == "rb_gig_cash") {
            if (!en) return fail("no event " + town + "/" + venue + "/" + name + " in the data");
            const int per = en->find("reward_per_star") ? en->find("reward_per_star")->intAt(1) : 0;
            const int most = solo->find("max_money") ? solo->find("max_money")->intAt(1) : 999999999;
            const int want = std::min(most, careerCash_ + per * careerStars_);
            const bool won = rcat.empty() || owns(rcat, rout);
            std::printf("  expected: cash %d + %d x %d stars = %d, %s %s owned; measured: cash %d, owned %d (event %s/%s/%s)\n", careerCash_, per,
                        careerStars_, want, rcat.c_str(), rout.c_str(), money, int(won), town.c_str(), venue.c_str(), name.c_str());
            if (money != want) return fail("the gig paid " + std::to_string(money - careerCash_) + ", not " + std::to_string(want - careerCash_));
            if (!won) return fail("the event's outfit " + rout + " was not won");
            const std::string keep = std::to_string(money) + " " + std::to_string(skin) + " " + hair + " " + torso + " " + std::to_string(send(c, "get_attitude").integerValue()) +
                                     " " + rcat + " " + rout;
            SDL_setenv_unsafe("GD05_WALK_CAREER", keep.c_str(), 1);
            return pass();
        }
        if (s.screen == "rb_career_reloaded") {
            std::istringstream is(SDL_getenv("GD05_WALK_CAREER") ? SDL_getenv("GD05_WALK_CAREER") : "");
            int wMoney = -1, wSkin = -1;
            std::string wHair, wTorso, wAtt, wCat, wOut;
            is >> wMoney >> wSkin >> wHair >> wTorso >> wAtt >> wCat >> wOut;
            const std::string att = std::to_string(send(c, "get_attitude").integerValue());
            // The hints seen, as the save holds them: the guitarist's
            // welcome only (config/tour_solo.dta: welcome_guitar's cond).
            std::string hints;
            try {
                std::ifstream f(prefDir() + "rb_profile.json");
                std::stringstream ss;
                ss << f.rdbuf();
                const Json root = Json::parse(ss.str());
                for (const Json& ch : root["characters"].a)
                    for (const Json& h : ch["seen_hints"].a) hints += (hints.empty() ? "" : ",") + h.str();
            } catch (const std::exception& e) {
                hints = std::string("(") + e.what() + ")";
            }
            std::printf("  expected: attitude %s, seen hints welcome_guitar; measured: attitude %s, seen hints %s\n", wAtt.c_str(), att.c_str(),
                        hints.c_str());
            if (att != wAtt) return fail("the attitude changed through the save");
            if (hints != "welcome_guitar") return fail("the seen hints are " + hints);
            const int fame = send(c, "world_fame").integerValue();
            const bool won = wCat.empty() || owns(wCat, wOut), bought = owns("torso", "flannelcoat_checked");
            std::printf("  expected (before the restart): cash %d, skin %d, hair %s, torso %s, %s owned, 1+ event played; measured: cash %d, skin %d, "
                        "hair %s, torso %s, owned %d / %d, events played %d\n",
                        wMoney, wSkin, wHair.c_str(), wTorso.c_str(), wOut.c_str(), money, skin, hair.c_str(), torso.c_str(), int(won), int(bought), fame);
            if (wMoney < 0) return fail("rb_career's first half left no expectations (GD05_WALK_CAREER)");
            if (money != wMoney || skin != wSkin || hair != wHair || torso != wTorso) return fail("the character changed through the save");
            if (!won || !bought) return fail("the owned clothes were lost through the save");
            if (fame < 1) return fail("the played event was lost through the save");
            return pass();
        }
        return fail("unknown check " + s.screen);
    }
    // rb_shop_items (GD05-072): the instrument shop against char/instruments.dta.
    if (s.screen.rfind("rb_shop_", 0) == 0 && s.screen != "rb_shop_bought") {
        Interp& in = ui.interp();
        ScriptObject* c = in.send("tour", "performer", {}).object;
        if (!c) return fail(s.screen + ": no character");
        auto send = [&](const char* msg, std::vector<Value> a = {}) { return in.send(c, msg, a); };
        const int money = send("money").integerValue();
        const std::string gender = send("get_gender").str(), guitar = send("get_outfit", {Value::symbol("guitar")}).str();
        // The first model of (guitar (groups ...)) and its price, from the data.
        {
            const ui::RbModel probe = ui::rbFirstModel(ui, gender, "guitar");
            const int price = probe.cost;
            const std::string model = probe.model, finish = probe.finish;
            std::printf("  %s: money %d, guitar %s; data: first guitar %s (%s) at %d\n", s.screen.c_str(), money, guitar.c_str(), model.c_str(),
                        finish.c_str(), price);
            if (model.empty() || price <= 0) return fail("no guitar model with a price in char/instruments.dta");
            if (s.screen == "rb_shop_too_dear") {
                std::printf("  expected: shop_error_money_screen, cash %d kept (%d < %d); measured: %s, cash %d\n", careerCash_, careerCash_, price,
                            ui.currentScreen().c_str(), money);
                if (money != careerCash_) return fail("the cash changed on a refused purchase");
                return pass();
            }
            if (s.screen == "rb_shop_cheat_money") {
                send("cheat_add_money", {Value::integer(2000)});
                careerCash_ = send("money").integerValue();
                std::printf("  cheat_add_money 2000: cash %d\n", careerCash_);
                return pass();
            }
            if (s.screen == "rb_shop_guitar") {
                const bool owned = send("has_outfit", {Value::symbol("guitar"), Value::symbol(model)}).truthy();
                std::printf("  expected: cash %d - %d = %d, guitar %s, owned; measured: cash %d, guitar %s, owned %d\n", careerCash_, price,
                            careerCash_ - price, finish.c_str(), money, guitar.c_str(), int(owned));
                if (money != careerCash_ - price) return fail("the guitar cost " + std::to_string(careerCash_ - money));
                if (guitar != finish || !owned) return fail("the bought guitar is not the character's (" + guitar + ")");
                SDL_setenv_unsafe("GD05_WALK_CAREER", (std::to_string(money) + " " + finish).c_str(), 1);
                return pass();
            }
            if (s.screen == "rb_shop_guitar_on_stage") {
                const int n = ui.host().gameCounter(0, "band_piece:" + finish);
                std::printf("  expected: the guitarist plays %s; measured %d member(s)\n", finish.c_str(), n);
                if (n < 1) return fail("the bought guitar is not on stage");
                return pass();
            }
            if (s.screen == "rb_shop_guitar_reloaded") {
                int wMoney = -1;
                std::string wGuitar;
                std::istringstream is(SDL_getenv("GD05_WALK_CAREER") ? SDL_getenv("GD05_WALK_CAREER") : "");
                is >> wMoney >> wGuitar;
                const bool owned = send("has_outfit", {Value::symbol("guitar"), Value::symbol(model)}).truthy();
                std::printf("  expected (before the restart): cash >= %d, guitar %s owned; measured: cash %d, guitar %s, owned %d\n", wMoney,
                            wGuitar.c_str(), money, guitar.c_str(), int(owned));
                if (wMoney < 0 || money < wMoney || guitar != wGuitar || !owned) return fail("the guitar was lost through the save");
                return pass();
            }
        }
        return fail("unknown check " + s.screen);
    }
    if (s.screen == "restart_app") {  // the game starts again in the same process (runApp): the walk goes on as <walk>_restarted
        ui.host().setEnhancedValue("restart_now", "1");
        return pass();
    }
    if (s.screen == "print_focus") {
        ui::PanelObj* fp = ui.focusPanel();
        std::printf("  focus: screen %s panel %s component %s (up %d)\n", ui.currentScreen().c_str(), fp ? fp->name().c_str() : "-",
                    fp ? fp->focus.c_str() : "-", fp ? int(fp->up) : -1);
        if (fp && fp->className() == "OutfitPanel") {
            const Value prov = ui.interp().send(fp, "focus_provider", {});
            std::printf("  outfit: category %s outfit %s (%d in the list, genre %s)\n", ui.interp().send(fp, "focus_category", {}).str().c_str(),
                        ui.interp().send(fp, "focus_outfit", {}).str().c_str(),
                        prov.object ? ui.interp().send(prov.object, "num_data", {}).integerValue() : -1, fp->getProp(ui.interp(), "genre").str().c_str());
        }
        return pass();
    }
    if (s.screen == "sweep") {
        // Every screen of the scripts, opened in turn (ui goto_screen) and
        // left 700 ms: reached or not, and the script gaps (UNHANDLED /
        // UNKNOWN / STUB keys) it added. Screens that load a song, a movie
        // or a tutorial are skipped (they need a game set up). A report,
        // not a pass / fail: the ledger's menu rows quote it.
        static std::vector<std::string> list;
        static size_t at = 0;
        static double since = -1;
        static std::set<std::string> seen;
        static int reached = 0, clean = 0;
        auto gaps = [&] {
            std::vector<std::string> v;
            for (const auto* m : {&ui.interp().unhandledCounts(), &ui.interp().unknownCounts(), &ui.interp().stubCounts()})
                for (const auto& [k, n] : *m)
                    if (seen.insert(k).second) v.push_back(k);
            return v;
        };
        if (list.empty()) {
            for (const std::string& n : ui.interp().objectNames()) {
                ScriptObject* o = ui.interp().objectNamed(n);
                if (!o || o->className().find("Screen") == std::string::npos) continue;
                static const char* const kSkip[] = {"game_screen", "loading", "gen_weights", "vocal_test", "movie", "preload", "load_song",
                                                    "tutorials_", "drum_trainer_", "intro", "attract", "featurette", "uploader", "autoload",
                                                    "auto_load", "practice_game", "network", "server_connect", "signin", "matchmaking_hth"};
                bool skip = false;
                for (const char* k : kSkip) skip |= n.find(k) != std::string::npos;
                if (!skip) list.push_back(n);
            }
            gaps();
            std::printf("  sweep: %d screens\n", int(list.size()));
        }
        if (at >= list.size()) {
            std::printf("  sweep: %d of %d screens reached, %d with no new script gap\n", reached, int(list.size()), clean);
            return pass();
        }
        if (since < 0) {
            ui.gotoScreen(list[at]);
            since = nowMs;
            return;
        }
        const bool there = ui.currentScreen() == list[at] && !ui.inTransition();
        if (!(there && nowMs - since >= 500) && nowMs - since < 2500) return;
        const bool ok = ui.currentScreen() == list[at];
        const std::vector<std::string> g = gaps();
        reached += ok, clean += ok && g.empty();
        std::string gs;
        for (const std::string& k : g) gs += " [" + k + "]";
        std::printf("  sweep %-45s %s%s\n", list[at].c_str(), ok ? "reached" : ("NOT reached (on " + ui.currentScreen() + ")").c_str(), gs.c_str());
        ++at, since = -1;
        return;
    }
    if (s.screen == "dev_cheat_on") {
        ui.host().setEnhancedValue("dev_key_cheats", "on");
        return pass();
    }
    if (s.screen == "dev_cheat_bound") {
        std::printf("  dev cheats: after the bound key '1', on %s\n", ui.currentScreen().c_str());
        if (ui.currentScreen() != "game_screen") return fail("a key bound to a control ran a developer cheat");
        return pass();
    }
    // counter:<key>:<min>: the game's counter (App::gameCounter) reaches min.
    if (s.screen == "counter:shop_bought:1") {
        // An outfit chosen in the shop (worn, bought when it costs).
        ScriptObject* panel = ui.interp().findObject("shop_clothing_genre_panel");
        const int n = panel ? ui.interp().send(panel, "num_bought", {}).integerValue() : 0;
        const Value c = ui.interp().send("tour", "performer", {});
        const int money = c.object ? ui.interp().send(c.object, "money", {}).integerValue() : -1;
        std::printf("  shop: %d outfit(s) worn, %d money left\n", n, money);
        if (n < 1) return fail("no outfit was chosen");
        return pass();
    }
    if (s.screen == "rb_helpbar") {
        // RB's help bar (meta.dta HELPBAR_HANDLERS, helpbar.milo): the
        // screen's (helpbar ...) actions shown with their fret icons.
        ui::PanelObj* hb = ui.panel("helpbar");
        if (!hb || !hb->view || !hb->up) return fail("no help bar up");
        int actions = 0, icons = 0;
        std::string line;
        for (const char* a : {"red_action.lbl", "green_action.lbl", "yellow_action.lbl", "blue_action.lbl"})
            if (hb->view->showing(a) && !hb->view->text(a).empty()) ++actions, line += std::string(a) + "=" + hb->view->text(a) + " ";
        for (const char* c : {"red1.lbl", "green1.lbl", "yellow1.lbl", "blue1.lbl"})
            if (hb->view->showing(c) && !hb->view->text(c).empty()) ++icons, line += std::string(c) + "=" + hb->view->text(c) + " ";
        std::printf("  help bar: %d actions, %d icons: %s\n", actions, icons, line.c_str());
        if (actions < 2) return fail("the help bar shows fewer than two actions");
        if (icons < actions) return fail("an action without its icon");
        return pass();
    }
    if (s.screen == "expect_quit") {  // the game may quit from here on (finish counts it done)
        expectQuit_ = true;
        return pass();
    }
    if (s.screen == "gamepads_button" || s.screen == "click_gamepads_button") {
        // The port's GAMEPADS button (bottom left of the main menu).
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        if (!ui.gamepadsButton(x0, y0, x1, y1)) return fail("no GAMEPADS button on " + ui.currentScreen());
        std::printf("  GAMEPADS button: %.0f %.0f .. %.0f %.0f\n", double(x0), double(y0), double(x1), double(y1));
        if (s.screen == "click_gamepads_button") {
            ui.mouse((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, false);
            ui.mouse((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, true);
        }
        return pass();
    }
    if (s.screen == "quit_missed") return fail("the game did not quit (Esc three times on " + ui.currentScreen() + ")");
    if (s.screen == "click_helpbar_back") {
        // A click on the help bar's Back (its red action) goes back, as red.
        ui::PanelObj* hb = ui.panel("helpbar");
        if (!hb || !hb->view) return fail("no help bar");
        for (const gfx::UiView::Hit& h : hb->view->hits())
            if (h.key == "red_action.lbl") {
                const float x = (h.x0 + h.x1) / 2, y = (h.y0 + h.y1) / 2;
                std::printf("  help bar: click on Back at %.0f, %.0f\n", x, y);
                ui.mouse(x, y, true);
                return pass();
            }
        return fail("the help bar's Back was not drawn");
    }
    // gd04's black-frame probe (usual-bugs.md): probe_black starts
    // counting, no_black fails on any all-black frame presented since.
    if (s.screen == "probe_black") return Canvas::countBlack = true, Canvas::countBlackLit = false, Canvas::blackFrames = 0, pass();
    if (s.screen == "no_black") {
        Canvas::countBlack = false;
        std::printf("  black frames: %d\n", Canvas::blackFrames);
        return Canvas::blackFrames == 0 ? pass() : fail(std::to_string(Canvas::blackFrames) + " black frames presented");
    }
    if (s.screen == "loading_icons") {
        // Wave 04 (GD05-026): loading.milo's band, four inline icon
        // directories from their proxy files (load_icon_*.milo, root
        // "loading"): each icon mesh hangs from its holder (guitar, vox and
        // drum under load_icon_bass), so the four stand side by side; the
        // AnimFilters play the directories' texture swaps.
        ui::PanelObj* p = ui.panel("loading_panel");
        if (!p || !p->view) return fail("no loading_panel view");
        std::vector<float> xs;
        for (const char* d : {"load_icon_guitar", "load_icon_vox", "load_icon_drum", "load_icon_bass"}) {
            const char* m = std::string(d) == "load_icon_guitar" ? "guitar.mesh" : std::string(d) == "load_icon_vox" ? "vox.mesh"
                          : std::string(d) == "load_icon_drum" ? "drummer.mesh" : "bass.mesh";
            const scene::Object* o = p->view->findIn(d, m);
            if (!o) return fail(std::string("no ") + m + " in " + d);
            xs.push_back(p->view->pose().world(o).pos[0]);
        }
        std::printf("  loading icons at x %.1f %.1f %.1f %.1f\n", double(xs[0]), double(xs[1]), double(xs[2]), double(xs[3]));
        for (size_t i = 0; i < xs.size(); ++i)
            for (size_t j = i + 1; j < xs.size(); ++j)
                if (std::fabs(xs[i] - xs[j]) < 20) return fail("two band icons stand on each other");
        return pass();
    }
    if (s.screen.rfind("dev_axis ", 0) == 0) {  // "dev_axis <n> <value>": an axis of the walk's device
        SDL_Joystick* j = SDL_GetJoystickFromID(adapter_);
        if (!j) return fail("the virtual device is not open");
        int axisN = 0, value = 0;
        std::sscanf(s.screen.c_str() + 9, "%d %d", &axisN, &value);
        devAxis(j, axisN, Sint16(std::clamp(value, -32768, 32767)));
        return pass();
    }
    // GD05-073's Overdrive walks (rb_od_*).
    if (s.screen.rfind("od_set:", 0) == 0) {
        const float v = float(std::atoi(s.screen.c_str() + 7)) / 100.0f;
        ui.host().setPlayerStarPower(0, "meter", v);
        return pass();
    }
    if (s.screen.rfind("od_state:", 0) == 0) {
        const Player* pl = ui.host().gamePlayer(0);
        if (!pl) return fail("no player");
        const bool want = s.screen[9] == '1';
        const std::string how = s.screen.size() > 11 ? s.screen.substr(11) : "";
        const bool active = pl->starPowerActive();
        std::printf("  Overdrive by %s with the meter at %s: %s (meter now %.3f)\n", how.c_str(), want ? "60 %" : "45 %", active ? "deployed" : "not deployed",
                    double(pl->starMeter()));
        ++odChecks_;
        if (active != want) return fail("Overdrive by " + how + (want ? " did not deploy at 60 %" : " deployed under 50 %"));
        if (active) odStartMeter_ = pl->starMeter(), odStartBeat_ = ui.host().gameBeat(), odVocal_ = false;
        return pass();
    }
    if (s.screen.rfind("od_auto:", 0) == 0) {
        // The drummer's fill / the singer's shout with the meter at n %:
        // under half nothing deploys by the end of a fill / a gap; at 60 %
        // the next fill's crash / gap's shout deploys.
        const bool vocals = s.screen.rfind("od_auto:vocals", 0) == 0;
        const int pct = std::atoi(s.screen.c_str() + s.screen.rfind(':') + 1);
        const Player* pl = ui.host().gamePlayer(0);
        if (!ui.host().gameLoaded() || (!vocals && !pl)) return fail("no song");
        if (odPhase_ == 0) {  // first poll: the meter set
            ui.host().setPlayerStarPower(0, vocals ? "vocal_meter" : "meter", float(pct) / 100.0f);
            odPhase_ = 1, odFills_ = ui.host().gameCounter(0, "fills_deployed"), odFreestyleSeen_ = false;
            return;
        }
        const bool active = vocals ? ui.host().gameCounter(0, "vocal_od_active") > 0 : pl->starPowerActive();
        const int meterPct = vocals ? ui.host().gameCounter(0, "vocal_od_meter_pct") : int(std::lround(pl->starMeter() * 100));
        if (vocals) {
            // A shout (1.5 s) once in each gap.
            const bool gap = ui.host().gameCounter(0, "vocal_freestyle") > 0;
            if (gap && !odFreestyleSeen_) ui.host().setPlayerStarPower(0, "vocal_shout", 1500), odFreestyleSeen_ = true, ++odGaps_;
            if (!gap && odFreestyleSeen_ && pct < 50) {  // a whole gap shouted through under half
                odFreestyleSeen_ = false;
                std::printf("  Overdrive by a shout at %d %%: %s\n", pct, active ? "deployed" : "not deployed");
                odPhase_ = 0;
                return active ? fail("the singer deployed under 50 %") : pass();
            }
            if (!gap) odFreestyleSeen_ = false;
        } else if (pct < 50) {
            ui.host().setPlayerStarPower(0, "meter", float(pct) / 100.0f);  // held under half while the fills pass
            if (nowMs - stepStart_ > 15000) {
                std::printf("  Overdrive by the drum fill at %d %%: %s (fills deployed %d)\n", pct, active ? "deployed" : "not deployed", ui.host().gameCounter(0, "fills_deployed"));
                odPhase_ = 0;
                return active || ui.host().gameCounter(0, "fills_deployed") > odFills_ ? fail("the drummer deployed under 50 %") : pass();
            }
        }
        if (pct >= 50 && active) {
            std::printf("  Overdrive by %s at %d %%: deployed (meter now %d %%, %d gap(s) shouted, fills deployed %d)\n", vocals ? "a shout" : "the drum fill", pct, meterPct,
                        odGaps_, ui.host().gameCounter(0, "fills_deployed"));
            odPhase_ = 0, ++odChecks_;
            odStartMeter_ = float(meterPct) / 100.0f, odStartBeat_ = ui.host().gameBeat(), odVocal_ = vocals;
            if (vocals) odStartMs_ = ui.host().gameTimeMs();
            return pass();
        }
        if (nowMs - stepStart_ > s.ms) {
            odPhase_ = 0;
            return fail(std::string("Overdrive by ") + (vocals ? "a shout" : "the drum fill") + " never deployed at " + std::to_string(pct) + " %");
        }
        return;
    }
    if (s.screen == "od_drain") {
        // RB drains a full meter over band_energy deploy_beats beats (32):
        // the meter lost since the deploy against the beats gone by / 32.
        // The singer's drains over 32 beats at the song's mean beat [I].
        const Player* pl = ui.host().gamePlayer(0);
        const float now = odVocal_ ? float(ui.host().gameCounter(0, "vocal_od_meter_pct")) / 100.0f : pl ? pl->starMeter() : 0.0f;
        const double beats = ui.host().gameBeat() - odStartBeat_;
        const double lost = double(odStartMeter_ - now), want = beats / 32.0;
        std::printf("  Overdrive drain: %.2f beats, meter %.3f -> %.3f: lost %.3f, want %.3f (beats / 32)\n", beats, double(odStartMeter_), double(now), lost, want);
        odDrainErrPct_ = std::max(odDrainErrPct_, want > 0 ? int(std::lround(std::fabs(lost - want) / want * 100)) : 100);
        if (beats < 2 || std::fabs(lost - want) > 0.12 * want + (odVocal_ ? 0.015 : 0.006)) return fail("the Overdrive drain is off");
        return pass();
    }
    if (s.screen == "unlock_all_songs") {  // long_cheats.dta's code: every song listed (walks that scroll a long list)
        ui.interp().send("profile_mgr", "unlock_all_songs", {});
        return pass();
    }
    if (s.screen == "qp_locked" || s.screen == "qp_locked_song") {
        // rb_qp_locked (GD05-073): the songs RB's SongMgr (0x82431AC8) makes
        // available on a new profile, from the data alone.
        const std::string inst = ui::gameCfgValue(ui, "unlock_song_instrument").str();
        const int initial = ui::gameCfgValue(ui, "unlock_song_initial_num").integerValue();
        std::vector<const SongInfo*> ranked;
        int disc = 0, bonus = 0;
        for (const SongInfo& si : ui.host().songs()) {
            if (si.tutorial) continue;
            ++disc, bonus += si.unlockable;
            if (!si.unlockable && si.rank.count(inst)) ranked.push_back(&si);
        }
        std::stable_sort(ranked.begin(), ranked.end(), [&](const SongInfo* a, const SongInfo* b) { return a->rank.at(inst) < b->rank.at(inst); });
        std::set<std::string> expected;
        for (size_t i = 0; i < ranked.size() && int(i) < initial; ++i) expected.insert(ranked[i]->id);
        if (s.screen == "qp_locked_song") {
            const std::string song = ui.session().song;
            std::printf("  quick play song started: %s (%s)\n", song.c_str(), expected.count(song) ? "available" : "LOCKED");
            return expected.count(song) ? pass() : fail("the random song " + song + " is locked");
        }
        auto* sp = dynamic_cast<ui::ListProvider*>(ui.interp().findObject("song_provider"));
        if (!sp) return fail("no song_provider");
        std::set<std::string> listed;
        for (int i = 0; i < sp->numData(); ++i)
            if (sp->symbol(i) != "random_song") listed.insert(sp->symbol(i));
        int lockedListed = 0, randomLocked = 0;
        for (const std::string& id : listed) lockedListed += !expected.count(id);
        for (int k = 0; k < 300; ++k) {
            const std::string pick = ui.interp().send("song_provider", "get_song_to_set", {Value::integer(0)}).str();
            randomLocked += !expected.count(pick);
        }
        qpLockedListed_ = lockedListed, qpRandomLocked_ = randomLocked;
        std::printf("  quick play: unlock instrument '%s', initial %d; %d disc songs (%d bonus); expected available %zu, listed %zu, locked listed %d, "
                    "random picks of locked songs %d of 300\n", inst.c_str(), initial, disc, bonus, expected.size(), listed.size(), lockedListed, randomLocked);
        if (inst.empty() || expected.empty()) return fail("no unlock instrument / no available song from the data");
        if (lockedListed || randomLocked || listed.size() != expected.size())
            return fail(std::to_string(lockedListed) + " locked songs listed, " + std::to_string(randomLocked) + " random picks locked");
        return pass();
    }
    if (s.screen == "songlist_highlight") {
        // rb_songlist_highlight (GD05-073): the song list's names and
        // headings as drawn (their glyphs' middle along the list, in the
        // panel's space) against the highlight's quad as drawn: exactly one
        // inside, the selected row's.
        ui::PanelObj* p = ui.panel("qp_selsong_panel");
        if (!p || !p->view) return fail("no qp_selsong_panel view");
        const std::string list = "ss_song.lst";
        const gfx::UiView::ListHighlight h = p->view->listHighlight(list);
        if (!h.drawn) return fail("the song list drew no highlight");
        int inside = 0, insideSelected = 0, rows = 0;
        std::string which;
        for (const gfx::TextProbe& t : p->view->textProbes()) {
            if (t.name.rfind(list + "#", 0) != 0 || t.verts.empty()) continue;
            const size_t a = list.size() + 1, b = t.name.find('#', a);
            if (b == std::string::npos) continue;
            const std::string element = t.name.substr(b + 1);
            if (element != "list.lbl" && element != "heading.lbl") continue;
            const int row = std::atoi(t.name.c_str() + a);
            float lo = 1e30f, hi = -1e30f;
            for (const scene::MeshVertex& v : t.verts) {
                const gfx::Vec3 q = t.world.transformPoint({v.pos[0], v.pos[1], v.pos[2]});
                lo = std::min(lo, q.z), hi = std::max(hi, q.z);
            }
            const float mid = (lo + hi) / 2;
            ++rows;
            if (mid >= h.lo && mid <= h.hi) {
                ++inside;
                insideSelected += row == h.selected && element == "list.lbl";
                which += " " + std::to_string(row) + ":" + element;
            }
        }
        ++songlistHlChecks_;
        songlistHlMax_ = std::max(songlistHlMax_, inside);
        std::printf("  songlist highlight: selected row %d, quad %.1f..%.1f, %d of %d drawn names inside:%s\n", h.selected, double(h.lo), double(h.hi),
                    inside, rows, which.c_str());
        if (rows == 0) return fail("no song names drawn");
        if (inside != 1 || insideSelected != 1)
            return fail("the highlight covers " + std::to_string(inside) + " names (" + which + " ), want only the selected row's");
        return pass();
    }
    if (s.screen.rfind("songlist_text:", 0) == 0) {
        // rb_songlist_text (GD05-063): every label text the song list drew
        // into its RenderDir's texture against the size RB gives it. RB's
        // UILabel update (RB 0x8226E0A0) lays the label's text out with
        // the font's kerning plus the label's (the font's +0x1044 raised by
        // the label's kerning while it lays out, then put back), wraps at
        // the label's width (RB 0x82215EE8: Text +0xF4 = width), and for
        // kFitJust (RB 0x8226DDA8) takes the text size down 0.2 at a time
        // from text_size while the text is wider than the box, higher than
        // it (height in the font's cell aspect, RB 0x82214FE8) or a line
        // splits a word. Expected: that size's cell height (size * cell_h /
        // cell_w) and the line's width, through the texture's camera, in
        // texture pixels; drawn: the glyph quads as drawn. > 5% fails.
        const std::string tag = s.screen.substr(14);
        ui::PanelObj* p = ui.panel("qp_selsong_panel");
        if (!p || !p->view) return fail("no qp_selsong_panel view");
        if (const char* dir = SDL_getenv("GD05_WALK_SHOTS")) ui.host().saveScreenshot(std::string(dir) + "/songlist_" + tag + ".bmp");
        if (SDL_getenv("GD05_SONGLIST_DUMP") && p->view->milo()) {
            std::function<void(const scene::Directory&, int)> dump = [&](const scene::Directory& d, int depth) {
                std::printf("  %*sdir %s (%s)\n", depth * 2, "", d.name.c_str(), d.cls.c_str());
                for (const auto& o : d.objects) {
                    const auto* l = o->as<scene::UILabel>();
                    if (!l && !o->as<scene::UIList>() && !o->as<scene::Mesh>() && !o->as<scene::Cam>()) continue;
                    if (const auto* c = o->as<scene::Cam>()) std::printf("  %*scam %s yfov %.4f near %.1f far %.1f\n", depth * 2 + 2, "", o->name.c_str(), double(c->yFov), double(c->nearPlane), double(c->farPlane));
                    const scene::Xfm w = p->view->pose().world(o.get());
                    std::printf("  %*s%s %s show %d at %.0f %.0f %.0f", depth * 2 + 2, "", o->cls.c_str(), o->name.c_str(), int(p->view->showing(o.get())),
                                double(w.pos[0]), double(w.pos[1]), double(w.pos[2]));
                    {
                        const scene::Xfm lo = p->view->pose().local(o.get());
                        std::printf(" local rot [%.3f %.3f %.3f | %.3f %.3f %.3f | %.3f %.3f %.3f] pos %.1f %.1f %.1f world rot0 %.3f %.3f %.3f",
                                    double(lo.rot[0]), double(lo.rot[1]), double(lo.rot[2]), double(lo.rot[3]), double(lo.rot[4]), double(lo.rot[5]),
                                    double(lo.rot[6]), double(lo.rot[7]), double(lo.rot[8]), double(lo.pos[0]), double(lo.pos[1]), double(lo.pos[2]),
                                    double(w.rot[0]), double(w.rot[1]), double(w.rot[2]));
                    }
                    if (l) std::printf(" type %s text '%s' token %s size %.1f fit %u box %.0fx%.0f lead %.2f k %.2f",
                                       l->header.type.c_str(), l->text.c_str(), l->textToken.c_str(), double(l->style.textSize), l->style.fitText,
                                       double(l->style.width), double(l->style.height), double(l->style.leading), double(l->style.kerning));
                    if (const auto* m = o->as<scene::Mesh>(); m && depth > 0 && !p->view->textProbes().empty()) {
                        const gfx::TextProbe& q0 = p->view->textProbes().front();
                        float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
                        const gfx::Mat4 wm = gfx::toMat4(w);
                        for (const scene::MeshVertex& v : m->verts) {
                            const gfx::Vec3 ww = wm.transformPoint({v.pos[0], v.pos[1], v.pos[2]});
                            const gfx::Mat4& mm = q0.drawViewProj;
                            const float cx = mm.at(0, 0) * ww.x + mm.at(0, 1) * ww.y + mm.at(0, 2) * ww.z + mm.at(0, 3);
                            const float cy = mm.at(1, 0) * ww.x + mm.at(1, 1) * ww.y + mm.at(1, 2) * ww.z + mm.at(1, 3);
                            const float cw = mm.at(3, 0) * ww.x + mm.at(3, 1) * ww.y + mm.at(3, 2) * ww.z + mm.at(3, 3);
                            const float px = (cx / cw + 1) * 0.5f * float(q0.targetW), py = (1 - cy / cw) * 0.5f * float(q0.targetH);
                            x0 = std::min(x0, px), x1 = std::max(x1, px), y0 = std::min(y0, py), y1 = std::max(y1, py);
                        }
                        std::printf(" | in the texture (%dx%d): x %.0f..%.0f y %.0f..%.0f", q0.targetW, q0.targetH, double(x0), double(x1), double(y0), double(y1));
                    }
                    std::printf("\n");
                }
                for (const auto& [owner, sub] : d.subdirs)
                    if (sub) dump(*sub, depth + 1);
            };
            dump(p->view->milo()->root(), 0);
        }
        float cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0;
        const bool cloth = p->view->clothRect(cx0, cy0, cx1, cy1);
        auto project = [](const gfx::TextProbe& q, gfx::Vec3 local, float& px, float& py) {
            const gfx::Vec3 w = q.world.transformPoint(local);
            const gfx::Mat4& m = q.drawViewProj;
            const float cx = m.at(0, 0) * w.x + m.at(0, 1) * w.y + m.at(0, 2) * w.z + m.at(0, 3);
            const float cy = m.at(1, 0) * w.x + m.at(1, 1) * w.y + m.at(1, 2) * w.z + m.at(1, 3);
            const float cw = m.at(3, 0) * w.x + m.at(3, 1) * w.y + m.at(3, 2) * w.z + m.at(3, 3);
            px = (cx / cw + 1) * 0.5f * float(q.targetW), py = (1 - cy / cw) * 0.5f * float(q.targetH);
        };
        int measured = 0, bad = 0;
        for (const gfx::TextProbe& q : p->view->textProbes()) {
            if (!q.hasStyle || !q.font || q.verts.size() < 4 || q.targetW <= 0) continue;
            gfx::FontMetrics fm(*q.font);
            const float aspect = fm.lineHeight(1.0f);
            gfx::TextStyle st;
            st.leading = q.leading, st.align = q.align, st.capsMode = q.capsMode, st.wrapWidth = q.boxW;
            st.overrideKerning = true, st.kerning = fm.kerning() + q.kerning;
            float size = q.dataSize;
            gfx::TextLayout lay;
            for (;;) {
                st.size = size;
                lay = gfx::layoutText(&fm, q.text, st);
                if (q.fitType != 2) break;
                float top, bottom;
                lay.extent(st, top, bottom);
                const float h = std::fabs(top - bottom) * aspect, w = lay.maxWidth();
                bool shrink = (q.boxW > 0 && q.boxW < w) || (q.boxH > 0 && q.boxH < h);
                for (size_t li = 0; !shrink && li + 1 < lay.lines.size(); ++li) {
                    const size_t e = lay.lines[li].end;
                    const char a = e > 0 ? lay.text[e - 1] : ' ', b = e < lay.text.size() ? lay.text[e] : ' ';
                    if (a != ' ' && a != '\n' && b != ' ' && b != '\n') shrink = true;
                }
                if (!shrink || size - 0.2f < 0) break;
                size -= 0.2f;
            }
            // Expected: the cell's height and the widest line's width at that
            // size, in the label's own space (its world before the fit scale).
            const float wantH = size * aspect, wantW = lay.maxWidth();
            float ax, ay, bx, by;
            project(q, {0, 0, 0}, ax, ay);
            project(q, {0, 0, wantH / q.fitZ}, bx, by);
            (void)wantW;
            const float expH = std::hypot(bx - ax, by - ay);
            // Drawn: the first glyph quad's height; the quads' x extent.
            float t0x, t0y, t1x, t1y;
            project(q, {q.verts[0].pos[0], 0, q.verts[0].pos[2]}, t0x, t0y);
            project(q, {q.verts[0].pos[0], 0, q.verts[1].pos[2]}, t1x, t1y);
            const float gotH = std::hypot(t1x - t0x, t1y - t0y);
            float minX = 1e30f, maxX = -1e30f;
            for (const scene::MeshVertex& v : q.verts) minX = std::min(minX, v.pos[0]), maxX = std::max(maxX, v.pos[0]);
            // The size the port laid the text out at (a quad is one cell high).
            const float drawnSize = (q.verts[0].pos[2] - q.verts[1].pos[2]) / aspect;
            // The glyphs' span: the first quad's left to the last's right.
            float l0x, l0y, l1x, l1y;
            project(q, {0, 0, 0}, l0x, l0y);
            project(q, {maxX - minX, 0, 0}, l1x, l1y);
            const float gotSpan = std::hypot(l1x - l0x, l1y - l0y);
            // The same span RB's layout gives (first glyph's left to the last glyph's right).
            float wantSpanL = 1e30f, wantSpanR = -1e30f;
            for (const scene::MeshVertex& v : lay.verts) wantSpanL = std::min(wantSpanL, v.pos[0]), wantSpanR = std::max(wantSpanR, v.pos[0]);
            float s0x, s0y, s1x, s1y;
            project(q, {0, 0, 0}, s0x, s0y);
            project(q, {(wantSpanR - wantSpanL) / q.fitX, 0, 0}, s1x, s1y);
            const float wantSpan = std::hypot(s1x - s0x, s1y - s0y);
            const float dh = expH > 0 ? gotH / expH - 1 : 0, dw = wantSpan > 0 ? gotSpan / wantSpan - 1 : 0;
            const float toScreen = cloth && q.targetH > 0 ? (cy1 - cy0) / float(q.targetH) : 1;
            const bool off = std::fabs(dh) > 0.05f || std::fabs(dw) > 0.05f;
            ++measured, bad += off;
            std::printf("  %-34s %-24.24s font %-22s size %5.2f -> RB %5.2f (port %5.2f) fit %u box %5.1fx%4.1f k %5.3f+%5.3f | cell h px want %5.1f got %5.1f (%+.1f%%) | span px want %6.1f got %6.1f (%+.1f%%) | screen h ~%4.1f px%s\n",
                        q.name.c_str(), q.text.c_str(), q.font->name.c_str(), double(q.dataSize), double(size), double(drawnSize * q.fitZ), q.fitType,
                        double(q.boxW), double(q.boxH), double(fm.kerning()), double(q.kerning), double(expH), double(gotH), double(dh * 100), double(wantSpan),
                        double(gotSpan), double(dw * 100), double(gotH * toScreen), off ? "  <-- off" : "");
        }
        if (SDL_getenv("GD05_SONGLIST_DUMP"))
            for (const gfx::TextProbe& q : p->view->textProbes()) {
                const gfx::Vec3 o = q.world.transformPoint({0, 0, 0});
                float zt = -1e9f, zb = 1e9f;
                for (const scene::MeshVertex& v : q.verts) zt = std::max(zt, v.pos[2]), zb = std::min(zb, v.pos[2]);
                const gfx::Vec3 ox = q.world.transformPoint({1, 0, 0}), oz = q.world.transformPoint({0, 0, 1});
                float px0, py0;
                {
                    const gfx::Mat4& m = q.drawViewProj;
                    const float cx = m.at(0, 0) * o.x + m.at(0, 1) * o.y + m.at(0, 2) * o.z + m.at(0, 3);
                    const float cy = m.at(1, 0) * o.x + m.at(1, 1) * o.y + m.at(1, 2) * o.z + m.at(1, 3);
                    const float cw = m.at(3, 0) * o.x + m.at(3, 1) * o.y + m.at(3, 2) * o.z + m.at(3, 3);
                    px0 = (cx / cw + 1) * 0.5f * float(q.targetW), py0 = (1 - cy / cw) * 0.5f * float(q.targetH);
                }
                std::printf("  probe %s align 0x%x lead %.3f origin %.1f %.1f %.1f (texture %.0f %.0f) scale x %.3f z %.3f quads z %.1f..%.1f\n", q.name.c_str(), q.align, double(q.leading),
                            double(o.x), double(o.y), double(o.z), double(px0), double(py0), double(std::hypot(ox.x - o.x, ox.z - o.z)), double(std::hypot(oz.x - o.x, oz.z - o.z)), double(zb), double(zt));
            }
        std::printf("  songlist_text %s: %d texts measured, %d off by more than 5%%\n", tag.c_str(), measured, bad);
        if (measured < 5) return fail("too few song list texts measured");
        if (bad) return fail(std::to_string(bad) + " song list text(s) off their data size by more than 5%");
        return pass();
    }
    if (s.screen == "song_list_layout") {
        // RB's song list (qp_selsong.milo, wave 03): drawn through its
        // texture onto the screen; its rows (names, headings) do not run
        // into each other and sit inside the cloth's place.
        ui::PanelObj* p = ui.panel("qp_selsong_panel");
        if (!p || !p->view) return fail("no qp_selsong_panel view");
        const auto& rd = p->view->rendered();
        std::vector<gfx::UiView::Hit> rows;
        for (const gfx::UiView::Hit& h : p->view->hits())
            if (h.key.rfind("ss_song.lst#", 0) == 0) rows.push_back(h);
        std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.y0 < b.y0; });
        // Wave 04 (GD05-022): the texture on the swaying cloth through
        // cloth.cam; the cloth is polled every frame (CharTransCopy, the
        // hair) and its mesh moves.
        float cx0 = rd.x0, cy0 = rd.y0, cx1 = rd.x1, cy1 = rd.y1;
        const bool cloth = p->view->clothRect(cx0, cy0, cx1, cy1);
        std::printf("  song list: %s, %d cloth polls, %zu row texts, cloth %.0f..%.0f x %.0f..%.0f\n",
                    cloth ? "on the cloth" : rd.texture.id ? "flat texture" : "none", p->view->clothPolls(), rows.size(), double(cx0), double(cx1), double(cy0), double(cy1));
        if (!cloth && !rd.texture.id) return fail("the song list's texture was not drawn");
        if (!SDL_getenv("GD05_SONGLIST_FLAT") && (!cloth || p->view->clothPolls() < 10)) return fail("the cloth is not polled");
        if (rows.size() < 5) return fail("too few rows drawn");
        for (size_t i = 0; i + 1 < rows.size(); ++i) {
            const float overlap = rows[i].y1 - rows[i + 1].y0, h = std::min(rows[i].y1 - rows[i].y0, rows[i + 1].y1 - rows[i + 1].y0);
            if (overlap > 0.35f * h) return fail(rows[i].key + " runs into " + rows[i + 1].key);
            if (rows[i].x0 < cx0 - 1 || rows[i].x1 > cx1 + 1) return fail(rows[i].key + " outside the cloth");
        }
        return pass();
    }
    if (s.screen.rfind("tshot:", 0) == 0) {  // rb_title_back: a named shot and the transition state
        const std::string tag = s.screen.substr(6);
        if (const char* dir = SDL_getenv("GD05_WALK_SHOTS")) ui.host().saveScreenshot(std::string(dir) + "/title_" + tag + ".bmp");
        std::string panels;
        if (ui::ScreenObj* sc = ui.screen(ui.currentScreen()))
            for (ui::PanelObj* p : sc->panels()) panels += " " + p->name() + (p->up ? "" : "(down)");
        std::printf("  %-12s %s:%s%s\n", tag.c_str(), ui.currentScreen().c_str(), panels.c_str(), ui.inTransition() ? " [transition]" : "");
        return pass();
    }
    if (s.screen.rfind("look:", 0) == 0) {
        // rb_look: the frame (saved as look_NN.bmp / look_pause.bmp under
        // GD05_WALK_SHOTS) at 640x360 (2x2 boxes of the 1280x720 picture).
        const std::string what = s.screen.substr(5);
        if (what.rfind("shot:", 0) == 0) {  // the next fixed shot (empty: the director's again)
            const std::string n = what.substr(5);
            if (n.empty()) SDL_unsetenv_unsafe("GD05_FORCE_SHOT");
            else SDL_setenv_unsafe("GD05_FORCE_SHOT", ("#" + n).c_str(), 1);
            return pass();
        }
        const int draws = ui.host().gameCounter(0, "world_draws"), holds = ui.host().gameCounter(0, "world_holds");
        if (what == "frame" && lookPace_[0] < 0) lookPace_ = {nowMs, double(draws), double(draws + holds)};
        if (what == "eval") {
            if (lookFrames_.empty()) return fail("look: no frames");
            // Rnd's pacing (RB 0x8218EF80): the world at the PostProc's
            // emulate_fps (ProFilm: 24), the frames around it at the display's.
            const double secs = std::max(1.0, (nowMs - lookPace_[0]) / 1000), fps = ui.host().gameCounter(0, "world_emulate_fps");
            const double drawn = (draws - lookPace_[1]) / secs, shown = (draws + holds - lookPace_[2]) / secs;
            std::printf("  look: the world drawn %.1f times a second (emulate_fps %.0f), frames %.1f a second\n", drawn, fps, shown);
            if (fps > 0 && fps < 40 && shown > 1.2 * fps && std::fabs(drawn / fps - 1) > 0.15)
                return fail("look: the world drawn " + std::to_string(drawn) + " times a second, not the PostProc's " + std::to_string(fps));
            std::array<double, 4> m{};
            for (const auto& f : lookFrames_)
                for (size_t k = 0; k < 4; ++k) m[k] += f[k] / double(lookFrames_.size());
            // The movies' numbers (the header of this walk).
            constexpr double kLuma = 69.3, kContrast = 55.4, kSat = 0.397, kGrain = 1.97;
            std::printf("  look: %zu frames: luma %.1f (movies %.1f, %+.0f%%), contrast %.1f (%.1f, %+.0f%%), saturation %.3f (%.3f), grain %.2f (%.2f, %+.0f%%)\n",
                        lookFrames_.size(), m[0], kLuma, 100 * (m[0] / kLuma - 1), m[1], kContrast, 100 * (m[1] / kContrast - 1), m[2], kSat, m[3],
                        kGrain, 100 * (m[3] / kGrain - 1));
            if (!lookPauseError_.empty()) return fail(lookPauseError_);
            if (std::fabs(m[0] / kLuma - 1) > 0.15) return fail("look: luma " + std::to_string(m[0]) + " not within 15% of the movies'");
            if (std::fabs(m[1] / kContrast - 1) > 0.15) return fail("look: contrast " + std::to_string(m[1]) + " not within 15% of the movies'");
            if (std::fabs(m[3] / kGrain - 1) > 0.40) return fail("look: grain " + std::to_string(m[3]) + " not within 40% of the movies'");
            return pass();
        }
        const char* dir = SDL_getenv("GD05_WALK_SHOTS");
        char name[48];
        std::snprintf(name, sizeof name, "/look_%s.bmp", what == "frame" ? std::to_string(lookFrames_.size()).c_str() : what.c_str());
        const std::string path = std::string(dir ? dir : SDL_GetBasePath()) + name;
        if (!ui.host().saveScreenshot(path)) return fail("look: no screenshot");
        SDL_Surface* raw = SDL_LoadBMP(path.c_str());
        SDL_Surface* img = raw ? SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32) : nullptr;
        if (raw) SDL_DestroySurface(raw);
        if (!img) return fail("look: the screenshot does not load");
        if (!dir) SDL_RemovePath(path.c_str());
        const int w = img->w / 2, h = img->h / 2;
        std::vector<float> y(size_t(w) * size_t(h));
        double sum = 0, sum2 = 0, sat = 0;
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) {
                float c[3] = {0, 0, 0};
                for (int dy = 0; dy < 2; ++dy)
                    for (int dx = 0; dx < 2; ++dx) {
                        const Uint8* px = static_cast<const Uint8*>(img->pixels) + (2 * j + dy) * img->pitch + (2 * i + dx) * 4;
                        for (int k = 0; k < 3; ++k) c[k] += 0.25f * px[k];
                    }
                const float l = 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
                y[size_t(j) * size_t(w) + size_t(i)] = l;
                sum += l, sum2 += double(l) * l;
                const float mx = std::max({c[0], c[1], c[2]}), mn = std::min({c[0], c[1], c[2]});
                if (mx > 8) sat += (mx - mn) / mx;
            }
        const double n = double(w) * double(h), mean = sum / n;
        SDL_DestroySurface(img);
        if (what == "prepause") {
            lookPrePause_ = y;
            std::printf("  look: before the pause, luma %.1f\n", mean);
            return pass();
        }
        if (what == "pause") {
            // The screen's outer strips (x < 90, x >= 1190 at 1280x720: the
            // pause art's octopus reaches in there only with its outlines),
            // the pixels lit before (luma > 25): the median of paused over
            // the rule's value, 0.3 x the colour before the display curve
            // (D25: the screen shows outputCurve(c)), back through it.
            float gamma = 0.85f;
            if (const char* e = SDL_getenv("GD05_GAMMA")) gamma = float(std::atof(e));
            auto inverse = [&](float v) {  // outputCurve is increasing
                float lo = 0, hi = 1;
                for (int k = 0; k < 24; ++k) {
                    const float m = 0.5f * (lo + hi);
                    (gamma > 0 ? gfx::outputCurve(m, gamma) : m) < v ? lo = m : hi = m;
                }
                return 0.5f * (lo + hi);
            };
            auto rule = [&](float before) {
                const float c = 0.3f * inverse(before / 255);
                return 255 * (gamma > 0 ? gfx::outputCurve(c, gamma) : c);
            };
            std::vector<double> r;
            if (lookPrePause_.size() == y.size())
                for (int j = 0; j < h; ++j)
                    for (int i = 0; i < w; ++i)
                        if (i < 45 || i >= 595)
                            if (const float b = lookPrePause_[size_t(j) * size_t(w) + size_t(i)]; b > 25)
                                r.push_back(y[size_t(j) * size_t(w) + size_t(i)] / std::max(1.0f, rule(b)));
            // A failure here is reported by look:eval (both measured in one run).
            if (r.size() < 500) {
                lookPauseError_ = "look: too little lit background before the pause (" + std::to_string(r.size()) + " pixels)";
                return pass();
            }
            std::nth_element(r.begin(), r.begin() + long(r.size() / 2), r.end());
            const double ratio = r[r.size() / 2];
            std::printf("  look: paused, the background at %.2f of black_bg.mat's rule (0.3 of the colour; median of %zu pixels)\n", ratio, r.size());
            // The flicker (x 0.8..0.9 under ProFilm) and the grain move it a little.
            if (std::fabs(ratio - 1) > 0.25) lookPauseError_ = "look: the pause background is " + std::to_string(ratio) + " of black_bg.mat's rule";
            return pass();
        }
        double grain = 0;
        int gn = 0;
        for (int j = 1; j + 1 < h; ++j)
            for (int i = 1; i + 1 < w; ++i) {
                const float l = y[size_t(j) * size_t(w) + size_t(i)];
                if (l <= 20) continue;
                float b = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) b += y[size_t(j + dy) * size_t(w) + size_t(i + dx)];
                grain += std::fabs(l - b / 9), ++gn;
            }
        lookFrames_.push_back({mean, std::sqrt(std::max(0.0, sum2 / n - mean * mean)), sat / n, gn ? grain / gn : 0});
        const auto& f = lookFrames_.back();
        std::printf("  look %2zu: luma %.1f contrast %.1f saturation %.3f grain %.2f\n", lookFrames_.size() - 1, f[0], f[1], f[2], f[3]);
        return pass();
    }
    if (s.screen == "band_start_shot") {  // rb_band_start's screenshots (GD05_WALK_SHOTS)
        static int n = 0;
        char name[32];
        std::snprintf(name, sizeof name, "/band_start_%02d.bmp", n++);
        if (const char* dir = SDL_getenv("GD05_WALK_SHOTS")) ui.host().saveScreenshot(std::string(dir) + name);
        return pass();
    }
    // counter_max:<key>:<max>: the game's counter stays at or under max.
    if (s.screen.rfind("counter_max:", 0) == 0) {
        const std::string rest = s.screen.substr(12);
        const size_t c = rest.rfind(':');
        const std::string key = rest.substr(0, c);
        const int most = std::atoi(rest.c_str() + c + 1);
        const int got = ui.host().gameCounter(0, key);
        std::printf("  counter %s: %d (want at most %d)\n", key.c_str(), got, most);
        if (got > most) return fail(key + " is " + std::to_string(got));
        return pass();
    }
    if (s.screen.rfind("od_meter:", 0) == 0) {  // rb_highway: player 1's Overdrive meter set
        ui.host().setPlayerStarPower(0, "meter", float(std::atof(s.screen.c_str() + 9)));
        return pass();
    }
    if (s.screen.rfind("counter:", 0) == 0) {
        const std::string rest = s.screen.substr(8);
        const size_t c = rest.rfind(':');
        const std::string key = rest.substr(0, c);
        const int want = c == std::string::npos ? 1 : std::atoi(rest.c_str() + c + 1);
        const int got = ui.host().gameCounter(0, key);
        std::printf("  counter %s: %d (want at least %d)\n", key.c_str(), got, want);
        if (got < want) return fail(key + " is " + std::to_string(got));
        return pass();
    }
    if (s.screen == "qp_footer") {
        // GD02-156: quick play's footer ("Play Career Mode to get more songs")
        // only while a tier is missing from the list; every save's The
        // Glitches lists all eight, so no row carries it.
        auto* sp = dynamic_cast<ui::ListProvider*>(ui.interp().findObject("song_provider"));
        if (!sp) return fail("no song_provider");
        std::vector<gfx::UiView::ListRow> rows;
        int sel = 0;
        sp->rows(rows, 0, sel);
        int blurbs = 0;
        std::set<std::string> venues;
        for (const auto& r : rows) {
            auto it = r.texts.find("blurb.txt");
            blurbs += it != r.texts.end() && !it->second.empty();
        }
        for (int i = 0; i < sp->numData(); ++i) venues.insert(ui::campaign::venueOf(ui, sp->symbol(i)));
        const bool all = std::all_of(ui::campaign::config(ui).tiers.begin(), ui::campaign::config(ui).tiers.end(),
                                     [&](const auto& tier) { return venues.count(tier.first) > 0; });
        std::printf("  quick play: %zu venues listed (all tiers: %s), %d footer row(s)\n", venues.size(), all ? "yes" : "no", blurbs);
        if (all && blurbs) return fail("every tier is listed but the footer still says to play more");
        return pass();
    }
    if (s.screen == "load_hold") {
        // The song's loading screen stays up at least 2 s with its animation
        // (UiManager::holdingLoadScreen, GD02-155).
        std::printf("  loading screen shown %.0f ms\n", lastLoadingMs_);
        if (lastLoadingMs_ < ui::UiManager::kLoadScreenHoldMs - 50) return fail("the loading screen passed in under 2 s");
        return pass();
    }
    if (s.screen == "name_entry_1" || s.screen == "name_entry_2") {
        // BandTextEntry's Texts are aligned middle left (0x82144558): the
        // entered text starts at the same x whatever its length and the
        // picked character follows it (GD02-068).
        ui::PanelObj* fp = ui.focusPanel();
        const gfx::UiView::Hit *entered = nullptr, *picking = nullptr;
        if (fp && fp->view)
            for (const gfx::UiView::Hit& h : fp->view->hits()) {
                if (h.key.size() > 8 && h.key.compare(h.key.size() - 8, 8, "#entered") == 0) entered = &h;
                if (h.key.size() > 8 && h.key.compare(h.key.size() - 8, 8, "#picking") == 0) picking = &h;
            }
        if (!entered || !picking) return fail("the name entry drew no entered text or picked character");
        if (const char* dir = SDL_getenv("GD05_WALK_SHOTS")) ui.host().saveScreenshot(std::string(dir) + "/" + s.screen + ".bmp");
        std::printf("  name entry: entered %.1f..%.1f, picking %.1f..%.1f\n", entered->x0, entered->x1, picking->x0, picking->x1);
        // Ink boxes: the dotted font's letters reach a little past their
        // advance (GH2 moves the picked Text by the layout's width), so a
        // few pixels of the boxes may touch; more is an overlap.
        if (picking->x0 < entered->x1 - 0.12f * (entered->x1 - entered->x0)) return fail("the picked character overlaps the entered name");
        if (s.screen == "name_entry_1") nameX0_ = entered->x0;
        else if (std::fabs(entered->x0 - nameX0_) > 1) return fail("the name does not grow left to right (its start moved)");
        return pass();
    }
    if (s.screen == "locked_portraits") {
        // career.dta's enter: each locked character's portrait draws with
        // char_locked.mat, each unlocked one with char_<c>.mat (GD02-079).
        ui::PanelObj* p = ui.panel("sel_character_panel");
        if (!p || !p->view) return fail("no sel_character_panel view");
        int locked = 0;
        for (const char* c : {"punk", "alterna", "glam", "goth", "metal", "rockabill", "rock", "deathmetal", "classic", "funk1", "grim"}) {
            const bool open = ui::campaign::isUnlocked(ui, c);
            const std::string mat = p->view->meshMat(std::string("char_") + c + ".mesh");
            const std::string want = open ? std::string("char_") + c + ".mat" : "char_locked.mat";
            if (mat != want) return fail(std::string("portrait ") + c + " drawn with '" + mat + "', not " + want);
            locked += open ? 0 : 1;
        }
        std::printf("  character select: %d locked portraits\n", locked);
        return locked > 0 ? pass() : fail("a new band has every character unlocked");
    }
    if (s.screen == "backdrop_band") {
        // Enhanced Options' still shot always shows the band, its singer on
        // the right (docs/guidelines/universal_launcher.md, GD02-074).
        const bool ok = ui.host().menuBackdropFramed();
        std::printf("  enhanced backdrop: %s\n", ok ? "the band, the singer on the right" : "no band in the shot");
        return ok ? pass() : fail("Enhanced Options' backdrop shows no band");
    }
    if (s.screen == "menu_sounds") {
        // A menu sound after a song plays (not held by the game's pause:
        // pause_all_sfx pauses only the sounds playing then, 0x822E7C68).
        const int before = ui.host().sfxLiveVoices();
        if (before < 0) return pass();  // no sound player in this run
        ui.playSfx("button_toggle");
        const int after = ui.host().sfxLiveVoices();
        std::printf("  menu sounds: %d live voice(s) after button_toggle (%d before)\n", after, before);
        return after > before ? pass() : fail("a menu sound after the song is silent (the synth stayed paused)");
    }
    // The mouse, gamepads and stickyaxis walks.
    if (s.screen == "focus_main_options") {
        ui::PanelObj* fp = ui.focusPanel();
        std::printf("  mouse: the hover focused %s\n", fp ? fp->focus.c_str() : "nothing");
        return fp && fp->focus == "main_options.btn" ? pass() : fail("hovering OPTIONS did not focus it");
    }
    if (s.screen == "click_help1") {  // the help bar's second item, clicked
        ui::PanelObj* hb = ui.panel("helpbar");
        if (hb && hb->view)
            for (const gfx::UiView::Hit& h : hb->view->hits())
                if (h.key == "hb1") {
                    ui.mouse((h.x0 + h.x1) * 0.5f, (h.y0 + h.y1) * 0.5f, true);
                    return pass();
                }
        return fail("the help bar's second item was not drawn");
    }
    if (s.screen == "fuzz_baseline" || s.screen == "fuzz") {
        // The buttons a panel of the screen draws on screen.
        auto drawnButtons = [&](ui::PanelObj* p) {
            std::set<std::string> out;
            if (!p || !p->view) return out;
            for (const gfx::UiView::Hit& h : p->view->hits()) {
                const std::string name = h.key.substr(0, h.key.find('#'));
                const scene::Object* o = p->view->find(name);
                if (!o || !(o->as<scene::BandButton>() || o->cls == "UIButton") || !p->view->showing(name)) continue;
                if (h.x1 < 0 || h.x0 > 1280 || h.y1 < 0 || h.y0 > 720 || h.x1 <= h.x0 || h.y1 <= h.y0) continue;
                out.insert(name);
            }
            return out;
        };
        auto onMainMenu = [&] {
            if (!ui.current()) return false;
            for (ui::PanelObj* p : ui.current()->panels())
                if (p->name() == "main_panel") return true;
            return false;
        };
        if (s.screen == "fuzz_baseline") {
            fuzzMain_ = drawnButtons(ui.panel("main_panel"));
            const char* seed = SDL_getenv("GD05_FUZZ_SEED");
            const char* steps = SDL_getenv("GD05_FUZZ_STEPS");
            fuzzRng_.seed(seed ? unsigned(std::strtoul(seed, nullptr, 10)) : 1u);
            fuzzSteps_ = steps ? std::max(1, std::atoi(steps)) : 2000;
            std::printf("  fuzz: seed %s, %d steps; main_panel draws %zu buttons\n", seed ? seed : "1", fuzzSteps_, fuzzMain_.size());
            return fuzzMain_.size() >= 5 ? pass() : fail("main_panel does not draw its five buttons");
        }
        auto rnd = [&](int n) { return n <= 1 ? 0 : int(fuzzRng_() % unsigned(n)); };
        auto sendKey = [&](SDL_Keycode k) {
            for (const bool down : {true, false}) {
                SDL_Event e{};
                e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
                e.key.timestamp = SDL_GetTicksNS();
                e.key.key = k;
                e.key.scancode = SDL_GetScancodeFromKey(k, nullptr);
                e.key.down = down;
                SDL_PushEvent(&e);
            }
        };
        if (nowMs < fuzzNextMs_) return;
        const std::string sc = ui.currentScreen();
        // Settling: once the menus are still, check them.
        if (fuzzSettle_) {
            if (ui.inTransition() || nowMs - lastChangeMs_ < 900 || nowMs - fuzzActMs_ < 900) {
                if (nowMs - fuzzActMs_ > 30000) return fail("fuzz: " + sc + " never settled (step " + std::to_string(fuzzStep_) + ")");
                return;
            }
            fuzzSettle_ = false;
            ++fuzzChecks_;
            ui::PanelObj* fp = ui.focusPanel();
            std::string why;
            if (!onMainMenu()) {
                // Off the main menu (the song list, GAMEPADS, a sub-screen): back out.
                // A flow Back does not leave (the band's Join Band slots):
                // out of this walk's scope, back to the main menu, counted.
                if (fuzzOffSince_ < 0) fuzzOffSince_ = nowMs;
                if (nowMs - fuzzOffSince_ > 20000) {
                    std::printf("  fuzz: step %d: Back does not leave %s, back to the main menu\n", fuzzStep_, sc.c_str());
                    ++fuzzRecovered_, fuzzOffSince_ = -1;
                    ui.gotoScreen("main_screen");
                }
            } else if (!fp || !fp->up) {
                why = "no panel has the focus";
            } else {
                fuzzOffSince_ = -1;
                for (ui::PanelObj* p : ui.current()->panels()) {
                    if (p->name() == "meta" || p->name() == "fx" || p->name() == "movie_panel" || p->name() == "main_lower3rd_panel") continue;
                    if (!p->up || !p->showing || !p->view) {
                        why = p->name() + " is not up and shown";
                        break;
                    }
                    const std::set<std::string> b = drawnButtons(p);
                    if (p->name() == "main_panel" && b != fuzzMain_) {
                        why = "main_panel draws " + std::to_string(b.size()) + " of its " + std::to_string(fuzzMain_.size()) + " buttons";
                        break;
                    }
                    if (b.empty()) {
                        why = p->name() + " draws no button";
                        break;
                    }
                }
                if (why.empty() && !fp->focus.empty() && !drawnButtons(fp).count(fp->focus) && fp->focused() && fp->view && fp->view->find(fp->focus) &&
                    fp->view->find(fp->focus)->as<scene::BandButton>())
                    why = "the focused " + fp->focus + " is not drawn";
            }
            if (!why.empty()) {
                std::printf("  fuzz: FAIL at step %d on %s: %s; last actions:%s\n", fuzzStep_, sc.c_str(), why.c_str(), fuzzLog_.c_str());
                if (const char* dir = SDL_getenv("GD05_WALK_SHOTS")) ui.host().saveScreenshot(std::string(dir) + "/fuzz_fail.bmp");
                return fail("fuzz: " + why + " on " + sc + " (step " + std::to_string(fuzzStep_) + ")");
            }
            if (fuzzStep_ >= fuzzSteps_) {
                std::printf("  fuzz: %d steps, %d checks, %d off the main menu, %d recovered\n", fuzzStep_, fuzzChecks_, fuzzOff_, fuzzRecovered_);
                return pass();
            }
        }
        // One random action.
        ++fuzzStep_;
        fuzzActMs_ = nowMs;
        std::string act;
        const bool off = !onMainMenu();
        if (off) ++fuzzOff_;
        const int r = rnd(100);
        if (off) {  // off the main menu: back (the walk stays on the menus); the title has none: Start
            const int b = sc == "splash_screen" ? ui::kPad_X : ui::kPad_Tri;
            ui.buttonDown(b, 0), ui.buttonUp(b, 0), act = sc == "splash_screen" ? "start" : "back";
        } else if (r < 45) {
            // The mouse on a button drawn by any panel of the screen.
            std::vector<std::pair<std::string, std::array<float, 2>>> targets;
            if (ui.current())
                for (ui::PanelObj* p : ui.current()->panels())
                    if (p->up && p->showing && p->view)
                        for (const gfx::UiView::Hit& h : p->view->hits()) {
                            const std::string name = h.key.substr(0, h.key.find('#'));
                            const scene::Object* o = p->view->find(name);
                            if (o && (o->as<scene::BandButton>() || o->cls == "UIButton"))
                                targets.push_back({name, {(h.x0 + h.x1) * 0.5f, (h.y0 + h.y1) * 0.5f}});
                        }
            if (!targets.empty()) {
                const auto& t = targets[size_t(rnd(int(targets.size())))];
                const bool click = rnd(3) != 0;
                ui.mouse(t.second[0], t.second[1], false);
                if (click) ui.mouse(t.second[0], t.second[1], true);
                act = std::string(click ? "click " : "hover ") + t.first;
            } else {
                ui.mouse(float(rnd(1280)), float(rnd(720)), false), act = "hover";
            }
        } else if (r < 75) {
            static const SDL_Keycode keys[] = {SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT, SDLK_UP, SDLK_DOWN};
            const SDL_Keycode k = keys[rnd(6)];
            sendKey(k), act = SDL_GetKeyName(k);
        } else if (r < 85) {
            sendKey(SDLK_RETURN), act = "Enter";
        } else if (r < 93) {
            // Esc: back (on the main menu itself the walk's back press, not
            // Esc's count of three that quits).
            if (sc == "main_screen" || sc == "splash_screen") ui.buttonDown(ui::kPad_Tri, 0), ui.buttonUp(ui::kPad_Tri, 0), act = "back";
            else sendKey(SDLK_ESCAPE), act = "Esc";
        } else {
            static const SDL_Keycode frets[] = {SDLK_1, SDLK_2, SDLK_4, SDLK_5};  // not yellow: GAMEPADS
            const SDL_Keycode k = frets[rnd(4)];
            sendKey(k), act = SDL_GetKeyName(k);
        }
        fuzzLog_ += " " + act;
        if (fuzzLog_.size() > 400) fuzzLog_.erase(0, fuzzLog_.size() - 400);
        // Fast (a frame, within a transition) or slow; every few steps settle.
        const int pace = rnd(10);
        fuzzNextMs_ = nowMs + (pace < 5 ? 0 : pace < 8 ? 120 : 450);
        if (rnd(6) == 0 || fuzzStep_ >= fuzzSteps_) fuzzSettle_ = true;
        return;
    }
    if (s.screen == "kb_guitar_attach") {
        adapter_ = Input::attachVirtualPad("1430:4748:guitar");
        return adapter_ ? pass() : fail("no virtual guitar");
    }
    if (s.screen == "kb_bind_q") {
        // GAMEPADS > Customize inputs > Green fret > Key 1, then Q pressed
        // (the capture takes the next key event).
        ui.input().beginEdit("keyboard");
        ui.input().beginCapture("keyboard", Input::Control::Green, 0);
        for (const bool down : {true, false}) {
            SDL_Event e{};
            e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            e.key.timestamp = SDL_GetTicksNS();
            e.key.key = SDLK_Q;
            e.key.scancode = SDL_SCANCODE_Q;
            e.key.down = down;
            SDL_PushEvent(&e);
        }
        return pass();
    }
    if (s.screen == "kb_save_q") {
        // ... and Save: controllers.json has it, the game plays green on Q.
        ui.input().commitEdit();
        kbGreen_ = SDLK_Q;
        const auto& g = ui.input().bindings("keyboard")[size_t(Input::Control::Green)];
        const bool bound = !g.empty() && g[0].kind == Input::Source::Kind::Key && g[0].index == int(SDLK_Q);
        const char* dir = SDL_getenv("GD05_SAVE_DIR");
        std::ifstream f(std::filesystem::path(dir ? dir : "") / "controllers.json");
        std::stringstream ss;
        ss << f.rdbuf();
        bool saved = false;
        try {
            const Json root = Json::parse(ss.str());
            for (const Json& j : root["devices"]["keyboard"]["bindings"]["green"].a)
                saved = saved || (j["kind"].str() == "key" && int(j["index"].num()) == int(SDLK_Q));
        } catch (const std::exception&) {
        }
        std::printf("  gamepads: green %s, controllers.json %s\n", bound ? "on Q" : "not on Q", saved ? "has it" : "does not");
        return bound && saved ? pass() : fail("the keyboard's green fret was not rebound to Q and saved");
    }
    if (s.screen.rfind("kb_play ", 0) == 0) {
        // Plays player 1's gems with key events: the gem's frets held from
        // 70 ms before it, a strum (Down) at it; with "od", until Overdrive
        // was deployed with Tab once it was ready.
        const Player* pl = ui.host().gamePlayer(0);
        if (!pl || !ui.host().gameLoaded() || ui.currentScreen() != "game_screen") return nowMs - stepStart_ > s.ms ? fail("no song to play") : void();
        const bool od = s.screen == "kb_play od";
        const int want = od ? 0 : std::atoi(s.screen.c_str() + 8);
        if (kbTarget_ < 0) kbTarget_ = kbPlayed_ + want, kbOdAsked_ = false;
        const double t = ui.host().gameTimeMs();
        auto send = [&](SDL_Keycode k, bool down) {
            SDL_Event e{};
            e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            e.key.timestamp = SDL_GetTicksNS();
            e.key.key = k;
            e.key.scancode = SDL_GetScancodeFromKey(k, nullptr);
            e.key.down = down;
            SDL_PushEvent(&e);
        };
        const SDL_Keycode frets[5] = {SDL_Keycode(kbGreen_), SDLK_2, SDLK_3, SDLK_4, SDLK_5};
        auto hold = [&](unsigned mask) {
            for (int f = 0; f < 5; ++f)
                if (((mask >> f) & 1) != ((kbHeld_ >> f) & 1)) send(frets[f], (mask >> f) & 1);
            kbHeld_ = mask;
        };
        const auto& gems = pl->chart().gems;
        while (kbGem_ < gems.size() && (gems[kbGem_].ms < t - 60 || !gems[kbGem_].ownedBy(0))) ++kbGem_;
        if (kbGem_ < gems.size()) {
            const Gem& g = gems[kbGem_];
            if (t >= g.ms - 70 && kbHeld_ != g.frets) hold(g.frets);
            if (t >= g.ms - 8) {
                send(SDLK_DOWN, true), send(SDLK_DOWN, false);
                ++kbGem_, ++kbPlayed_;
            }
        }
        if (od && pl->starPowerReady() && !kbOdAsked_) send(SDLK_TAB, true), send(SDLK_TAB, false), kbOdAsked_ = true;
        if (od && pl->starPowerActive()) {
            std::printf("  keyboard: Overdrive deployed with Tab at %.0f ms\n", t);
            hold(0), kbTarget_ = -1;
            return pass();
        }
        if (!od && kbPlayed_ >= kbTarget_) {
            hold(0), kbTarget_ = -1;
            return pass();
        }
        if (nowMs - stepStart_ > s.ms) return fail(od ? "Overdrive was never deployed with the keyboard" : "the song ended before the keyboard played its gems");
        return;
    }
    if (s.screen.rfind("dev_attach ", 0) == 0 && SDL_getenv("GD05_WALK_EVDEV") && *SDL_getenv("GD05_WALK_EVDEV") &&
        s.screen.find("rbguitar") != std::string::npos) {
        // The guitar as a kernel device (GD05-061): the game's own Input opens
        // it when SDL announces it; this step waits for that.
        if (g_evdev.fd < 0) {
            const std::string why = evdevOpen(SDL_getenv("GD05_WALK_EVDEV"));
            if (!why.empty()) return fail(why);
            std::printf("  device: kernel input device %s (%s)\n", g_evdev.node.c_str(), g_evdev.created ? "uinput, the owner's guitar" : "written into");
            for (int b = 0; b < 11; ++b) devButton(nullptr, b, false);
            devHat(nullptr, 0, SDL_HAT_CENTERED);
        }
        int n = 0;
        if (SDL_JoystickID* ids = SDL_GetGamepads(&n)) {
            for (int i = 0; i < n && !adapter_; ++i) {
                const char* path = SDL_GetGamepadPathForID(ids[i]);
                if (path && g_evdev.node == path && SDL_GetJoystickFromID(ids[i])) adapter_ = ids[i];
            }
            SDL_free(ids);
        }
        if (adapter_) {
            char guid[64];
            SDL_GUIDToString(SDL_GetGamepadGUIDForID(adapter_), guid, sizeof guid);
            std::printf("  device: SDL gamepad %u, %s, GUID %s, opened by the game\n", unsigned(adapter_), SDL_GetGamepadNameForID(adapter_), guid);
            return pass();
        }
        return nowMs - stepStart_ > s.ms ? fail("SDL never opened " + g_evdev.node) : void();
    }
    if (s.screen.rfind("dev_attach ", 0) == 0) {
        adapter_ = Input::attachVirtualPad(s.screen.substr(11));
        // A pad's triggers rest at the axis' bottom (SDL maps -32768..32767 to 0..32767).
        if (SDL_Joystick* j = adapter_ && s.screen.find("rbguitar") == std::string::npos ? SDL_OpenJoystick(adapter_) : nullptr) {
            devAxis(j, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768);
            devAxis(j, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
            SDL_UpdateJoysticks();
        }
        return adapter_ ? pass() : fail("no virtual device");
    }
    if (s.screen.rfind("dev_set ", 0) == 0) {
        // "dev_set b<n> <0|1>": a button; "dev_set hat<v> <0|1>": the hat at v or centred.
        SDL_Joystick* j = SDL_GetJoystickFromID(adapter_);
        if (!j) return fail("the virtual device is not open");
        const std::string what = s.screen.substr(8, s.screen.rfind(' ') - 8);
        const bool on = s.screen.back() == '1';
        if (what.rfind("hat", 0) == 0) devHat(j, 0, on ? Uint8(std::atoi(what.c_str() + 3)) : SDL_HAT_CENTERED);
        else devButton(j, std::atoi(what.c_str() + 1), on);
        return pass();
    }
    const std::string devId = "pad:" + std::to_string(adapter_);
    if (s.screen == "dev_unfocus") {
        SDL_Window* w = SDL_GetKeyboardFocus();
        if (w) SDL_HideWindow(w);
        SDL_PumpEvents();
        std::printf("  keyboard focus: %s (background events %s)\n", SDL_GetKeyboardFocus() ? "on a window" : "none",
                    SDL_GetHintBoolean(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, false) ? "on" : "off");
        return SDL_GetKeyboardFocus() ? fail("the window kept the keyboard focus") : pass();
    }
    if (s.screen == "dev_refocus") {
        int n = 0;
        if (SDL_Window** ws = SDL_GetWindows(&n)) {
            for (int i = 0; i < n; ++i) SDL_ShowWindow(ws[i]);
            SDL_free(ws);
        }
        return pass();
    }
    if (s.screen == "dev_owner_guid") {
        SDL_setenv_unsafe("GD05_TEST_PAD_GUID", "03006f16ad1b00000200000003010000", 1);
        return pass();
    }
    if (s.screen == "dev_owner_json") {
        const char* dir = SDL_getenv("GD05_SAVE_DIR");
        const std::string file = std::string(dir ? dir : "") + "owner-controllers.json";
        auto keys = [](std::initializer_list<long long> k) {
            std::string a = "[";
            for (long long v : k) a += std::string(a.size() > 1 ? "," : "") + "{\"index\":" + std::to_string(v) + ",\"kind\":\"key\"}";
            return a + "]";
        };
        std::ofstream(file) << "{\"devices\":{\"03006f16ad1b00000200000003010000\":{\"name\":\"Harmonix Rock Band Guitar\",\"player\":-1},"
                               "\"keyboard\":{\"bindings\":{"
                            << "\"blue\":" << keys({52, 1073741885}) << ",\"green\":" << keys({49, 1073741882})
                            << ",\"guitar_select\":" << keys({9}) << ",\"guitar_start\":" << keys({13}) << ",\"gyro\":" << keys({9})
                            << ",\"menu_down\":" << keys({1073741905}) << ",\"menu_up\":" << keys({1073741906})
                            << ",\"orange\":" << keys({53, 1073741886}) << ",\"red\":" << keys({50, 1073741883}) << ",\"select\":" << keys({9})
                            << ",\"start\":" << keys({13}) << ",\"strum_down\":" << keys({1073741905}) << ",\"strum_up\":" << keys({1073741906})
                            << ",\"whammy\":" << keys({1073742049, 1073742053}) << ",\"yellow\":" << keys({51, 1073741884})
                            << "},\"player\":0}},\"player2\":false,\"touch\":{\"buttons\":0,\"pause_corner\":0,\"swipe_down\":1,\"swipe_up\":0},\"version\":2}\n";
        return ui.input().importFrom(file) ? pass() : fail("the owner's controllers.json did not load");
    }
    if (s.screen == "dev_owner_players") {
        // The owner's controllers.json: the keyboard player 1, the guitar None.
        ui.input().setPlayer("keyboard", 0);
        ui.input().setPlayer(devId, -1);
        return pass();
    }
    if (s.screen == "dev_players") {
        int slot = -2;
        std::string profile;
        for (const Input::Device& d : ui.input().devices()) {
            std::printf("  device %s \"%s\" (%s): player %d\n", d.id.c_str(), d.name.c_str(), d.profile.c_str(), d.player + 1);
            if (d.id == devId) slot = d.player, profile = d.profile;
        }
        if (slot == -2) return fail("the virtual device is not a gamepad");
        if (scenario_ == "rb_guitar_none" || scenario_ == "rb_owner_guitar") return slot == -1 ? pass() : fail("the guitar is not None");
        if (scenario_ == "rb_guitar_play" && profile != "Xbox 360 guitar") return fail("the Rock Band guitar is not typed an Xbox 360 guitar");
        return slot == 0 ? pass() : fail("the device is not player 1");
    }
    if (s.screen.rfind("dev_play ", 0) == 0) {
        // Plays player 1's gems with the device: frets held from 70 ms before
        // a gem and a strum (the hat down for a frame) at it; a pad presses
        // its frets at the gem (every press strums) and lets go after it or
        // its sustain. "sustain": until a sustain was held 0.5 s with the
        // whammy moved; "od": until Overdrive was deployed (tilt / Back).
        const Player* pl = ui.host().gamePlayer(0);
        if (!pl || !ui.host().gameLoaded() || ui.currentScreen() != "game_screen") return nowMs - stepStart_ > s.ms ? fail("no song to play") : void();
        SDL_Joystick* j = SDL_GetJoystickFromID(adapter_);
        if (!j) return fail("the virtual device is not open");
        const bool pad = s.screen.rfind("dev_play pad", 0) == 0;
        const std::string goal = s.screen.substr(s.screen.rfind(' ') + 1);
        const bool od = goal == "od", sus = goal == "sustain", miss = goal == "miss";
        if (miss) {
            // No frets; one strum on the first gem (a wrong fret: a miss) and
            // then nothing until three gems passed with the guitar muted.
            if (devTarget_ < 0) devTarget_ = pl->passes() + 3, devReleaseMs_ = -1, devSustainSince_ = ui.host().gameTimeMs();
            if (devStrumDown_) devHat(j, 0, SDL_HAT_CENTERED), devStrumDown_ = false;
            const double t = ui.host().gameTimeMs();
            const auto& gems = pl->chart().gems;
            while (devGem_ < gems.size() && (gems[devGem_].ms < t - 60 || !gems[devGem_].ownedBy(0))) ++devGem_;
            // A wrong fret (not the second gem's) held, then a strum at that gem.
            const unsigned wrong = devGem_ < gems.size() ? ((gems[devGem_].frets & 1) ? 2u : 1u) : 1u;
            for (const int f : {0, 1, 2, 3, 4}) {
                static const int kGuitar[5] = {0, 1, 3, 2, 4};
                devButton(j, kGuitar[f], devReleaseMs_ < 0 && ((wrong >> f) & 1));
            }
            devHeld_ = 0;
            if (devReleaseMs_ < 0 && nowMs - stepStart_ > 150 && devGem_ < gems.size() && t >= gems[devGem_].ms - 8)  // the hat let go frames ago
                devHat(j, 0, SDL_HAT_DOWN), devStrumDown_ = true, devReleaseMs_ = t;
            if (pl->passes() >= devTarget_ && !pl->guitarAudible()) {
                int sounds = 0;
                for (const auto& [name, at] : ui.host().gameSfx())
                    if (name.rfind("miss_", 0) == 0 && at >= devSustainSince_) ++sounds;  // miss_gtr.cue (config/player.dta)
                std::printf("  device: %d gems passed, %d penalties, guitar stem muted, %d miss sound(s), rock meter %.2f, streak %d\n", pl->passes(), pl->penalties(), sounds,
                            pl->rockMeter(), pl->streak());
                if (devStrumDown_) devHat(j, 0, SDL_HAT_CENTERED), devStrumDown_ = false;
                if (!sounds) {
                    std::string heard;
                    for (const auto& [name, at] : ui.host().gameSfx())
                        if (at >= devSustainSince_ - 2000) heard += " " + name + "@" + std::to_string(int(at));
                    std::printf("  sounds since:%s\n", heard.c_str());
                }
                devTarget_ = -1, devReleaseMs_ = -1, devSustainSince_ = -1;
                return sounds > 0 ? pass() : fail("the wrong strum played no miss sound");
            }
            if (nowMs - stepStart_ > s.ms) return fail("missed gems did not mute the guitar stem");
            return;
        }
        if (devTarget_ < 0) devTarget_ = devPlayed_ + std::atoi(goal.c_str()), devOdAsked_ = devWhammied_ = false, devSustainSince_ = -1;
        const double t = ui.host().gameTimeMs();
        // Last frame's strum and tilt end.
        // (A hat let go and pressed again in one frame would be no event: the next strum waits a frame.)
        const bool strumWait = devStrumDown_;
        if (devStrumDown_) devHat(j, 0, SDL_HAT_CENTERED), devStrumDown_ = false;
        if (devTilt_) devAxis(j, pad ? 0 : 4, 0), devButton(j, SDL_GAMEPAD_BUTTON_BACK, false), devTilt_ = false;
        auto hold = [&](unsigned mask) {
            for (int f = 0; f < 5; ++f) {
                const bool on = (mask >> f) & 1;
                if (on == bool((devHeld_ >> f) & 1)) continue;
                if (!pad) {
                    static const int kGuitar[5] = {0, 1, 3, 2, 4};  // A B Y X LB (the report's mapping)
                    devButton(j, kGuitar[f], on);
                } else if (f == 0 || f == 3) {  // LT / RT (the virtual pad's axes 4 / 5)
                    devAxis(j, f == 0 ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, on ? 32767 : -32768);
                } else {
                    static const int kPad[5] = {0, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, 0, SDL_GAMEPAD_BUTTON_SOUTH};
                    devButton(j, kPad[f], on);
                }
            }
            devHeld_ = mask;
        };
        const auto& gems = pl->chart().gems;
        while (devGem_ < gems.size() && (gems[devGem_].ms < t - 60 || !gems[devGem_].ownedBy(0))) ++devGem_;
        if (pad && devReleaseMs_ >= 0 && t >= devReleaseMs_) hold(0), devReleaseMs_ = -1;
        if (devGem_ < gems.size()) {
            const Gem& g = gems[devGem_];
            const bool sustain = !g.noSustain && g.durationMs > 0;
            if (!pad) {
                if (t >= g.ms - 70 && devHeld_ != g.frets) hold(g.frets);
                if (t >= g.ms - 8 && !strumWait) {
                    devHat(j, 0, SDL_HAT_DOWN), devStrumDown_ = true;
                    ++devGem_, ++devPlayed_;
                }
            } else if (t >= g.ms - 8) {
                if (devHeld_) {
                    hold(0);  // let go first: the next frame presses
                } else {
                    hold(g.frets);
                    devReleaseMs_ = sustain ? g.ms + g.durationMs - 30 : g.ms + 40;
                    ++devGem_, ++devPlayed_;
                }
            }
        }
        // The whammy while a sustain is held: right stick X (a guitar's bar from -32768; a pad's left stick).
        const bool sustaining = pl->sustainGem() >= 0;
        devAxis(j, pad ? SDL_GAMEPAD_AXIS_LEFTX : 3, sustaining ? 20000 : pad ? 0 : -32768);
        if (sustaining) {
            if (devSustainSince_ < 0) devSustainSince_ = t;
            devWhammied_ = devWhammied_ || pl->whammying();
        } else {
            devSustainSince_ = -1;
        }
        if (od && pl->starPowerReady() && !devOdAsked_) {
            if (pad) devButton(j, SDL_GAMEPAD_BUTTON_BACK, true);
            else devAxis(j, 4, 32767);  // tilt
            devTilt_ = devOdAsked_ = true;
        }
        auto done = [&] {
            hold(0), devTarget_ = -1, devReleaseMs_ = -1;
            if (devStrumDown_) devHat(j, 0, SDL_HAT_CENTERED), devStrumDown_ = false;
            devAxis(j, pad ? SDL_GAMEPAD_AXIS_LEFTX : 3, pad ? 0 : -32768);
            return pass();
        };
        if (od && pl->starPowerActive()) return std::printf("  device: Overdrive deployed at %.0f ms\n", t), done();
        if (sus && devSustainSince_ >= 0 && t - devSustainSince_ > 500 && devWhammied_)
            return std::printf("  device: sustain held from %.0f ms, whammy %.2f\n", devSustainSince_, pl->whammyBar()), done();
        if (!od && !sus && devPlayed_ >= devTarget_) return done();
        if (nowMs - stepStart_ > s.ms)
            return fail(od ? "Overdrive was never deployed with the device" : sus ? "no sustain held with the whammy" : "the song ended before the device played its gems");
        return;
    }
    if (s.screen == "dev_trace") {
        // The input lines: the hat's raw events, each strum down reaching the song as Strum, none ignored.
        int hats = 0, strums = 0, ignored = 0;
        bool afterHat = false;
        for (const std::string& l : ui.input().traceLines()) {
            if (l.find(" hat 0 = 4 (down)") != std::string::npos) ++hats, afterHat = true;
            else if (afterHat && l.find("-> Strum down") != std::string::npos && l.find("in a song") != std::string::npos) ++strums, afterHat = false;
            if (l.find("ignored") != std::string::npos) ++ignored;
        }
        for (const std::string& l : ui.input().traceLines())
            if (l.find(" hat 0 = 4") != std::string::npos) {
                std::printf("  trace: %s\n", l.c_str());
                break;
            }
        std::printf("  input trace: %d hat strums down, %d reached the song as Strum, %d ignored lines\n", hats, strums, ignored);
        return hats > 10 && strums * 10 >= hats * 9 ? pass() : fail("hat strums did not reach the song");
    }
    if (s.screen == "dev_audible") {
        const Player* pl = ui.host().gamePlayer(0);
        return pl && pl->guitarAudible() ? pass() : fail("the guitar stem stayed muted after hits");
    }
    if (s.screen == "dev_hits") {
        const Player* pl = ui.host().gamePlayer(0);
        const int hit = pl ? pl->notesHit() : 0;
        std::printf("  device: %d gems played, %d notes hit, score %d, x%d, streak %d, rock meter %.2f (player 1: %s)\n", devPlayed_, hit,
                    pl ? pl->score() : 0, pl ? pl->multiplier() : 0, pl ? pl->streak() : 0, pl ? pl->rockMeter() : 0.0f,
                    ui.input().playerDevice(0).c_str());
        if (devPlayed_ < 10) return fail("the device played too few gems");
        // 80%: a frame of a busy test machine can take a press out of the window.
        return hit * 10 >= devPlayed_ * 8 ? pass() : fail("the device's frets and strums did not hit the notes");
    }
    if (s.screen == "kb_hits") {
        const Player* pl = ui.host().gamePlayer(0);
        const int hit = pl ? pl->notesHit() : 0;
        std::printf("  keyboard: %d gems strummed, %d notes hit (player 1: %s)\n", kbPlayed_, hit, ui.input().playerDevice(0).c_str());
        if (kbPlayed_ < 10) return fail("the keyboard played too few gems");
        // 80%, as the device walks: with walks run in parallel a late frame
        // takes presses out of the window (26 of 30 seen with eight at
        // once); a lost key (Q not green) misses far more.
        return hit * 10 >= kbPlayed_ * 8 ? pass() : fail("the keyboard's frets and strums did not hit the notes");
    }
    if (s.screen == "gp_p1_keyboard") {
        ui.input().setPlayer("keyboard", 0);
        return pass();
    }
    if (s.screen == "gp_editing") return ui.input().editing() ? pass() : fail("Customize inputs did not start an edit");
    if (s.screen == "gp_detecting") return ui.input().detecting() ? pass() : fail("Bind to > Detect did not wait for a device");
    if (s.screen == "vpad_attach") {
        adapter_ = Input::attachVirtualPad("045e:028e");
        return adapter_ ? pass() : fail("no virtual pad");
    }
    if (s.screen == "keyboard_p2") {
        ui.input().setPlayer("keyboard", 1);
        const std::string p1 = ui.input().playerDevice(0), p2 = ui.input().playerDevice(1);
        std::printf("  devices: player 1 %s, player 2 %s\n", p1.c_str(), p2.c_str());
        return p1 == "pad:" + std::to_string(adapter_) && p2 == "keyboard" ? pass() : fail("the pad is not player 1 with the keyboard player 2");
    }
    if (s.screen == "vpad_detach") {
        SDL_DetachVirtualJoystick(adapter_);
        return pass();
    }
    if (s.screen == "pad_loss_message") {
        ui::PanelObj* fp = ui.focusPanel();
        const std::string msg = fp && fp->view ? fp->view->text("pause_controller_msg.lbl") : std::string();
        std::printf("  controller lost: \"%s\", resume disabled %d\n", msg.c_str(), fp && fp->isDisabled("resume.btn") ? 1 : 0);
        if (msg != ui.localize("controller_loss_msg")) return fail("the controller message is not controller_loss_msg");
        return fp->isDisabled("resume.btn") ? pass() : fail("RESUME is not disabled while the pad is missing");
    }
    if (s.screen.rfind("still ", 0) == 0) return ui.currentScreen() == s.screen.substr(6) ? pass() : fail("left " + s.screen.substr(6));
    if (s.screen == "vpad_press") {
        SDL_Joystick* j = SDL_GetJoystickFromID(adapter_);
        if (!j) return fail("the virtual pad is not open");
        SDL_SetJoystickVirtualButton(j, SDL_GAMEPAD_BUTTON_SOUTH, true);
        return pass();
    }
    if (s.screen == "pad_is_p2") {
        if (SDL_Joystick* j = SDL_GetJoystickFromID(adapter_)) SDL_SetJoystickVirtualButton(j, SDL_GAMEPAD_BUTTON_SOUTH, false);
        const std::string p2 = ui.input().playerDevice(1), p1 = ui.input().playerDevice(0);
        std::printf("  gamepads: player 1 %s, player 2 %s\n", p1.c_str(), p2.c_str());
        return p2 == "pad:" + std::to_string(adapter_) && p1 == "keyboard" && !ui.input().detecting()
                   ? pass() : fail("Detect did not bind the pad pressed to player 2 (the keyboard to player 1)");
    }
    if (s.screen == "whammy_is_e" || s.screen == "whammy_e_unsaved") {
        // Key 2 of the keyboard's Whammy is E, Key 1 (Shift) kept.
        const auto& w = ui.input().bindings("keyboard")[size_t(Input::Control::Whammy)];
        const bool bound = w.size() == 2 && w[0].index == int(SDLK_LSHIFT) && w[1].kind == Input::Source::Kind::Key && w[1].index == int(SDLK_E);
        const char* dir = SDL_getenv("GD05_SAVE_DIR");
        std::ifstream f(std::filesystem::path(dir ? dir : "") / "controllers.json");
        std::stringstream ss;
        ss << f.rdbuf();
        bool saved = false;
        try {
            const Json root = Json::parse(ss.str());
            for (const Json& j : root["devices"]["keyboard"]["bindings"]["whammy"].a)
                saved = saved || (j["kind"].str() == "key" && int(j["index"].num()) == int(SDLK_E));
        } catch (const std::exception&) {
        }
        std::printf("  gamepads: Whammy %s, controllers.json %s\n", bound ? "on Shift / E" : "not on Shift / E", saved ? "has it" : "does not");
        if (s.screen == "whammy_e_unsaved")
            return bound && !saved && ui.input().editing() ? pass() : fail("Key 2 of Whammy did not take at once, or was saved before Save");
        return bound && saved && !ui.input().editing() ? pass() : fail("the rebinding of the keyboard's Whammy to E did not take or was not saved");
    }
    if (s.screen == "adapter_attach") {
        adapter_ = Input::attachVirtualPad("0079:0006:dpadaxis=-32768");
        sideBase_ = ui.pressCount(ui::kPad_DLeft) + ui.pressCount(ui::kPad_DRight);
        rightBase_ = ui.pressCount(ui::kPad_DRight);
        return adapter_ ? pass() : fail("no virtual adapter");
    }
    if (s.screen == "no_sideways") {
        const int n = ui.pressCount(ui::kPad_DLeft) + ui.pressCount(ui::kPad_DRight) - sideBase_;
        std::printf("  stickyaxis: %d left / right presses at rest\n", n);
        return n == 0 ? pass() : fail("the resting adapter pressed left / right by itself");
    }
    if (s.screen == "adapter_move") {
        if (SDL_Joystick* j = SDL_GetJoystickFromID(adapter_)) SDL_SetJoystickVirtualAxis(j, 6, 0);
        return pass();
    }
    if (s.screen == "one_right") {
        const int r = ui.pressCount(ui::kPad_DRight) - rightBase_;
        std::printf("  stickyaxis: %d right press(es) once moved\n", r);
        if (SDL_Joystick* j = SDL_GetJoystickFromID(adapter_)) SDL_SetJoystickVirtualAxis(j, 6, -32768);
        return r == 1 ? pass() : fail("the moved adapter did not press right once");
    }
    if (s.screen == "splashes_skipped") {
        if (!ui.inSplashes()) {
            SDL_Log("walk: splashes skipped (%.1f s)", nowMs / 1000);
            ++step_, stepStart_ = -1;
        } else if (nowMs - stepStart_ > s.ms) {
            fail("the splashes were not skipped by a button");
        }
        return;
    }
    if (s.screen == "bricks_record" || s.screen == "bricks_same") {
        std::array<float, 4> c{};
        if (!bricksLight(ui, c)) return fail(ui.currentScreen() + " shows no meta_bricks");
        if (s.screen == "bricks_record") {
            bricks_ = c;
        } else {
            float d = 0;
            for (size_t k = 0; k < 4; ++k) d = std::max(d, std::fabs(c[k] - bricks_[k]));
            std::printf("  bricks: light (%.2f %.2f %.2f %.2f), first (%.2f %.2f %.2f %.2f)\n", c[0], c[1], c[2], c[3], bricks_[0], bricks_[1], bricks_[2],
                        bricks_[3]);
            if (d > 0.01f) return fail(ui.currentScreen() + " came back darker (meta_bricks' light stayed off)");
        }
        ++step_, stepStart_ = -1;
        return;
    }
    if (s.screen == "camera_record" || s.screen == "camera_same") {
        std::array<float, 3> pos{};
        if (!mainCamera(ui, pos)) return fail("main_panel has no view");
        if (s.screen == "camera_record") {
            camera_ = pos;
        } else {
            float d = 0;
            for (int k = 0; k < 3; ++k) d = std::max(d, std::fabs(pos[size_t(k)] - camera_[size_t(k)]));
            std::printf("  camera: main menu at (%.2f %.2f %.2f), first at (%.2f %.2f %.2f)\n", pos[0], pos[1], pos[2], camera_[0],
                        camera_[1], camera_[2]);
            if (d > 0.01f) return fail("going back left the main menu's camera off its resting place");
        }
        ++step_, stepStart_ = -1;
        return;
    }
    if (s.screen == "miss_sound") {
        // An overstrum plays beatmatch.dta's miss: miss_gtr (a guitar track),
        // through player_matcher0's handlers; a passed gem plays nothing.
        if (ui.host().gameTimeMs() < 2300) {
            if (nowMs - stepStart_ > 30000) return fail("the song did not reach 2.3 s");
            return;
        }
        int misses = 0;
        for (const auto& [name, at] : ui.host().gameSfx())
            if (name == "miss_gtr" || name == "miss_bass") {
                std::printf("  miss sound %s at %.0f ms\n", name.c_str(), at);
                if (name != "miss_gtr" || (std::fabs(at - 1000) > 60 && std::fabs(at - 2000) > 60)) return fail("a miss sound not at an overstrum");
                ++misses;
            }
        if (misses != 2) return fail("overstrums: " + std::to_string(misses) + " of 2 miss sounds");
        ++step_, stepStart_ = -1;
        return;
    }
    if (s.screen == "intro_camera") {
        // The INTRO shot moves the camera before the track extends (every
        // venue's intro does); with the menus it once stood still until
        // -2000 ms, started on the previous clock (owner 10-28).
        const double travel = ui.host().introCameraTravel();
        std::printf("  intro camera travelled %.1f units before -2000 ms\n", travel);
        if (travel < 5) return fail("the intro camera did not move before the track extends");
        ++step_, stepStart_ = -1;
        return;
    }
    if (s.screen.rfind("whammy_tail", 0) == 0) {
        // The held tail's widest row and its glow line's scale (TrackView,
        // 0x8269EAD0): 1 + 3 x the bar, so 4 with the bar fully down; 1 once
        // the bar is up again and the bulges have run off (100 polls).
        const int w = ui.host().gameCounter(0, "held_tail_width"), g = ui.host().gameCounter(0, "held_glow_scale");
        const int t = ui.host().gameCounter(0, "tail_now_ms");
        const bool fat = s.screen == "whammy_tail_fat" || s.screen == "whammy_tail_key";
        const int after = s.screen == "whammy_tail_fat" ? 1000 : s.screen == "whammy_tail_back" ? 18000 : s.screen == "whammy_tail_key" ? 26000 : 44000;
        const bool ok = t >= after && w > 0 && (fat ? w >= 3900 && g > 1100 : w <= 1001 && g <= 1001);
        if (!ok) {
            if (nowMs - stepStart_ > 40000)
                return fail(s.screen + ": held tail width x1000 " + std::to_string(w) + ", glow " + std::to_string(g) + " at " + std::to_string(t) + " ms");
            return;
        }
        std::printf("  %s: held tail width x1000 %d, glow scale x1000 %d at song %d ms\n", s.screen.c_str(), w, g, t);
        ++step_, stepStart_ = -1;
        return;
    }
    if (s.screen == "track_surface") {
        // The track's surface is the player's character's bitmap, rosewood in
        // practice and the tutorials (TrackPanel 0x82693620, 0x82699B70).
        const int frames = ui.host().gameCounter(0, "surface_overrides");
        std::printf("  track surface: %d frames with the player's surface texture\n", frames);
        if (frames < 1) return fail("the track did not draw the player's surface texture (track/surfaces/<name>_keep.bmp)");
        ++step_, stepStart_ = -1;
        return;
    }
    if (s.screen == "band_flames") {
        const int n = ui.host().gameCounter(0, "band_fx_particles");
        std::printf("  flaming head: cheat %s, %d flame particles\n", ui.cheatOn("cheat_flaming_head") ? "on" : "off", n);
        if (!ui.cheatOn("cheat_flaming_head")) return fail("the Flaming Head code did not turn the cheat on");
        if (n < 1) return fail("no flame particle was drawn on the band");
        ++step_, stepStart_ = -1;
        return;
    }
    if (s.screen == "volumes_live") {
        // GD02-046: the sliders' values reach the mixer at once (0x822B0F40).
        const int band = ui.host().gameCounter(0, "mixer_band_pct"), guitar = ui.host().gameCounter(0, "mixer_guitar_pct");
        std::printf("  volumes: the mixer's band %d%%, guitar %d%%\n", band, guitar);
        if (band >= 100 || guitar >= 100) return fail("the sliders' volumes did not reach the mixer");
        ++step_, stepStart_ = -1;
        return;
    }
    if (s.screen == "game_sinks") {
        Interp& in = ui.interp();
        const bool track = in.send("game", "has_sink", {Value::symbol("track_panel")}).truthy(), hud = in.send("game", "has_sink", {Value::symbol("hud")}).truthy();
        std::printf("  game sinks after RESTART: track_panel %d, hud %d\n", int(track), int(hud));
        if (!track) return fail("track_panel lost the game's messages after RESTART");
        ++step_, stepStart_ = -1;
        return;
    }
    if (s.screen == "band_shadows") {
        // target_shadow's SHADOW_*.spot: the band's flattened shadow groups (0x8222EE58).
        const int n = ui.host().gameCounter(0, "spot_shadows");
        std::printf("  band shadows: %d drawn so far\n", n);
        if (n < 1) return fail("no spotlight drew a band member's shadow");
        ++step_, stepStart_ = -1;
        return;
    }
    if (s.screen == "star_phrase") {
        // A Star Power phrase played through (autoplay): player.dta's
        // phrase_captured plays sp_awarded, the star meter fills, its gems
        // started smash_star and the star tails' glow lines drew over their
        // tails (hidden under the solid tails once, owner 10-28) and its last gem
        // started the phrase_captured lightning (missing once, owner 10-28).
        const Player* pl = ui.host().gamePlayer(0);
        const int phrases = ui.host().gameCounter(0, "sp_phrases");
        const int smashes = ui.host().gameCounter(0, "star_smashes"), glows = ui.host().gameCounter(0, "star_glow_lines");
        double awardedAt = -1e9;
        for (const auto& [name, at] : ui.host().gameSfx())
            if (name == "sp_awarded" && awardedAt < -1e8) awardedAt = at;
        if (phrases < 1 || awardedAt < -1e8 || glows < 1) {
            if (nowMs - stepStart_ > 45000)
                return fail("Star Power phrase: " + std::to_string(phrases) + " captured, sp_awarded " + (awardedAt < -1e8 ? "not played" : "played") +
                            ", " + std::to_string(glows) + " star glow lines in 45 s");
            return;
        }
        const float meter = pl ? pl->starMeter() : 0.0f;
        const int combos = ui.host().gameCounter(0, "star_combos");
        std::printf("  star phrase: %d captured, sp_awarded at %.0f ms, star meter %.2f, %d smash_star, %d star glow lines, %d phrase lightning\n", phrases,
                    awardedAt, double(meter), smashes, glows, combos);
        if (meter <= 0) return fail("a captured Star Power phrase did not fill the star meter");
        if (smashes < 1) return fail("the Star Power phrase's gems started no smash_star effect");
        if (combos < 1) return fail("the captured phrase's last gem started no smash_combo lightning (owner 10-28)");
        ++step_, stepStart_ = -1;
        return;
    }
    if (s.screen == "intro_sequence") {
        // GH2's song start (app.cpp kTrackExtendMs): the track extends at
        // song time -2000 ms with track_unfurl, the smashers pop with
        // nowbar_1..5 at -700..-300 ms, meter_slide 0.25 s after
        // slide_meter_in (-200 ms), and no other sound before (track_panel.dta,
        // hud_panel.dta, game.dta extend_track).
        if (ui.host().gameTimeMs() < 300) {
            if (nowMs - stepStart_ > 30000) return fail("the song did not reach 0 ms");
            return;
        }
        const std::pair<const char*, double> want[] = {{"track_unfurl", -2000}, {"nowbar_1", -700}, {"nowbar_2", -600}, {"nowbar_3", -500},
                                                        {"nowbar_4", -400},      {"nowbar_5", -300}, {"meter_slide", 50}};
        const auto got = ui.host().gameSfx();
        size_t k = 0;
        int begin = 0;
        for (const auto& [name, at] : got) {
            if (at >= 300) break;
            // The crowd's crowd_begin as the intro starts (CrowdAudio's intro_start_msg, 0x822DFCA8).
            if (name == "crowd_begin") {
                std::printf("  intro sound %s at %.0f ms (the crowd)\n", name.c_str(), at);
                if (at > -2000) return fail("crowd_begin after the intro's start");
                ++begin;
                continue;
            }
            std::printf("  intro sound %s at %.0f ms\n", name.c_str(), at);
            if (k >= std::size(want) || name != want[k].first || std::fabs(at - want[k].second) > 60)
                return fail("intro sound " + name + " not as GH2's (" + (k < std::size(want) ? want[k].first : "none") + ")");
            ++k;
        }
        if (k != std::size(want)) return fail("intro: " + std::to_string(k) + " of 7 sounds");
        if (begin != 1) return fail("the crowd's crowd_begin did not play once at the intro");
        ++step_, stepStart_ = -1;
        return;
    }
    if (s.screen == "task_clock_song") {
        // In a game the TaskMgr's kTaskSeconds is the song's time (0x822CDEB8),
        // not the time spent in the menus: the faces' lip sync and the venue's
        // tasks run on it.
        const double task = ui.interp().tasks().time(kTaskSeconds), song = ui.host().gameTimeMs() / 1000.0;
        std::printf("  task clock: kTaskSeconds %.3f, song %.3f s\n", task, song);
        if (!ui.host().gameLoaded() || std::fabs(task - song) > 0.05) return fail("kTaskSeconds does not follow the song in the game");
        ++step_, stepStart_ = -1;
        return;
    }
    // The Enhanced Options (GDALL-015): each option's value as enhanced.json
    // has it, and what the program put in effect (UiHost::enhancedState).
    if (s.screen.rfind("enhanced_", 0) == 0) {
        Json saved;
        {
            std::ifstream f(std::filesystem::path(ui.savePath()).parent_path() / "enhanced.json");
            std::stringstream ss;
            ss << f.rdbuf();
            try {
                saved = Json::parse(ss.str());
            } catch (const std::exception&) {
                saved = Json::object();
            }
        }
        std::string problems;
        // Saved and in effect: each key's value.
        auto want = [&](const char* key, const std::string& v) {
            const std::string got = ui.host().enhancedValue(key), file = saved.has(key) ? saved[key].str() : "(unset)";
            std::printf("  enhanced: %s %s (enhanced.json %s)\n", key, got.c_str(), file.c_str());
            if (got != v || file != v) problems += std::string(problems.empty() ? "" : "; ") + key + " is " + got + " / " + file + ", not " + v;
        };
        auto state = [&](const char* key, int lo, int hi) {
            const int v = ui.host().enhancedState(key);
            std::printf("  enhanced state: %s %d\n", key, v);
            if (v < lo || v > hi) problems += std::string(problems.empty() ? "" : "; ") + key + " is " + std::to_string(v);
        };
        auto status = [&](const char* prefix) {
            const std::string st = ui.host().enhancedStatus();
            std::printf("  enhanced status: %s\n", st.c_str());
            if (st.rfind(prefix, 0) != 0) problems += std::string(problems.empty() ? "" : "; ") + "the status is \"" + st + "\", not " + prefix + "...";
        };
        constexpr int kMany = 1 << 30;
        const std::string& w = s.screen;
        if (w == "enhanced_default") {
            const std::string def = ui::enhancedDefault("internal_res", false);
            const std::string got = ui.host().enhancedValue("internal_res");
            std::printf("  enhanced: internal_res %s (the build's default %s)\n", got.c_str(), def.c_str());
            if (got != def) problems = "Internal Resolution is " + got + ", not " + def;
        } else if (w == "enhanced_fx_default") {
            // The defaults: the original look (GD02-130). GH2's own post
            // (glow, depth of field), particles and shake; none of the
            // port's effects, and the menus' textures as the original's.
            for (const char* key : {"bokeh", "bloom", "cinematic_color", "film_grain", "chromatic", "ssao", "sharpening", "star_blur",
                                    "shadows_all", "more_lights", "light_dust", "sharp_ui", "fps_counter"}) {
                const std::string got = ui.host().enhancedValue(key), def = ui::enhancedDefault(key, false);
                const bool original = def == "off" || def == "none" || (std::string(key) == "bokeh" && def == "medium");
                std::printf("  enhanced default: %s %s\n", key, got.c_str());
                if (got != def || !original) problems += std::string(problems.empty() ? "" : "; ") + key + " is " + got + ", not the original's";
            }
            state("fx_on", 0, 0), state("post_original", 1, 1);
            for (const char* pass : {"fx:down", "fx:up", "fx:ao", "fx:composite", "fx:finish"}) state(pass, 0, 0);
            state("particles", 1, 1), state("particle_rate", 10, 10), state("shake", 1, 1), state("sharp_ui", 0, 0);
            state("xinput", 1, 1), state("rockband", 1, 1);  // always on (no switches since 0.1.8)
        } else if (w == "enhanced_mode_deck" || w == "enhanced_mode_original" || w == "enhanced_mode_old_days" || w == "enhanced_mode_clean") {
            const char* mode = w == "enhanced_mode_deck" ? "steam_deck" : w == "enhanced_mode_original" ? "original"
                               : w == "enhanced_mode_clean" ? "clean" : "good_old_days";
            for (const auto& [key, v] : ui::enhancedPreset(mode)) want(key.c_str(), v);
            status("Mode applied");
            if (w == "enhanced_mode_original") want("internal_res", "800"), want("screen_mode", "fullscreen");  // Steam Deck's, kept
            if (w == "enhanced_mode_old_days") state("fx_on", 1, 1), state("particle_rate", 20, 20);
        } else if (w == "enhanced_hdr_emulated" || w == "enhanced_hdr_true" || w == "enhanced_hdr_none") {
            const std::string mode = w == "enhanced_hdr_emulated" ? "emulated" : w == "enhanced_hdr_true" ? "true" : "none";
            want("hdr", mode);
            const int level = mode == "none" ? 0 : mode == "emulated" ? 1 : 2;
            state("hdr", level, level);
            // The HDR passes (exposure, tone map) run with Emulated and True HDR only.
            state("fx:lum", level ? 1 : 0, level ? 1 : 0), state("fx:adapt", level ? 1 : 0, level ? 1 : 0);
            const std::string st = ui.host().enhancedValue("hdr_status");
            std::printf("  enhanced hdr status: %s\n", st.c_str());
            // True HDR chosen in the menu: the surface is made at start, so Emulated until then (or unavailable).
            const char* prefix = mode == "none" ? "" : mode == "emulated" ? "Emulated" : "True HDR ";
            if (st.rfind(prefix, 0) != 0 || (mode == "true" && st.find("Using Emulated") == std::string::npos) || (mode == "none" && !st.empty()))
                problems += std::string(problems.empty() ? "" : "; ") + "the HDR status is \"" + st + "\"";
        } else if (w == "enhanced_res_720") {
            want("internal_res", "720");
        } else if (w == "enhanced_controls") {
            want("touch", "on");
            state("touch", 1, 1), state("rockband", 1, 1), state("xinput", 1, 1);
        } else if (w == "enhanced_calibrated") {
            // The presses came 0..30 ms after the ticks and flashes.
            for (const char* key : {"audio_offset", "video_offset"}) {
                const std::string v = saved.has(key) ? saved[key].str() : "";
                std::printf("  enhanced: %s %s\n", key, v.c_str());
                if (v.empty() || std::abs(std::atoi(v.c_str())) > 337) problems += std::string(problems.empty() ? "" : "; ") + key + " not calibrated (" + v + ")";
            }
            state("calibration_ms", -500, 300);
        } else if (w == "enhanced_calibration_reset") {
            for (const char* key : {"audio_offset", "video_offset"}) {
                const std::string v = saved.has(key) ? saved[key].str() : "";
                std::printf("  enhanced: reset %s %s\n", key, v.c_str());
                if (v != "0") problems += std::string(problems.empty() ? "" : "; ") + key + " not reset (" + v + ")";
            }
        } else if (w == "enhanced_changed") {
            want("internal_res", "480");
        } else if (w == "enhanced_graphics") {
            want("sharp_ui", "off"), want("antialiasing", "fxaa"), want("ai_upscaling", "fsr1"), want("ai_upscaling_mode", "performance");
            want("bokeh", "none"), want("particles", "none"), want("bloom", "2x"), want("cinematic_color", "on"), want("color_grade", "warm");
            want("film_grain", "medium"), want("chromatic", "light"), want("ssao", "medium");
            want("sharpening", "high"), want("star_blur", "medium");
            state("sharp_ui", 0, 0), state("particles", 0, 0), state("post_dof", 0, 0);
            for (const char* pass : {"fx:down", "fx:up", "fx:ao", "fx:composite", "fx:fxaa", "fx:easu", "fx:rcas", "fx:finish"}) state(pass, 1, 1);
            state("fx:taa", 0, 0), state("fx:downsample", 0, 0);
        } else if (w == "enhanced_no_effects") {
            want("no_effects", "on");
            state("fx_on", 0, 0), state("post_original", 0, 0), state("post_key", 0, 0), state("post_dof", 0, 0), state("shake", 0, 0);
        } else if (w == "enhanced_effects_back") {
            want("no_effects", "off");
            state("fx_on", 1, 1), state("post_original", 1, 1), state("shake", 0, 0);  // Disable Shaky Camera is on
        } else if (w == "enhanced_lights_default") {
            state("shadows", 0, 0), state("beams", 0, 0), state("motes", 0, 0);
        } else if (w == "enhanced_enhancers") {
            want("shadows_all", "on"), want("more_lights", "2x"), want("light_dust", "1.5x"), want("no_shaky_cam", "on");
            state("shadows", 1, kMany), state("beams", 1, kMany), state("motes", 1, kMany), state("shake", 0, 0);
        } else if (w == "enhanced_easy_chords_default") {
            const std::string got = ui.host().enhancedValue("easy_chords");
            const std::string file = saved.has("easy_chords") ? saved["easy_chords"].str() : "(unset)";
            std::printf("  enhanced: easy_chords %s (enhanced.json %s)\n", got.c_str(), file.c_str());
            if (got != "off" || (file != "(unset)" && file != "off")) problems = "Easy Chords is not off by default";
        } else if (w == "enhanced_easy_chords") {
            want("easy_chords", "on");
        } else if (w == "enhanced_easy_chords_exported") {
            status("Exported to");
            const char* dir = SDL_getenv("GD05_DIALOG");
            try {
                std::ifstream in(std::filesystem::path(dir ? dir : "") / "GD05-user-settings.json");
                std::stringstream ss;
                ss << in.rdbuf();
                const Json j = Json::parse(ss.str());
                std::printf("  exported easy_chords: %s\n", j.has("easy_chords") ? j["easy_chords"].str().c_str() : "(missing)");
                if (!j.has("easy_chords") || j["easy_chords"].str() != "on") problems = "easy_chords on was not exported";
            } catch (const std::exception&) {
                problems = "GD05-user-settings.json does not parse";
            }
        } else if (w == "enhanced_easy_chords_loaded") {
            status("Settings loaded");
            want("easy_chords", "on");
        } else if (w == "enhanced_easy_chords_off") {
            want("easy_chords", "off");
        } else if (w == "enhanced_exported") {
            status("Exported to");
            const char* dir = SDL_getenv("GD05_DIALOG");
            const std::filesystem::path d = dir ? dir : "";
            for (const char* f : {"GD05-user-settings.json", "GD05-user-progress.json"}) {
                std::error_code ec;
                const auto size = std::filesystem::file_size(d / f, ec);
                std::printf("  exported %s: %s\n", f, ec ? "missing" : (std::to_string(size) + " bytes").c_str());
                if (ec || size < 10) problems += std::string(problems.empty() ? "" : "; ") + f + " was not written";
            }
            // Options removed in 0.1.8 (Ground Fog, Light Streaks) in the
            // file to load next: Load User Settings ignores them.
            try {
                std::ifstream in(d / "GD05-user-settings.json");
                std::stringstream ss;
                ss << in.rdbuf();
                Json j = Json::parse(ss.str());
                if (!j.has("easy_chords") || j["easy_chords"].str() != "on")
                    problems += std::string(problems.empty() ? "" : "; ") + "easy_chords on was not exported";
                j.set("ground_fog", Json::string("2x")), j.set("light_streaks", Json::string("medium"));
                j.set("graphics_backend", Json::string("vulkan")), j.set("audio_backend", Json::string("openal"));  // GDCORE-006
                std::ofstream(d / "GD05-user-settings.json") << j.dump(2);
            } catch (const std::exception&) {
                problems += std::string(problems.empty() ? "" : "; ") + "GD05-user-settings.json does not parse";
            }
        } else if (w == "enhanced_backends") {
            // The defaults (Vulkan, miniaudio: D71), nothing saved (no mode
            // sets them); a saved backend the build offers is kept, an unknown
            // one (or the removed "local") dropped.
            for (auto [key, def] : {std::pair<const char*, const char*>{"graphics_backend", "vulkan"}, {"audio_backend", "miniaudio"}}) {
                const std::string got = ui.host().enhancedValue(key), file = saved.has(key) ? saved[key].str() : "(unset)";
                std::printf("  enhanced: %s %s (enhanced.json %s)\n", key, got.c_str(), file.c_str());
                if (got != def || (file != "(unset)" && file != def))
                    problems += std::string(problems.empty() ? "" : "; ") + key + " is " + got + " / " + file + ", not " + def;
            }
            Json j = Json::parse(R"({"graphics_backend":"vulkan","audio_backend":"openal","bloom":"2x"})");
            ui::migrateEnhanced(j);
            if (j["graphics_backend"].str() != "vulkan" || j["audio_backend"].str() != "openal" || !j.has("bloom"))
                problems += std::string(problems.empty() ? "" : "; ") + "a saved backend the build offers was dropped";
            j = Json::parse(R"({"graphics_backend":"metal","audio_backend":true,"bloom":"2x"})");
            ui::migrateEnhanced(j);
            if (j.has("graphics_backend") || j.has("audio_backend") || !j.has("bloom"))
                problems += std::string(problems.empty() ? "" : "; ") + "an unknown backend was kept";
            j = Json::parse(R"({"graphics_backend":"local","audio_backend":"local"})");
            ui::migrateEnhanced(j);
            if (j.has("graphics_backend") || j.has("audio_backend")) problems += std::string(problems.empty() ? "" : "; ") + "local was kept";
#ifdef __APPLE__
            j = Json::parse(R"({"graphics_backend":"opengl3"})");
            ui::migrateEnhanced(j);
            if (j.has("graphics_backend")) problems += std::string(problems.empty() ? "" : "; ") + "OpenGL3 kept on macOS";
#endif
        } else if (w == "enhanced_backends_start" || w == "enhanced_backends_saved" || w == "enhanced_backends_restarted") {
            // What this start uses (latched), what is set and saved, what plays.
            const std::string g0 = startupGraphicsBackend(), a0 = startupAudioBackend();
            const std::string aNow = ui.host().enhancedValue("audio_backend_now"), gNow = ui.host().enhancedValue("graphics_backend_now");
            std::printf("  backends: started with graphics %s, audio %s; running graphics %s, audio %s\n", g0.c_str(), a0.c_str(),
                        gNow.c_str(), aNow.c_str());
            auto add = [&](const std::string& p) { problems += std::string(problems.empty() ? "" : "; ") + p; };
            if (w == "enhanced_backends_start") {
                // (the file keeps what it had until the next save; the game reads the defaults)
                want("graphics_backend", "vulkan"), want("audio_backend", "miniaudio");
                if (g0 != "vulkan" || a0 != "miniaudio" || aNow != "miniaudio") add("an unknown saved backend was not dropped at start");
            } else if (w == "enhanced_backends_saved") {
                want("graphics_backend", "opengl3"), want("audio_backend", "openal");
                if (!saved.has("graphics_backend") || saved["graphics_backend"].str() != "opengl3" || !saved.has("audio_backend") ||
                    saved["audio_backend"].str() != "openal")
                    add("the backends were not saved");
                if (g0 != "vulkan" || a0 != "miniaudio" || aNow != "miniaudio") add("a backend changed before the restart");
            } else {
                want("graphics_backend", "sdl3"), want("audio_backend", "openal");
                if (g0 != "sdl3" || a0 != "openal") add("the restart did not start on the saved backends");
                if (aNow != "openal" && aNow != "miniaudio") add("audio plays on " + aNow);
            }
        } else if (w == "enhanced_settings_loaded") {
            status("Settings loaded");
            want("more_lights", "2x"), want("internal_res", "480"), want("antialiasing", "fxaa");
            want("easy_chords", "on");
            for (const char* gone : {"ground_fog", "light_streaks"})
                if (saved.has(gone)) problems += std::string(problems.empty() ? "" : "; ") + gone + " (removed) was loaded";
            // The file's backends are loaded (GDCORE-008): they apply on the next start.
            want("graphics_backend", "vulkan"), want("audio_backend", "openal");
        } else if (w == "enhanced_progress_loaded") {
            status("Progress loaded");
        } else if (w == "enhanced_cheats") {
            want("no_fail", "on"), want("autoplay", "on"), want("hopo_mode", "forgiving");
            if (saved.has("cut_content")) problems += "Enable Cut Content (not built) was saved";
        } else {
            return fail("unknown check " + w);
        }
        if (!problems.empty()) return fail(problems);
        return pass();
    }
    if (s.screen == "cheat_menu_monkey" || s.screen == "cheat_menu_rest") {
        const bool monkey = ui.cheatOn("cheat_monkey_head_crowd"), eyeball = ui.cheatOn("cheat_eyeball_head_crowd");
        const bool flaming = ui.cheatOn("cheat_flaming_head"), hyper = ui.cheatOn("cheat_hyper_speed_mode");
        std::printf("  cheats (Enhanced Options): monkey %d, eyeball %d, flaming head %d, hyper speed %d\n", monkey, eyeball, flaming, hyper);
        if (s.screen == "cheat_menu_monkey" && !monkey) return fail("Monkey Head Crowd did not turn on from the menu");
        if (s.screen == "cheat_menu_rest" && (monkey || !eyeball)) return fail("Eyeball Head Crowd did not replace Monkey Head Crowd");
        if (s.screen == "cheat_menu_rest" && !flaming) return fail("Flaming Head did not turn on from the menu");
        if (s.screen == "cheat_menu_rest" && hyper) return fail("Hyper Speed Mode did not turn off from the menu");
        ++step_, stepStart_ = -1;
        return;
    }
    if (s.screen == "cheat_air_on" || s.screen == "cheat_air_off" || s.screen == "cheat_hyper_on") {
        const bool air = ui.cheatOn("cheat_air_guitar"), hyper = ui.cheatOn("cheat_hyper_speed_mode");
        std::printf("  cheats: air guitar %s, hyper speed %s\n", air ? "on" : "off", hyper ? "on" : "off");
        if (s.screen == "cheat_air_on" && !air) return fail("the Air Guitar code did not turn it on");
        if (s.screen == "cheat_air_off" && air) return fail("the Air Guitar code did not turn it off again");
        if (s.screen == "cheat_hyper_on" && !hyper) return fail("the Hyper Speed code did not turn it on");
        ++step_, stepStart_ = -1;
        return;
    }
    if (s.screen.rfind("shot_", 0) == 0) {
        // A screenshot now (GD05_WALK_SHOTS): the store's second page, the venue's unlock.
        if (const char* dir = SDL_getenv("GD05_WALK_SHOTS")) ui.host().saveScreenshot(std::string(dir) + "/" + s.screen.substr(5) + ".bmp");
        ++step_, stepStart_ = -1;
        return;
    }
    if (s.screen == "multi_guitar_skins") {
        if (ui.currentScreen() == "multi_sel_guitar_screen" && !ui.inTransition())
            for (int p = 0; p < 2; ++p) ui.buttonDown(kX, p), ui.buttonUp(kX, p);
        ++step_, stepStart_ = -1;
        return;
    }
    if (s.screen == "proface_seed") {
        // A tour won (Campaign +0x68): pro face-off unlocked.
        ui.career().proFaceOff = true;
        ++step_, stepStart_ = -1;
        return;
    }
    if (s.screen == "multi_setup") {
        // What GamePanel gave the game: the mode, both difficulties and parts.
        const ui::Session& ss = ui.session();
        const std::string mode = ui::gameCfgValue(ui, "mode").str();
        const std::vector<std::string> inst = ui::songInstruments(ui, ss.song);
        auto part = [&](int p) { const int t = ss.players[size_t(p)].track; return t >= 0 && t < int(inst.size()) ? inst[size_t(t)] : std::string("?"); };
        std::printf("  game: mode %s, song %s, P1 %s %s, P2 %s %s, pads %d %d\n", mode.c_str(), ss.song.c_str(), ui::difficultySym(ss.players[0].difficulty),
                    part(0).c_str(), ui::difficultySym(ss.players[1].difficulty), part(1).c_str(), ss.players[0].padnum, ss.players[1].padnum);
        const std::string want = scenario_ == "coop" ? "multi_coop" : scenario_ == "faceoff" ? "multi_vs" : "multi_fo";
        if (mode != want) return fail("the mode is " + mode + ", not " + want);
        const int d1 = scenario_ == "proface" ? 0 : 1;
        if (ss.players[0].difficulty != 0 || ss.players[1].difficulty != d1) return fail("the players' difficulties are not the ones chosen");
        if (scenario_ == "coop" && (part(0) != "guitar" || part(1) != "bass")) return fail("co-op's parts are not guitar and bass");
        if (scenario_ != "coop" && (part(0) != "guitar" || part(1) != "guitar")) return fail("face-off's parts are not both the guitar");
        if (ss.players[0].padnum != 0 || ss.players[1].padnum != 1) return fail("the controller select did not give pad 0 to P1 and pad 1 to P2");
        ++step_, stepStart_ = -1;
        return;
    }
    if (s.screen == "coop_record") {
        const auto it = ui.career().coop.find(ui.session().song);
        if (it == ui.career().coop.end()) return fail("finish_coop_song kept no co-op record");
        std::printf("  co-op record: %s %d (%d stars%s), awards:", ui.session().song.c_str(), it->second.score, it->second.stars,
                    it->second.perfect ? ", gold" : "");
        std::string awards;
        for (const auto& [g, v] : ui.career().coopAwards)
            if (v) awards += " " + g;
        std::printf("%s\n", awards.c_str());
        if (it->second.score <= 0) return fail("the co-op record has no score");
        // One song with three stars or more: guitars.dta's (require (songs 1) (stars 3)) bass.
        if (it->second.stars >= 3 && awards != " bass_sg_mahogany") return fail("co-op did not award bass_sg_mahogany (only)");
        ++step_, stepStart_ = -1;
        return;
    }
    if (s.screen == "band_named_CDE") {
        const int slot = ui.session().profileSlot;
        const std::string name = slot < 0 ? "" : ui.career().profiles[size_t(slot)].name;
        std::printf("  band: new band \"%s\"\n", name.c_str());
        if (name != "CDE") return fail("the band's name is \"" + name + "\", not \"CDE\" (arrows, green, typed letters, Enter)");
        ++step_, stepStart_ = -1;
        return;
    }
    if (s.screen == "band_named_A") {
        const int slot = ui.session().profileSlot;
        if (slot < 0) return fail("no band was created");
        const std::string& name = ui.career().profiles[size_t(slot)].name;
        std::printf("  career: new band \"%s\" (named with the keyboard's Enter)\n", name.c_str());
        if (name != "A") return fail("the band's name is \"" + name + "\", not \"A\" (Enter on the name must be Start)");
        ++step_, stepStart_ = -1;
        return;
    }
    if (s.screen == "career_seed") {
        for (const char* song : {"surrender", "possum", "heartshapedbox"})
            ui.interp().send("campaign", "cheat_beat_song", {Value::symbol(song), Value::integer(1)});
        ++step_, stepStart_ = -1;
        return;
    }
    if (s.screen.rfind("confirm_on ", 0) == 0) {
        // A second choice (outfit / skin) still open on that screen: take the first.
        if (ui.currentScreen() == s.screen.substr(11) && !ui.inTransition()) ui.buttonDown(kX, 0), ui.buttonUp(kX, 0);
        ++step_, stepStart_ = -1;
        return;
    }
    if (s.screen == "career_target" || s.screen == "career_song_target") {
        const char* w = SDL_getenv("GD05_WALK_SONG");
        const std::string target = !winTarget_.empty() ? winTarget_ : w ? w : "strutter";
        if (s.screen == "career_target") {
            // The tiers' songs in order up to the target, at this difficulty.
            const int d = ui::campaign::difficulty(ui);
            const auto& tiers = ui::campaign::config(ui).tiers;
            bool found = false;
            for (int t = 0; t < ui::campaign::numTiers(ui, d) && t < int(tiers.size()) && !found; ++t)
                for (const std::string& song : tiers[size_t(t)].second) {
                    if (song == target) {
                        found = true;
                        break;
                    }
                    ui.interp().send("campaign", "cheat_beat_song", {Value::symbol(song), Value::integer(1)});
                }
            if (!found) return fail("no career song " + target + " at difficulty " + std::to_string(d));
        } else {
            for (const auto& tier : ui::campaign::config(ui).tiers)
                if (!tier.second.empty() && tier.second.back() == target) {
                    // The venue's last song is its encore (never on the list to pick): walk career_encore plays it.
                    std::printf("  career: %s is %s's encore\n", target.c_str(), tier.first.c_str());
                    expect_.clear();
                    step_ = int(steps_.size()), stepStart_ = -1;
                    return;
                }
            std::printf("  career: venue %s, song %s, difficulty %d\n", ui.session().venue.c_str(), ui.session().song.c_str(),
                        ui::campaign::difficulty(ui));
            if (ui.session().song != target) return fail("the career offered " + ui.session().song + ", not " + target);
        }
        ++step_, stepStart_ = -1;
        return;
    }
    if (s.screen.rfind("career_venue_loaded ", 0) == 0) {
        const std::string v = s.screen.substr(20);
        const bool loaded = ui.host().gameCounter(0, "venue_loaded:" + v) == 1;
        std::printf("  career song: venue %s, %s's world %s\n", ui.session().venue.c_str(), v.c_str(), loaded ? "loaded" : "not loaded");
        if (ui.session().venue != v || !loaded) return fail("the career song is not in " + v);
        return pass();
    }
    if (s.screen == "focus_glitches") {
        ui::PanelObj* fp = ui.focusPanel();
        const auto& pr = ui.career().profiles;
        for (size_t i = 0; i < pr.size(); ++i)
            if (pr[i].inUse && pr[i].name == Career::kGlitches && fp) {
                fp->setFocus("cp_band" + std::to_string(i) + ".btn", true);
                std::printf("  career: The Glitches in slot %zu, focus %s\n", i, fp->focus.c_str());
                return pass();
            }
        return fail("no The Glitches band");
    }
    if (s.screen == "difficulty_status all") {
        ui::PanelObj* fp = ui.focusPanel();
        if (!fp || !fp->view) return fail("no difficulty panel");
        if (const char* dir = SDL_getenv("GD05_WALK_SHOTS")) ui.host().saveScreenshot(std::string(dir) + "/difficulty_glitches.bmp");
        // Each label against the band's own record: "(n OF m SONGS)" with n
        // its songs passed at that difficulty, blank with none (the original).
        Career::Profile* p = ui::campaign::profile(ui);
        if (!p) return fail("no band chosen");
        const char* labels[4] = {"sd_easy_status.lbl", "sd_med_status.lbl", "sd_hard_status.lbl", "sd_expert_status.lbl"};
        for (int d = 0; d < 4; ++d) {
            // The difficulty's tiers, Easy without its encores (0x82284F18).
            int n = 0, m = 0;
            const auto& tiers = ui::campaign::config(ui).tiers;
            for (int v = 0; v < ui::campaign::numTiers(ui, d) && v < int(tiers.size()); ++v)
                for (const std::string& song : tiers[size_t(v)].second) {
                    if (d == 0 && song == tiers[size_t(v)].second.back()) continue;
                    ++m, n += ui::campaign::passed(ui, *p, d, song) ? 1 : 0;
                }
            const std::string t = fp->view->text(labels[d]);
            std::printf("  difficulty status: %s '%s' (%d of %d songs passed)\n", labels[d], t.c_str(), n, m);
            const std::string want = "(" + std::to_string(n) + " OF " + std::to_string(m) + " ";
            if (n == 0 ? t.find("OF") != std::string::npos : t.find(want) != 0)
                return fail(std::string(labels[d]) + " does not show the band's songs passed");
        }
        return pass();
    }
    if (s.screen == "difficulty_status") {
        // sel_diff_career_panel's enter (career.dta): each difficulty's
        // campaign get_status_progress (0x82284F18) on its label.
        ui::PanelObj* fp = ui.focusPanel();
        if (!fp || !fp->view) return fail("no difficulty panel");
        if (const char* dir = SDL_getenv("GD05_WALK_SHOTS")) ui.host().saveScreenshot(std::string(dir) + "/difficulty_status.bmp");
        bool any = false;
        for (const char* l : {"sd_easy_status.lbl", "sd_med_status.lbl", "sd_hard_status.lbl", "sd_expert_status.lbl"}) {
            const std::string t = fp->view->text(l);
            std::printf("  difficulty status: %s '%s'\n", l, t.c_str());
            any = any || !t.empty();
        }
        if (fp->view->text("sd_med_status.lbl").find("OF") == std::string::npos) return fail("Medium's label shows no songs passed");
        return any ? pass() : fail("no difficulty shows its status");
    }
    if (s.screen == "quickplay_venue") {
        std::string want;
        for (const auto& song : ui.host().songs())
            if (song.id == ui.session().song) want = song.venue;
        const bool loaded = !want.empty() && ui.host().gameCounter(0, "venue_loaded:" + want) == 1;
        std::printf("  quick play: song %s, venue %s (the song's %s), its world %s\n", ui.session().song.c_str(), ui.session().venue.c_str(),
                    want.c_str(), loaded ? "loaded" : "not loaded");
        if (want.empty() || ui.session().venue != want || !loaded)
            return fail("quick play is not in the song's venue");
        return pass();
    }
    if (s.screen == "venue_scrolling") {
        ui::PanelObj* fp = ui.focusPanel();
        if (!fp || !fp->view) return fail("no sel_venue_panel");
        const float f = fp->view->frame("venue_anims.grp");
        std::printf("  venue select: mid-scroll map frame %.2f (focus %s)\n", f, fp->focus.c_str());
        if (const char* dir = SDL_getenv("GD05_WALK_SHOTS")) ui.host().saveScreenshot(std::string(dir) + "/venue_midscroll.bmp");
        return f > 4.0f && f < 5.0f ? pass() : fail("the map is not scrolling between theatre and big");
    }
    if (s.screen.rfind("click_updown ", 0) == 0) {
        // The help bar's UP/DOWN pill (GD02-137): the lower half is down, the upper up.
        const bool down = s.screen.substr(13) == "down";
        ui::PanelObj* hb = ui.panel("helpbar");
        if (hb && hb->view)
            for (const gfx::UiView::Hit& h : hb->view->hits())
                if (h.key == "hb2") {
                    ui.mouse((h.x0 + h.x1) * 0.5f, h.y0 + (h.y1 - h.y0) * (down ? 0.75f : 0.25f), true);
                    return pass();
                }
        return fail("the help bar's UP/DOWN pill was not drawn");
    }
    if (s.screen == "venue_follow") {
        ui::PanelObj* fp = ui.focusPanel();
        if (!fp || !fp->view) return fail("no sel_venue_panel");
        static const std::vector<std::string> order = {"battle", "small1", "small2", "big", "theatre", "fest", "arena", "stone"};
        const std::string f = fp->focus;
        int idx = -1;
        for (size_t i = 0; i < order.size(); ++i)
            if (f == "sv_" + order[i] + ".btn") idx = int(i);
        const float frame = fp->view->frame("venue_anims.grp");
        const ui::ComponentObj* c = fp->focused();
        const int state = c ? int(c->state()) : -1;
        std::printf("  venue select: focus %s, game venue %s, map frame %.2f, focused state %d\n", f.c_str(), ui.session().venue.c_str(),
                    frame, state);
        if (const char* dir = SDL_getenv("GD05_WALK_SHOTS"))
            ui.host().saveScreenshot(std::string(dir) + "/venue_" + std::to_string(++shotN_) + ".bmp");
        if (idx < 0) return fail("the focus is not a venue: " + f);
        if (ui.session().venue != order[size_t(idx)]) return fail("the game's venue is " + ui.session().venue + ", the focus " + f);
        if (std::fabs(frame - float(idx + 1)) > 0.01f) return fail("the map is at frame " + std::to_string(frame) + ", not " + std::to_string(idx + 1));
        if (state != int(scene::UiState::Focused)) return fail("the focused poster is not in its focused state");
        return pass();
    }
    if (s.screen == "career_pick_venue" || s.screen == "career_pick_song") {
        const char* w = SDL_getenv("GD05_WALK_SONG");
        const std::string target = !winTarget_.empty() ? winTarget_ : w ? w : "strutter";
        ui::PanelObj* fp = ui.focusPanel();
        std::string have, want;
        const int nav = kDown;  // sel_venue.milo's nav_down runs from the newest venue to the first
        if (s.screen == "career_pick_venue") {
            have = fp ? fp->focus : std::string(), want = "sv_" + ui::campaign::venueOf(ui, target) + ".btn";
        } else {
            ui::ComponentObj* c = fp ? fp->focused() : nullptr;
            have = c ? ui.interp().send(c, "selected_sym", {}).str() : std::string(), want = target;
        }
        if (have == want) return pass();
        if (nowMs - stepStart_ > s.ms) return fail(s.screen + ": " + want + " not reached (on " + have + ")");
        if (!ui.inTransition() && nowMs - lastNavMs_ > 250) ui.buttonDown(nav, 0), ui.buttonUp(nav, 0), lastNavMs_ = nowMs;
        return;
    }
    if (s.screen == "tour_won easy" || s.screen == "tour_won other") {
        // win_game_screen's enter (ui/gen/endgame.dta): beat_easy on Easy,
        // else beat_other, and the meta crowd_win; the campaign won (status
        // >= its tiers); a tour above Easy unlocks pro face-off (GH2 +0x68).
        std::map<std::string, int> n;
        for (const std::string& h : ui.host().sfxHistory()) ++n[h];
        Interp& in = ui.interp();
        const bool won = in.send("campaign", "won_campaign", {}).truthy();
        const int status = in.send("campaign", "status", {}).integerValue();
        std::printf("  tour: won %s, status %d, difficulty %d; beat_easy %d, beat_other %d, crowd_win %d, pro face-off %s\n", won ? "yes" : "no",
                    status, ui::campaign::difficulty(ui), n["beat_easy"], n["beat_other"], n["crowd_win"], ui.career().proFaceOff ? "unlocked" : "locked");
        const bool easy = s.screen == "tour_won easy";
        if (!won) return fail("the tour is not won after its last song");
        if (n[easy ? "beat_easy" : "beat_other"] != 1 || n[easy ? "beat_other" : "beat_easy"] != 0) return fail("win_game_screen did not play its difficulty's sound");
        if (!n["crowd_win"]) return fail("no crowd_win on win_game_screen");
        if (!easy && !ui.career().proFaceOff) return fail("pro face-off not unlocked by the tour");
        return pass();
    }
    if (s.screen == "career_song_salvation") {
        // set_career_song (0x822B89B8): the venue's first unlocked song not passed yet.
        std::printf("  career: venue %s, song %s\n", ui.session().venue.c_str(), ui.session().song.c_str());
        if (ui.session().venue != "battle" || ui.session().song != "salvation") return fail("the career did not offer battle's salvation");
        ++step_, stepStart_ = -1;
        return;
    }
    if (s.screen == "setlist_encore") {
        // The locked encore's row (GD02-089): inactive, drawn without its
        // name (0x822C2080), the blurb on the row above it (0x822C0070 /
        // 0x822C5A68) reads where the name would be.
        auto* sp = dynamic_cast<ui::ListProvider*>(ui.interp().findObject("song_provider"));
        if (!sp) return fail("no song_provider");
        std::vector<gfx::UiView::ListRow> rows;
        int sel = 0;
        sp->rows(rows, 0, sel);
        const int e = sp->indexOf("shoutatthedevil");
        if (e < 1 || e >= int(rows.size())) return fail("the setlist has no locked encore row");
        const auto& er = rows[size_t(e)];
        const auto& pr = rows[size_t(e - 1)];
        const auto text = [](const gfx::UiView::ListRow& r, const char* k) {
            auto it = r.texts.find(k);
            return it == r.texts.end() ? std::string() : it->second;
        };
        std::printf("  setlist: encore row %s, name '%s'; blurb '%s' on %s\n", er.disabled ? "inactive" : "active", text(er, "list.txt").c_str(),
                    text(pr, "blurb.txt").c_str(), sp->symbol(e - 1).c_str());
        if (!er.disabled || !text(er, "list.txt").empty() || !text(er, "blurb.txt").empty())
            return fail("the locked encore's row is active or shows its name (the blurb would overlap it)");
        if (text(pr, "blurb.txt").empty()) return fail("no blurb on the row above the locked encore");
        return pass();
    }
    if (s.screen == "career_encore_offered") {
        std::printf("  career: encore offered after %s (cash %d)\n", ui.session().song.c_str(),
                    ui.interp().send("campaign", "cash", {}).integerValue());
        ++step_, stepStart_ = -1;
        return;
    }
    if (s.screen == "career_tier" || s.screen == "career_bought") {
        // The tour at Medium (ui/campaign.h): battle complete with its
        // encore, small1 unlocked, status 1; the cash: 50 to start, 150 per
        // three-star song (salvation, the encore), 100 for status 1; then
        // the first finish of the store (lp_cayenne, 250) bought and on the
        // Les Paul's finishes.
        Interp& in = ui.interp();
        const int cash = in.send("campaign", "cash", {}).integerValue();
        const int status = in.send("campaign", "status", {}).integerValue();
        const bool small1 = in.send("campaign", "is_unlocked", {Value::symbol("small1")}).truthy();
        const bool battle = in.send("campaign", "is_song_passed", {Value::symbol("shoutatthedevil")}).truthy();
        std::printf("  career: status %d, cash %d, small1 %s, encore passed %s\n", status, cash, small1 ? "unlocked" : "locked", battle ? "yes" : "no");
        if (s.screen == "career_tier") {
            if (status != 1 || cash != 450 || !small1 || !battle) return fail("the first tier is not complete as GH2's rules say (status 1, $450)");
        } else {
            const auto skins = ui::chars::skins(ui, "lespaul");
            const bool on = std::find(skins.begin(), skins.end(), "lp_cayenne") != skins.end();
            const int spent = in.send("campaign", "cash_spent", {}).integerValue();
            std::printf("  store: lp_cayenne %s, cash %d, spent %d\n", on ? "bought" : "not bought", cash, spent);
            if (!on || cash != 200 || spent != 250) return fail("the store did not sell lp_cayenne for $250");
        }
        ++step_, stepStart_ = -1;
        return;
    }
    // Practice (practice.dta, PS2 0x00106840): 65% plays the song's
    // song_practice_2 audio (songs/<s>/<s>_p65.vgs), the clock runs at
    // 0.65 of the wall clock from one measure before the start section,
    // the metronome (click_parser: practice_kick / _snare / _hat) plays,
    // and the end screen's count is the section's gems.
    if (s.screen == "practice_stream" || s.screen.rfind("practice_stream ", 0) == 0) {
        const std::string a = ui.host().gameAudioPath();
        const ui::Session& ss = ui.session();
        const std::string pct = s.screen.size() > 16 ? s.screen.substr(16) : "65";
        std::printf("  practice: %s, audio %s, speed %d, sections %s\n", ss.song.c_str(), a.c_str(), ui::gameCfgValue(ui, "practice_speed").integerValue(),
                    ui::gameCfgValue(ui, "practice_sections").str().c_str());
        // The song's own mogg (or its song_coop block's, for the bass) at any
        // speed; below 100% the background silent, at 100% practice_attenuation.
        if (a.rfind("songs/" + ss.song + "/", 0) != 0 || a.size() < 5 || a.compare(a.size() - 5, 5, ".mogg") != 0)
            return fail("practice at " + pct + "% plays " + a + ", not the song's mogg");
        const float bg = ui.host().gameBackgroundDb();
        std::printf("  practice: background %.1f dB\n", double(bg));
        if (std::fabs(bg - (pct == "100" ? -7.5f : -96.0f)) > 0.01f) return fail("practice's background fader is wrong");
        if (ui.host().gameTimeMs() < 500) return fail("practice from the first section did not start at its measure");
        return pass();
    }
    if (s.screen == "practice_paused yes" || s.screen == "practice_paused no") {
        const bool want = s.screen.back() == 's';
        std::printf("  practice: the game %s at %.0f ms\n", ui.host().gamePaused() ? "paused" : "running", ui.host().gameTimeMs());
        if (ui.host().gamePaused() != want) return fail(want ? "Start did not pause practice" : "Start did not resume practice");
        return pass();
    }
    if (s.screen == "practice_career_back") {
        // career_screen resume_from_practice: career_mode off again, the career's mode back.
        Interp& in = ui.interp();
        ScriptObject* ss = in.findObject("practice_sel_section_screen");
        const bool careerMode = ss && ss->getProp(in, "career_mode").truthy();
        const std::string mode = ui::gameCfgValue(ui, "mode").str();
        std::printf("  career practice: back on the career menu, mode %s, career_mode %s\n", mode.c_str(), careerMode ? "on" : "off");
        if (careerMode || mode != "career") return fail("the career did not take over again after practice");
        return pass();
    }
    if (s.screen == "practice_click_fader") {
        // practice_panel's enter put the metronome under music_fader
        // (practice.dta); practice_end faded it with the music to -50 dB.
        const float db = ui::faderDb("music_fader");
        ui::PanelObj* pp = ui.panel("practice_panel");
        ui::ComponentObj* kick = pp ? pp->component("practice_kick") : nullptr;
        const bool under = kick && std::find(kick->faders.begin(), kick->faders.end(), "music_fader") != kick->faders.end();
        std::printf("  practice: music_fader at %.1f dB, practice_kick %s it\n", db, under ? "under" : "not under");
        if (!under || db > -49.0f) return fail("the metronome is not faded out with the music");
        return pass();
    }
    if (s.screen == "practice_clock") {
        const double song = ui.host().gameTimeMs();
        if (practiceClock_[0] < 0) {
            practiceClock_ = {nowMs, song};
            return pass();
        }
        const double rate = (song - practiceClock_[1]) / std::max(1.0, nowMs - practiceClock_[0]);
        std::printf("  practice: the song clock at %.3f x the wall clock\n", rate);
        if (std::fabs(rate - 0.65) > 0.05) return fail("the practice song clock does not run at 65%");
        return pass();
    }
    if (s.screen == "practice_end") {
        int kick = 0, snare = 0, hat = 0;
        for (const auto& [name, at] : ui.host().gameSfx()) kick += name == "practice_kick", snare += name == "practice_snare", hat += name == "practice_hat";
        const Player* pl = ui.host().gamePlayer(0);
        const int hit = pl ? pl->notesHit() : 0, passed = pl ? pl->gemsPassed() : 0;
        std::printf("  practice: metronome %d kick, %d snare, %d hat; the section's gems %d hit, %d passed\n", kick, snare, hat, hit, passed);
        if (!kick || !snare || !hat) return fail("the practice metronome (click_parser) did not play its three sounds");
        int bass = 0;
        for (const auto& [name, at] : ui.host().gameSfx()) bass += name == "miss_bass";
        std::printf("  practice: the bass part, %d miss_bass\n", bass);
        if (ui::gameCfgValue(ui, "mode").str() != "practice" || ui.session().players[0].trackType != 0) return fail("practice is not on the bass");
        if (bass != 1) return fail("the overstrum on the bass did not play miss_bass once");
        if (hit < 1 || hit + passed >= int(pl->chart().gems.size())) return fail("the practice end counts not just the section's gems");
        return pass();
    }
    // The first tutorial (tutorials.dta, tutorial_11_tuning.dta): its
    // voice-over plays to the tuning, the five frets each strummed three
    // times tune it (TutorialPanel's tuner: s101xx, tuning_hit_vo), then
    // tuning_complete plays and the next lesson (playing_notes) starts.
    if (s.screen == "tutorial_tuning" || s.screen == "tutorial_tuned" || s.screen == "tutorial_next_lesson") {
        ScriptObject* t = ui.interp().findObject("tutorial");
        if (!t) return fail("no tutorial object");
        Interp& in = ui.interp();
        const bool tuning = in.send(t, "is_tuning", {}).truthy();
        const std::string lesson = t->getProp(in, "lesson").str();
        bool ok = false;
        if (s.screen == "tutorial_tuning") ok = tuning;
        if (s.screen == "tutorial_tuned") ok = !tuning && lesson == "intro";
        if (s.screen == "tutorial_next_lesson") ok = lesson == "playing_notes";
        if (!ok) {
            if (nowMs - stepStart_ > s.ms) return fail(s.screen + " not reached (lesson " + lesson + (tuning ? ", tuning" : "") + ")");
            return;
        }
        if (s.screen == "tutorial_next_lesson") {
            int notes = 0, complete = 0;
            for (const auto& [name, at] : ui.host().gameSfx()) {
                notes += name.size() == 6 && name.rfind("s101", 0) == 0;
                complete += name == "tuning_complete";
            }
            std::printf("  tutorial: %d tuning notes, tuning_complete %s, lesson %s\n", notes, complete ? "played" : "not played", lesson.c_str());
            if (notes != 15 || !complete) return fail("the tuning did not play its 15 notes and tuning_complete");
        }
        return pass();
    }
    // The later tutorials: tutorials_panel's buttons (enabled by
    // tutorials_done), the lessons in turn, the hopo trainer, and what each
    // tutorial played to its end.
    if (s.screen.rfind("tut_locked ", 0) == 0 || s.screen.rfind("tut_unlocked ", 0) == 0) {
        const bool want = s.screen[4] == 'l';
        const std::string btn = "tut_" + s.screen.substr(s.screen.size() - 1) + ".btn";
        ui::PanelObj* p = ui.panel("tutorials_panel");
        const int done = ui.interp().send("campaign", "tutorials_done", {}).integerValue();
        std::printf("  tutorials: %d done, %s %s\n", done, btn.c_str(), p && p->isDisabled(btn) ? "disabled" : "enabled");
        if (!p || p->isDisabled(btn) != want) return fail(btn + (want ? " is not disabled" : " is not enabled"));
        return pass();
    }
    if (s.screen == "tutorials_seed") {
        ui.interp().send("campaign", "do_tutorial", {Value::integer(2)});  // tutorials 1 and 2 started before
        return pass();
    }
    if (s.screen.rfind("tutorial_lesson ", 0) == 0 || s.screen == "hopo_training" || s.screen.rfind("hopo_trained ", 0) == 0) {
        ScriptObject* t = ui.interp().findObject("tutorial");
        if (!t) return fail("no tutorial object");
        Interp& in = ui.interp();
        const std::string lesson = t->getProp(in, "lesson").str();
        const bool training = in.send(t, "is_hopo_training", {}).truthy();
        bool ok = false;
        if (s.screen.rfind("tutorial_lesson ", 0) == 0) ok = lesson == s.screen.substr(16);
        else if (s.screen == "hopo_training") ok = training;
        else ok = !training;
        if (!ok) {
            if (nowMs - stepStart_ > s.ms) return fail(s.screen + " not reached (lesson " + lesson + (training ? ", hopo training" : "") + ")");
            return;
        }
        std::printf("  tutorial: lesson %s%s\n", lesson.c_str(), training ? ", hopo training" : "");
        if (s.screen.rfind("hopo_trained ", 0) == 0) {
            ScriptObject* cb = in.findObject(s.screen.substr(13));
            const int hits = cb ? cb->getProp(in, "hopos_hit").integerValue() : -1;
            int notes = 0;
            for (const std::string& h : ui.host().sfxHistory()) notes += h.size() == 6 && h.rfind("s2060", 0) == 0;
            std::printf("  hopo trainer: %d tries hit (%s), %d notes so far\n", hits, s.screen.substr(13).c_str(), notes);
            if (hits != 3) return fail("the hopo trainer did not count three tries");
        }
        return pass();
    }
    if (s.screen == "tutorial_done 2" || s.screen == "tutorial_done 3") {
        const auto& vo = ui.session().voiceEvents;
        const auto has = [&](const char* e) { return std::find(vo.begin(), vo.end(), e) != vo.end(); };
        std::map<std::string, int> n;
        for (const std::string& h : ui.host().sfxHistory()) ++n[h];
        const bool two = s.screen.back() == '2';
        const std::vector<const char*> done = two ? std::vector<const char*>{"star_combos_done", "whammy_done", "wrapup1_2"}
                                                  : std::vector<const char*>{"hammeron_done", "hammeron_real_done", "pulloff_done",
                                                                             "pulloff_real_done", "wrapup1_3"};
        // A lesson's success voice-over requested while another (the last
        // hit's great_hit_vo / hit_vo) still plays is dropped, as on the PS2
        // (0x001407f0 plays only when no stream is playing): the success
        // branch is then shown by its banner sound and the drop.
        const auto& dropped = ui.session().voiceDropped;
        const auto wasDropped = [&](const char* e) { return std::find(dropped.begin(), dropped.end(), e) != dropped.end(); };
        std::string line;
        for (const char* e : done) line += std::string(" ") + e + (has(e) ? "" : wasDropped(e) ? "(dropped: a VO was playing)" : "(missing)");
        std::printf("  tutorial %c: voice-overs%s; timeout_vo %s\n", s.screen.back(), line.c_str(), has("timeout_vo") ? "played" : "not played");
        std::printf("  tutorial %c: button_error %d, s19901 %d, s19902 %d, s20601..3 %d %d %d\n", s.screen.back(), n["button_error"],
                    n["s19901"], n["s19902"], n["s20601"], n["s20602"], n["s20603"]);
        for (const char* e : done)
            if (!has(e) && !wasDropped(e)) return fail(std::string("the lesson's success voice-over ") + e + " was never requested");
        if (has("timeout_vo")) return fail("a lesson timed out");
        if (!n["s19901"] || !n["s19902"]) return fail("the tutorial's end (s19901 / s19902) did not play");
        if (two && !n["button_error"]) return fail("a gem hit before the wail did not play button_error");
        if (!two && (!n["s20601"] || !n["s20602"] || !n["s20603"])) return fail("the hopo trainer's notes s20601..3 did not play");
        return pass();
    }
    if (s.screen == "volumes_record" || s.screen == "volumes_saved") {
        Interp& in = ui.interp();
        std::array<int, 3> v{};
        const char* keys[] = {"band_volume_idx", "guitar_volume_idx", "fx_volume_idx"};
        for (size_t k = 0; k < 3; ++k) v[k] = in.send("options", "get", {Value::symbol(keys[k])}).integerValue();
        std::printf("  options: volumes band %d, guitar %d, fx %d\n", v[0], v[1], v[2]);
        if (s.screen == "volumes_record") return volumes_ = v, pass();
        for (size_t k = 0; k < 3; ++k)
            if (v[k] != std::max(0, volumes_[k] - 1)) return fail(std::string("the slider did not set ") + keys[k] + " one step down");
        return pass();
    }
    if (s.screen == "audio_sliders") {
        int band = 0, gtr = 0, sfx = 0;
        for (const std::string& n : ui.host().sfxHistory()) band += n == "slider_band", gtr += n == "slider_gtr", sfx += n == "slider_sfx";
        std::printf("  options: slider sounds band %d, guitar %d, sfx %d\n", band, gtr, sfx);
        if (!band || !gtr || !sfx) return fail("a volume slider moved without its sound");
        return pass();
    }
    if (s.screen.rfind("focus_to ", 0) == 0 || s.screen.rfind("focus_up_to ", 0) == 0) {
        // Down (focus_up_to: up) until the focus panel's focus is the component.
        const bool up = s.screen[6] == 'u';
        const std::string want = s.screen.substr(up ? 12 : 9);
        ui::PanelObj* fp = ui.focusPanel();
        if (fp && fp->focus == want) return pass();
        if (nowMs - stepStart_ > s.ms) return fail("no focus on " + want + " (on " + (fp ? fp->focus : std::string("-")) + ")");
        const int b = up ? kUp : kDown;
        if (!ui.inTransition() && nowMs - lastNavMs_ > 200) ui.buttonDown(b, 0), ui.buttonUp(b, 0), lastNavMs_ = nowMs;
        return;
    }
    if (s.screen == "lag_strike") {
        int clicks = 0;
        for (const std::string& n : ui.host().sfxHistory()) clicks += n == "sync_click.cue";
        if (clicks > lagStrikes_) ++lagStrikes_, ui.buttonDown(kDown, 0), ui.buttonUp(kDown, 0);
        if (lagStrikes_ >= 11) return pass();
        if (nowMs - stepStart_ > s.ms) return fail("the lag calibration played " + std::to_string(clicks) + " of 11 clicks");
        return;
    }
    if (s.screen == "lag_calibrated") {
        ScriptObject* lp = ui.interp().findObject("lag_panel");
        const std::string state = lp ? lp->getProp(ui.interp(), "state").str() : std::string();
        if (state == "calibrating" || state == "init") {
            if (nowMs - stepStart_ > s.ms) return fail("the lag calibration did not finish (state " + state + ")");
            return;
        }
        int clicks = 0, hats = 0;
        for (const std::string& n : ui.host().sfxHistory()) clicks += n == "sync_click.cue", hats += n == "practice_hat";
        std::printf("  lag: %s, lag %s ms, %d sync_click.cue, %d countdown hats; hits %s, clicks %s\n", state.c_str(), lp->getProp(ui.interp(), "lag").str().c_str(), clicks,
                    hats, lp->getProp(ui.interp(), "hits").str().c_str(), lp->getProp(ui.interp(), "seconds").str().c_str());
        if (clicks != 11 || hats != 4) return fail("the lag calibration did not play its countdown and 11 clicks");
        if (state != "success") return fail("strums on the clicks did not calibrate the lag");
        return pass();
    }
    if (s.screen == "encore_decline") {
        // On the encore offer: down to no.btn, then select it.
        if (ui.currentScreen() == "endgame_encore_screen" && !ui.inTransition()) {
            ui::PanelObj* fp = ui.focusPanel();
            const int b = fp && fp->focus == "no.btn" ? kX : kDown;
            if (b == kX) encoreDeclined_ = true;
            ui.buttonDown(b, 0), ui.buttonUp(b, 0);
        }
        return pass();
    }
    if (s.screen == "encore_declined") {
        if (!encoreDeclined_) return pass();  // no encore offered
        int no = 0;
        for (const std::string& h : ui.host().sfxHistory()) no += h == "encore_no";
        std::printf("  career: encore declined, encore_no %d, on %s\n", no, ui.currentScreen().c_str());
        if (!no) return fail("declining the encore did not play encore_no");
        if (ui.currentScreen() != "endgame_screen") return fail("declining the encore did not go on to the results");
        return pass();
    }
    if (s.screen == "big_note_missed") {
        // A big note (PART GUITAR pitch 110) whose gem passed unhit: GH2 asks
        // the big-note check only from a sustain let go early (0x822C86F8),
        // so a missed one plays no blew_bignote.cue (GD02-150) [C]; the
        // early release itself is a self-test rule check.
        double at = -1;
        for (const auto& [name, t] : ui.host().gameSfx())
            if (name == "blew_bignote.cue" && at < 0) at = t;
        std::printf("  big note missed: blew_bignote.cue %s\n", at < 0 ? "not played" : "played");
        return at < 0 ? pass() : fail("a missed big note played blew_bignote.cue (GH2 plays it only for a sustain let go early)");
    }
    if (s.screen == "sp_multi_fill") {
        // Both meters full once the song runs (the deploy comes from songKeys_).
        if (!ui.host().gamePlayer(0) || !ui.host().gamePlayer(1) || ui.host().gamePlayer(0)->nowMsForTest() < 1000) {
            if (nowMs - stepStart_ > s.ms) return fail("the face-off song did not start");
            return;
        }
        for (int p = 0; p < 2; ++p) ui.host().setPlayerStarPower(p, "meter", 1.0f);
        return pass();
    }
    if (s.screen == "sp_multi_sounds") {
        // Two-player Star Power's _multi sounds, and the averages the face-off
        // doing badly rule reads (0x822C6DC0, 0x822DCF70).
        std::map<std::string, int> n;
        for (const auto& [name, at] : ui.host().gameSfx()) ++n[name];
        const Player* a = ui.host().gamePlayer(0);
        const Player* b = ui.host().gamePlayer(1);
        std::printf("  face-off: sp_deployed_multi %d, average multipliers %.2f %.2f\n", n["sp_deployed_multi"], a ? a->averageMultiplier() : 0.0f,
                    b ? b->averageMultiplier() : 0.0f);
        if (!n["sp_deployed_multi"]) return fail("no sp_deployed_multi in a two-player song");
        if (!a || !b || a->averageMultiplier() < 1 || b->averageMultiplier() < 1) return fail("the average multipliers were not kept");
        return pass();
    }
    if (s.screen == "song_sounds") {
        // The song's sounds after its middle: Star Power deployed
        // (player.dta start_using: sp_deployed) and run out (stop_using:
        // sp_depleted), game.dta's claps 0.9 beats after each beat under it
        // (the world's claps sequence), and the crowd's crowd_win at the win
        // (CrowdAudio's game_won_msg).
        std::map<std::string, int> n;
        for (const auto& [name, at] : ui.host().gameSfx()) ++n[name];
        int crowdWin = 0;
        for (const std::string& h : ui.host().sfxHistory()) crowdWin += h == "crowd_win";
        std::printf("  song sounds: sp_deployed %d, sp_depleted %d, claps %d, crowd_win %d\n", n["sp_deployed"], n["sp_depleted"], n["claps"], crowdWin);
        if (!n["sp_deployed"] || !n["sp_depleted"]) return fail("Star Power was not deployed and run out (sp_deployed / sp_depleted)");
        if (!n["claps"]) return fail("no claps under Star Power");
        if (!crowdWin) return fail("the crowd did not cheer the win (crowd_win)");
        return pass();
    }
    fail("unknown check " + s.screen);
}

int WalkDriver::finish(ui::UiManager& ui) {
    if (!done_ && !failed_ && expectQuit_) {
        std::printf("  ok   the game quit (expected)\n");
        done_ = true;
    }
    bool ok = !failed_ && done_;
    std::printf("walktest %s:\n", scenario_.c_str());
    if (!error_.empty()) std::printf("  FAIL %s\n", error_.c_str());
    if (!done_) std::printf("  FAIL the walk did not finish (stopped at step %zu of %zu)\n", step_, steps_.size());
    // The screens, in order.
    std::string seq;
    for (const std::string& s : ui.history()) seq += (seq.empty() ? "" : " > ") + s;
    std::printf("  screens: %s\n", seq.c_str());
    if (textColors_ || !textColorsBad_.empty()) {
        std::printf("  text colours: %zu screens measured, %zu texts too dark\n", textColorsSeen_.size(), textColorsBad_.size());
        for (const std::string& b : textColorsBad_) std::printf("  FAIL text too dark: %s\n", b.c_str());
        if (!textColorsBad_.empty()) ok = false;
    }
    size_t k = 0;
    for (const std::string& s : ui.history())
        if (k < expect_.size() && s == expect_[k]) ++k;
    if (k < expect_.size()) {
        std::printf("  FAIL expected screen %s (in order) was not reached\n", expect_[k].c_str());
        ok = false;
    }
    // The save (quickplay): career.json with the song's high score.
    // GH2's save checks (gd02's quickplay walk): RB's saves are M7 (GD05-009).
    if (scenario_ == "gh2_quickplay") {
        std::ifstream f(ui.savePath());
        std::stringstream ss;
        ss << f.rdbuf();
        bool saved = false;
        try {
            const Json j = Json::parse(ss.str());
            // A score that is not one of the table's defaults (50000 .. 10000).
            for (const auto& [song, t] : j["highscores"].o)
                for (const Json& d : t.a)
                    for (const Json& e : d.a) {
                        const int sc = int(e["score"].num());
                        if (sc > 0 && (sc % 10000 != 0 || sc > 50000)) {
                            saved = true;
                            std::printf("  save: %s: %s %s %d\n", ui.savePath().c_str(), song.c_str(), e["name"].str().c_str(), sc);
                        }
                    }
        } catch (const std::exception& e) {
            std::printf("  save: %s: %s\n", ui.savePath().c_str(), e.what());
        }
        if (!saved) {
            std::printf("  FAIL career.json has no score of the song\n");
            ok = false;
        }
    }
    // The port's band in every save: a fresh career.json has The Glitches.
    if (scenario_ == "career" || scenario_ == "career_full") {
        std::ifstream f(ui.savePath());
        std::stringstream ss;
        ss << f.rdbuf();
        int glitches = 0;
        try {
            const Json save = Json::parse(ss.str());
            for (const Json& p : save["profiles"].a)
                if (p["in_use"].truthy() && p["name"].str() == Career::kGlitches) ++glitches;
        } catch (const std::exception&) {
        }
        std::printf("  bands: %d named %s\n", glitches, Career::kGlitches);
        if (glitches != 1) std::printf("  FAIL career.json does not have The Glitches once\n"), ok = false;
        // GD02-090: The Glitches has everything unlocked, as saved: every
        // tier venue unlocked and completed and its songs unlocked at every
        // difficulty, every store song, every sold item bought, the cash.
        try {
            const Json save = Json::parse(ss.str());
            const auto& cfg = ui::campaign::config(ui);
            int missing = 0, entries = 0;
            for (const Json& p : save["profiles"].a) {
                if (!p["in_use"].truthy() || p["name"].str() != Career::kGlitches) continue;
                for (int d = 0; d < Career::kDifficulties; ++d) {
                    const Json& t = p["tour"].a.at(size_t(d));
                    for (int v = 0; v < ui::campaign::numTiers(ui, d); ++v) {
                        const auto& [venue, songs] = cfg.tiers.at(size_t(v));
                        ++entries, missing += t[venue]["unlocked"].truthy() && t[venue]["completed"].truthy() ? 0 : 1;
                        for (const std::string& song : songs) ++entries, missing += t[song]["unlocked"].truthy() ? 0 : 1;
                    }
                    for (const std::string& song : cfg.storeSongs) ++entries, missing += t[song]["unlocked"].truthy() ? 0 : 1;
                }
                for (const auto& [item, sold] : cfg.tableItems)
                    if (sold) ++entries, missing += p["items"][item].truthy() ? 0 : 1;
                if (p["cash"].num() < 1000000) ++missing;
            }
            if (!save["pro_faceoff"].truthy()) ++missing;
            std::printf("  The Glitches: %d of %d unlocks missing\n", missing, entries);
            if (missing || entries < 300) std::printf("  FAIL The Glitches does not have everything unlocked\n"), ok = false;
        } catch (const std::exception& e) {
            std::printf("  FAIL career.json: %s\n", e.what()), ok = false;
        }
    }
    // The career's look: career.json's first band at Medium (campaign
    // set_character_info), and player 0's config back from it.
    if (scenario_ == "career" || scenario_ == "career_full") {
        std::ifstream f(ui.savePath());
        std::stringstream ss;
        ss << f.rdbuf();
        std::string got;
        try {
            const Json c = Json::parse(ss.str())["profiles"].a.at(0)["characters"].a.at(1);
            got = c["character"].str() + " " + c["outfit"].str() + " " + c["guitar"].str() + " " + c["skin"].str();
        } catch (const std::exception& e) {
            got = e.what();
        }
        const ui::Session::PlayerCfg& p = ui.session().players[0];
        const std::string cfg = p.character + " " + p.outfit + " " + p.guitar + " " + p.skin;
        const std::string want = "alterna alterna2 sg sg_white";
        std::printf("  look: career.json %s, player 0 %s\n", got.c_str(), cfg.c_str());
        if (got != want || cfg != want) std::printf("  FAIL the look is not %s\n", want.c_str()), ok = false;
    }
    // career_full: the band's tour, cash and items in career.json.
    if (scenario_ == "career_full") {
        std::ifstream f(ui.savePath());
        std::stringstream ss;
        ss << f.rdbuf();
        std::string got;
        try {
            const Json p = Json::parse(ss.str())["profiles"].a.at(0);
            const Json& med = p["tour"].a.at(1);
            char buf[256];
            std::snprintf(buf, sizeof buf, "cash %d spent %d battle %s small1 %s salvation %d* encore %d* lp_cayenne %s", int(p["cash"].num()),
                          int(p["cash_spent"].num()), med["battle"]["completed"].truthy() ? "completed" : "-",
                          med["small1"]["unlocked"].truthy() ? "unlocked" : "-", int(med["salvation"]["stars"].num()),
                          int(med["shoutatthedevil"]["stars"].num()), p["items"]["lp_cayenne"].truthy() ? "bought" : "-");
            got = buf;
        } catch (const std::exception& e) {
            got = e.what();
        }
        const std::string want = "cash 200 spent 250 battle completed small1 unlocked salvation 3* encore 3* lp_cayenne bought";
        std::printf("  career.json: %s\n", got.c_str());
        if (got != want) std::printf("  FAIL career.json is not: %s\n", want.c_str()), ok = false;
    }
    // The endgame photo (GH2's quickplay): player 0's outfit, one of its four pictures, on the disc.
    if (scenario_ == "gh2_quickplay") {
        const auto& photos = ui.session().winnerPhotos;
        const std::string outfit = ui.session().players[0].outfit;
        const std::string p = photos.empty() ? std::string() : photos[0];
        const std::string stem = "image/og/photo_" + outfit;
        const bool shape = photos.size() == 1 && p.size() == stem.size() + 10 && p.compare(0, stem.size(), stem) == 0 &&
                           p[stem.size()] >= '0' && p[stem.size()] <= '3' && p.compare(stem.size() + 1, 9, "_keep.bmp") == 0;
        std::string ark = p;
        if (shape) ark = "ui/image/ng/gen/" + p.substr(9) + "_xbox";
        std::printf("  photo: %s (%s)\n", p.c_str(), ark.c_str());
        if (!shape || !ui.data().exists(ark)) std::printf("  FAIL the endgame photo is not one of %s's\n", outfit.c_str()), ok = false;
    }
    // coop: career.json keeps the co-op record and the award.
    if (scenario_ == "coop") {
        std::ifstream f(ui.savePath());
        std::stringstream ss;
        ss << f.rdbuf();
        int score = 0;
        bool award = false;
        try {
            const Json j = Json::parse(ss.str());
            score = int(j["coop"]["surrender"]["score"].num());
            award = j["coop_awards"]["bass_sg_mahogany"].truthy();
        } catch (const std::exception&) {
        }
        std::printf("  career.json: co-op surrender %d, bass_sg_mahogany %s\n", score, award ? "awarded" : "-");
        if (score <= 0 || !award) std::printf("  FAIL career.json does not keep co-op's record\n"), ok = false;
    }
    // The script reports.
    Interp& in = ui.interp();
    const auto wl = whitelist();
    // rb_fuzz checks the menus' panels; the script gaps of the flows it
    // wanders into (the band's matchmaking) are other walks' and listed only.
    const bool gapsFail = scenario_ != "rb_fuzz";
    const char* const gapTag = gapsFail ? "FAIL" : "note";
    for (const auto& [key, n] : in.unhandledCounts()) std::printf("  %s UNHANDLED %s (%d)\n", gapTag, key.c_str(), n), ok = ok && !gapsFail;
    for (const auto& [key, n] : in.unknownCounts()) std::printf("  %s UNKNOWN %s (%d)\n", gapTag, key.c_str(), n), ok = ok && !gapsFail;
    for (const auto& [key, n] : in.stubCounts()) {
        const bool w = whitelisted(key, wl) || !gapsFail;
        std::printf("  %s STUB %s (%d)\n", w ? "ok  " : "FAIL", key.c_str(), n);
        ok = ok && w;
    }
    for (const std::string& e : in.errors()) std::printf("  FAIL script error: %s\n", e.c_str()), ok = false;
    std::printf("walktest %s: %s\n", scenario_.c_str(), ok ? "OK" : "FAILED");
    if (SDL_getenv("GD05_WALK_KEEP_GOING")) return 0;  // a diagnostic run goes on (a restart's second half) whatever failed
    return ok ? 0 : 1;
}
