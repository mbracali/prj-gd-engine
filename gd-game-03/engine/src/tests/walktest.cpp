// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/walktest.h"

#include "devaids/audit.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "data/game_data.h"
#include "data/json.h"
#include "game/player.h"
#include "input/input.h"
#include "render/camera.h"
#include "render/scene_gpu.h"
#include "render/ui_view.h"
#include "scene/milo.h"
#include "script/tasks.h"
#include "ui/campaign.h"
#include "ui/char_select.h"
#include "ui/enhanced_panel.h"
#include "ui/natives.h"
#include "ui/ui_objects.h"
#include "ui/ui_host.h"
#include "ui/ui_manager.h"

extern const unsigned char kUiStubsTxt[];
extern const std::size_t kUiStubsTxtSize;

namespace {

constexpr int kX = ui::kPad_X, kTri = ui::kPad_Tri, kStart = ui::kPad_Start, kUp = ui::kPad_DUp, kDown = ui::kPad_DDown;
constexpr int kLeft = ui::kPad_DLeft, kRight = ui::kPad_DRight;
// long_cheats.dta's frets on this disc (the PS2 guitar): G_YELLOW kPad_Tri,
// G_BLUE kPad_X, G_ORANGE kPad_Square [C data].
constexpr int kYellow = ui::kPad_Tri, kBlue = ui::kPad_X, kOrange = ui::kPad_Square;

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

WalkDriver::WalkDriver(const std::string& name)
    : scenario_(name == "menu_anim_loops" ? "career_full" : name), loops_(name == "menu_anim_loops") {
    // menu_anim_loops: career_full with the looping-animation checks.
    const std::string scenario = scenario_;
    using S = Step;
    auto screen = [&](const char* s, double timeout = 20000) { steps_.push_back({S::Screen, s, 0, timeout}); };
    auto press = [&](int b) { steps_.push_back({S::Press, "", b, 0}); };
    auto pressP = [&](int b, int player) { steps_.push_back({S::Press, "", b, 0, player}); };
    auto wait = [&](double ms) { steps_.push_back({S::Wait, "", 0, ms}); };
    // A real key press, through SDL's events and the input layer (as GD03_KEYS' kReturn).
    auto key = [&](SDL_Keycode k) { steps_.push_back({S::Key, "", int(k), 0}); };
    // A key held down (down) or let go (!down) on its own.
    auto keyHold = [&](SDL_Keycode k, bool down) {
        Step st{S::Key, "", int(k), 0};
        st.x = down ? 1.0f : 2.0f;
        steps_.push_back(st);
    };
    auto check = [&](const char* what, double timeout = 0) { steps_.push_back({S::Check, what, 0, timeout}); };
    // A beat-fixed wait: "until" waits for `ms` after the last "mark".
    auto mark = [&] { steps_.push_back({S::Wait, "mark", 0, 0}); };
    auto until = [&](double ms) { steps_.push_back({S::Wait, "until", 0, ms}); };
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
    // The PS2 boot (ui/mem_card.dta, splash.dta): no save yet -> "create a
    // save?" (yes is the focus) -> saved -> the intro movie's screen -> the
    // title screen (campaign num_profiles > 0: the port's The Glitches is
    // always there, so the guitar help is skipped) -> any button.
    auto bootToMain = [&] {
        at("bootup_create_save_check", 30000);
        press(kX);
        at("bootup_create_save");
        at("cut_scene_screen", 20000);
        at("splash_screen");
        press(kX);
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
        endMs_ = 60000;  // long enough for a high score (the table's lowest is 10000)
        // Star Power deployed once two phrases filled half the meter (20.8 s
        // and 35.2 s on Easy): claps on its beats, sp_depleted as it runs out.
        songKeys_ = "45000:starpower";
        wait(300);
        press(kX);  // skips the three splashes (9 s otherwise)
        check("splashes_skipped", 1000);
        bootToMain();
        quickplaySong(3);
        wait(3000);
        check("task_clock_song");
        check("intro_sequence");
        check("intro_camera");
        check("star_phrase");
        check("track_surface");
        at("endgame_screen", 90000);
        check("song_sounds");
        wait(600);
        press(kX);  // CONTINUE (me_continue.btn)
        at("highscore_screen");
        press(kStart);  // the name as it is
        wait(300);
        press(kX);
        at("complete_screen");
        press(kX);  // the song list (comp_selsong.btn)
        at("meta_loading_screen");
        at("autosave");
        at("qp_selsong_screen");
        press(kTri);  // back
        at("main_screen");
        check("menu_sounds");  // game_outro_complete paused the game: menu sounds still play (GD03-073)
        expect_ = {"bootup_load", "bootup_create_save_check", "bootup_create_save", "cut_scene_screen",
                   "splash_screen", "main_screen", "qp_selsong_screen", "qp_diff_screen", "loading_screen", "game_screen",
                   "post_show_screen", "endgame_screen", "highscore_screen", "complete_screen", "meta_loading_screen", "autosave",
                   "qp_selsong_screen", "main_screen"};
    } else if (scenario == "lose") {
        autoplay_ = false;
        failMs_ = 5000;  // past bangyourhead's first big note (beat 8), its gem passed unhit
        songKeys_ = "1000:strum,2000:strum";  // two overstrums: GH2's miss sound each
        quickplaySong(0);
        check("miss_sound", 30000);
        at("lose_screen", 30000);
        check("big_note_blown");
        press(kDown), wait(150), press(kDown), wait(150);  // QUIT (lose_quit_normal.btn)
        press(kX);
        at("meta_loading_screen");
        at("main_screen");
        check("menu_sounds");
        // After a song, Enhanced Options' backdrop still shows the band (GD03-074).
        wait(2000);  // the main menu's entrance (a press during it is lost)
        for (int i = 0; i < 3; ++i) press(kDown), wait(150);  // OPTIONS (this disc: CAREER, QUICK PLAY, MULTIPLAYER, TRAINING, OPTIONS)
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
        press(kDown);  // RESTART (this disc restarts at once: no confirm screen, ui/pause.dta)
        wait(150);
        press(kX);
        at("game_screen");
        wait(2500);
        press(kStart);
        at("pause_screen");
        press(kUp);  // QUIT (up from RESUME; quits at once)
        wait(150);
        press(kX);
        at("meta_loading_screen");
        at("qp_selsong_screen");
        check("menu_sounds");
        expect_ = {"main_screen", "qp_selsong_screen", "qp_diff_screen", "loading_screen", "game_screen", "pause_screen",
                   "game_screen", "pause_screen", "game_screen", "pause_screen", "meta_loading_screen", "qp_selsong_screen"};
    } else if (scenario == "flaming_head") {
        // GD03-048 / GD03-045: the Flaming Head code at the main menu, a
        // song: the guitarist's cheat_headflames (char/shared/og) burns;
        // then the Horse Head code and a song: the band wear their
        // <char>_horse.milo (which have no cheat_headflames: no flames).
        auto code = [&](std::initializer_list<int> buttons) {
            for (int b : buttons) key(b == kYellow ? SDLK_3 : b == kBlue ? SDLK_4 : SDLK_5), wait(120);
        };
        auto quit = [&] {
            press(kStart);
            at("pause_screen");
            press(kUp);  // QUIT
            wait(150);
            press(kX);
            at("meta_loading_screen");
            at("qp_selsong_screen");
        };
        at("main_screen");
        code({kYellow, kOrange, kYellow, kOrange, kYellow, kOrange, kBlue, kOrange});  // Flaming Head
        wait(2000);
        quickplaySong(0);
        wait(3000);
        check("band_flames");
        check("shot_flaming_head");  // GD03_WALK_SHOTS=<dir>: the burning head
        quit();
        press(kTri);
        at("main_screen");
        wait(2000);
        code({kBlue, kOrange, kOrange, kBlue, kYellow, kBlue, kOrange, kOrange, kBlue, kYellow});  // Horse Head
        check("cheat_horse_on");
        wait(2000);
        quickplaySong(0);
        wait(3000);
        check("band_horse");
        quit();
        expect_ = {"main_screen", "qp_selsong_screen", "qp_diff_screen", "loading_screen", "game_screen", "pause_screen",
                   "meta_loading_screen", "qp_selsong_screen", "main_screen", "qp_selsong_screen", "qp_diff_screen",
                   "loading_screen", "game_screen", "pause_screen", "meta_loading_screen", "qp_selsong_screen"};
    } else if (scenario == "whammy_tail") {
        // A held sustain's tail swells with the whammy bar and goes back when
        // it is let go (0x00158e50, Owner 10-30): the analog bar (the value
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
        press(kUp);  // QUIT
        wait(150);
        press(kX);
        at("meta_loading_screen");
        at("qp_selsong_screen");
        expect_ = {"main_screen", "qp_selsong_screen", "qp_diff_screen", "loading_screen", "game_screen", "pause_screen",
                   "meta_loading_screen", "qp_selsong_screen"};
    } else if (scenario == "career" || scenario == "career_full" || scenario == "career_song" || scenario == "career_encore" ||
               scenario == "career_win" || scenario == "career_win_medium" || scenario == "career_practice") {
        bootToMain();
        at("main_screen");
        if (scenario == "career_song" && SDL_getenv("GD03_WALK_ENHANCED")) {
            // Enhanced Options first (its backdrop loads a venue, then frees it).
            for (int i = 0; i < 3; ++i) press(kDown), wait(150);  // OPTIONS (this disc: CAREER, QUICK PLAY, MULTIPLAYER, TRAINING, OPTIONS)
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
            for (int i = 0; i < 3; ++i) press(kUp), wait(400);  // OPTIONS -> QUICK PLAY
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
        if (scenario == "career_win") press(kUp), wait(150);  // Medium -> Easy
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
        if (loops_) {
            // GD03-042: animate_forever_30fps sel_character.view, and
            // char_highlight.mnm (loop 0 3) (period 0.25) (career.dta enter).
            check("loop_record");
            wait(400);
            check("loop_advances|sel_character_panel sel_character.view|sel_character_panel char_highlight.mnm");
        }
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
        // This disc: one outfit per character (config/gh2.dta characters),
        // a new band owns each guitar's first skin only.
        press(kDown);  // Punk -> Alterna (Judy Nails)
        wait(300);
        press(kX);
        wait(800);
        at("sel_guitar_new_screen");
        press(kDown);  // the second guitar
        wait(300);
        if (loops_) {
            // The guitar's turn: GuitarDisplayPanel show_guitar ... guitar_single.filt.
            check("loop_record");
            wait(400);
            check("loop_advances|sel_guitar_panel guitar_single.filt");
        }
        press(kX);
        wait(600);
        }
        at("tour_start_screen");
        press(kX);
        at("career_screen");
        expect_ = {"main_screen", "chooseprof_screen", "nameprof_screen", "autosave", "sel_difficulty_screen", "sel_character_new_screen",
                   "sel_guitar_new_screen", "tour_start_screen", "autosave", "career_screen"};
        if (scenario == "career")
            expect_ = {"main_screen",           "chooseprof_screen",        "nameprof_screen",       "autosave",
                       "sel_difficulty_screen", "chooseprof_screen",        "sel_difficulty_screen", "sel_character_new_screen",
                       "sel_guitar_new_screen", "tour_start_screen",        "autosave",              "career_screen"};
        if (scenario == "career_song") {
            // One career song (GD03_WALK_SONG, default ballstothewall) at
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
        if (scenario == "career_win" || scenario == "career_win_medium") {
            // The tour won (GD03-023): every tier song before the tour's last
            // passed with cheat_beat_song, the last one offered and played
            // (cut short, a win). final_song (0x00132930): the last tier's
            // song that completes it; at Easy the tour ends with fest
            // (0x00130c58: one tier less) and its encore is never unlocked
            // (0x00131c70), so it is losangeles; above Easy arena's encore
            // playwithme [C]. win_game_screen: beat_easy / beat_other and
            // the crowd's crowd_win, then pro face-off's unlock once all
            // six tiers are complete (0x0012f130), the results.
            const bool easy = scenario == "career_win";
            winTarget_ = easy ? "losangeles" : "playwithme";
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
            // career_full's tier to its encore offer (iran played), declined:
            // the venue's encore_no (endgame.dta no.btn), then the results.
            endMs_ = 35000;
            check("career_seed");
            press(kX);  // LET'S ROCK
            at("sel_venue_screen");
            press(kX);
            at("sel_song_screen");
            wait(900);
            check("career_song_iran");
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
            check("career_song_iran");
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
            if (loops_) {
                // endgame.dta: unlock_anim.grp animate (range 0 200) (period 3) on TRANSITION_COMPLETE_MSG.
                check("loop_record");
                wait(300);
                check("loop_advances|unlock_venue_panel unlock_anim.grp");
            }
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
            if (loops_) {
                // The finish's guitar turning (store_guitar_display_panel show_guitar ... guitar_single.filt).
                check("loop_record");
                wait(400);
                check("loop_advances|store_panel guitar_single.filt");
            }
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
        for (int i = 0; i < 3; ++i) press(kDown), wait(150);  // QUICK PLAY -> MULTIPLAYER -> TRAINING -> OPTIONS
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
        for (int i = 0; i < 3; ++i) press(kDown), wait(150);
        press(kX);
        at("options_screen");
        wait(800);
        check("bricks_same");
        press(kTri);
        at("main_screen");
        expect_ = {"main_screen", "options_screen", "audio_settings_screen", "options_screen", "main_screen", "options_screen", "main_screen"};
    } else if (scenario == "enhanced") {
        at("main_screen");
        for (int i = 0; i < 3; ++i) press(kDown), wait(150);  // OPTIONS (past MULTIPLAYER)
        press(kX);
        at("options_screen");
        press(kDown), wait(150), press(kDown), wait(150);  // Audio Settings -> Video Settings -> Enhanced Options
        press(kX);
        at("enhanced_options_screen");
        check("menu_instant");  // no transition: on the next frame (universal_mechanisms.md 1.2)
        check("enhanced_default");
        check("enhanced_all_off");
        check("backdrop_band");
        wait(500);  // a few frames through the default look
        check("aspect_43");
        check("enhanced_fx_default");
        // GD03-043 / -047: HDR None with every effect off draws the venue
        // exactly as before the effects were ported (the old build's frame,
        // compared once by hand; here: the frame back to identical after
        // HDR Emulated and Bloom 2x, which change it).
        check("frame_record");
        check("fx_set_on");
        wait(400);
        check("frame_differs");
        check("fx_set_off");
        wait(400);
        check("frame_same");
        // Each Enhanced Option in turn (universal_mechanisms.md 1.2.2, as
        // gd02's walk): toggled or stepped with the pad, saved, and its
        // effect checked. A step: down, then a press.
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
        // CONTROLS: past the modes -> Gamepads -> Mouse (kept) -> Touch on.
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
        // PERFORMANCE & GRAPHICS -> Screen Mode -> Resolution; select opens
        // its dropdown, down and select pick the next option.
        for (int i = 0; i < 3; ++i) press(kDown), wait(120);
        press(kX);
        wait(200);
        press(kDown), wait(120);
        press(kX);  // 448p (Good Old Days') -> 480p
        wait(300);
        check("enhanced_changed");
        // Widescreen (16:9): on (the picture fills the frame), then off
        // again (4:3 with its bars).
        row(kX), wait(300);
        check("aspect_169");
        again(kX), wait(300);
        check("aspect_43");
        // From the original look: Sharper Text on and off again, FXAA, FSR 1
        // (Upscaling Quality appears) at Performance.
        row(kX), again(kX), row(kRight), row(kRight), row(kRight);
        // Max FPS: Unlimited (up from 60 wraps), then 60 again; FPS Counter kept.
        press(kDown), wait(120);
        press(kX), wait(150), press(kUp), wait(120), press(kX), wait(300);
        check("enhanced_fps_unlimited");
        press(kX), wait(150), press(kDown), wait(120), press(kX), wait(300);
        check("enhanced_fps_60");
        press(kDown), wait(120);
        // HDR (universal_mechanisms.md "HDR"): Emulated, True HDR (the window
        // was made without an HDR surface: the documented fallback to
        // Emulated, with why), None.
        row(kRight), wait(400);
        check("enhanced_hdr_emulated");
        again(kRight), wait(400);
        check("enhanced_hdr_true");
        again(kRight), wait(400);
        check("enhanced_hdr_none");
        // Bokeh None, Particles None, Bloom 2x, Cinematic Color on, Color
        // Grading Warm, Film Grain Medium, Chromatic Aberration Light, Ambient
        // Occlusion Medium, Sharpening High, Blur on Star Power Medium.
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
        // Remove All Effects on: every effect off, the game's own too; then off again.
        row(kX);
        wait(400);
        check("enhanced_no_effects");
        press(kX), wait(400);
        check("enhanced_effects_back");
        // CHEATS & ENHANCERS: Export User Files, Load User Settings, Load
        // User Progress (GD03_DIALOG: the walk's folder).
        press(kDown), wait(120);
        row(kX);
        wait(300);
        check("enhanced_exported");
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
    } else if (scenario == "mouse") {
        at("main_screen");
        wait(300);  // a frame drawn: the components' bounds are known
        mouseOn("main_options.btn", 0);
        wait(200);
        check("focus_main_options");
        check("options_hit");  // where OPTIONS is drawn in the 4:3 picture
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
        // MODES collapsed (a click on its title): Resolution is the list's
        // 11th row; its dropdown opens upward, 720p 180 units above the row.
        mouseAt(300, 190, 0);
        mouseAt(300, 190, 1);
        wait(300);
        mouseAt(480, 523, 0);
        mouseAt(480, 523, 1);
        wait(200);
        mouseAt(480, 343, 0);
        mouseAt(480, 343, 1);
        wait(300);
        check("enhanced_res_720");
        // Widescreen (16:9), the next row (cut off at the list's bottom): the
        // wheel down one row scrolls it into view (row 530..564), then clicked.
        mouseAt(480, 523, 5);
        wait(600);
        mouseAt(480, 547, 0);
        mouseAt(480, 547, 1);
        wait(300);
        check("widescreen_on");
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
        check("click_help1");  // the help bar's BACK, drawn in the 16:9 picture now
        at("main_screen");
        wait(300);
        check("options_hit");  // OPTIONS is where it was: the hits follow the picture
        mouseOn("main_options.btn", 0);
        wait(200);
        check("focus_main_options");
        expect_ = {"main_screen", "options_screen", "enhanced_options_screen", "options_screen", "main_screen"};
    } else if (scenario == "gamepads") {
        at("main_screen");
        check("gp_p1_keyboard");  // the keyboard is player 1 (whatever pads this machine has)
        key(SDLK_3);  // the keyboard's yellow fret
        at("gamepads_screen", 3000);
        check("menu_instant");  // no transition: on the next frame (universal_mechanisms.md 1.2)
        // The Gamepad menu (universal_mechanisms.md 1.2.1): Player 1 ->
        // Instrument -> Bind to -> Customize inputs, then INPUTS MAPPING.
        for (int i = 0; i < 3; ++i) press(kDown), wait(80);
        press(kX), wait(200);
        check("gp_editing");
        for (int i = 0; i < 13; ++i) press(kDown), wait(60);  // Start .. Whammy (the headings are skipped)
        press(ui::kPad_DRight), wait(80);  // Key 2
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
        bootToMain();
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
        key(SDLK_RETURN);
        at("sel_difficulty_screen");
        check("band_named_CD");
        expect_ = {"bootup_load", "bootup_create_save_check", "bootup_create_save", "cut_scene_screen", "splash_screen", "main_screen",
                   "chooseprof_screen", "nameprof_screen", "sel_difficulty_screen"};
    } else if (scenario == "logo_skip") {
        // Logos and credits skip (universal_mechanisms.md 1.1.1): the boot
        // splashes by the red fret (2), the credits by the green fret (1),
        // then again by a click.
        wait(300);
        key(SDLK_2);
        check("splashes_skipped", 1000);
        bootToMain();
        at("main_screen");
        for (int i = 0; i < 3; ++i) press(kDown), wait(150);  // OPTIONS (past MULTIPLAYER)
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
        expect_ = {"bootup_load", "bootup_create_save_check", "bootup_create_save", "cut_scene_screen", "splash_screen", "main_screen",
                   "options_screen", "credits_screen", "options_screen", "credits_screen", "options_screen"};
    } else if (scenario == "menu_anim") {
        // The menus' own animations (GD03-026, Encore's code): a button that
        // gains focus grows to the panel's focus_scale over 0.1 s
        // (button.tnm), and a screen change turns the shared metacam
        // (meta_cam_pause_right_out.tnm out, meta_cam_right_in.tnm in, 0.5 s
        // each; back: right_in and right_out reversed).
        at("main_screen");
        wait(2500);  // the main menu's entrance over
        press(kDown);  // QUICK PLAY -> MULTIPLAYER
        check("focus_anim_start");
        wait(300);
        check("focus_anim_end");
        // GD03-044: main.milo's focus_jitter_interval 0.1: the focused
        // button's letters jitter anew every 0.1 s once it has grown.
        check("jitter_record");
        wait(350);
        check("jitter_advances");
        press(kDown), wait(150), press(kDown), wait(300);  // TRAINING, OPTIONS
        press(kX);
        wait(200);
        check("metacam_out_forward");
        check("fade_mid");  // GHScreen's exit fade to black (0 -> 1 over 0.5 s)
        wait(500);  // the entrance's first half
        check("metacam_in_forward");
        check("fade_mid");  // the entrance's fade from black
        at("options_screen");
        wait(300);
        check("metacam_rest");
        check("fade_clear");
        press(kTri);
        wait(200);
        check("metacam_out_back");
        check("fade_mid");
        wait(500);
        check("metacam_in_back");
        at("main_screen");
        wait(300);
        check("metacam_rest");
        check("fade_clear");  // main's enter turn_off_ambient, then its entrance's fade to 0
        expect_ = {"main_screen", "options_screen", "main_screen"};
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
        // The codes at the main menu (config/long_cheats.dta), a press every 120 ms.
        // Entered with the keyboard's frets (1..5) through the input layer,
        // as a player does: yellow 3, blue 4, orange 5.
        auto code = [&](std::initializer_list<int> buttons) {
            for (int b : buttons) key(b == kYellow ? SDLK_3 : b == kBlue ? SDLK_4 : SDLK_5), wait(120);
        };
        at("main_screen");
        code({kYellow, kBlue, kYellow, kOrange, kBlue, kBlue});  // Air Guitar (this disc's config/long_cheats.dta)
        check("cheat_air_on");
        code({kYellow, kBlue, kOrange, kOrange, kBlue, kYellow, kYellow, kOrange});  // Hyper Speed (this disc's)
        check("cheat_hyper_on");
        wait(900);  // more than 750 ms: the next press starts afresh
        code({kYellow, kBlue, kYellow, kOrange, kBlue, kBlue});  // Air Guitar again: off
        check("cheat_air_off");
        wait(900);
        code({kBlue, kOrange, kOrange, kBlue, kYellow, kBlue, kOrange, kOrange, kBlue, kYellow});  // Horse Head (Encore's own)
        check("cheat_horse_on");
        expect_ = {"main_screen"};
    } else if (scenario == "coop" || scenario == "faceoff" || scenario == "proface") {
        endMs_ = 20000;
        bootToMain();
        at("main_screen");
        if (scenario == "proface") check("proface_seed");
        press(kDown);  // QUICK PLAY -> MULTIPLAYER
        wait(150);
        press(kX);
        // This disc has no controller select: pad 0 is P1, pad 1 is P2.
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
            press(kX);  // the first song
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
        if (scenario == "coop") at("autosave", 20000);
        const char* again = scenario == "coop" ? "multi_coop_selsong_screen" : scenario == "faceoff" ? "multi_vs_selsong_screen" : "multi_fo_selsong_screen";
        at(again, 30000);
        expect_ = {"main_screen", "multi_screen"};
        if (scenario == "coop")
            expect_.insert(expect_.end(), {"multi_coop_venue_screen", "multi_coop_selsong_screen", "selpart_screen", "multi_coop_seldiff_screen",
                                           "multi_sel_character_screen", "multi_sel_guitar_screen", "loading_screen", "game_screen",
                                           "multi_compete_coop_screen", "multi_unlock_bass_screen", "complete_screen", "autosave", "multi_coop_selsong_screen"});
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
    } else if (scenario == "options") {
        // The audio settings (options.dta AUDIO_SETTINGS_PANEL_HANDLERS):
        // each slider selected, moved one step (its sound at the new
        // volume: slider_band / slider_gtr / slider_sfx) and confirmed,
        // then saved on the way back; the lag calibration (lag_panel)
        // from the video settings: its countdown, the clicks
        // (sync_click.cue) struck along, its result, and back.
        at("main_screen");
        wait(2000);  // the main menu's entrance (a press during it is lost)
        for (int i = 0; i < 3; ++i) press(kDown), wait(150);  // OPTIONS
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
        scenario_ == "mouse" || scenario_ == "gamepads" || scenario_ == "gamepads_pad" || scenario_ == "menu_anim" || scenario_ == "stickyaxis" || scenario_ == "practice" ||
        scenario_ == "practice_menus" || scenario_ == "tutorial" || scenario_ == "tutorial2" || scenario_ == "tutorial3" || scenario_ == "options" || scenario_ == "whammy_tail" ||
        scenario_ == "flaming_head")
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

void WalkDriver::runCheck(ui::UiManager& ui, const Step& s, double nowMs) {
    auto fail = [&](const std::string& e) { error_ = e, failed_ = done_ = true; };
    auto pass = [&] { ++step_, stepStart_ = -1; };
    if (s.screen == "jitter_record" || s.screen == "jitter_advances") {
        ui::PanelObj* fp = ui.focusPanel();
        if (!fp || !fp->view) return fail("no focus panel with a view");
        const uint32_t n = fp->view->buttonJitter(fp->focus);
        std::printf("  focus jitter: %s re-jittered %u times\n", fp->focus.c_str(), n);
        if (s.screen == "jitter_record") return jitter0_ = n, pass();
        return n >= jitter0_ + 2 ? pass() : fail("the focused button's letters did not re-jitter every focus_jitter_interval");
    }
    if (s.screen == "fade_mid" || s.screen == "fade_clear") {
        const float f = ui.screenFade();
        std::printf("  screen fade (%s): %.2f\n", ui.currentScreen().c_str(), double(f));
        if (s.screen == "fade_mid") return f > 0.05f && f < 0.95f ? pass() : fail("no fade to / from black under way");
        return f == 0 ? pass() : fail("the screen is still dimmed");
    }
    if (s.screen == "loop_record") {
        loops0_ = ui.upPanelFrames();
        return pass();
    }
    if (s.screen.rfind("loop_advances|", 0) == 0) {
        // "loop_advances|<panel> <object>|...": each one's frame moved since
        // loop_record (a looping animation of the screen still plays).
        const std::map<std::string, float> now = ui.upPanelFrames();
        size_t at = s.screen.find('|');
        while (at != std::string::npos) {
            const size_t next = s.screen.find('|', at + 1);
            const std::string key = s.screen.substr(at + 1, next == std::string::npos ? std::string::npos : next - at - 1);
            at = next;
            auto a = loops0_.find(key);
            auto b = now.find(key);
            std::printf("  loop %s: %.2f -> %.2f\n", key.c_str(), a == loops0_.end() ? -1.0 : double(a->second),
                        b == now.end() ? -1.0 : double(b->second));
            if (b == now.end()) return fail(key + " never animated");
            if (a != loops0_.end() && a->second == b->second) return fail(key + " stopped");
        }
        return pass();
    }
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
        // The glyphs' boxes may touch: allow 10% of the name's width (the
        // camera zooms in on a short name: zoom_tnm animates to its length).
        if (picking->x0 < entered->x1 - std::max(1.0f, 0.1f * (entered->x1 - entered->x0)))
            return fail("the picked character overlaps the entered name");
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
        for (const char* c : {"punk", "alterna", "glam", "goth", "metal", "grim"}) {  // config/gh2.dta characters
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
    if (s.screen == "widescreen_on") {
        const std::string v = ui.host().enhancedValue("widescreen");
        std::printf("  mouse: Widescreen %s, the picture %.3f\n", v.c_str(), double(gfx::pictureAspect()));
        return v == "on" ? pass() : fail("clicking Widescreen's check box did not turn it on");
    }
    // GD03-016: the main menu's OPTIONS is drawn at the same place of the
    // window in 4:3 and 16:9 (the menu camera keeps its height), so its
    // hit box in menu units must be the same in both, and over the poster's
    // middle (where it is drawn) rather than spread to the 16:9 width.
    if (s.screen == "options_hit") {
        ui::PanelObj* fp = ui.focusPanel();
        const gfx::UiView::Hit* hit = nullptr;
        if (fp && fp->view)
            for (const gfx::UiView::Hit& h : fp->view->hits())
                if (h.key == "main_options.btn") hit = &h;
        if (!hit) return fail("OPTIONS was not drawn");
        const float cx = (hit->x0 + hit->x1) * 0.5f, cy = (hit->y0 + hit->y1) * 0.5f;
        std::printf("  mouse: OPTIONS at %.0f..%.0f x %.0f..%.0f (picture %.3f)\n", double(hit->x0), double(hit->x1), double(hit->y0),
                    double(hit->y1), double(gfx::pictureAspect()));
        if (cx < 560 || cx > 720) return fail("OPTIONS' hit box is not where the button is drawn");
        if (optionsHit_[0] < 0) optionsHit_ = {cx, cy};
        else if (std::fabs(cx - optionsHit_[0]) > 2 || std::fabs(cy - optionsHit_[1]) > 2) return fail("OPTIONS' hit box moved with the aspect");
        return pass();
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
    if (s.screen == "focus_anim_start" || s.screen == "focus_anim_end") {
        ui::PanelObj* fp = ui.focusPanel();
        if (!fp || !fp->view) return fail("no focus panel with a view");
        const float scale = fp->view->buttonScaleNow(fp->focus);
        std::printf("  menu anim: %s at %.4f\n", fp->focus.c_str(), double(scale));
        if (s.screen == "focus_anim_start") {
            focusScale0_ = scale;
            return scale >= 0.999f && scale < 1.04f ? pass() : fail("the newly focused button did not start its grow from 1");
        }
        return scale > focusScale0_ + 0.01f && scale > 1.01f ? pass() : fail("the focused button did not grow to its focus scale");
    }
    if (s.screen.rfind("metacam_", 0) == 0) {
        const float out = ui.metacamFrame("meta_cam_pause_right_out.tnm"), in = ui.metacamFrame("meta_cam_right_in.tnm"),
                    back = ui.metacamFrame("meta_cam_right_out.tnm");
        std::printf("  metacam: pause_right_out %.0f, right_in %.0f, right_out %.0f, in transition %d\n", double(out), double(in), double(back),
                    ui.inTransition());
        const auto mid = [](float f) { return f > 0 && f < 500; };
        if (s.screen == "metacam_out_forward") return mid(out) ? pass() : fail("no forward exit (meta_cam_pause_right_out.tnm) under way");
        if (s.screen == "metacam_in_forward") return mid(in) ? pass() : fail("no forward entrance (meta_cam_right_in.tnm) under way");
        if (s.screen == "metacam_out_back") return mid(in) ? pass() : fail("no backward exit (meta_cam_right_in.tnm reversed) under way");
        if (s.screen == "metacam_in_back") return mid(in) ? pass() : fail("no entrance after the backward exit (meta_cam_right_in.tnm) under way");
        return !ui.inTransition() && in == 500 && out < 0 && back < 0 ? pass() : fail("the metacam is not at rest (meta_cam_right_in.tnm 500)");
    }
    if (s.screen == "menu_instant") return !ui.inTransition() ? pass() : fail("a universal menu opened through a screen transition");
    if (s.screen == "gp_p1_keyboard") {
        ui.input().setPlayer("keyboard", 0);
        // Touch off (the desktop default, whatever the walk's save says), so
        // the keyboard's rows end as on a desktop (no TOUCH SCREEN section).
        ui.input().setTouch(false);
        return pass();
    }
    if (s.screen == "gp_editing") return ui.input().editing() ? pass() : fail("Customize inputs did not start an edit");
    if (s.screen == "gp_detecting") return ui.input().detecting() ? pass() : fail("Bind to > Detect did not wait for a device");
    if (s.screen == "vpad_attach") {
        adapter_ = Input::attachVirtualPad("045e:028e");
        return adapter_ ? pass() : fail("no virtual pad");
    }
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
        std::printf("  gamepads: Whammy %s, controllers.json %s\n", bound ? "on Shift / E" : "not on Shift / E", saved ? "has it" : "does not");
        if (s.screen == "whammy_e_unsaved")
            return bound && !saved && ui.input().editing() ? pass() : fail("Key 2 of Whammy did not take at once, or was saved before Save");
        return bound && saved && !ui.input().editing() ? pass() : fail("the rebinding of the keyboard's Whammy to E did not take or was not saved");
    }
    if (s.screen == "cheat_menu_monkey") {
        const bool monkey = ui.cheatOn("cheat_monkey_head_crowd");
        std::printf("  cheats (Enhanced Options): monkey %d\n", monkey);
        return monkey ? pass() : fail("Monkey Head Crowd did not turn on from the menu");
    }
    if (s.screen.rfind("enhanced_mode_", 0) == 0 || s.screen == "enhanced_res_720" || s.screen == "enhanced_fps_unlimited" ||
        s.screen == "enhanced_exported" || s.screen == "enhanced_settings_loaded" || s.screen == "enhanced_progress_loaded") {
        // The universal Enhanced menu (universal_mechanisms.md 1.2.2): what
        // is in effect, in enhanced.json and the panel's status line.
        Json saved = Json::object();
        {
            std::ifstream f(std::filesystem::path(ui.savePath()).parent_path() / "enhanced.json");
            std::stringstream ss;
            ss << f.rdbuf();
            try {
                saved = Json::parse(ss.str());
            } catch (const std::exception&) {
            }
        }
        std::string problems;
        auto add = [&](const std::string& p) { problems += (problems.empty() ? "" : "; ") + p; };
        auto want = [&](const char* key, const std::string& v) {
            const std::string got = ui.host().enhancedValue(key), file = saved.has(key) ? saved[key].str() : "(unset)";
            std::printf("  enhanced: %s %s (enhanced.json %s)\n", key, got.c_str(), file.c_str());
            if (got != v || file != v) add(std::string(key) + " is " + got + " / " + file + ", not " + v);
        };
        auto status = [&](const char* prefix) {
            const std::string st = ui.host().enhancedStatus();
            std::printf("  enhanced status: %s\n", st.c_str());
            if (st.rfind(prefix, 0) != 0) add("the status is \"" + st + "\", not " + prefix + "...");
        };
        const std::string& w = s.screen;
        const char* dir = SDL_getenv("GD03_DIALOG");
        const std::filesystem::path d = dir ? dir : "";
        if (w.rfind("enhanced_mode_", 0) == 0) {
            const std::string mode = w == "enhanced_mode_deck" ? "steam_deck" : w == "enhanced_mode_old_days" ? "good_old_days" : w.substr(14);
            const auto preset = ui::enhancedPreset(mode);
            if (preset.empty()) add("no mode " + mode);
            for (const auto& [key, v] : preset) want(key.c_str(), v);
            status("Mode applied");
            if (w == "enhanced_mode_original") want("internal_res", "800"), want("screen_mode", "fullscreen");  // Steam Deck's, kept
            if (w == "enhanced_mode_old_days") {
                for (const auto& [key, lo] : {std::pair{"fx_on", 1}, std::pair{"particle_rate", 20}}) {
                    const int v = ui.host().enhancedState(key);
                    std::printf("  enhanced state: %s %d\n", key, v);
                    if (v != lo) add(std::string(key) + " is " + std::to_string(v));
                }
            }
        } else if (w == "enhanced_res_720") {
            want("internal_res", "720");
        } else if (w == "enhanced_fps_unlimited") {
            want("max_fps", "unlimited");
        } else if (w == "enhanced_exported") {
            status("Exported to");
            for (const char* f : {"GD03-user-settings.json", "GD03-user-progress.json"}) {
                std::error_code ec;
                const auto size = std::filesystem::file_size(d / f, ec);
                std::printf("  exported %s: %s\n", f, ec ? "missing" : (std::to_string(size) + " bytes").c_str());
                if (ec || size < 10) add(std::string(f) + " was not written");
            }
            // Options removed from every game (Ground Fog, Light Streaks) and
            // 0.0.1's XInput switch in the file to load next: ignored. The
            // resolution changed in the file: loaded.
            try {
                std::ifstream in(d / "GD03-user-settings.json");
                std::stringstream ss;
                ss << in.rdbuf();
                Json j = Json::parse(ss.str());
                j.set("ground_fog", Json::string("full")), j.set("light_streaks", Json::string("on")), j.set("xinput", Json::string("off"));
                j.set("internal_res", Json::string("720"));
                std::ofstream(d / "GD03-user-settings.json") << j.dump(2);
            } catch (const std::exception&) {
                add("GD03-user-settings.json does not parse");
            }
        } else if (w == "enhanced_settings_loaded") {
            status("Settings loaded");
            want("internal_res", "720"), want("max_fps", "60");
            for (const char* gone : {"ground_fog", "light_streaks", "xinput"})
                if (saved.has(gone)) add(std::string(gone) + " (removed) was loaded");
        } else if (w == "enhanced_progress_loaded") {
            status("Progress loaded");
        }
        return problems.empty() ? pass() : fail(problems);
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
        // GH2's meta_bricks backdrop (its meta_light_on.mnm) is not on this
        // disc: the menus' backdrop is each panel's own scene, so there is
        // no shared light to compare [C data: no ui milo has it].
        if (!bricksLight(ui, c)) {
            std::printf("  bricks: %s has no meta_light_on (none on this disc)\n", ui.currentScreen().c_str());
            ++step_, stepStart_ = -1;
            return;
        }
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
        // 0x00158e50): 1 + 3 x the bar, so 4 with the bar fully down; 1 once
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
        // The track's surface is the player's character's
        // track/surfaces/<outfit>_keep.bmp (TrackPanel 0x001536e0 / 0x00155078).
        const int frames = ui.host().gameCounter(0, "surface_overrides");
        std::printf("  track surface: %d frames with the character's surface texture\n", frames);
        if (frames < 1) return fail("the track did not draw the character's surface texture (track/surfaces/<outfit>_keep.bmp)");
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
    // GD03-019: every Enhanced option starts at the original game's look.
    // The mouse is a control (on in every gd-game); the rest are off, the
    // PS2's 60 FPS, the window's own resolution and the 4:3 picture. The
    // rows gd03 does not have yet show off (greyed); the cheats are off.
    if (s.screen == "fx_set_on" || s.screen == "fx_set_off") {
        const bool on = s.screen == "fx_set_on";
        ui.host().setEnhancedValue("hdr", on ? "emulated" : "none");
        ui.host().setEnhancedValue("bloom", on ? "2x" : "none");
        return pass();
    }
    if (s.screen == "frame_record" || s.screen == "frame_differs" || s.screen == "frame_same") {
        // The backdrop's top right (clear of the panel's list and help box):
        // a hash of its pixels in a screenshot.
        const std::string shot = (std::filesystem::path(ui.savePath()).parent_path() / "walk_frame.bmp").string();
        uint64_t hash = 1469598103934665603ull;
        bool read = false;
        if (ui.host().saveScreenshot(shot))
            if (SDL_Surface* raw = SDL_LoadBMP(shot.c_str())) {
                if (SDL_Surface* px = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32)) {
                    read = true;
                    for (int y = px->h * 90 / 720; y < px->h * 350 / 720; ++y)
                        for (int x = px->w * 660 / 1280; x < px->w * 1270 / 1280; ++x) {
                            const Uint8* c = static_cast<const Uint8*>(px->pixels) + size_t(y) * size_t(px->pitch) + size_t(x) * 4;
                            for (int k = 0; k < 3; ++k) hash = (hash ^ c[k]) * 1099511628211ull;
                        }
                    SDL_DestroySurface(px);
                }
                SDL_DestroySurface(raw);
            }
        std::error_code ec;
        std::filesystem::remove(shot, ec);
        std::printf("  frame (%s): %016llx\n", s.screen.c_str(), static_cast<unsigned long long>(hash));
        if (!read) return fail("the frame could not be read back");
        if (s.screen == "frame_record") return frameHash_ = hash, pass();
        if (s.screen == "frame_differs") return hash != frameHash_ ? pass() : fail("HDR Emulated and Bloom 2x left the frame as it was");
        return hash == frameHash_ ? pass() : fail("HDR None with the effects off does not draw the frame as before");
    }
    if (s.screen == "enhanced_fx_default" || s.screen == "enhanced_controls" || s.screen == "enhanced_calibrated" ||
        s.screen.rfind("enhanced_hdr_", 0) == 0 || s.screen == "enhanced_graphics" || s.screen == "enhanced_no_effects" ||
        s.screen == "enhanced_effects_back" || s.screen == "enhanced_lights_default" || s.screen == "enhanced_enhancers" ||
        s.screen == "enhanced_cheats") {
        // The Enhanced Options' effects (GD03-043, gd02's walk): saved, in
        // effect (UiHost::enhancedState) and run by the port's chain.
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
        auto add = [&](const std::string& p) { problems += (problems.empty() ? "" : "; ") + p; };
        auto want = [&](const char* key, const std::string& v) {
            const std::string got = ui.host().enhancedValue(key), file = saved.has(key) ? saved[key].str() : "(unset)";
            std::printf("  enhanced: %s %s (enhanced.json %s)\n", key, got.c_str(), file.c_str());
            if (got != v || file != v) add(std::string(key) + " is " + got + " / " + file + ", not " + v);
        };
        auto state = [&](const char* key, int lo, int hi) {
            const int v = ui.host().enhancedState(key);
            std::printf("  enhanced state: %s %d\n", key, v);
            if (v < lo || v > hi) add(std::string(key) + " is " + std::to_string(v));
        };
        constexpr int kMany = 1 << 30;
        const std::string& w = s.screen;
        if (w == "enhanced_fx_default") {
            // The original look: the game's own depth of field, particles and
            // shake; none of the port's effects, the menus' textures as the
            // original's, and the frame drawn exactly as before (no chain).
            state("fx_on", 0, 0), state("post_original", 1, 1);
            for (const char* pass : {"fx:down", "fx:up", "fx:ao", "fx:composite", "fx:finish"}) state(pass, 0, 0);
            state("particles", 1, 1), state("particle_rate", 10, 10), state("shake", 1, 1), state("sharp_ui", 0, 0), state("hdr", 0, 0);
            state("xinput", 1, 1), state("rockband", 1, 1);  // always on (no switches since 0.0.2)
        } else if (w.rfind("enhanced_hdr_", 0) == 0) {
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
                add("the HDR status is \"" + st + "\"");
        } else if (w == "enhanced_controls") {
            want("touch", "on");
            state("touch", 1, 1), state("rockband", 1, 1), state("xinput", 1, 1);
        } else if (w == "enhanced_calibrated") {
            // The presses came 0..30 ms after the ticks and flashes.
            for (const char* key : {"audio_offset", "video_offset"}) {
                const std::string v = saved.has(key) ? saved[key].str() : "";
                std::printf("  enhanced: %s %s\n", key, v.c_str());
                if (v.empty() || std::abs(std::atoi(v.c_str())) > 337) add(std::string(key) + " not calibrated (" + v + ")");
            }
            state("calibration_ms", -500, 300);
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
        } else if (w == "enhanced_cheats") {
            want("no_fail", "on"), want("autoplay", "on"), want("hopo_mode", "forgiving");
            if (saved.has("cut_content")) add("Enable Cut Content (not built) was saved");
        }
        return problems.empty() ? pass() : fail(problems);
    }
    if (s.screen == "enhanced_all_off") {
        // Every effect off (GD03-043): Bokeh Medium and Particles Original are
        // the game's own, HDR None; touch off on a desktop.
        const std::vector<std::pair<const char*, const char*>> want = {
            {"screen_mode", "windowed"}, {"max_fps", "60"},      {"fps_counter", "off"},    {"mouse", "on"},         {"internal_res", "0"},
            {"widescreen", "off"},       {"touch", "off"},       {"hdr", "none"},           {"sharp_ui", "off"},     {"antialiasing", "none"},
            {"ai_upscaling", "none"},    {"bokeh", "medium"},    {"particles", "original"}, {"bloom", "none"},       {"cinematic_color", "off"},
            {"color_grade", "off"},      {"film_grain", "none"}, {"chromatic", "none"},     {"ssao", "none"},        {"sharpening", "off"},
            {"star_blur", "none"},       {"no_effects", "off"},  {"shadows_all", "off"},    {"more_lights", "none"}, {"light_dust", "none"},
            {"no_shaky_cam", "off"},     {"no_fail", "off"},     {"autoplay", "off"},       {"hopo_mode", "original"}};
        for (const auto& [k, v] : want) {
            const std::string got = ui.host().enhancedValue(k);
            if (got != v) return fail(std::string("Enhanced Options ") + k + " starts " + got + ", not " + v);
        }
        for (const ui::EnhancedSetting& e : ui::enhancedSettings())
            if (!e.values.empty() && ui.host().enhancedValue(e.key) != e.values.front())
                return fail(std::string("Enhanced Options ") + e.key + " does not start at its first value");
        for (const char* c : {"cheat_unlock_all", "cheat_air_guitar", "cheat_performance_mode", "cheat_hyper_speed_mode",
                              "cheat_flaming_head", "cheat_horse_head_mode", "cheat_eyeball_head_crowd", "cheat_monkey_head_crowd"})
            if (ui.cheatOn(c)) return fail(std::string("the cheat ") + c + " starts on");
        std::printf("  enhanced: every option at the original look (mouse on)\n");
        return pass();
    }
    // GD03-016: the picture is 4:3 between black bars by default, the whole
    // 16:9 frame with Widescreen on. The frame read back: its outer 3% each
    // side is black only with the bars (the venue backdrop shows there in 16:9).
    if (s.screen == "aspect_43" || s.screen == "aspect_169") {
        const bool wide = s.screen == "aspect_169";
        const float aspect = gfx::pictureAspect(), want = wide ? 16.0f / 9.0f : 4.0f / 3.0f;
        const std::string shot = (std::filesystem::path(ui.savePath()).parent_path() / "walk_aspect.bmp").string();
        bool barsBlack = true, read = false;
        if (ui.host().saveScreenshot(shot))
            if (SDL_Surface* raw = SDL_LoadBMP(shot.c_str())) {
                if (SDL_Surface* px = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32)) {
                    read = true;
                    const int band = std::max(1, px->w * 3 / 100);
                    for (int y = 0; y < px->h && barsBlack; ++y)
                        for (int x : {0, px->w - band})
                            for (int i = 0; i < band; ++i) {
                                const Uint8* c = static_cast<const Uint8*>(px->pixels) + size_t(y) * size_t(px->pitch) + size_t(x + i) * 4;
                                if (std::max({c[0], c[1], c[2]}) > 12) barsBlack = false;
                            }
                    SDL_DestroySurface(px);
                }
                SDL_DestroySurface(raw);
            }
        std::error_code ec;
        std::filesystem::remove(shot, ec);
        std::printf("  aspect: the picture %.3f (%s), the frame's sides %s\n", double(aspect), wide ? "16:9 wanted" : "4:3 wanted",
                    !read ? "not read" : barsBlack ? "black bars" : "drawn");
        if (std::fabs(aspect - want) > 0.01f) return fail(wide ? "Widescreen on did not make the picture 16:9" : "the picture is not 4:3");
        if (!read) return fail("the frame could not be read back");
        if (barsBlack == wide) return fail(wide ? "16:9: the frame's sides are still black" : "4:3: no black bars at the sides");
        return pass();
    }
    // The Enhanced Options: Resolution at its default (the window's own),
    // then 480p (one down from Good Old Days' 448p), saved in enhanced.json.
    if (s.screen == "enhanced_default" || s.screen == "enhanced_changed") {
        const std::string want = s.screen == "enhanced_default" ? "0" : "480";
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
    if (s.screen == "band_horse" || s.screen == "band_flames") {
        const int horses = ui.host().gameCounter(0, "band_horse_members"), flames = ui.host().gameCounter(0, "band_fx_particles");
        const bool ok = s.screen == "band_horse" ? horses > 0 : flames > 0;
        if (!ok) {
            if (nowMs - stepStart_ > 20000)
                return fail(s.screen == "band_horse" ? "no band member loaded its _horse.milo" : "the band's cheat_headflames drew no particles");
            return;
        }
        std::printf("  %s: %d members with horse heads, %d flame particles drawn\n", s.screen.c_str(), horses, flames);
        ++step_, stepStart_ = -1;
        return;
    }
    if (s.screen == "cheat_horse_on") {
        // GD03-045: long_cheats.dta's Horse Head code sets
        // $cheat_enabled_horse_head_mode; the band then loads every
        // character's <char>_horse.milo (0x00128778).
        const bool horse = ui.cheatOn("cheat_horse_head_mode");
        std::printf("  cheats: horse head %s\n", horse ? "on" : "off");
        if (!horse) return fail("the Horse Head code did not turn it on");
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
    if (s.screen == "band_named_CD") {
        const int slot = ui.session().profileSlot;
        const std::string name = slot < 0 ? "" : ui.career().profiles[size_t(slot)].name;
        std::printf("  band: new band \"%s\"\n", name.c_str());
        if (name != "CD") return fail("the band's name is \"" + name + "\", not \"CD\" (arrows, green, Enter)");
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
        for (const char* song : {"bangyourhead", "wegotthebeat"})  // this disc's battle: two passed, iran played
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
        const std::string target = !winTarget_.empty() ? winTarget_ : w ? w : "ballstothewall";  // battle's last song before its encore
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
    if (s.screen == "sp_multi_fill") {
        if (ui.host().gameTimeMs() < 6000) {
            if (nowMs - stepStart_ > s.ms) return fail("the face-off song did not reach 6 s");
            return;
        }
        Interp& in = ui.interp();
        for (const char* p : {"player0", "player1"}) {
            in.send(p, "fill_star_power", {});
            in.send(p, "set_star_power_deploy_rate", {Value::real(1.0f)});
        }
        return pass();
    }
    if (s.screen == "sp_multi_sounds") {
        std::map<std::string, int> n;
        for (const std::string& h : ui.host().sfxHistory()) ++n[h];
        std::printf("  face-off Star Power: sp_available_multi %d, sp_deployed_multi %d, sp_depleted_multi %d (single: %d %d %d)\n",
                    n["sp_available_multi"], n["sp_deployed_multi"], n["sp_depleted_multi"], n["sp_available"], n["sp_deployed"], n["sp_depleted"]);
        if (!n["sp_available_multi"] || !n["sp_deployed_multi"] || !n["sp_depleted_multi"]) return fail("the two-player Star Power sounds did not play");
        if (n["sp_available"] || n["sp_deployed"] || n["sp_depleted"]) return fail("a single-player Star Power sound played in face-off");
        return pass();
    }
    if (s.screen == "tour_won easy" || s.screen == "tour_won other") {
        // win_game_screen's enter: beat_easy on Easy, else beat_other, and the
        // meta crowd_win; the campaign won (status >= its tiers, 0x0012e248).
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
    if (s.screen == "career_song_iran") {
        // set_career_song (0x822B89B8): the venue's first unlocked song not passed yet.
        std::printf("  career: venue %s, song %s\n", ui.session().venue.c_str(), ui.session().song.c_str());
        if (ui.session().venue != "battle" || ui.session().song != "iran") return fail("the career did not offer battle's iran");
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
        const int e = sp->indexOf("18andlife");
        if (e < 1 || e >= int(rows.size())) return fail("the setlist has no locked encore row (index " + std::to_string(e) + " of " + std::to_string(rows.size()) + " rows)");
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
        // encore, small1 unlocked, status 1; the cash on this disc: 50 to
        // start, 100 per three-star song (iran, the encore), 100 for status 1; then
        // the first finish of the store (lp_cayenne, 250) bought and on the
        // Les Paul's finishes.
        Interp& in = ui.interp();
        const int cash = in.send("campaign", "cash", {}).integerValue();
        const int status = in.send("campaign", "status", {}).integerValue();
        const bool small1 = in.send("campaign", "is_unlocked", {Value::symbol("small1")}).truthy();
        const bool battle = in.send("campaign", "is_song_passed", {Value::symbol("18andlife")}).truthy();
        std::printf("  career: status %d, cash %d, small1 %s, encore passed %s\n", status, cash, small1 ? "unlocked" : "locked", battle ? "yes" : "no");
        if (s.screen == "career_tier") {
            if (status != 1 || cash != 350 || !small1 || !battle) return fail("the first tier is not complete as the rules say (status 1, $350)");
        } else {
            // The store's first finish on this disc is the Flying V's fv_v2
            // ($250; the finishes are listed by guitars.dta's guitar order).
            const auto skins = ui::chars::skins(ui, "flying_v");
            const bool on = std::find(skins.begin(), skins.end(), "fv_v2") != skins.end();
            const int spent = in.send("campaign", "cash_spent", {}).integerValue();
            std::printf("  store: fv_v2 %s, cash %d, spent %d\n", on ? "bought" : "not bought", cash, spent);
            if (!on || cash != 100 || spent != 250) return fail("the store did not sell fv_v2 for $250");
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
        const std::string want = "songs/" + ss.song + "/" + ss.song + "_p" + pct + ".vgs";
        if (a != want) return fail("practice at " + pct + "% plays " + a + ", not " + want);
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
        const std::vector<const char*> done = two ? std::vector<const char*>{"star_combos_done", "whammy_done", "wail_done", "wrapup1_2"}
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
    if (s.screen.rfind("focus_to ", 0) == 0) {
        // Down until the focus panel's focus is the component.
        ui::PanelObj* fp = ui.focusPanel();
        if (fp && fp->focus == s.screen.substr(9)) return pass();
        if (nowMs - stepStart_ > s.ms) return fail("no focus on " + s.screen.substr(9) + " (on " + (fp ? fp->focus : std::string("-")) + ")");
        if (!ui.inTransition() && nowMs - lastNavMs_ > 200) ui.buttonDown(kDown, 0), ui.buttonUp(kDown, 0), lastNavMs_ = nowMs;
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
    if (s.screen == "big_note_blown") {
        // A gem passed within 0.2 beats of a big note (PART GUITAR pitch
        // 110): the player's blew_big_note, game.dta's {world play_sfx
        // blew_bignote.cue} with the crowd audio on (0x00110e78) [C].
        double at = -1;
        for (const auto& [name, t] : ui.host().gameSfx())
            if (name == "blew_bignote.cue" && at < 0) at = t;
        std::printf("  big note: blew_bignote.cue %s%s\n", at < 0 ? "not played" : "at ", at < 0 ? "" : (std::to_string(int(at)) + " ms").c_str());
        return at < 0 ? fail("the passed big note did not play blew_bignote.cue") : pass();
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
            // 190 entries on this disc (GH2 360: over 300).
            if (missing || entries < 190) std::printf("  FAIL The Glitches does not have everything unlocked\n"), ok = false;
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
        const std::string want = "alterna alterna1 lespaul lp_cherry";  // this disc: one outfit, the second guitar's first skin
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
            std::snprintf(buf, sizeof buf, "cash %d spent %d battle %s small1 %s iran %d* encore %d* fv_v2 %s", int(p["cash"].num()),
                          int(p["cash_spent"].num()), med["battle"]["completed"].truthy() ? "completed" : "-",
                          med["small1"]["unlocked"].truthy() ? "unlocked" : "-", int(med["iran"]["stars"].num()),
                          int(med["18andlife"]["stars"].num()), p["items"]["fv_v2"].truthy() ? "bought" : "-");
            got = buf;
        } catch (const std::exception& e) {
            got = e.what();
        }
        const std::string want = "cash 100 spent 250 battle completed small1 unlocked iran 3* encore 3* fv_v2 bought";
        std::printf("  career.json: %s\n", got.c_str());
        if (got != want) std::printf("  FAIL career.json is not: %s\n", want.c_str()), ok = false;
    }
    // The endgame photo (quickplay): player 0's outfit, one of its four pictures, on the disc.
    if (scenario_ == "quickplay") {
        const auto& photos = ui.session().winnerPhotos;
        // Player 0's outfit when the photo was taken: quick play gives player
        // 0 the song's (quickplay (character_outfit X)) [C data] and the menus
        // change it again afterwards, so the photo's own name says which, and
        // that character must exist on the disc.
        const std::string p = photos.empty() ? std::string() : photos[0];
        std::string outfit = ui.session().players[0].outfit;
        if (p.size() > 26 && p.rfind("image/og/photo_", 0) == 0) {
            const std::string o = p.substr(15, p.size() - 15 - 10);
            if (ui.data().exists("char/" + o + "/og/gen/" + o + ".milo_ps2")) outfit = o;
        }
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
            score = int(j["coop"]["bangyourhead"]["score"].num());  // the first co-op song on this disc
            award = j["coop_awards"]["bass_sg_mahogany"].truthy();
        } catch (const std::exception&) {
        }
        std::printf("  career.json: co-op bangyourhead %d, bass_sg_mahogany %s\n", score, award ? "awarded" : "-");
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
    // --audittest: the reference audit of this run (GD03_AUDIT: the report's path).
    if (audit::enabled()) {
        const char* out = SDL_getenv("GD03_AUDIT");
        audit::writeReport(ui.data(), out && *out ? out : "audit.md", scenario_);
    }
    return ok ? 0 : 1;
}
