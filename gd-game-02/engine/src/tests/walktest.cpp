// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/walktest.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "data/game_data.h"
#include "data/json.h"
#include "render/scene_gpu.h"
#include "render/ui_view.h"
#include "scene/milo.h"
#include "script/tasks.h"
#include "ui/char_select.h"
#include "ui/natives.h"
#include "ui/ui_objects.h"
#include "ui/ui_host.h"
#include "ui/ui_manager.h"

extern const unsigned char kUiStubsTxt[];
extern const std::size_t kUiStubsTxtSize;

namespace {

constexpr int kX = ui::kPad_X, kTri = ui::kPad_Tri, kStart = ui::kPad_Start, kUp = ui::kPad_DUp, kDown = ui::kPad_DDown;
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

}  // namespace

WalkDriver::WalkDriver(const std::string& scenario) : scenario_(scenario) {
    using S = Step;
    auto screen = [&](const char* s, double timeout = 20000) { steps_.push_back({S::Screen, s, 0, timeout}); };
    auto press = [&](int b) { steps_.push_back({S::Press, "", b, 0}); };
    auto pressP = [&](int b, int player) { steps_.push_back({S::Press, "", b, 0, player}); };
    auto wait = [&](double ms) { steps_.push_back({S::Wait, "", 0, ms}); };
    // A real key press, through SDL's events and the input layer (as GD02_KEYS' kReturn).
    auto key = [&](SDL_Keycode k) { steps_.push_back({S::Key, "", int(k), 0}); };
    auto check = [&](const char* what, double timeout = 0) { steps_.push_back({S::Check, what, 0, timeout}); };
    // Each screen: wait until it is up and its transition is over, then a moment.
    auto at = [&](const char* s, double timeout = 20000) {
        screen(s, timeout);
        wait(400);
    };
    auto quickplaySong = [&](int downs) {
        at("main_screen");
        press(kX);  // QUICK PLAY (main_quickspin.btn, the focus)
        at("qp_selsong_screen");
        for (int i = 0; i < downs; ++i) press(kDown), wait(150);
        press(kX);
        at("qp_diff_screen");
        press(kUp);  // Medium (the last difficulty) -> Easy
        wait(150);
        press(kX);
        at("loading_screen");
        at("game_screen", 30000);
    };
    if (scenario == "quickplay") {
        endMs_ = 50000;  // long enough for a high score (the table's lowest is 10000)
        wait(300);
        press(kX);  // skips the three splashes (9 s otherwise)
        check("splashes_skipped", 1000);
        at("splash_screen", 30000);
        press(kX);  // any button
        at("startup_create_save_screen");
        press(kX);  // choose a device (create the save)
        at("saving_screen");
        at("guitar_help_screen");
        press(kX);
        quickplaySong(3);
        wait(3000);
        check("task_clock_song");
        at("endgame_screen", 90000);
        wait(600);
        press(kX);  // CONTINUE (me_continue.btn)
        at("highscore_screen");
        press(kStart);  // the name as it is
        wait(300);
        press(kX);
        at("complete_screen");
        press(kX);  // the song list (comp_selsong.btn)
        at("meta_loading_screen");
        at("saving_screen");
        at("qp_selsong_screen");
        press(kTri);  // back
        at("main_screen");
        expect_ = {"cut_scene_screen", "splash_screen", "startup_load_finder_screen", "startup_create_save_screen", "saving_screen",
                   "guitar_help_screen", "main_screen", "qp_selsong_screen", "qp_diff_screen", "loading_screen", "game_screen",
                   "post_show_screen", "endgame_screen", "highscore_screen", "complete_screen", "meta_loading_screen", "saving_screen",
                   "qp_selsong_screen", "main_screen"};
    } else if (scenario == "lose") {
        autoplay_ = false;
        failMs_ = 3000;
        quickplaySong(0);
        at("lose_screen", 30000);
        press(kDown), wait(150), press(kDown), wait(150);  // QUIT (lose_quit_normal.btn)
        press(kX);
        at("meta_loading_screen");
        at("main_screen");
        expect_ = {"main_screen", "qp_selsong_screen", "qp_diff_screen", "loading_screen", "game_screen", "lose_screen",
                   "meta_loading_screen", "main_screen"};
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
                   "game_screen", "pause_screen", "pause_confirm_restart_screen", "game_screen", "pause_screen",
                   "pause_confirm_quit_screen", "meta_loading_screen", "qp_selsong_screen"};
    } else if (scenario == "career" || scenario == "career_full") {
        at("splash_screen", 30000);
        press(kX);
        at("startup_create_save_screen");
        press(kX);
        at("saving_screen");
        at("guitar_help_screen");
        press(kX);
        at("main_screen");
        press(kUp);  // CAREER (main_career.btn)
        wait(150);
        press(kX);
        at("chooseprof_screen");  // a save always has a band (The Glitches, the last slot)
        press(kX);  // cp_band0.btn: NEW BAND
        at("nameprof_screen");
        press(kX);  // the band's name: "A"
        wait(150);
        key(SDLK_RETURN);  // the keyboard's Enter is Start here: finishes the name (adds no letter)
        at("sel_difficulty_screen");
        check("band_named_A");
        press(kX);  // Medium (the profile's last difficulty)
        at("sel_character_new_screen");
        wait(1500);  // the character opens the door
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
        at("tour_start_screen");
        press(kX);
        at("career_screen");
        expect_ = {"main_screen", "chooseprof_screen", "nameprof_screen", "sel_difficulty_screen", "sel_character_new_screen", "sel_guitar_new_screen",
                   "tour_start_screen", "saving_screen", "career_screen"};
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
        press(kX);  // Audio Settings (the focus)
        at("audio_settings_screen");
        press(kTri);
        at("options_screen");
        press(kTri);
        at("main_screen");
        check("camera_same");
        expect_ = {"main_screen", "options_screen", "audio_settings_screen", "options_screen", "main_screen"};
    } else if (scenario == "enhanced") {
        at("main_screen");
        for (int i = 0; i < 4; ++i) press(kDown), wait(150);  // OPTIONS (past MULTIPLAYER)
        press(kX);
        at("options_screen");
        press(kDown), wait(150), press(kDown), wait(150);  // Audio Settings -> Video Settings -> Enhanced Options
        press(kX);
        at("enhanced_options_screen");
        check("enhanced_default");
        // Gamepads (the focus) -> 4 controls -> calibration -> Screen Mode -> Max FPS -> FPS Counter -> Internal Resolution.
        for (int i = 0; i < 9; ++i) press(kDown), wait(120);
        press(kX);  // the window's own -> 448p (ps2)
        wait(300);
        check("enhanced_changed");
        press(kTri);
        at("options_screen");
        press(kTri);
        at("main_screen");
        expect_ = {"main_screen", "options_screen", "enhanced_options_screen", "options_screen", "main_screen"};
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
        expect_ = {"main_screen"};
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
        const char* results = scenario == "coop" ? "multi_compete_coop_screen" : scenario == "faceoff" ? "multi_compete_screen" : "multi_compete_fo_screen";
        at(results, 90000);
        wait(1500);
        if (scenario == "coop") check("coop_record");
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
    if (scenario_ == "lose" || scenario_ == "pause" || scenario_ == "back" || scenario_ == "enhanced" || scenario_ == "cheats")
        return "main_screen";
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
    // career: the band has bought Judy Nails' second outfit and the SG's
    // white skin in the store (M10), so both choices offer two.
    if ((scenario_ == "career" || scenario_ == "career_full") && !seeded_ && ui.currentScreen() == "sel_difficulty_screen" && ui.session().profileSlot >= 0) {
        seeded_ = true;
        Career::Profile& p = ui.career().profiles[size_t(ui.session().profileSlot)];
        p.items["alterna2"] = true;
        p.items["sg_white"] = true;
    }
    switch (s.kind) {
    case Step::Screen:
        if (ui.currentScreen() == s.screen && !ui.inTransition()) {
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
    case Step::Wait:
    case Step::Hold:
        if (nowMs - stepStart_ >= s.ms) ++step_, stepStart_ = -1;
        return;
    }
}

// The main menu's camera and scene: the camera's position plus the sum of
// every transform's world position in its view (the metacam moves the
// camera's rig and the walls with each transition).
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

void WalkDriver::runCheck(ui::UiManager& ui, const Step& s, double nowMs) {
    auto fail = [&](const std::string& e) { error_ = e, failed_ = done_ = true; };
    if (s.screen == "splashes_skipped") {
        if (!ui.inSplashes()) {
            SDL_Log("walk: splashes skipped (%.1f s)", nowMs / 1000);
            ++step_, stepStart_ = -1;
        } else if (nowMs - stepStart_ > s.ms) {
            fail("the splashes were not skipped by a button");
        }
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
    // The Enhanced Options: Internal Resolution at its default (the
    // window's own), then 448p after one select, saved in enhanced.json.
    if (s.screen == "enhanced_default" || s.screen == "enhanced_changed") {
        const std::string want = s.screen == "enhanced_default" ? "0" : "448";
        const std::string got = ui.host().enhancedValue("internal_res");
        std::printf("  enhanced: internal_res %s\n", got.c_str());
        if (got != want) return fail("Internal Resolution is " + got + ", not " + want);
        if (s.screen == "enhanced_changed") {
            std::ifstream f(std::filesystem::path(ui.savePath()).parent_path() / "enhanced.json");
            std::stringstream ss;
            ss << f.rdbuf();
            std::string saved;
            try {
                saved = Json::parse(ss.str())["internal_res"].str();
            } catch (const std::exception& e) {
                saved = e.what();
            }
            std::printf("  enhanced.json: internal_res %s\n", saved.c_str());
            if (saved != want) return fail("enhanced.json does not have the change");
        }
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
        // A screenshot now (GD02_WALK_SHOTS): the store's second page, the venue's unlock.
        if (const char* dir = SDL_getenv("GD02_WALK_SHOTS")) ui.host().saveScreenshot(std::string(dir) + "/" + s.screen.substr(5) + ".bmp");
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
    if (s.screen == "career_song_salvation") {
        // set_career_song (0x822B89B8): the venue's first unlocked song not passed yet.
        std::printf("  career: venue %s, song %s\n", ui.session().venue.c_str(), ui.session().song.c_str());
        if (ui.session().venue != "battle" || ui.session().song != "salvation") return fail("the career did not offer battle's salvation");
        ++step_, stepStart_ = -1;
        return;
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
    fail("unknown check " + s.screen);
}

int WalkDriver::finish(ui::UiManager& ui) {
    bool ok = !failed_ && done_;
    std::printf("walktest %s:\n", scenario_.c_str());
    if (!error_.empty()) std::printf("  FAIL %s\n", error_.c_str());
    if (!done_) std::printf("  FAIL the walk did not finish (stopped at step %zu of %zu)\n", step_, steps_.size());
    // The screens, in order.
    std::string seq;
    for (const std::string& s : ui.history()) seq += (seq.empty() ? "" : " > ") + s;
    std::printf("  screens: %s\n", seq.c_str());
    size_t k = 0;
    for (const std::string& s : ui.history())
        if (k < expect_.size() && s == expect_[k]) ++k;
    if (k < expect_.size()) {
        std::printf("  FAIL expected screen %s (in order) was not reached\n", expect_[k].c_str());
        ok = false;
    }
    // The save (quickplay): career.json with the song's high score.
    if (scenario_ == "quickplay") {
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
    if (scenario_ == "quickplay" || scenario_ == "career" || scenario_ == "career_full") {
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
    // The endgame photo (quickplay): player 0's outfit, one of its four pictures, on the disc.
    if (scenario_ == "quickplay") {
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
    for (const auto& [key, n] : in.unhandledCounts()) std::printf("  FAIL UNHANDLED %s (%d)\n", key.c_str(), n), ok = false;
    for (const auto& [key, n] : in.unknownCounts()) std::printf("  FAIL UNKNOWN %s (%d)\n", key.c_str(), n), ok = false;
    for (const auto& [key, n] : in.stubCounts()) {
        const bool w = whitelisted(key, wl);
        std::printf("  %s STUB %s (%d)\n", w ? "ok  " : "FAIL", key.c_str(), n);
        ok = ok && w;
    }
    for (const std::string& e : in.errors()) std::printf("  FAIL script error: %s\n", e.c_str()), ok = false;
    std::printf("walktest %s: %s\n", scenario_.c_str(), ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
