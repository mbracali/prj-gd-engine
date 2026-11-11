// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/walk.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

#include "audio/audio_device.h"
#include "data/band.h"
#include "data/json.h"
#include "data/qbkey.h"
#include "render/canvas.h"
#include "game/save.h"
#include "script/vm.h"
#include "ui/front_end.h"
#include "ui/port_screens.h"

namespace {

// The flow state player 1 is in (ui_flow_manager_state[0]).
std::string flowState(FrontEnd& fe) { return fe.flowState(); }  // gh4: the UI state machine's top (D5)

}  // namespace

Walk::Walk(const std::string& name) : name_(name) {
    const Action C = Action::Confirm, D = Action::Down, B = Action::Back;
    // Boot -> press any button -> save -> main menu -> Quickplay ->
    // difficulty -> setlist -> song -> results [C: flow_path.md].
    // gh4's boot states (ui/ui_states.h): the movies skipped with A, press
    // start, the drum tip pop-up (only without a save), the main menu [C
    // scripts RegisterUIStates_Boot, ui_boot_* ; docs/ui.md].
    std::vector<Step> boot = {
        {"UIstate_boot_movie_atvi", {"wait", C}, 30000, true},
        {"UIstate_boot_movie_red_octane", {"wait", C}, 30000, true},
        {"UIstate_boot_movie_neversoft", {"wait", C}, 30000, true},
        {"UIstate_boot_movie_intro", {"wait", C}, 30000, true},
        {"uistate_boot_iis", {"wait", "wait", C}, 60000},
        {"uistate_optimal_drum", {"wait", "wait", C}, 60000, true},
        {"main_menu_fs", {}, 60000},
    };
    if (name == "qp_pause") {
        // gh4 pause: Quickplay to a song, Esc pauses (the pause menu), Back
        // resumes, Esc again, then the pause menu's items one by one.
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {"wait", "wait", "wait", "wait", "wait", "wait", "wait", D, C}, 10000});
        steps_.push_back({"uistate_game_mode", {"wait", "wait", C}, 20000});
        steps_.push_back({"uistate_select_instrument", {"wait", "wait", C}, 20000});
        steps_.push_back({"UIstate_setlist_prompt", {"wait", "wait", "wait", C}, 20000, true});
        steps_.push_back({"uistate_setlist", {"wait", "wait", "wait", "wait", "wait", "wait", "wait", "wait", D, "wait", C, "wait", "wait", "key:Return"}, 20000});
        steps_.push_back({"UIstate_setlist_prompt", {"wait", "wait", "wait", C}, 20000, true});
        steps_.push_back({"UIstate_double_bass_popup", {"wait", "wait", "wait", C}, 20000, true});
        steps_.push_back({"uistate_setlist", {"wait", "wait", "wait", "wait", "wait", "wait", "wait", "wait", D, "wait", C, "wait", "wait", "key:Return"}, 20000});
        steps_.push_back({"uistate_select_difficulty", {"wait", "wait", "wait", "wait", D, D, D, D, "wait", C}, 20000});
        steps_.push_back({"song", {Op("wait", 3000), "key:Escape"}, 30000});
        steps_.push_back({"uistate_pausemenu", {"wait", "wait", "wait", "shot:pause_menu@", B}, 20000});
        steps_.push_back({"song", {Op("wait", 2000), "key:Escape"}, 20000});
        steps_.push_back({"uistate_pausemenu", {"wait", "wait", "wait", D, "wait", "shot:pause_1@", D, "wait", "shot:pause_2@", D, "wait",
                                                "shot:pause_3@", D, "wait", "shot:pause_4@", D, "wait", "shot:pause_5@", "wait"}, 20000});
    } else if (name == "qp4" || name == "quickplay") {
        // gh4 (Metallica) Quickplay (M4): the main menu's
        // QUICKPLAY (second item), single player, guitar, the set list.
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {"wait", "wait", "wait", "wait", "wait", "wait", "wait", D, C}, 10000});
        steps_.push_back({"uistate_game_mode", {"wait", "wait", C}, 20000});
        steps_.push_back({"uistate_select_instrument", {"wait", "wait", C}, 20000});
        steps_.push_back({"UIstate_setlist_prompt", {"wait", "wait", "wait", C}, 20000, true});
        steps_.push_back({"uistate_setlist", {"wait", "wait", "wait", "wait", "wait", "wait", "wait", "wait", D, "wait", C, "wait", "wait", "key:Return"}, 20000});
        steps_.push_back({"UIstate_setlist_prompt", {"wait", "wait", "wait", C}, 20000, true});
        steps_.push_back({"UIstate_double_bass_popup", {"wait", "wait", "wait", C}, 20000, true});
        // A song joins the set list (pad_choose), Start plays it (gh4's custom set list) [C scripts].
        steps_.push_back({"uistate_setlist", {"wait", "wait", "wait", "wait", "wait", "wait", "wait", "wait", D, "wait", C, "wait", "wait", "key:Return"}, 20000});
        steps_.push_back({"uistate_select_difficulty", {"wait", "wait", "wait", "wait", D, D, D, D, "wait", C}, 20000});
        steps_.push_back({"song", {}, 30000});
        steps_.push_back({"song_over", {}, 600000});
        // The results (song breakdown), then on.
        steps_.push_back({"uistate_song_breakdown", {"wait", "wait", "wait", "wait", "wait", "wait", C}, 30000});
        steps_.push_back({"uistate_cash_rewards_3", {"wait", "wait", "wait", "wait", "wait", "wait", C}, 30000, true});
        steps_.push_back({"uistate_setlist", {"wait"}, 30000});
    } else if (name == "quickplay" || name == "lose") {
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {D, D, C}, 10000});  // career, co-op career, QUICKPLAY
        // The keyboard's Enter selects in the menus (the owner's rule: Enter is Start).
        steps_.push_back({"quickplay_select_difficulty_fs", {"key:Return"}, 20000});
        steps_.push_back({"quickplay_setlist_fs", {"clear:topscores", C}, 20000});
        steps_.push_back({"song", {}, 30000});
        steps_.push_back({"song_over", {}, 600000});
        if (name == "lose") {
            // A few steps on the fail menu, over the paused song (screenshots).
            steps_.push_back({"quickplay_fail_song_fs", {"wait", "wait", "wait"}, 30000});
        } else {
            // CONTINUE -> Top Rockers: the autoplayed score beats the scores
            // (cleared: the walk's song is cut short), so GH3 asks for
            // the name (menu_top_rockers_init_band_name_creation, score_name1
            // focused); the keyboard's Enter is Start there: ACCEPT
            // (menu_tr_confirm_band_name -> continue -> autosave -> setlist)
            // instead of NEXT (pad_choose: the next letter, the screen stays) [C scripts].
            steps_.push_back({"quickplay_win_song_fs", {C}, 30000});
            // The autosave's overwrite shows its SAVING... popup: no black
            // frame from here to the setlist (GD10-093).
            steps_.push_back({"quickplay_toprockers_fs", {"check:focus:score_name1", "probe:black", "key:Return"}, 30000});
            steps_.push_back({"quickplay_setlist_fs", {Op("check:no_black", 1000)}, 30000});
        }
    } else if (name == "results_art") {
        // A quick play song as GD10_WALK_CHARACTER ("<name>[,<outfit>,<style>]",
        // default Morello: a body with no asset_context), set while it plays;
        // the results art must be that character's (check:mag_photo).
        const char* who = SDL_getenv("GD10_WALK_CHARACTER");
        const std::string setChar = std::string("set:character:") + (who && *who ? who : "Morello");
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {D, D, C}, 10000});
        steps_.push_back({"quickplay_select_difficulty_fs", {C}, 20000});
        steps_.push_back({"quickplay_setlist_fs", {C}, 20000});
        steps_.push_back({"song", {setChar.c_str()}, 30000});
        steps_.push_back({"song_over", {}, 600000});
        steps_.push_back({"quickplay_win_song_fs", {"check:mag_photo"}, 30000});
    } else if (name == "career") {
        // Career: band -> difficulty -> character hub -> venue -> setlist ->
        // the first song -> won -> newspaper -> autosave -> setlist [C: the
        // flow states GH3's own scripts walk, GD10_FLOWLOG].
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {C}, 10000});  // CAREER
        steps_.push_back({"career_choose_band_fs", {C}, 20000, true});  // not on the first play
        // The name: three letters (NEXT) then ACCEPT (Start) [C script menu_ebn
        // handlers], by the keyboard's Enter (Start on a text entry).
        steps_.push_back({"career_enter_band_name_fs", {C, C, C, "key:Return"}, 20000, true});
        steps_.push_back({"career_select_difficulty_fs", {C}, 20000});
        steps_.push_back({"career_select_character_fs", {C}, 20000, true});  // first play
        // The career's first venue select plays singleplayer_01 and marks the band
        // (create_select_venue_menu; `$ game_mode_names . $ game_mode`, owner 1.5).
        steps_.push_back({"career_character_hub_fs", {"check:tag:band1_info_p1_career.first_venue_movie_played=0", "check:global:game_mode=p1_career", C}, 20000});
        steps_.push_back({"career_select_venue_fs", {"check:tag:band1_info_p1_career.first_venue_movie_played=1", C}, 20000});
        steps_.push_back({"career_setlist_fs", {C}, 20000});
        steps_.push_back({"song", {}, 30000});
        steps_.push_back({"song_over", {}, 600000});
        steps_.push_back({"career_newspaper_fs", {"check:mag_photo", C}, 30000});
        steps_.push_back({"career_cash_reward_fs", {C}, 30000, true});  // the song's cash (Progression_AwardCash)
        steps_.push_back({"career_unlock_1_fs", {C}, 30000, true});
        steps_.push_back({"career_setlist_fs", {}, 30000});
    } else if (name == "career_encore") {
        // #84 (GD10-116): a new band on Medium wins three of tier 1's songs
        // (GH3_Career_NumSongToProgress medium = 3); the third win's
        // UpdateAtoms Progression completes the tier's songs and runs
        // Progression_TierEncoreUnlock (current_transition = preencore) ->
        // GuitarEvent_SongWon_Spawned's preencore -> preencore_win_song ->
        // career_encore_confirmation_fs -> start_encore: the encore (Rock and
        // Roll All Nite, encorep1) -> won: Progression_TierEncoreComplete,
        // Progression_TierComplete (careertier1.complete, tier 2 unlocked)
        // [C scripts guitar_progression.qb, the career flow states].
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {C}, 10000});
        steps_.push_back({"career_choose_band_fs", {C}, 20000, true});
        steps_.push_back({"career_enter_band_name_fs", {C, C, C, Action::Start}, 20000, true});
        steps_.push_back({"career_select_difficulty_fs", {D, C}, 20000});  // MEDIUM
        steps_.push_back({"career_select_character_fs", {C}, 20000, true});
        steps_.push_back({"career_character_hub_fs", {"check:global:current_difficulty=medium", C}, 20000});
        steps_.push_back({"career_select_venue_fs", {C}, 20000});
        const char* songs[] = {"slowride", "talkdirtytome", "hitmewithyourbestshot"};
        for (int i = 0; i < 3; ++i) {
            steps_.push_back({"career_setlist_fs", {Op(std::string("setlist:") + songs[i]), C}, 30000});
            steps_.push_back({"song", {}, 30000});
            steps_.push_back({"song_over", {}, 600000});
            if (i == 2) break;
            // No encore before the third win.
            steps_.push_back({"career_newspaper_fs", {Op("check:tag:careertier1.num_songs_to_progress=" + std::to_string(2 - i)), C}, 30000});
            steps_.push_back({"career_cash_reward_fs", {C}, 30000, true});
            steps_.push_back({"career_unlock_1_fs", {C}, 30000, true});
        }
        steps_.push_back({"career_encore_confirmation_fs",
                          {"check:tag:careertier1.num_songs_to_progress=0", "check:global:setlist_previous_tier=1", "check:global:setlist_previous_song=4", "check:tag:careertier1.complete=0",
                           // PLAY ENCORE once the score, cash, stars and the crowd's shout are in (g_encore_ready)
                           Op("check:global:g_encore_ready=1", 8000), C},
                          60000});
        steps_.push_back({"song", {"check:global:current_song=rocknrollallnite"}, 60000});
        steps_.push_back({"song_over", {}, 600000});
        steps_.push_back({"career_newspaper_fs", {"check:tag:careertier1.complete=1", "check:global:progression_completion_tier=1", C}, 60000});
        // The tier complete: the sponsor (Progression_TierEncoreComplete), then on to tier 2.
        steps_.push_back({"career_sponsored_fs", {}, 60000});
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
        // GD10-041: Enhanced > Developer Debug Menu on -> the pause menu
        // offers GH3's DEBUG MENU (enable_button_cheats) -> its debug menu.
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {"set:enhanced:dev_debug_menu=on", D, D, C}, 10000});
        steps_.push_back({"quickplay_select_difficulty_fs", {C}, 20000});
        steps_.push_back({"quickplay_setlist_fs", {C}, 20000});
        steps_.push_back({"song", {Action::Start}, 30000});
        steps_.push_back({"quickplay_pause_fs", {"check:text:pause_debug_menu!=", D, D, D, D, D, C}, 20000});
        steps_.push_back({"debug_menu_fs", {"check:text:debug_vmenu!=never"}, 20000});
    } else if (name == "pad_unplugged") {
        // GD10-042: a pad as player 1, the keyboard given player 2; the pad
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
            // The help bar's pills take the mouse over the song too: UP/DOWN's
            // lower half (down) and SELECT pick QUIT.
            steps_.push_back({"quickplay_pause_fs", {D, D, D, "click:helplow:UP/DOWN", "click:help:SELECT"}, 20000});
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
        // First The Glitches' Vault (all bought for their band, GD10-026),
        // saved while their career is popped, then left: band 1's Vault
        // and the save must not have their purchases (GD10-096).
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {C}, 10000});
        steps_.push_back({"career_choose_band_fs", {D, D, D, D, C}, 20000});
        steps_.push_back({"career_select_difficulty_fs", {C}, 20000});
        steps_.push_back({"career_character_hub_fs", {D, D, D, D, C}, 20000});  // VISIT STORE
        steps_.push_back({"store_fs", {C}, 20000});
        steps_.push_back({"store_guitars_fs",
                          {"check:tag:Instrument_focus01.unlocked=1", "save:write", "check:save:Instrument_focus01.unlocked=0",
                           "check:save:Guitarist_Judy_Outfit2_Style2.unlocked=0", B},
                          20000});
        steps_.push_back({"store_fs", {B}, 20000});
        steps_.push_back({"career_character_hub_fs", {B}, 20000});
        steps_.push_back({"career_select_character_fs", {B}, 20000});
        steps_.push_back({"career_select_difficulty_fs", {B}, 20000});
        steps_.push_back({"career_choose_band_fs", {Action::Up, Action::Up, Action::Up, Action::Up, C}, 20000});
        steps_.push_back({"career_enter_band_name_fs", {C, C, C, Action::Start}, 20000, true});
        steps_.push_back({"career_select_difficulty_fs", {C}, 20000});
        steps_.push_back({"career_select_character_fs", {C}, 20000, true});
        steps_.push_back({"career_character_hub_fs", {D, D, D, D, C}, 20000});  // VISIT STORE
        steps_.push_back({"store_fs", {C}, 20000});
        // The first category; band 1's Vault is the save's own (not The Glitches', GD10-026).
        steps_.push_back({"store_guitars_fs",
                          {"check:tag:Instrument_focus01.unlocked=0", "check:tag:Guitarist_Judy_Outfit2_Style2.unlocked=0", "save:write",
                           "check:save:Instrument_focus01.unlocked=0", B},
                          20000});
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
        chords.push_back(Op("key:Return"));  // FINISH ENTERING CHEAT: the keyboard's Enter is Start on the entry
        steps_.push_back({"options_enter_new_cheat_fs", chords, 20000});
        steps_.push_back({"options_cheats_fs", {"check:text:Cheat_PerformanceMode_Text!=locked"}, 20000});
    } else if (name == "glitches") {
        // The Glitches: band slot 5, everything unlocked (ui/glitches.h): its
        // name and cash, then chosen, its career's last tier complete.
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {"check:tag:band5_info_p1_career.cash=1000000", C}, 10000});
        steps_.push_back({"career_choose_band_fs", {D, D, D, D, C}, 20000});
        steps_.push_back({"career_select_difficulty_fs", {Action::Down, Action::Down, Action::Down, C}, 20000});
        // The Vault all bought for this band only (GD10-026).
        steps_.push_back({"career_character_hub_fs",
                          {"check:tag:careertier8.complete=1", "check:tag:career_song1_tier8.stars=5", "check:tag:Instrument_focus01.unlocked=1", C},
                          20000});
        // Its venues are unlocked too: the list moves (owner report 1.4). The
        // career's atoms (GD10-116) leave current_level on the last tier's
        // venue (Progression_UnlockTier 8: z_hell), so the list starts there.
        steps_.push_back({"career_select_venue_fs", {"check:tag:venue_z_hell.unlocked=1", "check:focus:venue_7", "key:Down", "wait", "check:focus:venue_6", B}, 20000});
        steps_.push_back({"career_character_hub_fs", {}, 20000});

    } else if (name == "setlist_tabs") {
        // GD10-090: quick play's set list tabs: blue BONUS (the bonus songs),
        // orange DOWNLOADS (none on the disc), yellow SETLIST [C data
        // setlist_event_handlers, change_tab].
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {D, D, C}, 10000});
        steps_.push_back({"quickplay_select_difficulty_fs", {C}, 20000});
        steps_.push_back({"quickplay_setlist_fs",
                          {"wait", "check:global:current_tab=tab_setlist", Action::Fret3, "wait", "wait", "check:global:current_tab=tab_bonus",
                           "check:text:sl_text_1!=", "log:focus", Action::Down, "log:focus", Action::Fret4, "wait", "wait",
                           "check:global:current_tab=tab_downloads", Action::Fret2, "wait", "wait", "check:global:current_tab=tab_setlist",
                           // A bonus song bought (its save tag unlocked): listed and played.
                           "set:tag:avalancha.unlocked=1", Action::Fret3, "wait", "wait", C},
                          20000});
        steps_.push_back({"song", {"check:global:current_tab=tab_bonus", "check:global:current_song=avalancha"}, 30000});

    } else if (name == "cutcontent") {
        // GD10-110: Enable Cut Content removed from Enhanced Options: a saved
        // "on" is dropped on load, a set to on refused, the row is gone; The
        // Glitches (every character unlocked) -> CHANGE CHARACTER -> down
        // past the last character: GH3's 13 guitarists on one page (the
        // GD10-094 paging stays, unreachable).
        steps_ = boot;
        steps_.push_back({"main_menu_fs",
                          {"check:cut_content_ignored", "set:enhanced:cut_content=on", "check:enhanced:cut_content=off", "check:backends_saved",
                           "click:menu:main_menu_options_text"},
                          10000});
        steps_.push_back({"options_select_option_fs", {"click:item"}, 20000});
        steps_.push_back({"port_enhanced", {"hover:row:no_fail", "check:norow:cut_content", "rclick"}, 10000});
        steps_.push_back({"options_select_option_fs", {B}, 20000});
        steps_.push_back({"main_menu_fs", {"wait", C}, 20000});
        steps_.push_back({"career_choose_band_fs", {D, D, D, D, C}, 20000});
        steps_.push_back({"career_select_difficulty_fs", {C}, 20000});
        steps_.push_back({"career_character_hub_fs", {D, C}, 20000});  // CHANGE CHARACTER
        std::vector<Op> cs{Op("wait"), Op("wait")};
        for (int i = 0; i < 20; ++i) cs.push_back(Op(D, 300));
        cs.push_back(Op("log:cs"));
        cs.push_back(Op("check:global:g_cs_jumped_p1=0"));
        cs.push_back(Op("check:global:cs_total_guitarists_p1=13"));
        // Quick play's random guitarist keeps to GH3's 13 (GD10-091).
        cs.push_back(Op("check:qp_guitarists=13"));
        steps_.push_back({"career_select_character_fs", cs, 20000});

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
        // The boss after the song that unlocks it (GD10-038): a new band on
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
    } else if (name == "menus") {
        // Every main menu item (CAREER .. MUSIC STUDIO), its screen and the
        // focused item under it, each saved (GD10_WALK_SHOTS), then Back to the
        // main menu, which must come back each time [C: create_main_menu's
        // seven items].
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {"wait", "wait", "wait", "wait", "wait", "wait", "wait", "shot:menus_main@"}, 20000});
        // GD10_WALK_MENUS=<digits>: only those items (e.g. "46").
        const char* only = SDL_getenv("GD10_WALK_MENUS");
        for (int i = 0; i < 7; ++i) {
            if (only && *only && !std::strchr(only, '0' + i)) continue;
            std::vector<Op> k = {"wait", "wait"};
            for (int d = 0; d < i; ++d) k.push_back(D);
            const std::string tag = "menus_" + std::to_string(i);
            for (Op o : {Op(C), Op("wait"), Op("wait"), Op("wait"), Op("wait"), Op("wait"), Op("wait"), Op("shot:" + tag + "a@"), Op("wait"),
                         Op(C), Op("wait"), Op("wait"), Op("wait"), Op("wait"), Op("wait"), Op("wait"), Op("shot:" + tag + "b@"), Op(B), Op("wait"),
                         Op("wait"), Op("wait"), Op("wait"), Op("backto:main_menu_fs")})
                k.push_back(o);
            steps_.push_back({"main_menu_fs", k, 30000});
        }
        steps_.push_back({"main_menu_fs", {"wait", "wait", "wait", "shot:menus_end@"}, 30000});
    } else if (name == "fuzz") {
        // Random menu presses from the main menu (GD10_SEED, GD10_FUZZ_PRESSES,
        // default 300); then Back to the main menu.
        const char* n = SDL_getenv("GD10_FUZZ_PRESSES");
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {"wait", "wait", "wait", "wait", "wait", "wait", "wait", Op(std::string("fuzz:") + (n && *n ? n : "300")), "wait", "wait", "wait", "wait", "backto:main_menu_fs"}, 20000});
        steps_.push_back({"main_menu_fs", {"wait", "shot:fuzz_end@"}, 20000});
    } else if (name == "back") {
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {D, D, C}, 10000});
        steps_.push_back({"quickplay_select_difficulty_fs", {B}, 20000});
        steps_.push_back({"main_menu_fs", {}, 20000});
    } else if (name == "enhanced_backends") {
        // GDCORE-010 (universal-menus.md "Backends"): main menu -> OPTIONS ->
        // ENHANCED -> Audio Backend miniaudio: Restart Now / Later opens, Back
        // is Later (the value kept, the screen stays) -> OpenAL -> the
        // pop-up's Restart Now clicked: the game starts again in this
        // process, back on Enhanced Options (enhanced_backends_restarted).
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {"click:menu:main_menu_options_text"}, 10000});
        steps_.push_back({"options_select_option_fs", {"click:item"}, 20000});
        steps_.push_back({"port_enhanced",
                          {"check:audio_now=local", "click:row:audio_backend", "click:option:1", "check:asking=1", B, "check:asking=0",
                           "check:enhanced:audio_backend=miniaudio", "click:row:graphics_backend", "click:option:0", "check:asking=0",
                           "click:row:audio_backend", "click:option:2", "check:asking=1", "click:ask:0"},
                          10000});
    } else if (name == "enhanced_backends_restarted") {
        // After Restart Now: straight back on Enhanced Options (no boot
        // screens), on OpenAL, with the walk's settings; Back leads to GH3's
        // options and the main menu as always.
        steps_.push_back({"port_enhanced", {"check:enhanced:audio_backend=openal", "check:audio_now=openal", "check:asking=0", B}, 90000});
        steps_.push_back({"options_select_option_fs", {B}, 20000});
        steps_.push_back({"main_menu_fs", {}, 20000});
    } else if (name == "mouse") {
        // The mouse alone: hover OPTIONS on the main menu (focused), click it
        // -> Options -> click the help bar's ENHANCED -> In-Game Max FPS's
        // dropdown clicked open, 120 clicked -> right click -> Options -> the
        // help bar's BACK clicked -> main (docs/guidelines/universal_launcher.md).
        steps_ = boot;
        // ONLINE is greyed and skipped (GH3's demo_mode_disable, GD10-101):
        // Up from CAREER wraps to OPTIONS, the pad's Down and the keyboard's
        // back to CAREER, and the mouse over ONLINE focuses nothing.
        steps_.push_back({"main_menu_fs",
                          {Action::Up, "check:selected:main_menu_options_text", "key:Down", "check:selected:main_menu_career_text", Action::Up, D,
                           "check:selected:main_menu_career_text", "hover:menu:main_menu_leaderboards_text", "check:selected:main_menu_career_text",
                           "hover:menu:main_menu_options_text", "check:selected:main_menu_options_text", "click:menu:main_menu_options_text"},
                          10000});
        steps_.push_back({"options_select_option_fs", {"click:item"}, 20000});
        steps_.push_back({"port_enhanced",
                          {"hover:row:max_fps", "click:row:max_fps", "click:option:1", "check:enhanced:max_fps=120",
                           // Audio Backend (GDCORE-010): local, the one running, chosen again asks nothing.
                           "click:row:audio_backend", "click:option:0", "check:asking=0", "check:enhanced:audio_backend=local",
                           // Export User Files, a setting changed, Load User Settings brings it back (the dialogs answered with the save folder).
                           // Accessibility > Easy Chords (GDALL-039): off on a fresh save; on, exported, and loaded back.
                           "check:enhanced:easy_chords=off", "click:row:easy_chords", "check:enhanced:easy_chords=on", "shot:enhanced_accessibility",
                           "dialog:savedir", "click:row:export", "set:enhanced:max_fps=60", "set:enhanced:easy_chords=off", "click:row:load_settings",
                           "check:enhanced:max_fps=120", "check:enhanced:easy_chords=on", "click:row:load_progress", "set:enhanced:audio_offset=40", "set:enhanced:video_offset=-20", "click:row:calibrate_reset", "check:enhanced:audio_offset=0", "check:enhanced:video_offset=0",
                           "click:row:cheat_air_guitar", "check:global:Cheat_AirGuitar=1", "click:row:cheat_air_guitar", "check:global:Cheat_AirGuitar=2",
                           "rclick"},
                          10000});
        steps_.push_back({"options_select_option_fs", {"click:help:BACK"}, 10000});
        steps_.push_back({"main_menu_fs", {}, 20000});
    } else if (name == "mouseall") {
        // GD10-111: the help bar's pills on every screen the walk reaches take
        // the mouse over all they draw (check:pills), and clicked (at their
        // backing's left end: click:helpend) do what their buttons do; the
        // port's screens' three pills too (click:porthelp).
        const Op W("wait"), P("check:pills");
        steps_ = boot;
        steps_.push_back({"main_menu_fs",
                          {W, W, P, "click:helpend:UP/DOWN", "check:selected:main_menu_options_text", "click:helplow:UP/DOWN",
                           "check:selected:main_menu_career_text", "click:helpend:GAMEPADS"},
                          10000});
        steps_.push_back({"port_gamepads", {Op("click:porthelp:2low", 700), "click:porthelp:2top", "click:porthelp:1"}, 10000});
        steps_.push_back({"main_menu_fs", {W, "click:menu:main_menu_options_text"}, 10000});
        steps_.push_back({"options_select_option_fs", {W, W, P, "click:helpend:ENHANCED"}, 20000});
        steps_.push_back({"port_enhanced", {Op("click:porthelp:2low", 700), "click:porthelp:2top", "click:porthelp:1"}, 10000});
        const std::pair<int, const char*> screens[] = {{0, "options_audio_settings_fs"},       {1, "options_calibrate_lag_fs"},
                                                       {2, "options_controller_settings_fs"}, {4, "options_data_settings_fs"},
                                                       {5, "options_bonus_videos_fs"},        {7, "options_top_rockers_difficulty_select_fs"},
                                                       {8, "options_cheats_fs"}};
        for (const auto& [downs, state] : screens) {
            std::vector<Op> go{W};
            for (int k = 0; k < downs; ++k) go.push_back(D);
            go.push_back(C);
            steps_.push_back({"options_select_option_fs", go, 20000});
            steps_.push_back({state, {W, W, W, P, "click:helpend:BACK"}, 20000});
        }
        steps_.push_back({"options_select_option_fs", {W, W, "click:helpend:BACK"}, 20000});
        steps_.push_back({"main_menu_fs", {W, D, D, C}, 20000});  // QUICKPLAY
        steps_.push_back({"quickplay_select_difficulty_fs", {W, W, P, "click:helpend:SELECT"}, 20000});
        // The set list's five pills touch (narrowed to the safe width): each takes its own clicks.
        steps_.push_back({"quickplay_setlist_fs",
                          {W, W, W, P, "click:helpend:BONUS", W, W, "check:global:current_tab=tab_bonus", P, "click:helpend:DOWNLOADS", W, W,
                           "check:global:current_tab=tab_downloads", P, "click:helpend:BACK"},
                          20000});
        steps_.push_back({"quickplay_select_difficulty_fs", {W, "click:helpend:BACK"}, 20000});
        steps_.push_back({"main_menu_fs", {W, C}, 20000});  // CAREER
        steps_.push_back({"career_choose_band_fs", {W, W, P, C}, 20000, true});
        steps_.push_back({"career_enter_band_name_fs", {W, W, P, C, C, C, W, "click:helpend:ACCEPT"}, 20000, true});
        steps_.push_back({"career_select_difficulty_fs", {W, W, P, "click:helpend:SELECT"}, 20000});
        steps_.push_back({"career_select_character_fs", {W, W, W, P, C}, 20000, true});
        steps_.push_back({"career_character_hub_fs", {W, W, P, D, D, D, D, C}, 20000});  // VISIT STORE
        steps_.push_back({"store_fs", {W, W, W, P, "click:helpend:BACK"}, 20000});
        steps_.push_back({"career_character_hub_fs", {W, C}, 20000});  // PLAY SHOW
        steps_.push_back({"career_select_venue_fs", {W, W, P, "click:helpend:SELECT"}, 20000});
        steps_.push_back({"career_setlist_fs", {W, W, W, P, C}, 20000});
        steps_.push_back({"song", {Op(Action::Start, 3000)}, 30000});
        // The pause menu (no BACK pill: Start resumes) -> OPTIONS and back -> QUIT -> its warning: QUIT.
        steps_.push_back({"career_pause_fs", {W, W, P, D, D, D, "click:helpend:SELECT"}, 20000});  // OPTIONS
        steps_.push_back({"career_pause_options_fs", {W, W, P, "click:helpend:BACK"}, 20000});
        steps_.push_back({"career_pause_fs", {W, W, D, D, D, D, "click:helpend:SELECT"}, 20000});  // QUIT
        // The popups' items (no help bar) take hover and click: QUIT (not
        // CANCEL), then NEW SONG (pausequit's keys, by the mouse).
        steps_.push_back({"career_quit_warning_fs", {W, W, "hover:menuitem:1", "check:menusel:1", "click:menuitem:1"}, 20000});
        steps_.push_back({"career_song_ended_fs", {Op("wait", 4000), "hover:menuitem:1", "check:menusel:1", "click:menuitem:1"}, 20000});
        steps_.push_back({"career_setlist_fs", {}, 40000});
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
        // A new career starts on tier 1's venue: Progression_Init, the first
        // atom, sets current_level (z_party) (GD10-116; before, the global's
        // default z_artdeco).
        steps_.push_back({"career_select_venue_fs", {"check:focus:venue_0", Action::Up, "wait", "check:focus:venue_1", Action::Up, "wait", "check:focus:venue_2", D, "wait",
                                                     "check:focus:venue_1", "key:Down", "wait", "check:focus:venue_0", "key:Up", "wait",
                                                     "check:focus:venue_1", B}, 20000});
        steps_.push_back({"career_character_hub_fs", {}, 20000});
    } else if (name == "hub") {
        // The character hub's CHANGE OUTFIT and CHANGE GUITAR screens
        // (GD10-043) [C data
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
        // The Vault's pages no other walk opens (GD10-043): finishes, songs,
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
            if (downs == 0) steps_.push_back({state, {C, Op(Action::Up, 300), Op(Action::Up, 300), B, B}, 20000});
            // The characters' names stay centred on the frame (x 900) from one to the next (GD10-099).
            else if (downs == 3) steps_.push_back({state, {"wait", "check:centrex:character_name_text=900", D, "wait", "wait", "wait", "check:centrex:character_name_text=900", B}, 20000});
            else steps_.push_back({state, {B}, 20000});
        }
        steps_.push_back({"store_fs", {}, 20000});
    } else if (name == "practice") {
        // Training -> PRACTICE -> the first song -> difficulty -> VERSE 1 as
        // the start and the end section -> Slowest -> the section played ->
        // its results; then CHANGE SPEED to Slower, Slow and Full Speed, the
        // section played at each. Each result checks the first gem against
        // the clocks at that speed (check:sync, GD10-100) (GD10-043) [C data
        // practice_*_fs, scripts practice_update, menu_choose_practice_speed_set_speed].
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {"click:menu:main_menu_training_text"}, 10000});
        steps_.push_back({"practice_select_mode_fs", {D, C}, 20000});  // TUTORIAL, PRACTICE
        steps_.push_back({"practice_setlist_fs", {C}, 20000});
        steps_.push_back({"practice_select_part_fs", {C}, 20000, true});
        steps_.push_back({"practice_select_difficulty_fs", {C}, 20000});
        // The section menu plays the song from the highlighted section (the
        // preview follows the highlight) and stops on leaving: Back to the
        // difficulty and in again (the highlight comes back on VERSE 1) (GD10-105).
        const Action U = Action::Up;
        steps_.push_back({"practice_select_song_section_fs",
                          {D, D, D, D, Op("check:preview:on", 3000), U, Op("check:preview:on", 3000), B}, 20000});
        steps_.push_back({"practice_select_difficulty_fs", {Op("check:preview:off", 1500), C}, 20000});
        steps_.push_back({"practice_select_song_section_fs", {Op("check:preview:on", 3000), C, Op(C, 800)}, 20000});  // VERSE 1 .. VERSE 1
        // Slowest, Slower, Slow, Full Speed: current_speedfactor 0.5, 2 / 3, 0.8, 1.
        const std::pair<int, const char*> speeds[] = {{3, "check:sync:0.5"}, {2, "check:sync:0.6666667"}, {1, "check:sync:0.8"}, {0, "check:sync:1"}};
        for (size_t k = 0; k < 4; ++k) {
            std::vector<Op> pick(size_t(speeds[k].first), D);
            // The section menu stays under the speed menu, its preview playing [C scripts create_choose_practice_speed_menu].
            if (k == 0) pick.insert(pick.begin(), Op("check:preview:on"));
            pick.push_back(C);
            steps_.push_back({"practice_select_speed_fs", pick, 20000});
            steps_.push_back({"song", k == 0 ? std::vector<Op>{Op("check:preview:off")} : std::vector<Op>{}, 30000});
            steps_.push_back({"song_over", {}, 600000});
            // The section's results: CONTINUE / RESTART / CHANGE SPEED / CHANGE SECTION / QUIT.
            std::vector<Op> next{Op(speeds[k].second, 1000)};
            if (k < 3) next.push_back(D), next.push_back(D), next.push_back(C);
            steps_.push_back({"practice_newspaper_fs", next, 60000});
        }
    } else if (name == "options") {
        // Options' screens no other walk opens (GD10-043): AUDIO SETTINGS,
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
            if (downs == 0) steps_.push_back({state, {C, Op(Action::Up, 300), Op(Action::Up, 300), B, B}, 20000});
            else steps_.push_back({state, {B}, 20000});
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
    } else if (name == "keyboard") {
        // #70 (GD10-115): a quick play song on the keyboard alone, with
        // Require Strum off and a virtual Xbox pad plugged in (its sticks:
        // an analog whammy at rest). The frets alone hit the notes and the
        // Whammy key bends a held sustain (App::walkKeyBot plays it).
        steps_ = boot;
        steps_.push_back({"main_menu_fs", {"xpad_attach", "keyboard_p1", "keyboard_nostrum", D, D, C}, 10000});
        steps_.push_back({"quickplay_select_difficulty_fs", {C}, 20000});
        steps_.push_back({"quickplay_setlist_fs", {C}, 20000});
        steps_.push_back({"song", {}, 30000});
        steps_.push_back({"song_over", {"check:keyplay"}, 600000});
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
        // Each controller ends on its player's shield (menu_select_controller_icon_positions
        // [0] / [1]): light_up_controller's move survives controller_jiggle (GD10-102).
        steps_.push_back({"coop_career_select_controllers_fs",
                          {Op("check:pos:controller0_icon=370,350", 1500), C, P2D, Op("check:pos:controller1_icon=900,420", 800), P2C},
                          20000});
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
        steps_.push_back({"coop_career_newspaper_fs", {"check:mag_photo"}, 30000});
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
        // Each player scrolls the characters before picking (player 1 down
        // three and up one: Izzy; player 2 down one: Casey): the results art
        // shows a player's character (check:mag_photo), and GD10_FRAMELOG
        // times the scrolls.
        steps_.push_back({"mp_faceoff_character_select_fs", {Op(D, 300), Op(D, 300), Op(D, 300), Op(Action::Up, 300), Op::p2(D, 300), Op(C, 300), P2C}, 20000});
        steps_.push_back({"mp_faceoff_character_hub_fs", {C, P2C}, 20000});
        steps_.push_back({"mp_faceoff_select_venue_fs", {C}, 20000});
        steps_.push_back({"mp_faceoff_setlist_fs", {C}, 20000});
        steps_.push_back({"mp_faceoff_battle_help_fs", {C}, 20000, true});  // battle: GH3's battle help
        if (name == "proface") steps_.push_back({"mp_faceoff_select_difficulty_fs", {C}, 20000});
        else steps_.push_back({"mp_faceoff_select_difficulty_fs", {C, P2C}, 20000});
        steps_.push_back({"song", {}, 30000});
        steps_.push_back({"song_over", {}, 900000});
        steps_.push_back({"mp_faceoff_newspaper_fs", {"check:mag_photo"}, 120000});
    } else if (name == "escquit") {
        // Back three times in a row on the main menu quits (to the launcher);
        // another button between them starts the count again (owner 1.9).
        steps_ = boot;
        expectQuit_ = true;
        steps_.push_back({"main_menu_fs", {B, "wait", B, "wait", D, "wait", B, "wait", B, "wait", B}, 10000});
    } else if (name == "boot") {
        steps_ = boot;
        // No black frame from the press-start screen's first picture to the
        // main menu (GD10-089: a render target made mid-frame, the backdrop's,
        // had the frame presented black).
        // gh4: from the press-start screen on (the boot movies fade through black).
        for (Step& st : steps_)
            if (st.state == "uistate_boot_iis") st.keys.insert(st.keys.begin(), Op("probe:black"));
        steps_.back().keys = {Op("check:no_black", 1000)};
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
    // A song under a loading screen (GD10-040's regression): 5 s into a song
    // the loading screen's tip and picture are gone [C script start_gem_scroller].
    // GH3's tip text, or gh4's loading screen (create_loading_screen's
    // loading_screen_container with its desc) [C scripts].
    if (!songPlaying && (fe.ui().find(qbkey::key("loading_tip_text")) || fe.ui().find(qbkey::key("loading_screen_container"))))
        loadingSeen_ = true;
    if (songPlaying && songSeenAt_ < 0) {
        songSeenAt_ = now;
        if (!loadingSeen_ && !fe.attractMode()) ++songsWithoutLoading_;
        loadingSeen_ = false;
    }
    if (!songPlaying) songSeenAt_ = -1;
    if (songSeenAt_ >= 0 && now - songSeenAt_ > 5000 && !loadingChecked_) {
        loadingChecked_ = true;
        if (fe.ui().loadingScreenUp() || fe.ui().find(qbkey::key("loading_tip_text"))) {
            failed_ = true, failure_ = "the loading screen is still up 5 s into the song";
            std::printf("walk: FAILED: %s\n", failure_.c_str());
            return out;
        }
    }
    // The fuzz walk's check (fuzz:<n>): a focused menu or item and some text.
    if (fuzzLeft_ > 0 && !songPlaying) {
        bool focus = false;
        for (const ui::Element* e : fe.ui().focusedElements())
            if (e != &fe.ui().root()) focus = true;
        if ((focus && !fe.ui().visibleTexts().empty()) || fe.ui().loadingScreenUp()) fuzzOkAt_ = now;
        else if (fuzzOkAt_ >= 0 && now - fuzzOkAt_ > 8000) {
            failed_ = true, failure_ = "no focused menu for 8 s at " + flowState(fe) + " after " + std::to_string(fuzzPresses_) + " presses";
            SDL_Log("walk: FAILED: %s", failure_.c_str());
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
    // GD10_WALK_SHOTS=<dir>: every reached screen saved once, as
    // <dir>/<step>_<state>.bmp, just before the walk's first real button (its
    // "wait"s let the screen build; the button waits one frame for the shot).
    const bool waitNext = key_ < s.keys.size() && s.keys[key_].act == "wait";
    if (!autoShot_ && !waitNext && now - reachedAt_ >= 1000.0 && s.state != "song" && s.state != "song_over") {
        autoShot_ = true;
        if (const char* dir = SDL_getenv("GD10_WALK_SHOTS")) {
            char n[16];
            std::snprintf(n, sizeof n, "%02zu_", step_);
            Canvas::shotPath = std::string(dir) + "/" + n + s.state + ".bmp";
        }
        // GD10_UIDUMP=walk: the element tree of every reached screen, in the log.
        if (const char* d = SDL_getenv("GD10_UIDUMP"); d && std::strcmp(d, "walk") == 0) {
            SDL_Log("ui-dump of %s:", s.state.c_str());
            fe.ui().dump();
        }
        return out;
    }
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
                // Down (or Up) until the setlist's cursor (setlist_selection_tier /
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
                // Up when the song is above the cursor (a career whose atoms put
                // the setlist on a later tier, GD10-116), else Down.
                const uint32_t want = qbkey::key(op.act.substr(8).c_str());
                bool above = false;
                if (sl && sl->st)
                    for (int t = 1; t <= tier && !above; ++t)
                        if (const qb::Value* tv = fe.vm().globals().field(*sl->st, qbkey::key(("tier" + std::to_string(t)).c_str())); tv && tv->st)
                            if (const qb::Value* songs = fe.vm().globals().field(*tv->st, qbkey::key("songs")); songs && songs->arr)
                                for (size_t k = 0; k < songs->arr->values.size() && (t < tier || int(k) < song); ++k)
                                    if (songs->arr->values[k].key == want) above = true;
                if (at != want) {
                    if (++seek_ > 80) {
                        failed_ = true, failure_ = "the setlist has no " + op.act.substr(8);
                        SDL_Log("walk: FAILED: %s", failure_.c_str());
                        return out;
                    }
                    if (SDL_getenv("GD10_WALKLOG")) SDL_Log("walk: setlist at tier %d song %d (%s)", tier, song, qbkey::name(at).c_str());
                    --key_;  // again next time
                    out.push_back(Press{above ? Action::Up : Action::Down, op.player});
                } else {
                    SDL_Log("walk: setlist on %s (tier %d, song %d)", op.act.substr(8).c_str(), tier, song);
                }
            } else if (op.act.rfind("fuzz:", 0) == 0) {
                // "fuzz:<n>": n random menu presses (GD10_SEED), one each 350 ms.
                // Confirm is skipped on the difficulty screen (no song starts).
                // The check: some menu or item keeps the focus and some text
                // is shown, never failing for 8 s in a row (menus that vanish
                // or lose the focus; gd05's fuzz walk found such) [port check].
                if (fuzzLeft_ == -1) {
                    fuzzLeft_ = std::atoi(op.act.c_str() + 5);
                    const char* seed = SDL_getenv("GD10_SEED");
                    fuzzRng_ = seed && *seed ? uint32_t(std::strtoul(seed, nullptr, 10)) | 1u : 12345u;
                    fuzzOkAt_ = now;
                }
                if (fuzzLeft_ > 0) {
                    --fuzzLeft_;
                    --key_;
                    lastKey_ = now + 50.0;
                    fuzzRng_ = fuzzRng_ * 1664525u + 1013904223u;
                    static const Action acts[] = {Action::Up, Action::Down, Action::Down, Action::Up, Action::Confirm, Action::Confirm, Action::Back,
                                                  Action::Left, Action::Right};
                    Action a = acts[(fuzzRng_ >> 16) % 9];
                    // No song or tutorial starts (they would need the pause menu to leave).
                    if (a == Action::Confirm && (flowState(fe) == "uistate_select_difficulty" || flowState(fe) == "uistate_select_tutorial"))
                        a = Action::Back;
                    ++fuzzPresses_;
                    if (SDL_getenv("GD10_WALKLOG")) SDL_Log("walk: fuzz %d at %s", int(a), flowState(fe).c_str());
                    out.push_back(Press{a, op.player});
                } else {
                    SDL_Log("walk: fuzz: %d presses, the menus kept the focus", fuzzPresses_);
                    fuzzLeft_ = -2;  // done
                }
            } else if (op.act.rfind("backto:", 0) == 0) {
                // "backto:<state>": Back (one press a second) until that screen
                // is up; 12 presses at most.
                if (flowState(fe) != op.act.substr(7)) {
                    if (++seek_ > 12) {
                        failed_ = true, failure_ = "Back never reached " + op.act.substr(7) + " (at " + flowState(fe) + ")";
                        SDL_Log("walk: FAILED: %s", failure_.c_str());
                        return out;
                    }
                    --key_;
                    lastKey_ = now + 700.0;
                    out.push_back(Press{Action::Back, op.player});
                } else {
                    seek_ = 0;
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
        autoShot_ = false;
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
        } else if (target.rfind("help:", 0) == 0 || target.rfind("helplow:", 0) == 0) {
            // A help bar pill's text; helplow: its bottom edge (the strum bar's down half).
            const bool low = target[4] == 'l';
            if (const ui::Element* box = fe.ui().find(qbkey::key("user_control_container")))
                for (const auto& c : box->children)
                    if (c->text == target.substr(low ? 8 : 5) && c->drawnIn == fe.ui().drawCount())
                        x = (c->bx0 + c->bx1) * 0.5f, y = low ? c->by1 + 4 : (c->by0 + c->by1) * 0.5f, found = true;
        } else if (target.rfind("menuitem:", 0) == 0) {
            // The focused menu's item N (popups' items have no names), at its drawn bounds.
            const ui::Element* menu = nullptr;
            for (const ui::Element* e : fe.ui().focusedElements())
                if (!e->children.empty() && e->selected >= 0) menu = e;
            const size_t n = size_t(std::atoi(target.c_str() + 9));
            float x0, y0, x1, y1;
            if (menu && n < menu->children.size() && fe.ui().drawnBounds(menu->children[n]->id, x0, y0, x1, y1))
                x = (x0 + x1) * 0.5f, y = (y0 + y1) * 0.5f, found = true;
        } else if (target.rfind("helpend:", 0) == 0) {
            // A help bar pill's backing: 3 units in from its left end (GD10-111).
            for (const auto& p : fe.ui().helperPills())
                if (p.text == target.substr(8)) x = p.x0 + 3, y = (p.y0 + p.y1) * 0.5f - 1, found = true;
        } else if (target.rfind("porthelp:", 0) == 0 && host.walkPorts() && host.walkPorts()->isOpen()) {
            // The port's help bar: porthelp:<k>[low] (0 SELECT, 1 BACK, 2 UP/DOWN), at its left end (low: the bottom half).
            float x0, y0, x1, y1;
            host.walkPorts()->helpRect(std::atoi(target.c_str() + 9), x0, y0, x1, y1);
            const bool low = target.find("low") != std::string::npos;
            x = x0 + 3, y = low ? y1 - 2 : (target.back() == 'p' ? y0 + 2 : (y0 + y1) * 0.5f), found = true;
        } else if (target == "item" && host.walkPorts()) {
            float x0, y0, x1, y1;
            found = host.walkPorts()->itemRect(flowState(fe), x0, y0, x1, y1);
            x = (x0 + x1) * 0.5f, y = (y0 + y1) * 0.5f;
        } else if (target.rfind("row:", 0) == 0 && host.walkPorts()) {
            found = enhancedRowCenter(*host.walkPorts()->screen(PortScreens::Id::Enhanced), target.substr(4), x, y);
        } else if (target.rfind("ask:", 0) == 0 && host.walkPorts()) {  // the Restart Now / Later pop-up's button (0 / 1)
            found = enhancedAskCenter(*host.walkPorts()->screen(PortScreens::Id::Enhanced), std::atoi(target.c_str() + 4), x, y);
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
    if (what == "probe:black") return Canvas::countBlack = true, Canvas::countBlackLit = false, Canvas::blackFrames = 0, true;
    if (what == "check:no_black") {  // no all-black frame presented since probe:black (after its first lit one)
        Canvas::countBlack = false;
        std::printf("  black frames: %d\n", Canvas::blackFrames);
        return Canvas::blackFrames == 0 ? true : fail(std::to_string(Canvas::blackFrames) + " black frames presented");
    }
    if (what == "check:pills") {
        // GD10-111: every help bar pill takes the mouse over all it draws
        // (glyph, text, backing), each has a button the click acts as, and
        // every element the bar draws is part of a pill.
        const auto pills = fe.ui().helperPills();
        if (pills.empty()) return fail("no help bar pills drawn");
        std::string bad;
        for (const auto& p : pills) {
            std::printf("  pill %-24s %-8s (%.0f,%.0f)-(%.0f,%.0f)\n", p.text.c_str(), p.button.c_str(), double(p.x0), double(p.y0), double(p.x1), double(p.y1));
            if (p.button.empty() && p.text != "BACK") bad += " " + p.text + ": no button;";
            const float xs[] = {p.x0 + 1, (p.x0 + p.x1) * 0.5f, p.x1 - 1}, ys[] = {p.y0 + 1, (p.y0 + p.y1) * 0.5f, p.y1 - 1};
            auto shared = [&](float px, float py) {  // also on another pill (the set list's touch): the nearest takes it
                for (const auto& o : pills)
                    if (&o != &p && px >= o.x0 && px <= o.x1 && py >= o.y0 && py <= o.y1) return true;
                return false;
            };
            for (float px : xs)
                for (float py : ys)
                    if (const std::string h = fe.ui().helperAt(px, py); h != p.text && !shared(px, py)) {
                        bad += " " + p.text + " at " + std::to_string(int(px)) + "," + std::to_string(int(py)) + " is " + (h.empty() ? "nothing" : h) + ";";
                        goto nextPill;
                    }
        nextPill:;
        }
        if (const ui::Element* box = fe.ui().find(qbkey::key("user_control_container")))
            for (const auto& c : box->children) {
                if (c->drawnIn != fe.ui().drawCount() || c->bx1 - c->bx0 < 1) continue;
                const float cx = (c->bx0 + c->bx1) * 0.5f, cy = (c->by0 + c->by1) * 0.5f;
                if (fe.ui().helperAt(cx, cy).empty())
                    bad += " " + qbkey::name(c->type) + " " + (c->text.empty() ? qbkey::name(c->texture) : c->text) + " at " + std::to_string(int(cx)) + "," +
                           std::to_string(int(cy)) + " takes no click;";
            }
        return bad.empty() ? true : fail("help bar:" + bad);
    }
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
    if (starts("set:character:")) {  // set:character:<name>[,<outfit>,<style>]: player 1's character (player1_status)
        std::string v = rest("set:character:");
        int o = 1, sy = 1;
        if (size_t c = v.find(','); c != std::string::npos) {
            std::sscanf(v.c_str() + c + 1, "%d,%d", &o, &sy);
            v.resize(c);
        }
        qb::Value* st = fe.vm().globals().ensure(qbkey::key("player1_status"));
        if (!st->st) return fail("no player1_status");
        auto copy = std::make_shared<qb::Struct>(*st->st);
        qs::Vm::set(*copy, qbkey::key("character_id"), qs::Vm::makeKey(qbkey::key(v.c_str())));
        qs::Vm::set(*copy, qbkey::key("outfit"), qs::Vm::makeInt(o));
        qs::Vm::set(*copy, qbkey::key("style"), qs::Vm::makeInt(sy));
        st->st = copy;
        std::printf("  character: %s outfit %d style %d\n", v.c_str(), o, sy);
        return true;
    }
    if (what == "check:mag_photo") {
        // The results art (create_newspaper_menu's Mag_Photo) comes from the
        // body pak of a player's character, outfit and style (its asset
        // context), not whichever body pak loaded first [C script].
        qs::Vm& vm = fe.vm();
        auto keyOf = [](const qb::Value* v) -> uint32_t {
            if (!v) return 0;
            return v->type == qb::Type::String || v->type == qb::Type::WString ? qbkey::key(v->str.c_str()) : v->key;
        };
        std::vector<uint32_t> want;
        for (const char* ps : {"player1_status", "player2_status"}) {
            const qb::Value* st = vm.global(qbkey::key(ps));
            if (!st || !st->st) continue;
            const uint32_t id = keyOf(qs::Vm::param(*st->st, qbkey::key("character_id")));
            const int o = int(qs::Vm::number(qs::Vm::param(*st->st, qbkey::key("outfit")), 1));
            const int sy = int(qs::Vm::number(qs::Vm::param(*st->st, qbkey::key("style")), 1));
            const qb::Value* profs = vm.global(qbkey::key("musician_profiles"));
            if (!profs || !profs->arr) continue;
            for (const qb::Value& pr : profs->arr->values) {
                const qb::Value* n = pr.st ? pr.st->find(qbkey::key("name")) : nullptr;
                if (!n || keyOf(n) != id) continue;
                char body[160];
                std::snprintf(body, sizeof body, "Guitarist_%s_Outfit%d_Style%d", n->str.c_str(), o, sy);
                if (const qb::Struct* b = band::bodyDesc(vm.globals(), qbkey::key(body))) {
                    // No asset_context (Morello_1, the BoneMen): the pak's name.
                    uint32_t ctx = keyOf(b->find(qbkey::key("asset_context")));
                    if (const qb::Value* pk = b->find(qbkey::key("pak")); !ctx && pk) {
                        std::string stem = pk->str.substr(pk->str.find_last_of("\\/") + 1);
                        if (stem.size() > 4) stem.resize(stem.size() - 4);  // ".pak"
                        ctx = qbkey::key(stem.c_str());
                    }
                    want.push_back(ctx);
                }
                std::printf("  mag_photo: %s wants %s (%s)\n", ps, body, want.empty() ? "-" : qbkey::name(want.back()).c_str());
            }
        }
        int n = 0, ok = 0;
        std::vector<const ui::Element*> stack{&fe.ui().root()};
        while (!stack.empty()) {
            const ui::Element* e = stack.back();
            stack.pop_back();
            for (const auto& c : e->children) stack.push_back(c.get());
            if (e->texture != qbkey::key("Mag_Photo")) continue;
            ++n;
            const bool good = e->texContext && std::find(want.begin(), want.end(), e->texContext) != want.end();
            ok += good;
            std::printf("  mag_photo: a sprite from %s%s\n", e->texContext ? qbkey::name(e->texContext).c_str() : "the global cache", good ? "" : " (wrong)");
        }
        return n && ok == n ? true : fail("the results art is not the player's character (" + std::to_string(ok) + " / " + std::to_string(n) + ")");
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
    if (starts("set:tag:")) {  // set:tag:<tag>.<field>=<int>: a save tag's field set (a test's shortcut)
        const std::string kv = rest("set:tag:");
        const size_t dot = kv.find('.'), eq = kv.find('=');
        qs::Vm::set(fe.vm().globalTags[qbkey::key(kv.substr(0, dot).c_str())], qbkey::key(kv.substr(dot + 1, eq - dot - 1).c_str()),
                    qs::Vm::makeInt(std::atoi(kv.c_str() + eq + 1)));
        return true;
    }
    if (starts("check:pos:")) {  // check:pos:<id>=<x>,<y>: the element's pos (parent space, +-2)
        const std::string kv = rest("check:pos:");
        const size_t eq = kv.find('='), comma = kv.find(',');
        const ui::Element* e = fe.ui().find(qbkey::key(kv.substr(0, eq).c_str()));
        if (!e) return fail(kv.substr(0, eq) + " does not exist");
        const float x = float(std::atof(kv.c_str() + eq + 1)), y = float(std::atof(kv.c_str() + comma + 1));
        std::printf("  pos: %s = %.1f, %.1f\n", kv.substr(0, eq).c_str(), double(e->x), double(e->y));
        return std::fabs(e->x - x) <= 2.0f && std::fabs(e->y - y) <= 2.0f
                   ? true
                   : fail(kv.substr(0, eq) + " is at " + std::to_string(int(e->x)) + ", " + std::to_string(int(e->y)));
    }
    if (starts("check:centrex:")) {  // check:centrex:<id>=<x>: the element's drawn centre is at x (1280-wide, +-12 px)
        const std::string kv = rest("check:centrex:");
        const size_t eq = kv.find('=');
        float x0, y0, x1, y1;
        if (!fe.ui().drawnBounds(qbkey::key(kv.substr(0, eq).c_str()), x0, y0, x1, y1)) return fail(kv.substr(0, eq) + " is not drawn");
        const float cx = (x0 + x1) * 0.5f, want = float(std::atof(kv.c_str() + eq + 1));
        std::printf("  centre: %s x = %.1f\n", kv.substr(0, eq).c_str(), double(cx));
        return std::fabs(cx - want) <= 12.0f ? true : fail(kv.substr(0, eq) + " is centred at x " + std::to_string(int(cx)));
    }
    if (what == "save:write") {  // the save written now, as GH3's SaveToMemoryCard writes it
        qs::ScriptRun run;
        qb::Struct params;
        return fe.vm().callNative(qbkey::key("SaveToMemoryCard"), run, params) ? true : fail("SaveToMemoryCard failed");
    }
    if (starts("check:save:")) {
        // check:save:<tag>.<field>=<int>: a tag's field in save.json (absent field: 0).
        const std::string kv = rest("check:save:");
        const size_t dot = kv.find('.'), eq = kv.find('=');
        const std::string tag = kv.substr(0, dot), field = kv.substr(dot + 1, eq - dot - 1);
        save::Tags tags;
        save::Sections sections;
        std::string err;
        if (!save::read(tags, sections, err)) return fail("save: " + err);
        auto it = tags.find(qbkey::key(tag.c_str()));
        const qb::Value* v = it == tags.end() ? nullptr : qs::Vm::param(it->second, qbkey::key(field.c_str()));
        const int got = v ? int(v->type == qb::Type::Float ? v->f[0] : float(v->i)) : 0;
        std::printf("  save: %s.%s = %d\n", tag.c_str(), field.c_str(), got);
        return got == std::atoi(kv.c_str() + eq + 1) ? true : fail("saved tag " + kv + " is " + std::to_string(got));
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
    if (what == "clear:topscores") {  // every song's Top Rockers scores 0 (the walk's short song then makes the list)
        int n = 0;
        for (auto& [k, st] : fe.vm().globalTags)
            for (const char* f : {"score1", "score2", "score3", "score4", "score5"})
                if (qs::Vm::param(st, qbkey::key(f))) qs::Vm::set(st, qbkey::key(f), qs::Vm::makeInt(0)), ++n;
        std::printf("  top scores cleared: %d\n", n);
        return true;
    }
    if (starts("check:focus:")) {  // check:focus:<id>: that element is the one focused (no other item focused)
        const uint32_t want = qbkey::key(rest("check:focus:").c_str());
        int items = 0;
        bool has = false;
        std::string seen;
        for (const ui::Element* e : fe.ui().focusedElements())
            if (e->id != qbkey::key("root_window")) ++items, has = has || e->id == want, seen += " " + qbkey::name(e->id);
        std::printf("  focus: %s %s (%d focused:%s)\n", rest("check:focus:").c_str(), has ? "yes" : "no", items, seen.c_str());
        return has && items == 1 ? true : fail("focus is not on " + rest("check:focus:") + " alone");
    }
    if (starts("check:norow:")) {  // an Enhanced Options row that must not exist (the screen open)
        float x = 0, y = 0;
        if (!host.walkPorts()) return fail("no port screens");
        return !enhancedRowCenter(*host.walkPorts()->screen(PortScreens::Id::Enhanced), rest("check:norow:"), x, y)
                   ? true
                   : fail("Enhanced has a " + rest("check:norow:") + " row");
    }
    if (what == "check:cut_content_ignored") {  // an enhanced.json with cut_content on loads as off (GD10-110)
        const std::string dir = (std::filesystem::temp_directory_path() / "gd10_walk_cutcontent").string() + "/";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        std::ofstream(dir + "enhanced.json") << R"({"version": 2, "settings": {"cut_content": "on", "no_fail": "on"}})";
        EnhancedStore st;
        st.load(dir);
        const std::string cut = st.get("cut_content"), noFail = st.get("no_fail");
        std::filesystem::remove_all(dir, ec);
        std::printf("  a saved cut_content on loads as %s (no_fail %s)\n", cut.c_str(), noFail.c_str());
        return cut == "off" && noFail == "on" ? true : fail("a saved cut_content on loads as " + cut);
    }
    if (what == "check:backends_saved") {  // saved Graphics / Audio Backends load as saved, an unknown one is dropped (GDCORE-010)
        const std::string dir = (std::filesystem::temp_directory_path() / "gd10_walk_backends").string() + "/";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        std::ofstream(dir + "enhanced.json")
            << R"({"version": 2, "settings": {"graphics_backend": "vulkan", "audio_backend": "openal", "no_fail": "on"}})";
        EnhancedStore st;
        st.load(dir);
        const std::string g = st.get("graphics_backend"), a = st.get("audio_backend"), noFail = st.get("no_fail");
        st.set("audio_backend", "glide");  // refused: not a backend
        const std::string a2 = st.get("audio_backend");
        std::filesystem::remove_all(dir, ec);
        std::printf("  saved backends vulkan / openal load as %s / %s (no_fail %s); glide set: %s\n", g.c_str(), a.c_str(), noFail.c_str(), a2.c_str());
        return g == "vulkan" && a == "openal" && noFail == "on" && a2 == "openal" ? true
                                                                                  : fail("saved backends vulkan / openal load as " + g + " / " + a);
    }
    if (starts("check:asking=")) {  // the Restart Now / Later pop-up open (1) or not (0)
        if (!host.walkPorts()) return fail("no port screens");
        const bool open = enhancedAsking(*host.walkPorts()->screen(PortScreens::Id::Enhanced));
        std::printf("  Restart Now / Later: %s\n", open ? "open" : "closed");
        return open == (rest("check:asking=") == "1") ? true : fail(std::string("the Restart Now / Later pop-up is ") + (open ? "open" : "closed"));
    }
    if (starts("check:audio_now=")) {  // the audio backend playing now (audio/audio_device.h)
        std::string b = audiodev::backend();
        if (b.empty()) b = "local";
        std::printf("  audio backend now: %s\n", b.c_str());
        return b == rest("check:audio_now=") ? true : fail("the audio backend is " + b);
    }
    if (starts("check:qp_guitarists=")) {  // quick play's random guitarist count (cs_get_total_guitarists from quickplay_start_song)
        qs::ScriptRun r;
        qs::Frame f;
        f.code = fe.vm().code(qbkey::key("quickplay_start_song"));
        f.locals = std::make_shared<qb::Struct>();
        r.frames.push_back(std::move(f));
        qb::Struct none;
        fe.vm().condition(r, qbkey::key("cs_get_total_guitarists"), none);
        const qb::Value* n = qs::Vm::param(*r.frames.back().locals, qbkey::key("num_guitarists"));
        const int got = n ? int(n->number()) : -1;
        std::printf("  quick play guitarists: %d\n", got);
        return got == std::atoi(rest("check:qp_guitarists=").c_str()) ? true : fail("quick play deals from " + std::to_string(got) + " guitarists");
    }
    if (what == "log:cs") {  // character select's page, icon and name (player 1)
        auto num = [&](const char* g) {
            const qb::Value* v = fe.vm().global(qbkey::key(g));
            return v ? double(v->number()) : -1.0;
        };
        const ui::Element* t = fe.ui().find(qbkey::key("cs_name_text_p1"));
        std::printf("  cs: jumped %g icon %g total %g name \"%s\"\n", num("g_cs_jumped_p1"), num("g_character_select_icon_index_p1"),
                    num("cs_total_guitarists_p1"), t ? t->text.c_str() : "(none)");
        return true;
    }
    if (starts("check:menusel:")) {  // check:menusel:<n>: the focused menu's item n is selected (menuitem:)
        int sel = -1;
        for (const ui::Element* e : fe.ui().focusedElements())
            if (!e->children.empty() && e->selected >= 0) sel = e->selected;
        return sel == std::atoi(rest("check:menusel:").c_str()) ? true : fail("the focused menu's item is " + std::to_string(sel) + ", not " + rest("check:menusel:"));
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
    if (starts("check:sync:")) {
        // check:sync:<speed>: the last song ran at that speed and its first gem
        // reached the strike line in step with the clocks: the frame it did is
        // under two frames (x the speed) past the note, and the song clock
        // there is within 25 ms of the audio's own clock and 40 ms of the
        // wall clock since the first frame x the speed (GD10-100).
        const FrontEnd::SongSync& s = fe.songSync;
        const float want = float(std::atof(rest("check:sync:").c_str()));
        std::printf("  sync: speed %.3f, first gem %.0f ms: %.1f ms past it, wall clock x speed %+.1f ms, audio %+.1f ms\n", double(s.speed),
                    s.noteMs, s.lateMs, s.clockErrMs, s.audioErrMs);
        if (!s.measured) return fail("sync: no first gem measured");
        if (std::fabs(s.speed - want) > 0.01f) return fail("sync: speed " + std::to_string(s.speed));
        if (s.lateMs < 0 || s.lateMs > 40.0 * s.speed || std::fabs(s.clockErrMs) > 40 || std::fabs(s.audioErrMs) > 25) return fail("sync: out of step");
        return true;
    }
    if (starts("check:preview:")) {
        // check:preview:on: practice's section preview plays the song from the
        // highlighted section's marker (target_practice_songpreview_starttime)
        // at full speed, its stream past that time by no more than it has had
        // to play; check:preview:off: no preview plays, and none was left
        // playing as a song started (GD10-105).
        const FrontEnd::SongPreview& pv = fe.songPreview;
        const double target = qs::Vm::number(fe.vm().global(qbkey::key("target_practice_songpreview_starttime")), -1);
        const double pos = pv.positionMs ? pv.positionMs() : -1;
        std::printf("  preview: %s%s %s from %.0f ms (target %.0f), now %.0f ms, speed %.2f, %d starts%s\n", pv.active ? "active" : "off",
                    pv.playing ? " playing" : "", pv.prefix.c_str(), pv.startMs, target, pos, double(pv.speed), pv.starts,
                    pv.leaked ? ", LEAKED into a song" : "");
        if (pv.leaked) return fail("preview: still playing as a song started");
        if (rest("check:preview:") == "off") return pv.active ? fail("preview: still playing") : true;
        if (!pv.active || !pv.playing) return fail("preview: not playing");
        if (std::fabs(pv.startMs - target) > 1.0 || target <= 0) return fail("preview: not from the highlighted section's marker");
        if (std::fabs(pv.speed - 1.0f) > 0.001f) return fail("preview: not at full speed");
        if (pos < pv.startMs || pos > pv.startMs + 15000.0) return fail("preview: the stream is not where it started");
        return true;
    }
    if (starts("check:global:")) {  // check:global:<name>=<number>: a GH3 global's value
        const std::string kv = rest("check:global:");
        const size_t eq = kv.find('=');
        const qb::Value* v = fe.vm().global(qbkey::key(kv.substr(0, eq).c_str()));
        // A key value (game_mode = p1_career) compares by name, a number by value.
        if (v && (v->type == qb::Type::Key || v->type == qb::Type::KeyPtr)) {
            std::printf("  global: %s = %s\n", kv.substr(0, eq).c_str(), qbkey::name(v->key).c_str());
            return v->key == qbkey::key(kv.substr(eq + 1).c_str()) ? true : fail("global " + kv);
        }
        const float got = v ? v->number() : -1;
        std::printf("  global: %s = %g\n", kv.substr(0, eq).c_str(), double(got));
        return got == std::atof(kv.c_str() + eq + 1) ? true : fail("global " + kv);
    }
    if (starts("shot:")) {  // shot:<name>: the next frame saved as <GD10_WALK_SHOTS>/<name>.bmp
        // "shot:<name>@": the name followed by the screen's state (gh4's UI stack top).
        std::string n = rest("shot:");
        if (!n.empty() && n.back() == '@') n.back() = '_', n += fe.flowState();
        if (const char* dir = SDL_getenv("GD10_WALK_SHOTS")) Canvas::shotPath = std::string(dir) + "/" + n + ".bmp";
        if (const char* d = SDL_getenv("GD10_UIDUMP"); d && std::strcmp(d, "walk") == 0) {
            SDL_Log("ui-dump of %s:", n.c_str());
            fe.ui().dump();
        }
        menuShots_.push_back(n);
        return true;
    }
    if (what == "dialog:savedir") {  // GD10_DIALOG: the user-file dialogs answered with the walk's save folder
        SDL_setenv_unsafe("GD10_DIALOG", host.walkSaveDir().c_str(), 1);
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
    if (what == "keyboard_p1") {  // the keyboard kept on player 1 with a pad plugged in
        host.walkInput().setPlayer("keyboard", 0);
        return true;
    }
    if (what == "keyboard_nostrum") {  // the Gamepads screen's Require Strum: Off for the keyboard
        host.walkInput().setRequireStrum("keyboard", false);
        return true;
    }
    if (what == "check:keyplay") {
        std::printf("  keyboard: %d notes pressed, %d hit; sustains held %d frames (%d with the Whammy key), whammy %.2f with it, %.2f without\n",
                    keyNotes, keyHits, keyHoldFrames, keyWhammyFrames, double(keyWhammy), double(keyRest));
        if (keyNotes < 10) return fail("the keyboard pressed only " + std::to_string(keyNotes) + " notes");
        if (keyHits * 10 < keyNotes * 9) return fail("the fret keys alone (Require Strum off) hit " + std::to_string(keyHits) + " of " +
                                                     std::to_string(keyNotes) + " notes");
        if (keyHoldFrames == 0) return fail("no sustain was held");
        if (keyWhammyFrames < 10 || keyWhammy < 0.99f || keyRest > 0.01f) return fail("the Whammy key does not move the whammy on a held sustain");
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
    if (expectQuit_) {  // escquit: the quit is the walk's end (the last Back)
        const bool ok = !failed_ && quitSeen_ && step_ + 1 >= steps_.size() && key_ >= steps_.back().keys.size();
        std::printf("walktest %s: %s; reached: %s\n", name_.c_str(), ok ? "passed (Back x3 quit)" : "FAILED (no quit on the third Back)", path.c_str());
        return ok ? 0 : 1;
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
    // Every loading screen stayed up 2 s (GD10-040).
    const double shown = fe.vm().minLoadingShownMs();
    if (shown >= 0) std::printf("walk: loading screens up at least %.0f ms\n", shown);
    if (shown >= 0 && shown < qs::Vm::kLoadingHoldMs - 50) {
        std::printf("walktest %s: FAILED (a loading screen passed in %.0f ms); reached: %s\n", name_.c_str(), shown, path.c_str());
        return 1;
    }
    // Quick play and career put the loading screen up before every song (create_loading_screen) [C scripts].
    if (songsWithoutLoading_ > 0) std::printf("walk: %d song(s) started without a loading screen\n", songsWithoutLoading_);
    if (songsWithoutLoading_ > 0 && (name_ == "quickplay" || name_ == "career" || name_ == "career_quickplay")) {
        std::printf("walktest %s: FAILED (a song started without its loading screen); reached: %s\n", name_.c_str(), path.c_str());
        return 1;
    }
    std::printf("walktest %s: passed (%s); songs started %d, ended %d\n", name_.c_str(), path.c_str(), songsStarted, songsEnded);
    return 0;
}
