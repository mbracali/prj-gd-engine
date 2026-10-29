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
#include "game/player.h"
#include "input/input.h"
#include "render/scene_gpu.h"
#include "render/ui_view.h"
#include "scene/milo.h"
#include "script/tasks.h"
#include "ui/campaign.h"
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
    // A real key press, through SDL's events and the input layer (as GD03_KEYS' kReturn).
    auto key = [&](SDL_Keycode k) { steps_.push_back({S::Key, "", int(k), 0}); };
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
        check("intro_sequence");
        check("intro_camera");
        check("star_phrase");
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
        check("menu_sounds");  // game_outro_complete paused the game: menu sounds still play (GD03-073)
        expect_ = {"cut_scene_screen", "splash_screen", "startup_load_finder_screen", "startup_create_save_screen", "saving_screen",
                   "guitar_help_screen", "main_screen", "qp_selsong_screen", "qp_diff_screen", "loading_screen", "game_screen",
                   "post_show_screen", "endgame_screen", "highscore_screen", "complete_screen", "meta_loading_screen", "saving_screen",
                   "qp_selsong_screen", "main_screen"};
    } else if (scenario == "lose") {
        autoplay_ = false;
        failMs_ = 3000;
        songKeys_ = "1000:strum,2000:strum";  // two overstrums: GH2's miss sound each
        quickplaySong(0);
        check("miss_sound", 30000);
        at("lose_screen", 30000);
        press(kDown), wait(150), press(kDown), wait(150);  // QUIT (lose_quit_normal.btn)
        press(kX);
        at("meta_loading_screen");
        at("main_screen");
        check("menu_sounds");
        // After a song, Enhanced Options' backdrop still shows the band (GD03-074).
        wait(2000);  // the main menu's entrance (a press during it is lost)
        for (int i = 0; i < 4; ++i) press(kDown), wait(150);  // OPTIONS
        press(kX);
        at("options_screen");
        press(kDown), wait(150), press(kDown), wait(150);
        press(kX);
        at("enhanced_options_screen");
        check("backdrop_band");
        press(kTri);
        at("options_screen");
        press(kTri);
        at("main_screen");
        expect_ = {"main_screen", "qp_selsong_screen", "qp_diff_screen", "loading_screen", "game_screen", "lose_screen",
                   "meta_loading_screen", "main_screen", "options_screen", "enhanced_options_screen", "options_screen", "main_screen"};
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
        check("menu_sounds");
        expect_ = {"main_screen", "qp_selsong_screen", "qp_diff_screen", "loading_screen", "game_screen", "pause_screen",
                   "game_screen", "pause_screen", "pause_confirm_restart_screen", "game_screen", "pause_screen",
                   "pause_confirm_quit_screen", "meta_loading_screen", "qp_selsong_screen"};
    } else if (scenario == "career" || scenario == "career_full" || scenario == "career_song") {
        at("splash_screen", 30000);
        press(kX);
        at("startup_create_save_screen");
        press(kX);
        at("saving_screen");
        at("guitar_help_screen");
        press(kX);
        at("main_screen");
        if (scenario == "career_song" && SDL_getenv("GD03_WALK_ENHANCED")) {
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
            // meta_bricks once no panel holds it (GD03-077 / GD03-081).
            wait(800);
            check("bricks_record");
            press(kTri);
            at("chooseprof_screen");
            press(kX);  // cp_band0.btn: the band "A"
            at("sel_difficulty_screen");
            wait(800);
            check("bricks_same");
        }
        if (scenario == "career_song") {
            // GD03_WALK_DIFF=0..3 (default Medium, the focus).
            const char* d = SDL_getenv("GD03_WALK_DIFF");
            const int diff = d ? std::clamp(std::atoi(d), 0, 3) : 1;
            for (int i = diff; i < 1; ++i) press(kUp), wait(150);
            for (int i = 1; i < diff; ++i) press(kDown), wait(150);
        }
        press(kX);  // Medium (the profile's last difficulty)
        at("sel_character_new_screen");
        check("locked_portraits");
        wait(1500);  // the character opens the door
        if (scenario == "career_song" && SDL_getenv("GD03_WALK_CHAR")) {
            // GD03_WALK_CHAR=<downs> from Punk, GD03_WALK_GUITAR=<downs> from
            // the Les Paul; the first outfit / skin when there are two.
            const char* g = SDL_getenv("GD03_WALK_GUITAR");
            for (int i = std::atoi(SDL_getenv("GD03_WALK_CHAR")); i > 0; --i) press(kDown), wait(300);
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
            // One career song (GD03_WALK_SONG, default strutter) at
            // GD03_WALK_DIFF: every tier song before it passed with GH2's
            // cheat_beat_song (its venue unlocked), set_career_venue /
            // set_career_song offer it, it is played (autoplay; GD03_WALK_END
            // <song ms> ends it early) and its endgame screens followed back
            // towards the career menu.
            if (const char* e = SDL_getenv("GD03_WALK_END")) endMs_ = float(std::atof(e));
            check("career_target");
            press(kX);  // LET'S ROCK
            at("sel_venue_screen");
            press(kX);  // the venue (set_career_venue: the newest unlocked)
            at("sel_song_screen");
            check("career_song_target");
            press(kX);
            at("loading_screen");
            at("game_screen", 30000);
            if (const char* f = SDL_getenv("GD03_WALK_FAIL")) {
                // GD03_WALK_FAIL=<song ms>: the song fails then; RESTART (the
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
        // Gamepads (the focus) -> 4 controls -> the PERFORMANCE AND GRAPHICS
        // section -> calibration -> Screen Mode -> Max FPS -> FPS Counter ->
        // Internal Resolution; select opens its dropdown, down and select
        // pick the next option.
        for (int i = 0; i < 10; ++i) press(kDown), wait(120);
        press(kX);
        wait(200);
        press(kDown), wait(120);
        press(kX);  // the window's own -> 448p (ps2)
        wait(300);
        check("enhanced_changed");
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
        // In-Game Max FPS (the list's 13th row; its dropdown opens upward):
        // 60 is its first option, 85 units above the row.
        mouseAt(480, 592, 0);
        mouseAt(480, 592, 1);
        wait(200);
        mouseAt(480, 502, 0);
        mouseAt(480, 502, 1);
        wait(300);
        check("enhanced_fps_60");
        mouseAt(480, 592, 3);  // right click: back
        at("options_screen");
        wait(300);
        check("click_help1");  // the help bar's BACK
        at("main_screen");
        expect_ = {"main_screen", "options_screen", "enhanced_options_screen", "options_screen", "main_screen"};
    } else if (scenario == "gamepads") {
        at("main_screen");
        key(SDLK_3);  // the keyboard's yellow fret
        at("gamepads_screen", 3000);
        for (int i = 0; i < 11; ++i) press(kUp), wait(80);  // up from the keyboard wraps to its rows: Whammy
        press(kX);  // waits for a key
        wait(200);
        key(SDLK_E);
        wait(300);
        check("whammy_is_e");
        press(kTri);
        at("main_screen");
        wait(300);
        check("click_help1");  // the help bar's GAMEPADS
        at("gamepads_screen", 3000);
        expect_ = {"main_screen", "gamepads_screen", "main_screen", "gamepads_screen"};
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
    if (scenario_ == "lose" || scenario_ == "pause" || scenario_ == "back" || scenario_ == "enhanced" || scenario_ == "cheats" ||
        scenario_ == "mouse" || scenario_ == "gamepads" || scenario_ == "stickyaxis")
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
    if ((scenario_ == "career" || scenario_ == "career_full" || scenario_ == "career_song") && !seeded_ && ui.currentScreen() == "sel_difficulty_screen" && ui.session().profileSlot >= 0) {
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
        if (nowMs - stepStart_ >= s.ms) ++step_, stepStart_ = -1;
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

void WalkDriver::runCheck(ui::UiManager& ui, const Step& s, double nowMs) {
    auto fail = [&](const std::string& e) { error_ = e, failed_ = done_ = true; };
    auto pass = [&] { ++step_, stepStart_ = -1; };
    if (s.screen == "name_entry_1" || s.screen == "name_entry_2") {
        // BandTextEntry's Texts are aligned middle left (0x82144558): the
        // entered text starts at the same x whatever its length and the
        // picked character follows it (GD03-068).
        ui::PanelObj* fp = ui.focusPanel();
        const gfx::UiView::Hit *entered = nullptr, *picking = nullptr;
        if (fp && fp->view)
            for (const gfx::UiView::Hit& h : fp->view->hits()) {
                if (h.key.size() > 8 && h.key.compare(h.key.size() - 8, 8, "#entered") == 0) entered = &h;
                if (h.key.size() > 8 && h.key.compare(h.key.size() - 8, 8, "#picking") == 0) picking = &h;
            }
        if (!entered || !picking) return fail("the name entry drew no entered text or picked character");
        std::printf("  name entry: entered %.1f..%.1f, picking %.1f..%.1f\n", entered->x0, entered->x1, picking->x0, picking->x1);
        if (picking->x0 < entered->x1 - 1) return fail("the picked character overlaps the entered name");
        if (s.screen == "name_entry_1") nameX0_ = entered->x0;
        else if (std::fabs(entered->x0 - nameX0_) > 1) return fail("the name does not grow left to right (its start moved)");
        return pass();
    }
    if (s.screen == "locked_portraits") {
        // career.dta's enter: each locked character's portrait draws with
        // char_locked.mat, each unlocked one with char_<c>.mat (GD03-079).
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
        // the right (guidelines/universal_launcher.md, GD03-074).
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
    if (s.screen == "enhanced_fps_60") {
        const std::string v = ui.host().enhancedValue("max_fps");
        std::printf("  mouse: In-Game Max FPS %s\n", v.c_str());
        return v == "60" ? pass() : fail("clicking the dropdown's 60 did not set In-Game Max FPS");
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
    if (s.screen == "whammy_is_e") {
        bool bound = false;
        for (const Input::Source& src : ui.input().bindings("keyboard")[size_t(Input::Control::Whammy)])
            bound = bound || (src.kind == Input::Source::Kind::Key && src.index == int(SDLK_E));
        const char* dir = SDL_getenv("GD03_SAVE_DIR");
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
        std::printf("  gamepads: Whammy %s, controllers.json %s\n", bound ? "on E" : "not on E", saved ? "has it" : "does not");
        return bound && saved ? pass() : fail("the rebinding of the keyboard's Whammy to E did not take or was not saved");
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
        for (const auto& [name, at] : got) {
            if (at >= 300) break;
            std::printf("  intro sound %s at %.0f ms\n", name.c_str(), at);
            if (k >= std::size(want) || name != want[k].first || std::fabs(at - want[k].second) > 60)
                return fail("intro sound " + name + " not as GH2's (" + (k < std::size(want) ? want[k].first : "none") + ")");
            ++k;
        }
        if (k != std::size(want)) return fail("intro: " + std::to_string(k) + " of 7 sounds");
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
        // A screenshot now (GD03_WALK_SHOTS): the store's second page, the venue's unlock.
        if (const char* dir = SDL_getenv("GD03_WALK_SHOTS")) ui.host().saveScreenshot(std::string(dir) + "/" + s.screen.substr(5) + ".bmp");
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
    if (s.screen.rfind("confirm_on ", 0) == 0) {
        // A second choice (outfit / skin) still open on that screen: take the first.
        if (ui.currentScreen() == s.screen.substr(11) && !ui.inTransition()) ui.buttonDown(kX, 0), ui.buttonUp(kX, 0);
        ++step_, stepStart_ = -1;
        return;
    }
    if (s.screen == "career_target" || s.screen == "career_song_target") {
        const char* w = SDL_getenv("GD03_WALK_SONG");
        const std::string target = w ? w : "strutter";
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
            std::printf("  career: venue %s, song %s, difficulty %d\n", ui.session().venue.c_str(), ui.session().song.c_str(),
                        ui::campaign::difficulty(ui));
            if (ui.session().song != target) return fail("the career offered " + ui.session().song + ", not " + target);
        }
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
    if (s.screen == "setlist_encore") {
        // The locked encore's row (GD03-089): inactive, drawn without its
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
        // GD03-090: The Glitches has everything unlocked, as saved: every
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
    // The endgame photo (quickplay): player 0's outfit, one of its four pictures, on the disc.
    if (scenario_ == "quickplay") {
        const auto& photos = ui.session().winnerPhotos;
        const std::string outfit = ui.session().players[0].outfit;
        const std::string p = photos.empty() ? std::string() : photos[0];
        const std::string stem = "image/og/photo_" + outfit;
        const bool shape = photos.size() == 1 && p.size() == stem.size() + 10 && p.compare(0, stem.size(), stem) == 0 &&
                           p[stem.size()] >= '0' && p[stem.size()] <= '3' && p.compare(stem.size() + 1, 9, "_keep.bmp") == 0;
        std::string ark = p;
        if (shape) ark = "ui/image/og/gen/" + p.substr(9) + "_ps2";
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
