// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/walk.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <sstream>

#include "data/json.h"
#include "data/qbkey.h"
#include "script/vm.h"
#include "ui/front_end.h"
#include "ui/port_screens.h"

namespace {

// The flow state player 1 is in (ui_flow_manager_state[0]).
std::string flowState(FrontEnd& fe) {
    const qb::Value* a = fe.vm().global(qbkey::key("ui_flow_manager_state"));
    if (!a || a->type != qb::Type::Array || !a->arr || a->arr->values.empty()) return "";
    return qbkey::name(a->arr->values[0].key);
}

}  // namespace

Walk::Walk(const std::string& name) : name_(name) {
    const Action C = Action::Confirm, D = Action::Down, B = Action::Back;
    // Boot -> press any button -> save -> main menu -> Quickplay ->
    // difficulty -> setlist -> song -> results [C: flow_path.md].
    std::vector<Step> boot = {
        {"bootup_press_any_button_fs", {C}, 30000},
        {"bootup_using_guitar_controller_fs", {C}, 60000, true},  // only without a save [C data: flow_path.md]
        {"main_menu_fs", {}, 60000},
    };
    if (name == "quickplay" || name == "lose") {
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {D, D, C}, 10000});  // career, co-op career, QUICKPLAY
        steps_.push_back({"quickplay_select_difficulty_fs", {C}, 20000});
        steps_.push_back({"quickplay_setlist_fs", {C}, 20000});
        steps_.push_back({"song", {}, 30000});
        steps_.push_back({"song_over", {}, 600000});
        steps_.push_back({name == "lose" ? "quickplay_fail_song_fs" : "quickplay_win_song_fs", {}, 30000});
    } else if (name == "career") {
        // Career: band -> difficulty -> character hub -> venue -> setlist ->
        // the first song -> won -> newspaper -> autosave -> setlist [C: the
        // flow states GH3's own scripts walk, GD04_FLOWLOG].
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {C}, 10000});  // CAREER
        steps_.push_back({"career_choose_band_fs", {C}, 20000, true});  // not on the first play
        // The name: three letters (NEXT) then ACCEPT (Start) [C script menu_ebn handlers].
        steps_.push_back({"career_enter_band_name_fs", {C, C, C, Action::Start}, 20000, true});
        steps_.push_back({"career_select_difficulty_fs", {C}, 20000});
        steps_.push_back({"career_select_character_fs", {C}, 20000, true});  // first play
        steps_.push_back({"career_character_hub_fs", {C}, 20000});
        steps_.push_back({"career_select_venue_fs", {C}, 20000});
        steps_.push_back({"career_setlist_fs", {C}, 20000});
        steps_.push_back({"song", {}, 30000});
        steps_.push_back({"song_over", {}, 600000});
        steps_.push_back({"career_newspaper_fs", {C}, 30000});
        steps_.push_back({"career_cash_reward_fs", {C}, 30000, true});  // the song's cash (Progression_AwardCash)
        steps_.push_back({"career_unlock_1_fs", {C}, 30000, true});
        steps_.push_back({"career_setlist_fs", {}, 30000});
    } else if (name == "career_quickplay") {
        // A career song in the venue picked (career_select_venue_fs), then
        // back to the main menu and a quick play song: each song loads its own
        // venue. Quick play has no venue select: setlist_choose_song's
        // `change { current_level = <g_last_venue_selected> }` changes nothing
        // (no such parameter), then quickplay_start_song picks a random
        // unlocked venue, guitarist and guitar (get_valid_venue_index) [C scripts].
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {C}, 10000});
        steps_.push_back({"career_choose_band_fs", {C}, 20000, true});
        steps_.push_back({"career_enter_band_name_fs", {C, C, C, Action::Start}, 20000, true});
        steps_.push_back({"career_select_difficulty_fs", {C}, 20000});
        steps_.push_back({"career_select_character_fs", {C}, 20000, true});
        steps_.push_back({"career_character_hub_fs", {C}, 20000});
        steps_.push_back({"career_select_venue_fs", {C}, 20000});
        steps_.push_back({"career_setlist_fs", {C}, 20000});
        steps_.push_back({"song", {}, 30000});
        steps_.push_back({"song_over", {}, 600000});
        steps_.push_back({"career_newspaper_fs", {C}, 30000});
        steps_.push_back({"career_cash_reward_fs", {C}, 30000, true});
        steps_.push_back({"career_unlock_1_fs", {C}, 30000, true});
        steps_.push_back({"career_setlist_fs", {B}, 30000});
        steps_.push_back({"career_select_venue_fs", {B}, 20000, true});
        steps_.push_back({"career_character_hub_fs", {B}, 20000, true});
        steps_.push_back({"career_select_character_fs", {B}, 20000, true});
        steps_.push_back({"career_select_difficulty_fs", {B}, 20000, true});
        steps_.push_back({"career_choose_band_fs", {B}, 20000, true});
        steps_.push_back({"main_menu_fs", {D, D, C}, 20000});
        steps_.push_back({"quickplay_select_difficulty_fs", {C}, 20000});
        steps_.push_back({"quickplay_setlist_fs", {C}, 20000});
        steps_.push_back({"song", {}, 30000});
    } else if (name == "dev_debug") {
        // GD04-041: Enhanced > Developer Debug Menu on -> the pause menu
        // offers GH3's DEBUG MENU (enable_button_cheats) -> its debug menu.
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {"set:enhanced:dev_debug_menu=on", D, D, C}, 10000});
        steps_.push_back({"quickplay_select_difficulty_fs", {C}, 20000});
        steps_.push_back({"quickplay_setlist_fs", {C}, 20000});
        steps_.push_back({"song", {Action::Start}, 30000});
        steps_.push_back({"quickplay_pause_fs", {"check:text:pause_debug_menu!=", D, D, D, D, D, C}, 20000});
        steps_.push_back({"debug_menu_fs", {"check:text:debug_vmenu!=never"}, 20000});
    } else if (name == "pad_unplugged") {
        // GD04-042: a pad as player 1, the keyboard given player 2; the pad
        // unplugged mid-song: GH3's controller_disconnect_fs ("YOU ARE ROCKING
        // OUT A BIT TOO HARD"); plugged back, RESUME and the song plays on.
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {"xpad_attach", Op("keyboard_p2", 600), D, D, C}, 10000});
        steps_.push_back({"quickplay_select_difficulty_fs", {C}, 20000});
        steps_.push_back({"quickplay_setlist_fs", {C}, 20000});
        steps_.push_back({"song", {Op("pad_detach", 10000)}, 30000});  // past the intro (playing_song = 1)
        steps_.push_back({"controller_disconnect_fs", {Op("xpad_attach", 1000), Op(C, 1500)}, 20000});
        steps_.push_back({"song_over", {}, 600000});
    } else if (name == "pause" || name == "pausequit") {
        // Start in the song -> GH3's pause menu (quickplay_pause_fs) -> RESUME
        // and the song plays on; pausequit: QUIT -> its warning -> YES -> the menus.
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {D, D, C}, 10000});
        steps_.push_back({"quickplay_select_difficulty_fs", {C}, 20000});
        steps_.push_back({"quickplay_setlist_fs", {C}, 20000});
        // The keyboard: Esc pauses, Enter picks RESUME (pausequit: Start, the pad's).
        steps_.push_back({"song", {name == "pause" ? Op("key:Escape") : Op(Action::Start)}, 30000});
        if (name == "pause") {
            steps_.push_back({"quickplay_pause_fs", {Op("key:Return")}, 20000});
            steps_.push_back({"song_over", {}, 600000});
            steps_.push_back({"quickplay_win_song_fs", {}, 30000});
        } else {
            steps_.push_back({"quickplay_pause_fs", {D, D, D, D, C}, 20000});
            steps_.push_back({"quickplay_quit_warning_fs", {D, C}, 20000});         // QUIT (not CANCEL)
            // NEW SONG: kill_gem_scroller. The menu comes 3 s after the state
            // (the quit's spawned_func waits first) [C script].
            steps_.push_back({"quickplay_song_ended_fs", {Op(D, 4000), C}, 20000});
            steps_.push_back({"quickplay_setlist_fs", {}, 30000});
        }
    } else if (name == "attract") {
        // Idle on "press any button": after 110 s the attract mode plays a
        // song by itself; a button ends it [C scripts attract_mode_spawner,
        // check_for_attract_mode_input].
        steps_ = {{"bootup_press_any_button_fs", {}, 30000},
                  {"bootup_attract_mode_fs", {}, 150000},
                  {"song", {Op(C, 8000)}, 60000},
                  {"bootup_press_any_button_fs", {}, 60000}};
    } else if (name == "vault") {
        // The Vault: career band 1 -> character hub -> THE STORE -> guitars ->
        // back -> back to the hub [C: career_character_hub_fs, store_fs].
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {C}, 10000});
        steps_.push_back({"career_choose_band_fs", {C}, 20000, true});
        steps_.push_back({"career_enter_band_name_fs", {C, C, C, Action::Start}, 20000, true});
        steps_.push_back({"career_select_difficulty_fs", {C}, 20000});
        steps_.push_back({"career_select_character_fs", {C}, 20000, true});
        steps_.push_back({"career_character_hub_fs", {D, D, D, D, C}, 20000});  // VISIT STORE
        steps_.push_back({"store_fs", {C}, 20000});
        // The first category; band 1's Vault is the save's own (not The Glitches', GD04-026).
        steps_.push_back({"store_guitars_fs", {"check:tag:Instrument_focus01.unlocked=0", B}, 20000});
        steps_.push_back({"store_fs", {B}, 20000});
        steps_.push_back({"career_character_hub_fs", {}, 20000});
    } else if (name == "cheats") {
        // Options -> CHEATS -> ENTER NEW CHEAT -> Performance Mode's chords,
        // strummed (GH3's guitar_hero_cheats unlock_pattern) -> Start.
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {"click:menu:main_menu_options_text"}, 10000});
        steps_.push_back({"options_select_option_fs", {D, D, D, D, D, D, D, D, C}, 20000});  // CHEATS
        steps_.push_back({"options_cheats_fs", {D, D, D, D, D, D, D, C}, 20000});              // ENTER CHEAT
        std::vector<Op> chords;
        for (const char* f : {"1100", "1010", "1001", "1010", "1100", "10010", "1100", "1010"}) {
            chords.push_back(Op(("frets:" + std::string(f)).c_str()));
            chords.push_back(D);  // strum
            chords.push_back(Op("frets:0"));
        }
        chords.push_back(Action::Start);
        steps_.push_back({"options_enter_new_cheat_fs", chords, 20000});
        steps_.push_back({"options_cheats_fs", {"check:text:Cheat_PerformanceMode_Text!=locked"}, 20000});
    } else if (name == "glitches") {
        // The Glitches: band slot 5, everything unlocked (ui/glitches.h): its
        // name and cash, then chosen, its career's last tier complete.
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {"check:tag:band5_info_p1_career.cash=1000000", C}, 10000});
        steps_.push_back({"career_choose_band_fs", {D, D, D, D, C}, 20000});
        steps_.push_back({"career_select_difficulty_fs", {Action::Down, Action::Down, Action::Down, C}, 20000});
        // The Vault all bought for this band only (GD04-026).
        steps_.push_back({"career_character_hub_fs",
                          {"check:tag:careertier8.complete=1", "check:tag:career_song1_tier8.stars=5", "check:tag:Instrument_focus01.unlocked=1"},
                          20000});

    } else if (name == "boss") {
        // A career boss battle: The Glitches (everything unlocked) -> EASY ->
        // venue -> the setlist's tier-2 boss, Tom Morello (bosstom) [C data
        // GH3_Career_Songs] -> GH3's battle help -> the battle -> the result.
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {C}, 10000});
        steps_.push_back({"career_choose_band_fs", {D, D, D, D, C}, 20000});
        steps_.push_back({"career_select_difficulty_fs", {C}, 20000});
        steps_.push_back({"career_character_hub_fs", {C}, 20000});
        steps_.push_back({"career_select_venue_fs", {C}, 20000});
        steps_.push_back({"career_setlist_fs", {"setlist:bosstom", C}, 20000});
        steps_.push_back({"career_battle_help_fs", {C}, 20000});  // "MORELLO CHALLENGES YOU!": BATTLE
        steps_.push_back({"song", {}, 60000});
        steps_.push_back({"song_over", {}, 900000});
        steps_.push_back({"career_newspaper_fs", {}, 60000});
    } else if (name == "boss_devil") {
        // Lou's battle (bossdevil, the last tier's boss): won, GH3 restarts
        // the song mid-way for the finish (start_devil_finish: <song>_markers[37],
        // devil_finish_restart = 1; player 2's highway at x 1000, no death
        // meter) and plays it to its end [C scripts] -> the newspaper.
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {C}, 10000});
        steps_.push_back({"career_choose_band_fs", {D, D, D, D, C}, 20000});
        steps_.push_back({"career_select_difficulty_fs", {C}, 20000});
        steps_.push_back({"career_character_hub_fs", {C}, 20000});
        steps_.push_back({"career_select_venue_fs", {C}, 20000});
        steps_.push_back({"career_setlist_fs", {"setlist:bossdevil", C}, 20000});
        steps_.push_back({"career_battle_help_fs", {C}, 20000});
        steps_.push_back({"song", {}, 60000});
        steps_.push_back({"song_over", {}, 1200000});
        steps_.push_back({"career_newspaper_fs", {}, 120000});
    } else if (name == "boss_helper") {
        // The boss after the song that unlocks it (GD04-038): a new band on
        // Easy; Slow Ride played and, during it, GH3's own
        // Progression_TierBossUnlock tier = 2 (what Progression_SongWon runs
        // when a win completes tier 2: the boss song tag, its unlock,
        // current_transition = preboss); the song won: GuitarEvent_SongWon_Spawned's preboss ->
        // preboss_win_song -> start_boss (show_boss_helper_screen = 1) -> the
        // boss song set up, its helper (should_play_boss_intro) ->
        // battle_mode_helper -> BATTLE -> the battle [C scripts].
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {C}, 10000});
        steps_.push_back({"career_choose_band_fs", {C}, 20000, true});
        steps_.push_back({"career_enter_band_name_fs", {C, C, C, Action::Start}, 20000, true});
        steps_.push_back({"career_select_difficulty_fs", {C}, 20000});
        steps_.push_back({"career_select_character_fs", {C}, 20000, true});
        steps_.push_back({"career_character_hub_fs", {C}, 20000});
        steps_.push_back({"career_select_venue_fs", {C}, 20000});
        steps_.push_back({"career_setlist_fs", {"setlist:slowride", C}, 30000});
        steps_.push_back({"song", {Op("script:Progression_TierBossUnlock tier=2", 3000)}, 30000});
        steps_.push_back({"song_over", {}, 900000});
        steps_.push_back({"career_autosave_boss_confirmation_fs", {}, 60000, true});
        steps_.push_back({"career_battle_help_boss_confirmation_fs", {C}, 60000});  // the boss's help: BATTLE
        steps_.push_back({"song", {}, 60000});
        steps_.push_back({"song_over", {}, 900000});
        steps_.push_back({"career_newspaper_fs", {}, 60000});
    } else if (name == "back") {
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {D, D, C}, 10000});
        steps_.push_back({"quickplay_select_difficulty_fs", {B}, 20000});
        steps_.push_back({"main_menu_fs", {}, 20000});
    } else if (name == "mouse") {
        // The mouse alone: hover OPTIONS on the main menu (focused), click it
        // -> Options -> click the help bar's ENHANCED -> In-Game Max FPS's
        // dropdown clicked open, 120 clicked -> right click -> Options -> the
        // help bar's BACK clicked -> main (guidelines/universal_launcher.md).
        steps_ = boot;
        steps_.push_back({"main_menu_fs",
                          {"hover:menu:main_menu_options_text", "check:selected:main_menu_options_text", "click:menu:main_menu_options_text"},
                          10000});
        steps_.push_back({"options_select_option_fs", {"click:item"}, 20000});
        steps_.push_back({"port_enhanced",
                          {"hover:row:max_fps", "click:row:max_fps", "click:option:1", "check:enhanced:max_fps=120",
                           // Export User Files, a setting changed, Load User Settings brings it back (the dialogs answered with the save folder).
                           "dialog:savedir", "click:row:export", "set:enhanced:max_fps=60", "click:row:load_settings", "check:enhanced:max_fps=120", "click:row:load_progress", "set:enhanced:audio_offset=40", "set:enhanced:video_offset=-20", "click:row:calibrate_reset", "check:enhanced:audio_offset=0", "check:enhanced:video_offset=0",
                           "click:row:cheat_air_guitar", "check:global:Cheat_AirGuitar=1", "click:row:cheat_air_guitar", "check:global:Cheat_AirGuitar=2",
                           "rclick"},
                          10000});
        steps_.push_back({"options_select_option_fs", {"click:help:BACK"}, 10000});
        steps_.push_back({"main_menu_fs", {}, 20000});
    } else if (name == "venues") {
        // Career venue select: down moves the focus to the next venue (owner
        // report: stuck on the first) [C script create_select_venue_menu].
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {C}, 10000});
        steps_.push_back({"career_choose_band_fs", {C}, 20000, true});
        steps_.push_back({"career_enter_band_name_fs", {C, C, C, Action::Start}, 20000, true});
        steps_.push_back({"career_select_difficulty_fs", {C}, 20000});
        steps_.push_back({"career_select_character_fs", {C}, 20000, true});
        steps_.push_back({"career_character_hub_fs", {"unlock:all", C}, 20000});  // the venues unlocked (as progress does)
        steps_.push_back({"career_select_venue_fs", {"check:focus:venue_3", Action::Up, "wait", "check:focus:venue_4", Action::Up, "wait", "check:focus:venue_5", D, "wait",
                                                     "check:focus:venue_4", B}, 20000});
        steps_.push_back({"career_character_hub_fs", {}, 20000});
    } else if (name == "hub") {
        // The character hub's CHANGE OUTFIT and CHANGE GUITAR screens
        // (GD04-043) [C data
        // career_character_hub_fs].
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {C}, 10000});
        steps_.push_back({"career_choose_band_fs", {C}, 20000, true});
        steps_.push_back({"career_enter_band_name_fs", {C, C, C, Action::Start}, 20000, true});
        steps_.push_back({"career_select_difficulty_fs", {C}, 20000});
        steps_.push_back({"career_select_character_fs", {C}, 20000, true});
        steps_.push_back({"career_character_hub_fs", {D, D, C}, 20000});  // PLAY SHOW, CHANGE CHARACTER, CHANGE OUTFIT
        steps_.push_back({"career_select_outfit_fs", {B}, 20000});
        steps_.push_back({"career_character_hub_fs", {D, D, D, C}, 20000});  // CHANGE GUITAR
        steps_.push_back({"career_select_guitar_fs", {C}, 20000});  // the guitar chosen: back to the hub
        steps_.push_back({"career_character_hub_fs", {}, 20000});
    } else if (name == "store") {
        // The Vault's pages no other walk opens (GD04-043): finishes, songs,
        // characters, outfits, styles, videos -- each opened and backed out
        // of [C data store_fs].
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {C}, 10000});
        steps_.push_back({"career_choose_band_fs", {C}, 20000, true});
        steps_.push_back({"career_enter_band_name_fs", {C, C, C, Action::Start}, 20000, true});
        steps_.push_back({"career_select_difficulty_fs", {C}, 20000});
        steps_.push_back({"career_select_character_fs", {C}, 20000, true});
        steps_.push_back({"career_character_hub_fs", {D, D, D, D, C}, 20000});  // VISIT STORE
        const std::pair<int, const char*> pages[] = {{1, "store_finishes_fs"}, {2, "store_songs_fs"},   {3, "store_characters_fs"},
                                                     {4, "store_outfits_fs"},  {5, "store_styles_fs"}, {6, "store_videos_fs"}};
        for (const auto& [downs, state] : pages) {
            std::vector<Op> go(size_t(downs), D);
            go.push_back(C);
            steps_.push_back({"store_fs", go, 20000});
            steps_.push_back({state, {B}, 20000});
        }
        steps_.push_back({"store_fs", {}, 20000});
    } else if (name == "practice") {
        // Training -> PRACTICE -> the first song -> difficulty -> the song's
        // first section -> speed -> the section played -> its results
        // (GD04-043) [C data practice_*_fs, script practice_update].
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {"click:menu:main_menu_training_text"}, 10000});
        steps_.push_back({"practice_select_mode_fs", {D, C}, 20000});  // TUTORIAL, PRACTICE
        steps_.push_back({"practice_setlist_fs", {C}, 20000});
        steps_.push_back({"practice_select_part_fs", {C}, 20000, true});
        steps_.push_back({"practice_select_difficulty_fs", {C}, 20000});
        steps_.push_back({"practice_select_song_section_fs", {C}, 20000});
        steps_.push_back({"practice_select_speed_fs", {C}, 20000});
        // The section's end brought to 5 s in (the walk's song ends early):
        // practice_update then ends it as GH3 does, Song_Win_Delay later.
        steps_.push_back({"song", {"set:global:practice_end_time=5000"}, 30000});
        steps_.push_back({"song_over", {}, 600000});
        steps_.push_back({"practice_newspaper_fs", {}, 60000});  // the section's results: restart / speed / section / new song / quit
    } else if (name == "options") {
        // Options' screens no other walk opens (GD04-043): AUDIO SETTINGS,
        // CALIBRATE LAG, CONTROLLER SETTINGS, DATA SETTINGS, BONUS VIDEOS,
        // TOP ROCKERS, each opened and backed out of (the menu comes back
        // with the top item focused) [C data options_select_option_fs].
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {"click:menu:main_menu_options_text"}, 10000});
        const std::pair<int, const char*> screens[] = {{0, "options_audio_settings_fs"},   {1, "options_calibrate_lag_fs"},
                                                       {2, "options_controller_settings_fs"}, {4, "options_data_settings_fs"},
                                                       {5, "options_bonus_videos_fs"},        {7, "options_top_rockers_difficulty_select_fs"}};
        for (const auto& [downs, state] : screens) {
            std::vector<Op> go(size_t(downs), D);
            go.push_back(C);
            steps_.push_back({"options_select_option_fs", go, 20000});
            steps_.push_back({state, {B}, 20000});
        }
        steps_.push_back({"options_select_option_fs", {}, 20000});
    } else if (name == "touch") {
        // Enhanced > Enable Touch Controls on; a finger taps OPTIONS on the
        // main menu (SDL finger events, as a touch screen sends them) ->
        // Options -> back -> main.
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {"set:enhanced:touch=on", "tap:menu:main_menu_options_text"}, 10000});
        steps_.push_back({"options_select_option_fs", {B}, 20000});
        steps_.push_back({"main_menu_fs", {}, 20000});
    } else if (name == "gamepads") {
        // The universal Gamepad menu (universal_mechanisms.md 1.2.1): the
        // keyboard's yellow fret (3) on the main menu opens it; Player 1's
        // Customize inputs (the 4th field); Whammy (13 rows down the mapping)
        // key 1 rebound to E; Save (4 rows down: Gyro, Require Strum, Reset,
        // Save) writes controllers.json; back -> main; then the help bar's
        // GAMEPADS clicked opens it again.
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {"key:3"}, 10000});
        std::vector<Op> ops{D, D, D, C};
        for (int k = 0; k < 13; ++k) ops.push_back(D);
        ops.push_back(C), ops.push_back(Op("key:E"));
        for (int k = 0; k < 4; ++k) ops.push_back(D);
        ops.push_back(C), ops.push_back(Op("check:whammy_is_e")), ops.push_back(B);
        steps_.push_back({"port_gamepads", ops, 10000});
        steps_.push_back({"main_menu_fs", {"click:item"}, 10000});
        steps_.push_back({"port_gamepads", {}, 10000});
    } else if (name == "stickyaxis") {
        // An adapter whose D-pad is an axis resting off centre (a virtual
        // pad, 0079:0006:dpadaxis=-32768) plugged in at the main menu: no
        // left / right press in 2 s; moved, one right press.
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {"adapter_attach", Walk::Op("check:no_sideways", 2000), "adapter_move", "check:one_right"}, 10000});
    } else if (name == "coop") {
        // Co-op career: CO-OP CAREER -> both controllers to their sides and
        // ready (player 2 moves down, then each chooses) [C script
        // menu_select_controller_*] -> ...
        twoPlayers_ = true;
        const Op P2D = Op::p2(D), P2C = Op::p2(C);
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {D, C}, 10000});
        steps_.push_back({"coop_career_select_controllers_fs", {Op(C, 1500), P2D, P2C}, 20000});
        steps_.push_back({"coop_career_choose_band_fs", {C}, 20000, true});
        steps_.push_back({"coop_career_enter_band_name_fs", {C, C, C, Action::Start}, 20000, true});
        steps_.push_back({"coop_career_select_difficulty_fs", {C, P2C}, 20000});
        steps_.push_back({"coop_career_character_select_fs", {C, P2C}, 20000, true});  // first play
        steps_.push_back({"coop_career_character_hub_fs", {C, P2C}, 20000});
        steps_.push_back({"coop_career_select_venue_fs", {C}, 20000});
        steps_.push_back({"coop_career_setlist_fs", {C}, 20000});
        // Player 1 GUITAR, player 2 moved to BASS / RHYTHM [C choose_part_menu_select_part].
        steps_.push_back({"coop_career_choose_part_fs", {C, P2C}, 20000});
        steps_.push_back({"song", {}, 30000});
        steps_.push_back({"song_over", {}, 600000});
        steps_.push_back({"coop_career_newspaper_fs", {}, 30000});
    } else if (name == "faceoff" || name == "proface" || name == "battle") {
        // MULTIPLAYER -> controllers (as co-op) -> the mode [C script
        // create_mp_select_mode_menu: FACE-OFF, PRO FACE-OFF, BATTLE; CO-OP
        // only online] -> each player's character and hub -> venue ->
        // setlist -> difficulty (pro face-off: one shared) -> the song ->
        // the results.
        twoPlayers_ = true;
        const Op P2D = Op::p2(D), P2C = Op::p2(C);
        const int modeIdx = name == "faceoff" ? 0 : name == "proface" ? 1 : 2;
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {D, D, D, C}, 10000});
        steps_.push_back({"mp_select_controller_fs", {Op(C, 1500), P2D, P2C}, 20000});
        std::vector<Op> mode;
        for (int i = 0; i < modeIdx; ++i) mode.push_back(D);
        mode.push_back(C);
        steps_.push_back({"mp_select_mode_fs", mode, 20000});
        steps_.push_back({"mp_faceoff_character_select_fs", {C, P2C}, 20000});
        steps_.push_back({"mp_faceoff_character_hub_fs", {C, P2C}, 20000});
        steps_.push_back({"mp_faceoff_select_venue_fs", {C}, 20000});
        steps_.push_back({"mp_faceoff_setlist_fs", {C}, 20000});
        steps_.push_back({"mp_faceoff_battle_help_fs", {C}, 20000, true});  // battle: GH3's battle help
        if (name == "proface") steps_.push_back({"mp_faceoff_select_difficulty_fs", {C}, 20000});
        else steps_.push_back({"mp_faceoff_select_difficulty_fs", {C, P2C}, 20000});
        steps_.push_back({"song", {}, 30000});
        steps_.push_back({"song_over", {}, 900000});
        steps_.push_back({"mp_faceoff_newspaper_fs", {}, 120000});
    } else if (name == "boot") {
        steps_ = boot;
    } else if (name == "uitest") {
        steps_ = boot;
        uitest_ = true;
    }
}

void Walk::uiStep(FrontEnd& fe, double now) {
    if (states_.empty() && stateIdx_ == 0) {
        // Every global struct ending in _fs with a Create script [C data].
        for (const auto& name : fe.vm().globals().names()) {
            const std::string n = qbkey::name(name);
            if (n.size() < 4 || n.compare(n.size() - 3, 3, "_fs") != 0) continue;
            const qb::Value* v = fe.vm().globals().find(name);
            if (!v || !v->st || !v->st->find(qbkey::key("Create"))) continue;
            states_.push_back(name);
        }
        std::sort(states_.begin(), states_.end(), [](uint32_t a, uint32_t b) { return qbkey::name(a) < qbkey::name(b); });
    }
    if (stateAt_ >= 0 && now - stateAt_ < 500) return;
    if (stateAt_ >= 0) {
        const size_t made = size_t(fe.ui().stats().created) - created0_;
        if (made == 0) ++empty_, emptyStates_.push_back(qbkey::name(states_[stateIdx_ - 1]));
    }
    if (stateIdx_ >= states_.size()) {
        step_ = steps_.size();  // done
        return;
    }
    const uint32_t st = states_[stateIdx_++];
    created0_ = size_t(fe.ui().stats().created);
    qb::Struct p;
    qs::Vm::set(p, qbkey::key("flow_state"), qs::Vm::makeKey(st));
    fe.vm().spawn(qbkey::key("start_flow_manager"), p);
    stateAt_ = now;
}

std::vector<Walk::Press> Walk::update(FrontEnd& fe, bool songPlaying, double now, WalkHost* host, const std::string& port) {
    std::vector<Press> out;
    if (uitest_ && step_ >= steps_.size() - 1 && reachedAt_ >= 0) {
        if (!done()) uiStep(fe, now);
        return out;
    }
    if (done() || failed_) return out;
    // A song under a loading screen (GD04-040's regression): 5 s into a song
    // the loading screen's tip and picture are gone [C script start_gem_scroller].
    if (songPlaying && songSeenAt_ < 0) songSeenAt_ = now;
    if (!songPlaying) songSeenAt_ = -1;
    if (songSeenAt_ >= 0 && now - songSeenAt_ > 5000 && !loadingChecked_) {
        loadingChecked_ = true;
        if (fe.ui().loadingScreenUp() || fe.ui().find(qbkey::key("loading_tip_text"))) {
            failed_ = true, failure_ = "the loading screen is still up 5 s into the song";
            std::printf("walk: FAILED: %s\n", failure_.c_str());
            return out;
        }
    }
    const Step& s = steps_[step_];
    if (reachedAt_ < 0) {
        bool reached;
        if (s.state == "song") reached = songPlaying;
        else if (s.state == "song_over") reached = !songPlaying && songsEnded > 0;
        else reached = (port.empty() ? flowState(fe) : port) == s.state;
        // An optional step is skipped when a later step's state comes first
        // (looking past the optional steps that follow it).
        size_t later = step_ + 1;
        if (!reached && s.optional) {
            const std::string cur = flowState(fe);
            while (later < steps_.size() && steps_[later].state != cur && steps_[later].optional) ++later;
        }
        if (!reached && s.optional && later < steps_.size() && flowState(fe) == steps_[later].state) {
            log_.push_back("(" + s.state + " skipped)");
            ++step_;
            stepStart_ = now;
            return out;
        }
        if (reached) {
            reachedAt_ = now;
            lastKey_ = now;
            key_ = 0;
            log_.push_back(s.state);
            SDL_Log("walk: reached %s at %.0f ms", s.state.c_str(), now);
        } else if (now - stepStart_ > s.timeoutMs) {
            failed_ = true;
            failure_ = "waited " + std::to_string(int(s.timeoutMs)) + " ms for " + s.state + " (flow state: " + flowState(fe) + ")";
            SDL_Log("walk: FAILED: %s", failure_.c_str());
            return out;
        }
        return out;
    }
    // Reached: its buttons one by one (a screen gets 1 s to build first).
    if (key_ < s.keys.size()) {
        const Op& op = s.keys[key_];
        if (now - lastKey_ >= (key_ == 0 ? std::max(1000.0, op.delayMs) : op.delayMs)) {
            ++key_;
            lastKey_ = now;
            if (op.act.rfind("global:", 0) == 0) {
                // "global:<name>=<int>": a script global set (GH3's own debug
                // switches, e.g. enable_button_cheats for setlist_debug_completesong).
                const std::string kv = op.act.substr(7);
                const size_t eq = kv.find('=');
                fe.vm().setGlobal(qbkey::key(kv.substr(0, eq).c_str()), qs::Vm::makeInt(std::atoi(kv.substr(eq + 1).c_str())));
            } else if (op.act.rfind("script:", 0) == 0) {
                // "script:<name> [<param>=<int>]": one of GH3's scripts run now.
                std::string rest = op.act.substr(7), name = rest;
                qb::Struct params;
                if (const size_t sp = rest.find(' '); sp != std::string::npos) {
                    name = rest.substr(0, sp);
                    const std::string kv = rest.substr(sp + 1);
                    const size_t eq = kv.find('=');
                    qs::Vm::set(params, qbkey::key(kv.substr(0, eq).c_str()), qs::Vm::makeInt(std::atoi(kv.substr(eq + 1).c_str())));
                }
                fe.vm().run(qbkey::key(name.c_str()), params);
            } else if (op.act.rfind("setlist:", 0) == 0) {
                // Down until the setlist's cursor (setlist_selection_tier /
                // _song, set by the items' focus handlers) is on the song.
                const qb::Value* sl = fe.vm().global(qbkey::key("g_gh3_setlist"));
                if (sl && !sl->st) sl = fe.vm().globals().find(sl->key);  // it names the tier global (GH3_Career_Songs)
                const int tier = int(qs::Vm::number(fe.vm().global(qbkey::key("setlist_selection_tier")), 1));
                const int song = int(qs::Vm::number(fe.vm().global(qbkey::key("setlist_selection_song")), 0));
                uint32_t at = 0;
                if (sl && sl->st)
                    if (const qb::Value* t = fe.vm().globals().field(*sl->st, qbkey::key(("tier" + std::to_string(tier)).c_str())); t && t->st)
                        if (const qb::Value* songs = fe.vm().globals().field(*t->st, qbkey::key("songs")); songs && songs->arr &&
                            song >= 0 && size_t(song) < songs->arr->values.size())
                            at = songs->arr->values[size_t(song)].key;
                if (at != qbkey::key(op.act.substr(8).c_str())) {
                    if (++seek_ > 80) {
                        failed_ = true, failure_ = "the setlist has no " + op.act.substr(8);
                        SDL_Log("walk: FAILED: %s", failure_.c_str());
                        return out;
                    }
                    if (SDL_getenv("GD04_WALKLOG")) SDL_Log("walk: setlist at tier %d song %d (%s)", tier, song, qbkey::name(at).c_str());
                    --key_;  // again next time
                    out.push_back(Press{Action::Down, op.player});
                } else {
                    SDL_Log("walk: setlist on %s (tier %d, song %d)", op.act.substr(8).c_str(), tier, song);
                }
            } else if (op.act.empty()) {
                out.push_back(Press{op.a, op.player});
            } else if (!host || !act(op.act, fe, *host)) {
                if (failure_.empty()) failure_ = op.act + ": no host";
                failed_ = true;
                SDL_Log("walk: FAILED: %s", failure_.c_str());
            }
        }
        return out;
    }
    if (now - lastKey_ >= 300.0) {
        ++step_;
        reachedAt_ = -1;
        stepStart_ = now;
    }
    return out;
}

// The port's walks' acts (mouse, gamepads, stickyaxis).
bool Walk::act(const std::string& what, FrontEnd& fe, WalkHost& host) {
    auto fail = [&](const std::string& why) {
        failure_ = why;
        return false;
    };
    auto rest = [&](const char* prefix) { return what.substr(std::strlen(prefix)); };
    auto starts = [&](const char* prefix) { return what.rfind(prefix, 0) == 0; };
    // The mouse on something: "<verb>:menu:<element>" (GH3's element, at its
    // drawn bounds), "<verb>:help:<TEXT>" (GH3's help bar), "<verb>:item"
    // (the port's help bar item), "<verb>:row:<key>" / "<verb>:option:<k>"
    // (Enhanced Options).
    if (starts("hover:") || starts("click:")) {
        const int button = starts("click:") ? 1 : 0;
        const std::string target = what.substr(6);
        float x = 0, y = 0;
        bool found = false;
        if (target.rfind("menu:", 0) == 0) {
            float x0, y0, x1, y1;
            found = fe.ui().drawnBounds(qbkey::key(target.substr(5).c_str()), x0, y0, x1, y1);
            x = (x0 + x1) * 0.5f, y = (y0 + y1) * 0.5f;
        } else if (target.rfind("help:", 0) == 0) {
            if (const ui::Element* box = fe.ui().find(qbkey::key("user_control_container")))
                for (const auto& c : box->children)
                    if (c->text == target.substr(5) && c->drawnIn == fe.ui().drawCount())
                        x = (c->bx0 + c->bx1) * 0.5f, y = (c->by0 + c->by1) * 0.5f, found = true;
        } else if (target == "item" && host.walkPorts()) {
            float x0, y0, x1, y1;
            found = host.walkPorts()->itemRect(flowState(fe), x0, y0, x1, y1);
            x = (x0 + x1) * 0.5f, y = (y0 + y1) * 0.5f;
        } else if (target.rfind("row:", 0) == 0 && host.walkPorts()) {
            found = enhancedRowCenter(*host.walkPorts()->screen(PortScreens::Id::Enhanced), target.substr(4), x, y);
        } else if (target.rfind("option:", 0) == 0 && host.walkPorts()) {
            found = enhancedOptionCenter(*host.walkPorts()->screen(PortScreens::Id::Enhanced), std::atoi(target.c_str() + 7), x, y);
        }
        if (!found) return fail("the mouse: " + target + " is not on the screen");
        SDL_Log("walk: %s at %.0f, %.0f", what.c_str(), x, y);
        host.walkMouse(x, y, 0);
        if (button) host.walkMouse(x, y, 1);
        return true;
    }
    if (starts("tap:menu:")) {  // a finger down and up on an element (window-normalised, the walk's 1280 x 720 window)
        float x0, y0, x1, y1;
        if (!fe.ui().drawnBounds(qbkey::key(what.substr(9).c_str()), x0, y0, x1, y1)) return fail("the finger: " + what + " is not on the screen");
        const float nx = (x0 + x1) * 0.5f / 1280.0f, ny = (y0 + y1) * 0.5f / 720.0f;
        for (SDL_EventType t : {SDL_EVENT_FINGER_DOWN, SDL_EVENT_FINGER_UP}) {
            SDL_Event e{};
            e.type = t;
            e.tfinger.touchID = 1, e.tfinger.fingerID = 7;
            e.tfinger.x = nx, e.tfinger.y = ny, e.tfinger.pressure = t == SDL_EVENT_FINGER_DOWN ? 1.0f : 0.0f;
            SDL_PushEvent(&e);
        }
        return true;
    }
    if (what == "rclick") return host.walkMouse(640, 360, 3), true;
    if (starts("check:text:")) {  // check:text:<element id>!=<text>: the element's text is not that
        const std::string kv = rest("check:text:");
        const size_t ne = kv.find("!=");
        const ui::Element* e = fe.ui().find(qbkey::key(kv.substr(0, ne).c_str()));
        const std::string got = e ? e->text : "(no element)";
        std::printf("  text: %s = \"%s\"\n", kv.substr(0, ne).c_str(), got.c_str());
        return e && got != kv.substr(ne + 2) ? true : fail("text " + kv + ": " + got);
    }
    if (starts("frets:")) {  // frets:<hex pattern>: hold these frets (GetHeldPattern) until the next frets:
        fe.heldPattern = uint32_t(std::stoul(rest("frets:"), nullptr, 16));
        return true;
    }
    if (starts("key:")) {
        const SDL_Keycode k = SDL_GetKeyFromName(rest("key:").c_str());
        if (k == SDLK_UNKNOWN) return fail("no key " + rest("key:"));
        host.walkKey(int(k));
        return true;
    }
    if (starts("check:selected:")) {
        // The focused menu's selected item stands for that element (its focus handler names it).
        const uint32_t want = qbkey::key(rest("check:selected:").c_str());
        std::vector<const ui::Element*> stack{&fe.ui().root()};
        while (!stack.empty()) {
            const ui::Element* e = stack.back();
            stack.pop_back();
            for (const auto& c : e->children) stack.push_back(c.get());
            if (!e->focused || e->selected < 0 || size_t(e->selected) >= e->children.size()) continue;
            const ui::Element& sel = *e->children[size_t(e->selected)];
            if (sel.id == want) return true;
            for (const ui::Element::Handler& h : sel.handlers)
                if (const qb::Value* v = qs::Vm::param(h.params, qbkey::key("id")); v && v->key == want) {
                    std::printf("  mouse: the hover focused %s\n", rest("check:selected:").c_str());
                    return true;
                }
        }
        return fail("hovering did not focus " + rest("check:selected:"));
    }
    if (starts("check:tag:")) {
        // check:tag:<tag>.<field>=<int>: a live global tag's field.
        const std::string kv = rest("check:tag:");
        const size_t dot = kv.find('.'), eq = kv.find('=');
        const std::string tag = kv.substr(0, dot), field = kv.substr(dot + 1, eq - dot - 1);
        const int want = std::atoi(kv.c_str() + eq + 1);
        auto it = fe.vm().globalTags.find(qbkey::key(tag.c_str()));
        const qb::Value* v = it == fe.vm().globalTags.end() ? nullptr : qs::Vm::param(it->second, qbkey::key(field.c_str()));
        const int got = v ? int(v->type == qb::Type::Float ? v->f[0] : float(v->i)) : -999;
        std::printf("  tags: %s.%s = %d\n", tag.c_str(), field.c_str(), got);
        return got == want ? true : fail("tag " + kv + " is " + std::to_string(got));
    }
    if (starts("set:enhanced:")) {  // set:enhanced:<key>=<value>: an Enhanced option set
        const std::string kv = rest("set:enhanced:");
        const size_t eq = kv.find('=');
        host.walkSetEnhanced(kv.substr(0, eq), kv.substr(eq + 1));
        return true;
    }
    if (what == "wait") return true;  // one step (300 ms) of nothing
    if (what == "unlock:all") {  // every save tag with an `unlocked` field set to 1 (the venues as a finished career has them)
        int n = 0;
        for (auto& [k, st] : fe.vm().globalTags)
            if (qs::Vm::param(st, qbkey::key("unlocked"))) qs::Vm::set(st, qbkey::key("unlocked"), qs::Vm::makeInt(1)), ++n;
        std::printf("  unlocked: %d tags\n", n);
        return true;
    }
    if (starts("check:focus:")) {  // check:focus:<id>: that element is the one focused (no other item focused)
        const uint32_t want = qbkey::key(rest("check:focus:").c_str());
        int items = 0;
        bool has = false;
        for (const ui::Element* e : fe.ui().focusedElements())
            if (e->id != qbkey::key("root_window")) ++items, has = has || e->id == want;
        std::printf("  focus: %s %s (%d focused)\n", rest("check:focus:").c_str(), has ? "yes" : "no", items);
        return has && items == 1 ? true : fail("focus is not on " + rest("check:focus:") + " alone");
    }
    if (what == "log:focus") {  // the focused menu item's id and text (development aid)
        for (const ui::Element* e : fe.ui().focusedElements())
            std::printf("  focus: %s \"%s\"\n", qbkey::name(e->id).c_str(), e->text.c_str());
        return true;
    }
    if (starts("set:global:")) {  // set:global:<name>=<number>: a GH3 global set (a test's shortcut)
        const std::string kv = rest("set:global:");
        const size_t eq = kv.find('=');
        fe.vm().setGlobal(qbkey::key(kv.substr(0, eq).c_str()), qs::Vm::makeInt(std::atoi(kv.c_str() + eq + 1)));
        return true;
    }
    if (starts("check:global:")) {  // check:global:<name>=<number>: a GH3 global's value
        const std::string kv = rest("check:global:");
        const size_t eq = kv.find('=');
        const qb::Value* v = fe.vm().global(qbkey::key(kv.substr(0, eq).c_str()));
        const float got = v ? v->number() : -1;
        std::printf("  global: %s = %g\n", kv.substr(0, eq).c_str(), double(got));
        return got == std::atof(kv.c_str() + eq + 1) ? true : fail("global " + kv);
    }
    if (what == "dialog:savedir") {  // GD04_DIALOG: the user-file dialogs answered with the walk's save folder
        SDL_setenv_unsafe("GD04_DIALOG", host.walkSaveDir().c_str(), 1);
        return true;
    }
    if (starts("check:enhanced:")) {
        const std::string kv = rest("check:enhanced:");
        const size_t eq = kv.find('=');
        const std::string v = host.walkEnhanced(kv.substr(0, eq));
        std::printf("  mouse: %s is %s\n", kv.substr(0, eq).c_str(), v.c_str());
        return v == kv.substr(eq + 1) ? true : fail("Enhanced " + kv.substr(0, eq) + " is " + v);
    }
    if (what == "check:whammy_is_e") {
        bool bound = false;
        for (const Input::Source& src : host.walkInput().bindings("keyboard")[size_t(Input::Control::Whammy)])
            bound = bound || (src.kind == Input::Source::Kind::Key && src.index == int(SDLK_E));
        std::ifstream f(host.walkSaveDir() + "controllers.json");
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
        return bound && saved ? true : fail("the rebinding of the keyboard's Whammy to E did not take or was not saved");
    }
    if (what == "xpad_attach") {  // a virtual Xbox 360 pad (player 1 when plugged in first)
        adapter_ = unsigned(Input::attachVirtualPad("045e:028e"));
        return adapter_ ? true : fail("no virtual pad");
    }
    if (what == "pad_detach") {
        SDL_DetachVirtualJoystick(SDL_JoystickID(adapter_));
        return true;
    }
    if (what == "keyboard_p2") {  // the keyboard given player 2 (the Gamepads screen's Bind to)
        host.walkInput().setPlayer("keyboard", 1);
        return true;
    }
    if (what == "adapter_attach") {
        adapter_ = unsigned(Input::attachVirtualPad("0079:0006:dpadaxis=-32768"));
        sideBase_ = presses_[size_t(Action::Left)] + presses_[size_t(Action::Right)];
        rightBase_ = presses_[size_t(Action::Right)];
        return adapter_ ? true : fail("no virtual adapter");
    }
    if (what == "check:no_sideways") {
        const int n = presses_[size_t(Action::Left)] + presses_[size_t(Action::Right)] - sideBase_;
        std::printf("  stickyaxis: %d left / right presses at rest\n", n);
        return n == 0 ? true : fail("the resting adapter pressed left / right by itself");
    }
    if (what == "adapter_move") {
        if (SDL_Joystick* j = SDL_GetJoystickFromID(SDL_JoystickID(adapter_))) SDL_SetJoystickVirtualAxis(j, 6, 0);
        return true;
    }
    if (what == "check:one_right") {
        const int r = presses_[size_t(Action::Right)] - rightBase_;
        std::printf("  stickyaxis: %d right press(es) once moved\n", r);
        if (SDL_Joystick* j = SDL_GetJoystickFromID(SDL_JoystickID(adapter_))) SDL_SetJoystickVirtualAxis(j, 6, -32768);
        return r == 1 ? true : fail("the moved adapter did not press right once");
    }
    return fail("unknown act " + what);
}

int Walk::finish(FrontEnd& fe) const {
    std::string path;
    for (const std::string& s : log_) path += (path.empty() ? "" : " > ") + s;
    // Every sound effect reached the device (0.1.12: none did, the menus were silent).
    const Sfx::Stats& sx = fe.sfx.stats();
    std::printf("sound effects: %llu played, %llu lost\n", (unsigned long long)sx.plays, (unsigned long long)sx.lost);
    if (!failed_ && done() && sx.lost) {
        std::printf("walktest %s: FAILED (sound effects lost on the device); reached: %s\n", name_.c_str(), path.c_str());
        return 1;
    }
    if (failed_ || !done()) {
        std::printf("walktest %s: FAILED (%s); reached: %s\n", name_.c_str(),
                    failure_.empty() ? "did not finish in time" : failure_.c_str(), path.c_str());
        return 1;
    }
    if (uitest_) {
        std::printf("uitest: %zu flow states started, %d built no element:", states_.size(), empty_);
        for (const std::string& n : emptyStates_) std::printf(" %s", n.c_str());
        std::printf("\nuitest: every screen was started through the flow manager\n");
        return 0;
    }
    // Every loading screen stayed up 2 s (GD04-040).
    const double shown = fe.vm().minLoadingShownMs();
    if (shown >= 0) std::printf("walk: loading screens up at least %.0f ms\n", shown);
    if (shown >= 0 && shown < qs::Vm::kLoadingHoldMs - 50) {
        std::printf("walktest %s: FAILED (a loading screen passed in %.0f ms); reached: %s\n", name_.c_str(), shown, path.c_str());
        return 1;
    }
    std::printf("walktest %s: passed (%s); songs started %d, ended %d\n", name_.c_str(), path.c_str(), songsStarted, songsEnded);
    return 0;
}
