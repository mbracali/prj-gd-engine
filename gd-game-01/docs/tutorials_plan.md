# Tutorials: how the original runs them and how to make them playable in gd01

Tags: **[C]** confirmed from the decompilation or the shipped scripts/data, **[I]** inferred or a port decision.
Addresses are EE (SLUS_212.24) addresses; decompiled code is in `analysis/decomp/00150000.c` unless stated otherwise.

---------------------------------------------------------------------------------------------------
## 0. Summary

- Three tutorials: **Basic Lessons**, **Star Power Lessons** and **Advanced Techniques** (hammer-ons and pull-offs).
  The tutorial flow lives in the scripts (`ghui/tutorials.dta` + 14 `tutorial_*.dta` lesson files).
  The native code adds one panel class, **TutorialPanel** (`new_tutorial_panel`, object `tutorial`), with three helpers:
  - a **VoiceMgr / StreamPlayer** that plays the voice-over VGS streams,
  - **TutorialGuitarTune**, the fret-tuning trainer of lesson 1,
  - **TutorialHopoTrain**, the hammer-on and pull-off fingering trainer.
- A lesson is one **ThreadTask** (`$this thread_task …`). It runs the lesson file statement by statement and blocks on
  `$task wait {cond}`, `$task wait_timeout`, `$task sleep`. It shows and hides `tut_script.gh` scene objects, plays VO, and
  pauses or unpauses the song clock (`game set_time_paused`). The player's gameplay comes back to it as script
  callbacks (`gem_hit_callback`, `hopo_callback`, `held_note_released_callback`, …).
- A lesson succeeds when its counter reaches the target. It **times out** when the song passes 97 % (or after 60 s for the
  two trainers). Either way the tutorial goes on to the next lesson; there is no fail and no retry.
- Unlocking happens **when a tutorial is selected**, not when it is finished: `campaign do_tutorial N` raises
  `tutorials_done`. Tutorial 2 needs `tutorials_done >= 1` and tutorial 3 needs `>= 2`.
- Missing from gd01 today: the ThreadTask runtime; the TutorialPanel natives (`tutorial` is a `StubObj`); VO streaming;
  the two trainers; the tutorial game mode, i.e. starting the song from the `tut_script` screen, loading tutorial songs
  (they are filtered out of `songs_`), `load_song`/`loaded`/`start_song`/`set_time_paused`; about 10 Player setters; 6 player
  callbacks; real `set_local_pos/scale`, `set_stage_tex` and `set_wrap_width`; and tutorial-mode differences in the arena, HUD and banks.
  §9 lists the implementation steps.

---------------------------------------------------------------------------------------------------
## 1. Data inventory [C]

| what | where |
|---|---|
| Menu, flow and lesson state machine | `extracted_dta/ghui/gen/tutorials.dta` (panels `tut`, `tutorials`, `tut_script`, `tut_pause`, `tut_pause_controller`; screens `tutorials`, `tut_loading`, `tut_script`, `tut_pause`, `tut_pause_controller`, `tutorial_quit`) |
| Lessons (included into `tut set_state`) | `tutorial_11_tuning`, `12_playing_notes`, `13_diff_notes`, `14_held_notes`, `15_chords`, `16_interface`, `19_wrapup`, `21_star_intro`, `22_star_combos`, `23_whammy`, `24_wail`, `31_freestyle`, `32_sustain`, `33_pulloff` (`.dta`) |
| Menu scene | `ghui/tutorials.gh` (`tut_1.btn`, `tut_2.btn`, `tut_3.btn`, `tut_blurb.lbl`, poster, wall) |
| Lesson overlay scene | `ghui/tut_script.gh` (220 objects; `ghui/tut_script_lefty.gh` when `options get_lefty 0`) |
| Pause scenes | `ghui/tut_pause.gh`, `ghui/tut_pause_controller.gh` |
| VO event table | `tutorial/gen/tutorial_vo.dtb` → `config/sound.dta (tutorial (streaming_vo (volume 1.0) (dir tutorial/streams) (events …)))` |
| VO audio | `tutorial/streams/<file>.vgs`, 149 files. All 121 file references in the event table exist (checked). |
| Sound bank | `tutorial/gen/tutorial.{bnk,nse,seq}` (`sound.dta (banks (tutorial tutorial/tutorial))`) |
| Songs | `tutorial/tutorial{102,103,104,105,202,203,204,205,206}.{mid,vgs}` and `config/songs.dta` entries; all 108 bpm, MIDI tracks `T1 GEMS` and `EVENTS`. There is **no tutorial101**. |
| Camera | `arena/gen/camera_tutorial.dta` (intro_skip → `$camedit.TUTORIAL`), basement `camera.dta` `$camedit.TUTORIAL` = `switch_cam Cam_nt_np_close Tut_01 … (hide_crowd 1)` |
| Strings | `ghui/eng/gen/locale.dta`: `tutorials`, `TUTORIAL_1..3`, `TUT_n_BLURB`, `tut_task`, `tut_notes_fmt "%s %d/%d"`, `tut_timeout`, `tutN_M_part/_intro/_info/_task/_score/_done/_timeout`, `tut1_complete`, `tut3_complete`, `tut_guitar_loss_msg` |

Sound bank contents (from `.seq`/`.bnk`): miss_chink(4–9), button_play/select/back/toggle, sp_gemhit, sp_awarded,
sp_deployed, sp_depleted, sp_available, track_unfurl, track_exp, button_error, **bkgd_music** (looping), **tuning_complete**,
**s19901/s19902** (wrap-up stings), **s10101..s10115** (tuning notes) and **s20601..s20605** (hammer/pull fret notes).
`docs/audio_sfx.md` §"Not sfx" calls s10101..s10115 and s20601..05 *unused*. **That is wrong**: TutorialGuitarTune and
TutorialHopoTrain play them (§5, §6).

---------------------------------------------------------------------------------------------------
## 2. Menu flow, unlocks, controller rules [C unless marked]

### 2.1 Main menu (`ghui/gen/main.dta`)
- `main` panel `enter` sends `{tutorial set_tutorial_running FALSE}`, so leaving a tutorial clears the flag.
- On the first visit, with a guitar on pad 0 and `campaign tutorials_done == 0`, focus goes to `main_tutorial.btn`.
  The port intercepts that focus (ui_system.cpp `set_focus`) and always opens on Career. Keep that.
- `poll` disables `main_tutorial.btn` when `!campaign tutorial_access && game get_controller == joypad`.
- `main_tutorial.btn` → `ui goto_screen tutorials`.

### 2.2 `tutorials` screen (panels `tutorials meta helpbar`)
- `enter`: focus `tut_1.btn`. `tut_2.btn` is kNormal if `tutorials_done >= 1`, otherwise kDisabled. `tut_3.btn` needs `>= 2`.
  - `campaign tutorials_done` returns 3 when `skip_tutorials` or `tutorial_access` is set (`docs/career.md`, `FUN_001392b8`). gd01 already does this (career_objects.cpp:131).
- `FOCUS_MSG` puts `TUT_n_BLURB` into `tut_blurb.lbl`.
- `SELECT_MSG`:

  | button | start_state | tut_song | campaign |
  |---|---|---|---|
  | tut_1 | `intro` | `tutorial102` | `do_tutorial 1` |
  | tut_2 | `star_intro` | `tutorial202` | `do_tutorial 2` |
  | tut_3 | `freestyle` | `tutorial205` | `do_tutorial 3` |

  After the switch it does `ui goto_screen tut_loading`. The screen-level `SELECT_MSG` also runs `game set_multiplayer FALSE` and `tutorial set_tutorial_running TRUE`.
- `do_tutorial n` sets `profile.tutorials_done = max(·, n)` and needs_save (`FUN_001392f0`, docs/career.md:249). This is
  the **only** unlock path: **starting** tutorial 1 unlocks tutorial 2. Finishing a tutorial writes nothing.

### 2.3 `tut_loading` screen (panels `loading`, no back, no transition)
`reset_ambient`, `{game get_bank_loader} reset`, `arena_panel set_load_banks FALSE` (no venue bank),
`game set_character nu_metal`, `set_guitar gibson_flying_v`, `set_venue basement`, `set_song {tutorials get tut_song}`,
`set_difficulty kDifficultyEasy`, then `ui goto_screen tut_script`.

### 2.4 `tut_script` screen (panels `game arena track hud mtv_overlay tut tut_script`, focus `tut_script`)
- `tut_script` **panel** `enter`: `game set_skip_intro TRUE`, `game set_start_paused TRUE`, `game set_time_paused TRUE`, `tutorial start_tutorial`.
  `exit` clears skip_intro and start_paused and runs `reset_all_callbacks`.
- **Screen** `enter`: `{ui panel tut} set_paused FALSE`.
- Screen `BUTTON_DOWN_MSG`: pad 0 Start pauses `tut` and goes to `tut_pause`.
- Screen `poll`: if `tutorial is_missing_guitar && !campaign tutorial_access`, it pauses `tut` and goes to `tut_pause_controller`.
- `tut_pause` offers resume (back to `tut_script`), restart (`stop_sfx bkgd_music`, `goto tut_loading`; the whole tutorial
  starts again from its start_state) and quit (`goto tutorial_quit`).
  The shared `TUT_PAUSE_SCREEN_HANDLERS`:
  - `enter`: remembers `game time_paused`, sets `game set_time_paused TRUE`, `tut_script set_paused TRUE`, `synth pause_all_sfx TRUE`, `hud set_paused TRUE`, `app set_paused TRUE`.
  - `resume`: `pause_all_sfx FALSE`, restores `time_paused`, `goto tut_script`.
- `tut_pause_controller` shows `tut_guitar_loss_msg`. Its resume is disabled while `is_missing_guitar`.
- `tutorial_quit` (panels `tut tut_script`): `stop_sfx bkgd_music`, `synth stop_all_sfx`, hide `tut_script.view`, `meta_loading_goto main`.
  The lesson files call it at the end of each tutorial.

### 2.5 Port decision on controllers [I]
The original refuses tutorials without a guitar (main button disabled, `tut_pause_controller`).
On PC, keyboard and gamepads have a strum key in gd01. Recommendation:
- `tutorial is_missing_guitar` returns 0 whenever the active input mode is Guitar.
- `game get_controller` keeps returning `guitar`/`joypad` from `ui_.guitar_`.
- Make the keyboard count as a guitar for this purpose. Optionally add an Enhanced Setting "allow tutorials on joypad", which maps to `campaign set_tutorial_access`.

---------------------------------------------------------------------------------------------------
## 3. The lesson state machine (`new_tutorial_panel tut (object_name tutorial) (rnd_file "")`) [C]

The script part of the `tut` panel (tutorials.dta):
- `start_tutorial` → `set_state {tutorials get start_state}`.
- `set_state $state` → `$this set state $state`, then `$this thread_task (preserve $this) #include <lesson file>`.
- `TUTORIAL_STATES` = `(intro playing_notes diff_notes held_notes chords interface wrapup star_intro star_combos whammy wail freestyle sustain pulloff)`.
  The states run in this order. The tutorials end at `wrapup` (T1), `wail` (T2, which has its own wrap-up) and `pulloff` (T3, own wrap-up).
- `switch_to_state $s` → `set_state $s` (a new thread), then `$task exit` (ends the calling thread).
- `tutorial_inc n` (debug cheats `next/prev_tutorial_lesson`) → `clear_tasks`, `reset_vo`, `set_state` (next state, modulo the list).
- `exit` → `clear_tasks`.

### 3.1 Common lesson macros (tutorials.dta)
- `WAIT_FOR_VO` = `{$task wait {tutorial is_vo_playing}}`: wait while a VO plays.
- `LONG_VO_GAP` = sleep 0.6 s. `SHORT_VO_GAP` = sleep 0.5 s. `sleep_seconds $taskmgr s` = `$taskmgr sleep s*1000`.
- `set_beatmatch_muting B` → `player0::player_matcher set_volume 0|1` mutes the guitar track during demos.
- `WAIT_FOR_SONG_LOAD` = `{$task wait {! {game loaded}}}` then `{game start_song}`.
- `END_OF_SONG_PERCENT` = 97. `SONG_DELAY_SEC` = 3.2.
- `update_tut_score label n total` → `tut_score.lbl set_localized_text (sprintf tut_notes_fmt label n total)`.
- `size_left_box w h` / `size_right_box w h score`: position and scale the 9-slice box meshes `tut_box_*` / `tut_box_*1`
  (corner 16, piece 16, filler 16, +25 height for the score line), plus `tut_box_left.view` / `tut_box_right.view` / `tut_score.lbl`.
  This needs **real `set_local_pos` and `set_local_scale`** on meshes, views and labels.
- `TUTn_m_BOX` / `TUTn_m_TASK` give the box sizes per lesson.

### 3.2 The lesson pattern
1. Reset the overlay: hide the task, score, info and banner labels and the right box; set `tut_part.lbl`; size the boxes.
2. Optionally `game load_song tutorialNNN` + `WAIT_FOR_SONG_LOAD`. Then `game set_time_paused TRUE`, `player0::player set_crowd_rating_active FALSE`, `track_panel animate_track_out_fast`.
3. **Demo**: mute the guitar, unpause for `SONG_DELAY_SEC` (or 3 s) so the highway scrolls, pause again, unmute.
4. VO explanation with highlight views (`t1_note_highlight.view`, `t1_now_highlight.view`, `t1_long_note_highlight.view`, `t1_chord_highlight.view`, `tut_star_highlight.view`, `tut_rock_highlight.view`, `t1_score_highlight.view`), each followed by `WAIT_FOR_VO` and gaps.
   `bkgd_music` loops during explanations and stops (`stop_sfx bkgd_music`) when the task begins.
5. Task: show the right box with `tut_header.lbl` (`tut_task`) and `tut_task.lbl`, and the score label.
   Define the `main` namespace callbacks, `game set_time_paused FALSE`, then `$task wait {lessonN_running}`, where
   running = `count < target && player0::player percent_complete < 97`.
6. End: `track_panel animate_track_in_fast`, `reset_vo`, an optional 0.5–1.5 s sleep on success, `game set_time_paused TRUE`.
   The banner shows `tut_timeout` + `timeout_vo` on failure, or the lesson's `_done` text + VO on success.
   Then `play_sfx bkgd_music`, `WAIT_FOR_VO`, `reset_all_callbacks`, `switch_to_state next`.

### 3.3 Lesson catalogue

| # | state (file) | song | gameplay task / success | callbacks used | fail (timeout) | notes |
|---|---|---|---|---|---|---|
| T1.1 | intro (11_tuning) | tutorial102 loaded, paused | `start_tuning_mode`: strum each of the 5 frets 3× (single fret held) | `fret_played $fret` (anim `t1_frethitN.view` 300 ms 0→100) | `wait_timeout {is_tuning} 60000` → `end_tuning_mode`, banner `tut1_1_timeout` | Banner `tut1_title` + VO `main_intro`, then `arena_panel set_paused TRUE`, lamp hidden, `arena set_lights_okay_chorus`. Guitar diagrams `t1_guitar.view` (zoom anim 0→30 over 500 ms), `t1_demo_1/2/3.mnm`. End: `play_sfx tuning_complete`, VO `all_frets_tuned`. |
| T1.2 | playing_notes (12) | tutorial102 | hit 6 gems (`GEMS_TO_HIT`) | `gem_hit_callback` (count, score anim `tut_score.tnm` 150 ms 0→2, VO `hit_vo`); `gem_miss/pass_callback` (miss+pass == 5 → `playing_notes_miss_a_lot`, == 15 → `..._a_ton`) | 97 % | perfect (no miss/pass) → `playing_notes_done_perfect` |
| T1.3 | diff_notes (13) | **load** tutorial103 | hit 8 | hit (4 hits in a row on the same slot → VO `diff_notes_same_note` once); miss/pass clear `$perfect_tutorial` | 97 % | VO `diff_notes_1` plays while the song loads |
| T1.4 | held_notes (14) | **load** tutorial104 | 6 sustains held to the end (`held_note_released_callback $fraction`, fraction == 1) | fraction < 1 → VO `held_notes_released` once; every release → `player0::player empty_star_power` | 97 % | |
| T1.5 | chords (15) | **load** tutorial105 | 6 hits (chords) | `gem_hit_callback` | 97 % | success VO `chords_6` |
| T1.6 | interface (16) | – | none (VO tour) | – | – | `hud set_showing TRUE`; `set_crowd_rating 0.88/0.2/0.01/0.5` moves the rock meter while `tut_info.lbl` text accumulates (`sprint`) |
| T1.9 | wrapup (19) | – | – | – | – | `tut_screen_dim.view` anim 0→30 over 500 ms, banner `tut1_complete`, `s19901`, VO `wrapup1_1`, 15 s, `t1_lets_rock.view` + `s19902`, 4 s → `goto tutorial_quit` |
| T2.1 | star_intro (21) | tutorial202 loaded | – | – | – | Switches overlay to `tutorial2.view`, HUD on, `tut_star_highlight.view` |
| T2.2 | star_combos (22) | tutorial202 | capture one Star Power phrase | `star_phrase_captured_callback`; `star_gem_hit_callback` (streak++); `star_phrase_miss_callback` (streak > 6 → VO `near_miss_vo`) | 97 % | `set_whammystarpowerenabled FALSE`, `set_mercuryswitchenabled FALSE` |
| T2.3 | whammy (23) | **load** tutorial203 | whammy Star Power sustains until `star_power_ready` | `whammy_start_callback` ($whammied), `whammy_end_callback` (VO `whammy_keepmoving` once), `held_note_released_callback` (full and no whammy → `whammy_no_whammy`; partial and not ready → `whammy_let_go`); hit/miss/pass reset $whammied | 97 % | `set_star_power_phrase_boost 0.0` (only whammy fills); mercury off; diagrams `t2_whammy.view`, `t2_whammy_wrong.mnm` (7 s 0→14), `t2_whammy_right.mnm` (9 s 0→18) |
| T2.4 | wail (24) | **load** tutorial204 | 8 gems hit **while in star mode** | `gem_hit_callback` (in star mode: count + VO `great_hit_vo`; else VO `wail_no_wail_1` + `button_error`); `star_use_begin_callback` (first: VO `wail_first`, hide `t2_guitar.view`); `star_use_end_callback` (re-show the tilt diagram once); miss/pass → `check_wail_warning` (8 → `wail_no_wail_1`, 16 → `wail_no_wail_2` while not wailed) | 97 % | `fill_star_power`, `set_star_power_deploy_rate 0.01`, `score_meter_0.view` hidden, `tut_info.lbl set_wrap_width 500`, `t2_wail.tnm` 1000 ms 0→100. End: `lightning_track.view` hidden, VO `wrapup1_2`, `t1_stamps.view` + `t1_lets_rock.view` + `s19902` → `tutorial_quit` |
| T3.1 | freestyle (31) | tutorial205 loaded | `start_hopo_train kHammerOnTutorial(1)`: 3 correct green→red→yellow hammer runs | `fret_played`, `hopo_hit` (count, VO `hit_vo`) | `wait_timeout {is_hopo_training} 60000`, banner `tut3_1_timeout` | Demo frames `t3_hopo_anim.view` 0..4; `end_hopo_train` at the end |
| T3.2 | sustain (32) | tutorial205 | 8 HOPO hits | `hopo_callback` (count + `hit_vo`), `swingAtHopo_callback` (VO `hammeron_strum`: "don't strum") | 97 % | |
| T3.3 | pulloff (33) | tutorial205, then **load** tutorial206 | (a) `start_hopo_train kPullOffTutorial(2)`: 3 yellow→red→green pull runs; (b) 8 pull-off HOPO hits | as T3.1, then as T3.2 (`pulloff_real_strum`) | 60 s / 97 % | End: dim, `tut3_complete`, `s19901`, VO `wrapup1_3`, `t1_lets_rock.view`, `s19902` → `tutorial_quit` |

Notes:
- The hit-count lessons do not check *which* gem was hit. Hitting any gem counts. In T1.5, single notes would also count if the chart had any.
- `hit_vo` / `great_hit_vo` are **dropped** while another VO is still playing (§4.2), so they don't pile up.
- `HOPOS_TO_HIT` in T3.3's callback compares `$pulloffs_hit <= HOPOS_TO_HIT`. HOPOS_TO_HIT is still 3 from the earlier `#define`, so after 3 pull-offs the `hit_vo`/score update stops, while the count still increments. This is an original script quirk; keep it as-is. [C]

---------------------------------------------------------------------------------------------------
## 4. The `tutorial` object: TutorialPanel natives

### 4.1 Class and lifecycle [C]
- Registration: FUN_00153b10 registers the verb `new_tutorial_panel` (string 0x30be40) with factory FUN_00153aa8.
  The factory allocates 0x1a4 bytes, runs the ctor FUN_00153b48 and stores the instance in the global **DAT_002edc54**. Other natives
  use this global to ask `is_tutorial_running` (FUN_00153f08).
- Ctor FUN_00153b48: GHPanel ctor (FUN_00205870). Then +0x80 tuning = 0; +0x84 TutorialGuitarTune (ctor FUN_00155990);
  +0xe4 hopo-training = 0; +0xe8 TutorialHopoTrain (ctor FUN_00156530); +0x19c VoiceMgr* = 0; +0x1a0 tutorial_running = 0.
- vtable 0x2f8a20 (vtables.tsv):

  | slot | function | role |
  |---|---|---|
  | 1 | TutorialPanel_v1 0x153f60 | message handler (§4.3) |
  | 3 | TutorialPanel_v3 0x153cd0 | **Enter**: tuning = 0, hopo = 0, TuneEnter FUN_00155ae8, HopoEnter FUN_00156780, then ArenaPanel_v3 (base Enter) |
  | 4 | TutorialPanel_v4 0x153d10 | **Exit**: tuning = hopo = 0, TuneExit FUN_00155ee8, HopoExit FUN_00156b78 (delete their Sequences), base Exit |
  | 7 | TutorialPanel_v7 0x153e00 | **SetPaused(b)**: VoiceMgr pause (FUN_00154cb8 → StreamPlayer FUN_00155670), then FadePanel_v7 |
  | 10 | TutorialPanel_v10 0x153d50 | **Poll**: if not paused (+0x50): VoiceMgr poll (FUN_00154c98); if tuning: TunePoll FUN_00156390(pad 0 state) and tuning = !done; if hopo: HopoPoll FUN_001576c0 and hopo = !done. Then GHPanel_v10 (TaskMgr: threads, anims) |
  | 13 | TutorialPanel_v13 0x153ba8 | **Load**: base load, `new VoiceMgr` (FUN_001543a0), queue sound bank `{sound banks tutorial}` = `tutorial/tutorial` into the bank loader DAT_002edb74 (FUN_0010d8f0) |
  | 14 | TutorialPanel_v14 0x153c90 | **Unload**: delete the VoiceMgr (FUN_00154508) |
  | 15 | TutorialPanel_v15 0x153c50 | **IsLoaded**: base && bank loader done (FUN_0010d998) |

- `rnd_file ""`: the panel has no scene of its own. Scene names in the lessons (`t1_guitar.view`, …) resolve in the `ui`
  namespace to the `tut_script` panel's RndDir, which is up on the same screen [I].

### 4.2 VoiceMgr + StreamPlayer [C]
- Ctor FUN_001543a0 reads `{sound tutorial streaming_vo}`:
  - `volume` (float, +0x10)
  - `dir` (symbol, +0x14; strings 0x30c170 "dir", 0x30c178 "events")
  - `events`, parsed by FUN_00154620 into a vector of {name, idx = 0, vector<file>} (0x18 bytes each)
  - +0x18 = a new StreamPlayer (FUN_001554e8; vol fields 1.0, 1.0)
  - If the block is missing: "VoiceMgr: no streaming vo for tutorial".
- **play_vo event** (FUN_00154b78):
  - If the StreamPlayer is busy (FUN_001556c8: stream exists and is not finished) → **dropped** ("VoiceMgr::Play - dropping voice over %s").
  - Otherwise it finds the event (FUN_00154b30) and advances `idx = (idx+1) % nfiles` (FUN_00154c10) **before** use, so a multi-file event starts at its 2nd file and then round-robins.
  - Path = `sprintf("%s/%s", dir, file)`, e.g. `tutorial/streams/V10001` (the synth stream adds `.vgs`).
  - StreamPlayer play (FUN_001555f0) stops the old stream, asks the synth for a new stream (Synth vtable +0x124), and stores volume and "pending start".
  - An unknown event gives "Streaming voice-over event %s does not exist".
- **is_vo_playing** (FUN_00154c58 → FUN_00155700): `stream != null`. It stays true from the request until the poll deletes the finished stream, including the load latency.
- **reset_vo** (FUN_00154c78 → FUN_00155588): stop and delete the stream.
- **Poll** (FUN_00155710):
  - Not paused, stream exists and not finished: when it becomes ready the first time, set the volume `vol * master` (FUN_001557d0) and play.
  - Once it has been started and has ended, delete it, so `is_vo_playing` becomes false.
- **Pause** (FUN_00155670): store the flag and pause the playing stream.
- StreamPlayer also answers `set_volume f` (StreamPlayer_v1 0x155890), which sets the user volume.

### 4.3 Message table (TutorialPanel_v1 0x153f60; strings 0x30be78..0x30bf48)

| message | semantics | evidence | tag |
|---|---|---|---|
| `play_vo <event>` | VoiceMgr play (§4.2); returns 0 | 0x153f60 → FUN_00154b78 | C |
| `is_vo_playing` | 1 while a VO stream exists | → FUN_00154c58 | C |
| `reset_vo` | stop the VO now | → FUN_00154c78 | C |
| `start_tuning_mode` | FUN_00153e40(1): if not already tuning, TuneStart (FUN_00155f08: latch the current strum state, so a held strum doesn't count), tuning = 1. Fret counters are **not** reset (that happens at panel Enter). | 0x153e40, 0x155f08 | C |
| `end_tuning_mode` | tuning = 0 | FUN_00153e40(0) | C |
| `is_tuning` | tuning flag (cleared by Poll when all 5 frets reach 3) | FUN_00153e88, 0x153d50 | C |
| `start_hopo_train <type>` | FUN_00153e90(1,type): if not training, HopoStart(type) (FUN_00156b98); training = 1 | 0x153e90 | C |
| `end_hopo_train` | if training: stop the 5 fret sequences (FUN_00156b40), training = 0 | 0x153e90 | C |
| `is_hopo_training` | flag (cleared when 3 runs are complete) | FUN_00153ef8 | C |
| `is_tutorial_running` | +0x1a0 | FUN_00153f08 | C |
| `set_tutorial_running B` | +0x1a0 = B != 0 | FUN_00153f00 | C |
| `is_missing_guitar` | 0 if pad 0 is connected (+0x20) **and** is a guitar (FUN_002218e0), else 1 | FUN_00153f10 | C |
| anything else | GHPanel/UIPanel handler (StatusPanel_v1 0x207748): `thread_task`, `anim_task`, `delay_task`, `clear_tasks`, `set`, `get`, `set_paused`, `is_up`, … | | C |

Script-level messages on the same object (`start_tutorial`, `set_state`, `get_next_state`, `switch_to_state`,
`tutorial_inc`) are handlers from tutorials.dta (§3).

### 4.4 ThreadTask: the lesson runtime (TaskMgr FUN_002428c0, ThreadTask 0x241958/0x241a40/0x241bd0, in `00240000.c`) [C]
TaskMgr messages: `delay_task`, `interp_task`, `thread_task`, `anim_task`, `clear_tasks`, plus 0x335648.
- `thread_task [(preserve $v…)] stmt…` creates ThreadTask (FUN_00241958):
  - the statement index starts at the first **command** node (type 0x11), skipping the preserve list;
  - wake time = the TaskMgr's time;
  - wait_timeout deadline = -1.
- Poll (ThreadTask_v0 0x241a40), at TaskMgr time `now`:
  - If `wake <= now`: clear `yield`, set `wake = now`, swap in the preserved vars, bind **`$task`** (string 0x3355a0 "task") to the thread.
  - Then: `while index < size: exec stmt[index]; if yield: break; index++`.
  - Afterwards restore the vars. The task is done when `index == size`.
- `$task` messages (ThreadTask_v1 0x241bd0):
  - `wait COND` (0x241e60): `yield = COND != 0`, and the index does not advance, so the **same top-level statement is re-run next poll**. The thread waits *while* COND is true.
  - `wait_timeout COND ms [$var]` (0x241eb0):
    - on the first call, deadline = wake + ms;
    - `timed_out = deadline <= wake`;
    - `yield = !timed_out && COND`;
    - when not yielding, reset the deadline and, if a 4th arg is given, set `$var = timed_out`.
  - `sleep ms` (0x241fe8): yield, `index++` (the thread resumes **after** the current top-level statement), `wake += ms`.
  - `exit` (0x242050; string 0x3355c8): yield, `index = size`, so the thread finishes after the current statement returns, even when `exit` was sent from a nested handler (`switch_to_state`).
  - `current` / `set_current n`: get or set the index.
- Consequence: a `sleep` inside a nested block (`{if c {sleep_seconds $task 1.5}}`) delays the **next top-level** statement; anything after it in the same block runs at once. Reproduce this exactly.
- `anim_task obj ms from to` (0x242b48 / AnimateTask_v0 0x242300) sets the frame linearly and reaches `to` at the end.
- TaskMgr time is the panel's time.
  - While the panel is paused (`{ui panel tut} set_paused TRUE` in pause screens), Poll is not called, so threads, anims and VO freeze [C for VO/trainers via the +0x50 check in TutorialPanel_v10; I for the TaskMgr part].
  - Make the TaskMgr clock not advance while paused [I].

---------------------------------------------------------------------------------------------------
## 5. TutorialGuitarTune (lesson T1.1) [C]

Object at TutorialPanel+0x84, class "tut_tune"/"guitar_tune" (strings 0x30c630..0x30c658), typeinfo `18TutorialGuitarTune`.

Fields:
- +0x10 previous strum state
- +0x14[5] `ui::t1_fret%1.1i.view`
- +0x28[5] `ui::t1_fretcheck%1.1i.view`
- +0x3c[5] counts
- +0x50 vector of 15 Sequences

- **Enter** (FUN_00155ae8):
  - Resolve the 5 fret views and 5 fretcheck views; hide each fretcheck (`set_showing FALSE` through drawable vtable +0x24) and zero its count.
  - Create 15 `Sequence` objects for the sfx descs `s10101..s10115` (FUN_0023af28 "Sequence" + FUN_002127f8).
- Pad 0 state: frets are the pad bits **R2(1), Circle(5), Triangle(4), X(6), Square(7)** = green, red, yellow, blue, orange.
  Strum = bit 0x1000 or 0x4000 (DUp/DDown = strum bar) (FUN_00155960).
- **Poll** (FUN_00156390):
  1. For each fret: `t1_fretN.view.SetFrame(held ? 1 : 0)` (FUN_00155f48 → RndAnimatable::SetFrame FUN_001abab8). Count the held frets and remember the last one held.
  2. On a strum **rising edge** (the previous strum state was 0):
     - exactly 1 fret held → **FretHit(f)**;
     - 2 or more held → play the synth sequence `miss_chink` (string 0x30c750);
     - none → nothing.
  3. Store the strum state.
  4. done = every count >= 3. When done, reset all 5 fret views to frame 0.
- **FretHit(f)** (FUN_00156118):
  - `count[f]++`.
  - Stop any of the fret's 3 sequences that are playing, then play `s101xx` at index `f*3 + min(count,3) - 1`: three pitch steps per fret, the 3rd being "in tune" [I for the musical meaning].
  - Call the script func `fret_played (f+1)`.
  - When `count[f]` reaches exactly 3: show `t1_fretcheckN.view` and send `play_vo tuning_hit_vo` (round-robin over V10106..V10110).
  - Clamp the count to 3.
- **Exit** (FUN_00155ee8 / FUN_00155e48) deletes the Sequences.

## 6. TutorialHopoTrain (lessons T3.1 / T3.3a) [C]

Object at TutorialPanel+0xe8, class "tut_hopo"/"hopo_train", typeinfo `17TutorialHopoTrain`.

Fields:
- +0x10[5] `t1_fret%d.view`
- +0x24[5] `t1_fretcheck%d.view`
- +0x38[5] `t1_fret_check%d.mnm`
- +0x4c previous strum
- +0x50 active
- +0x54 valid
- +0x58 previous top fret
- +0x5c progress
- +0x60 completed runs
- +0x64 last event time (ms, from the CPU Count clock)
- +0x68[5] held
- +0x7c[5] check fading
- +0x90 type
- +0x94 vector<int> target sequence
- +0xa4 5 Sequences `s20601..s20605`

- **Enter** (FUN_00156780) resolves the views and mnms (in the namespace named by string 0x30ca68), hides the fretchecks and creates the 5 Sequences.
- **Start(type)** (FUN_00156b98): completed = 0, latch the current strum.
  - type 1 (`kHammerOnTutorial`, macros.dta) → sequence **[0,1,2]** (green, red, yellow).
  - Any other type (2, `kPullOffTutorial`) → **[2,1,0]**.
- **Poll** (FUN_001576c0):
  1. Read the held frets. `t1_fretN.view` frame is 1 or 0. **top** = the highest held fret, or -1.
  2. **Strum rising edge**: clear any fading checks.
     - top == -1 → active = valid = progress = 0.
     - top == seq[0] → progress = 1, active = valid = 1, show its check (FUN_00157548: check mnm frame 0, fretcheck view shown).
     - else → active = 1, valid = 0.
     - If top >= 0: **FretEvent(top)**.
  3. **No strum edge**, active, and top changed since the last poll:
     - top < 0 → active = 0, which resets valid and progress and fades the checks.
     - Within 2000 ms of the last event: FretEvent(top); if valid, then top == seq[progress] → check and `progress++`, else valid = 0 and fade the checks.
     - Otherwise (more than 2000 ms): active = valid = progress = 0 and fade.
     - Separately, if valid and more than 2000 ms have passed since the last event, fade and reset.
  4. Fade (FUN_001575f0): each fading check's mnm frame += 4 per poll. Above 100 the fretcheck view is hidden and the mnm goes to frame 0.
  5. The last-event time is updated whenever top changed (or on a strum edge).
  6. If valid and progress == size (3): **completed++**, fade the checks, valid = 0, call the script func **`hopo_hit`** (string 0x30cb00).
  7. done = `completed == size(seq)`, i.e. **3 runs**. When done, the fret views go to frame 0.
- **FretEvent(f)** (FUN_00157320): call the script func `fret_played (f+1)`, stop the playing `s206xx`, play `s2060(f+1)`.
- In practice:
  - Hammer-on = strum green, then press red, then press yellow, without strumming and within 2 s per step.
  - Pull-off = hold G+R+Y and strum (top is yellow), lift yellow, then lift red.

---------------------------------------------------------------------------------------------------
## 7. Everything else the lessons send, and the gameplay callbacks

### 7.1 `game` (GamePanel_v1 0x106548, `00100000.c`) [C]
| message | semantics | evidence |
|---|---|---|
| `set_skip_intro B` (+0x88) | the next start sends `intro_skip` and starts the music at once (no 10 s intro, no extend_track timeline) | GamePanel_v1, FUN_00105b80 (docs/endgame_and_hud.md §3.2) |
| `set_start_paused B` (+0x8c) | after starting, pause the clock (FUN_00106168(1)) | same |
| `set_time_paused B` / `time_paused` (+0x90) | pause the song clock **and** BeatMatch (audio and gems freeze, FUN_0010ebc0). The UI, VO and sfx keep running | FUN_00106168 |
| `load_song <sym>` | GameConfig set_song (FUN_00113600) + a new BeatMatch loader (FUN_00104f70 → FUN_0010db90): async load of chart and audio | FUN_001062b8 |
| `loaded` | true when the BeatMatch loader is done | string 0x2fe810 [I exact field] |
| `start_song` | clock reset (FUN_00104a38) + start pending (+0xa8). The next Poll runs FUN_00105b80 (skip_intro → `intro_skip` + music start; start_paused → time paused) | FUN_001062e8 |
| (Load) | in a tutorial the `endgame` panel and the winner photos are not loaded | GamePanel_v13 0x104b00 / v15 0x105270 (`is_tutorial_running`) |

### 7.2 `player0::player` (Player_v1 0x1174f8, `00110000.c`) [C]
| message | semantics |
|---|---|
| `percent_complete`, `star_power_ready`, `in_star_mode` | getters (gd01 already has them) |
| `set_crowd_rating_active B` (+0x64) | when 0, `FUN_001171b8` skips every crowd update, so the rock meter is frozen and the player **cannot fail**. A fail also clears it. |
| `set_crowd_rating x` | clamp 0..1, then set the rock meter's rating and raw value (FUN_0011d3f8) |
| `fill_star_power` / `empty_star_power` / `set_star_power x` | meter = 1 / 0 / x (FUN_0011cd68; crossing ready sends the deploy-ready message) |
| `set_star_power_deploy_rate r` (SP+0x3c) | drain per measure (normally 0.125); the wail lesson uses 0.01 (FUN_0011d018) |
| `set_star_power_phrase_boost b` (SP+0x40) | meter gain per captured phrase; the whammy lesson uses 0.0 (FUN_0011d030) |
| `set_whammystarpowerenabled B` (+0x30) | whammy feeds Star Power and whammy events fire only when set (FUN_00116880; used in FUN_00116930) |
| `set_mercuryswitchenabled B` (+0x2c) | tilt deploy on or off (FUN_00116888). The button deploy is not affected [I] |

`player0::player_matcher set_volume v` (PlayerMatcher_v1, string 0x2ff318) sets the guitar track volume (mute during demos) [C].

### 7.3 Gameplay → script callbacks [C scripts; I where noted]
`config/beatmatch.dta (player_matcher_handlers)` and `config/player.dta (handlers, star_power_handlers)` forward the
native events to global funcs. `system_script.dta reset_all_callbacks` redefines them all as no-ops.

| native event | handler block | callback | when |
|---|---|---|---|
| `hit ($slot $star $hopoable $hopoed $p)` | player_matcher_handlers | `gem_hit_callback $slot $hopoable $hopoed` | gem hit (gd01 sends this) |
| `miss ($p)` | player_matcher_handlers | `gem_miss_callback` (+ seq miss_chink) | overstrum / wrong strum [I] |
| `pass` | player_matcher_handlers | `gem_pass_callback` | gem scrolled past unplayed [I] |
| `hopo` (PlayerMatcher_v17, string 0x2ff310 "hopo") | player_matcher_handlers | `hopo_callback` | gem hit by hammer-on/pull-off without strum [I] |
| `swingAtHopo` (PlayerMatcher_v16) | player_matcher_handlers | `swingAtHopo_callback` | a HOPO-able gem was strummed: either strum-hit (FUN_00263860 checks the gem's hopo flag 0x100) or a strum on a gem already HOPO-hit (FUN_00263248). Both go through the sink FUN_00264760 [I mapping] |
| `held_note_released ($fraction)` (FUN_00116f50) | handlers | `held_note_released_callback $fraction` | a sustained gem ends; fraction = kept part (FUN_0011d5c8). 1 = held to the end |
| `whammy_start` / `whammy_end` (FUN_00116930) | handlers | `whammy_*_callback` | while holding a sustain with whammy-SP enabled. **In a tutorial every held sustain counts; otherwise only Star Power phrase notes count.** `start` is sent on every poll while the bar is active; `end` on every poll once it has been inactive for 500 ms (and the note has been held for 500 ms or more). Level-triggered [C] |
| `star_gem_hit` | star_power_handlers | `star_gem_hit_callback` | a gem of a star phrase is hit (FUN_0011c390) |
| `phrase_captured`, `phrase_miss`, `start_using`, `stop_using` | star_power_handlers | `star_phrase_captured/miss`, `star_use_begin/end_callback` | gd01 already sends these events |

### 7.4 Other objects
- `track_panel animate_track_out_fast` = `game anim_task track_panel 600 0 1920`. `animate_track_in_fast` reverses it after a 500 ms delay (track_panel.dta). The intro_skip in tutorial mode puts the track at frame 0 (hidden) [C].
- `hud set_showing B`. HUD `on_reset` hides the HUD while `is_tutorial_running` (hud_panel.dta).
  `score_meter_0.view`, `lightning_track.view` and `tut_*` labels and views come from the panels on screen.
- `arena set_lights_okay_chorus` is a venue light preset. `arena::lampshade02.mesh` / `arena::lamp.view set_showing FALSE` hide basement objects in front of the tutorial camera [I purpose].
  `arena_panel set_paused TRUE` freezes the venue after the opening VO.
- `synth pause_all_sfx B`, `synth stop_all_sfx`, `play_sfx`/`stop_sfx` (bkgd_music, tuning_complete, s19901, s19902, button_error).
- Scene messages used: `set_showing`, `set_frame`, `set_local_pos x y z`, `set_local_scale x y z`, `unhook_anim_parents` (views), `set_stage_tex 0 <tex>` (mats `t1_guitar_1.mat`, `t1_guitar_3.mat`), `set_text`, `set_localized_text`, `set_wrap_width` (labels).

### 7.5 Tutorial-mode differences elsewhere (`is_tutorial_running` consumers) [C]
- `arena/load_scripts.dta`: the guitarist and band scripts are **not** loaded. ArenaPanel_v13 0x10c758 sets arena+0x90 = !tutorial and skips loading the `guitarist`/`guitars`, so there is **no character on stage**.
- Camera: `camera_tutorial.dta`. intro_skip evaluates `$camedit.TUTORIAL[0]`, which in basement is one fixed shot `Cam_nt_np_close`/`Tut_01` with `hide_crowd 1`.
- `arena_game.dta`: no star-power claps. `game.dta mtv_overlay`: hidden. `hud_panel.dta`: HUD hidden on reset.
- FUN_00116930: whammy rule above. GamePanel load: no endgame panel or photos.
- Banks: only `tutorial/tutorial` (tut_loading resets the loader and disables the arena's venue bank). Crowd and venue sfx calls fail silently (docs/audio_sfx.md).

---------------------------------------------------------------------------------------------------
## 8. Completion and return [C]
- T1 `wrapup`, T2 end of `wail`, T3 end of `pulloff` → `ui goto_screen tutorial_quit` + `$task exit`.
  `tutorial_quit` stops all sfx, hides the overlay and goes `meta_loading_goto main`.
  `main` enter then sets `tutorial set_tutorial_running FALSE` and the menu bank and music come back.
- Nothing is written on completion. `tutorials_done` was raised when the tutorial was selected, and saving follows the normal autosave/needs_save path.
- Quitting from the pause menu takes the same `tutorial_quit` path.

---------------------------------------------------------------------------------------------------
## 9. Implementation plan for gd01

Order matters: steps 1–3 make T1.1/T1.6/T1.9 run, step 4 adds VO, steps 5–8 make the song lessons work, step 9 adds the trainers.

### Step 1: ThreadTask runtime (engine/src/ui/ui_system.cpp, PanelObj) [C semantics §4.4]
- Today `PanelObj::handleNative` treats `thread_task`, `clear_tasks` and `play_vo` as no-ops (ui_system.cpp:1238). Replace that.
- Add `struct ThreadTask : ScriptObject { std::vector<Value> body /*Code*/; size_t index; float wake, deadline=-1; bool yield; std::vector<std::pair<std::string,Value>> preserved; PanelObj* owner; }`.
  - `thread_task` collects the leading `(preserve …)` arrays like `delay_task` does (capture the values now) and the remaining `Code` args as the body.
  - Messages: `wait c`, `wait_timeout c ms [$var]`, `sleep ms`, `exit`, `current`, `set_current`.
  - Evaluate `c` with `interp.evalValue` **each call**.
- `PanelObj::poll(dt)`: if `!paused_`, advance the panel clock and poll the threads.
  - Bind the var `task` to the thread (save and restore the old value; threads can nest via `set_state`).
  - Restore the preserved vars, then run statements until `yield` (`interp.eval(stmt.code)`).
  - Remove finished threads. Threads created during a poll start on the next poll [I].
- `clear_tasks`: drop this panel's threads, delay tasks and the anim tasks it started.
  The current `ui_.animTask` is global, so tag each anim with an owner panel. This is needed for `animate_forever_30fps $this …` [I].
- Unit-test it with a tiny script: wait / sleep inside an if / exit from a nested handler.

### Step 2: TutorialPanel native (new `engine/src/ui/tutorial_panel.{h,cpp}`)
- `class TutorialPanel : public NativePanel` with the members `tuning_`, `hopo_`, `running_`, `VoiceMgr vo_`, `GuitarTune tune_`, `HopoTrain train_`.
  - Messages per §4.3.
  - `enter()` = trainer Enter; `exit()` = trainer Exit; `poll(dt)` = §4.1 slot 10 (skip while paused). Pause hooks into PanelObj `set_paused`.
- `makeNativePanel` (select_panels.cpp:699): `new_tutorial_panel` → TutorialPanel.
- Remove `"tutorial"` from the StubObj list (ui_system.cpp:1568). Otherwise `definePanel` will not register `object_name tutorial`, because `findObject` already finds the stub.
  `tutorials.dta` is loaded with `ghui/ui.dta`, so the object exists before the HUD scripts ask `is_tutorial_running`.
- Expose `UiSystem::tutorialRunning()` for App (whammy rule, end-of-song guard, venue mode).
- `is_missing_guitar`: §2.5 decision [I].

### Step 3: Scene and label messages (SceneProxy / ComponentObj in ui_system.cpp, ui/panel.cpp)
- Implement `set_local_pos` and `set_local_scale` on SceneProxy (meshes/views; they are no-ops today) and on labels. The box layout depends on them.
- `set_stage_tex <stage> <tex>` on `.mat` proxies (swap the material texture to a scene Tex).
- `unhook_anim_parents` can be a no-op [I]: it detaches the view from parent anims so the zoom tnm owns it.
- Label `set_wrap_width w`.
- Check that `.mnm`/`.tnm` `set_frame` and `anim_task` reach MatAnim and TransAnim objects in `tut_script.gh`.

### Step 4: VO streaming (tutorial_panel.cpp + audio/mixer)
- Add `Mixer::kVoice` (StreamSlot 2, `kStreamSlots` = 3).
- `VoiceMgr` loads `config/sound.dta` → `tutorial streaming_vo` (`volume`, `dir`, `events` (name files…)) with the existing DTA loader; the `#include` resolves `tutorial/gen/tutorial_vo.dtb`.
- `play(event)`: drop if busy; round-robin as in §4.2; `data.read(dir + "/" + file + ".vgs")` (GameData lookups are case-insensitive) → `mixer.playStream(kVoice, …, loop=false, fade 0, volume)`.
- `isPlaying` must be true from the request until the stream ends: track it with a flag cleared when `!streamPlaying(kVoice)` is seen in poll.
- `pause` → `setStreamPaused`. `reset` → `stopStream(kVoice, 0)`. `leaveGame` must also stop it.

### Step 5: Tutorial song lookup and the tutorial game mode (app.cpp, ui_system.cpp GameObj)
- Keep tutorial songs out of the quickplay/career list, but keep them reachable.
  - Store `tutorialSongs_` (or all songs) in App.
  - Add `hooks.findSong(id) -> const SongInfo*` and make `game set_song`/`load_song` accept ids that are not in the provider list, via `tutorialSongId`.
- Generalise the start trigger in `UiSystem::enterScreen` (ui_system.cpp:1846): start the game when entering any screen whose panels include `game` from one that does not (`tut_script`), not only `next->name()=="game"`.
- New hooks:
  - `setTimePaused(bool)`: `mixer_.setPaused` only. Do not change `screen_`, synth or crowd. Keep the UI "Paused" screen semantics separate.
  - `timePaused()`.
  - `loadSong(id)`: load the chart and VGS like `startSong` but keep the venue, banks and UI state; reset the Player.
  - `songLoaded()`: true once loaded. A synchronous load is fine; the script's `wait {! {game loaded}}` passes at once.
  - `startSong()`: start at song time 0 with no lead-in/intro; apply `start_paused`; send `intro_skip` instead of `intro_start` when `skip_intro`.
  - `guitarVolume(float)` for `player_matcher set_volume`.
  - GameObj currently swallows `set_skip_intro` and friends via the `set_*` catch-all (ui_system.cpp:1035) and answers `loaded` with 0 (ui_system.cpp:1021). Handle them explicitly.
- `App::startSong` in tutorial mode (`ui_->tutorialRunning()`):
  - Banks: `synth_->clearSlots(); loadSlot(0,"tutorial/tutorial")`. Add a `Synth::loadTutorialBanks()`.
  - `crowd_` off. Difficulty Easy (already set by the script).
  - Venue `basement` built **without a band**: a VenueRenderer option "no guitarist/band".
  - Camera group forced to `TUTORIAL` (VenueCamera: select the `$camedit.TUTORIAL` shot and stay on it).
  - Skip the 10 s intro timeline (`intro_skip` path: arena, track and hud intro_skip; track at frame 0).
  - Start paused.
- Frame loop:
  - While time-paused, do not feed strum/fret to `Player` (BeatMatch is paused in the original) [I], but do feed the trainers (step 9).
  - Suppress the end-of-song `game_over`/`game_won` path when `tutorialRunning()`. The script stops at 97 %, and the original never shows endgame in tutorials [I].
  - `percent_complete` must use the song time even while paused.
- `track_panel animate_track_*_fast` already go through `game anim_task track_panel` + `trackFrame`; check that they work with time paused (UI-clock anim).

### Step 6: Player additions (engine/src/game/player.{h,cpp})
- Events:
  - Split `Miss` into `Miss` (overstrum, from `overstrum()`) and `Pass` (from `sweep()`) [I].
  - `Hopo` (in `hitGem` when `viaHopo`).
  - `SwingAtHopo` (strum-hit of a gem with `g.hopo`, and the forgiven strum after a HOPO hit in `watcherStrum`).
  - `HeldNoteReleased{fraction}` (in `finalizeHeld`, only for gems with a sustain; fraction as computed now, 1.0 when held to the end).
  - `WhammyStart` / `WhammyEnd` (§7.3 rule, with `tutorialMode` widening it to any held sustain).
  - `StarGemHit` (in `spGemHit` when the gem has a phrase).
  - Add a `float value` to `PlayerEvent`.
- Setters:
  - `setCrowdRatingActive(bool)`, which gates `crowdUpdate`; a fail clears it.
  - `setCrowdRating(float)`, which sets `rating_` and `raw_` via the range mapping.
  - `fillStarPower`, `emptyStarPower`, `setStarPower(x)` (also `setHudStarLevel` so a deploy is possible at once).
  - `setDeployRate(r)` and `setPhraseBoost(b)`: per-player copies of `rules_.spDeployRate` / `spPhrase`.
  - `setWhammySpEnabled(bool)`.
  - `setMercuryEnabled(bool)`: gates tilt only. The port's SP button is the "select" deploy; if tilt is mapped, gate that [I].
  - `setTutorialMode(bool)`.

### Step 7: Wire the callbacks (app.cpp `playerEvent`, ui_system.cpp handler sets)
- `playerEvent`: Pass → `player_matcher_handlers pass`; Hopo → `hopo`; SwingAtHopo → `swingAtHopo`;
  HeldNoteReleased → `handlers held_note_released (fraction)`; Whammy* → `handlers whammy_start/end`;
  StarGemHit → `star_power_handlers star_gem_hit`.
- Add `handlerSet("config/player.dta", "handlers", "player_handlers")` (ui_system.cpp:1621); only star_power_handlers is registered today.
- `player0::player` (PlayerStatsObj, ui_system.cpp:913): add the §7.2 setters via a new `hooks.playerCommand(msg,args)` into App/Player.
- `player0::player_matcher set_volume` → `hooks.guitarVolume`. Register an object under `player0::player_matcher`, or rely on the `::` fallback to `player_matcher` (StubObj today).

### Step 8: Arena / HUD / misc
- `arena set_lights_*` from scripts → VenueRenderer light preset (the VenueScript handler names already exist).
- `arena::<obj> set_showing` → a hook into the venue scene (hide lampshade02 / lamp).
- `arena_panel set_paused` → freeze venue animation.
- `hud set_paused`, `app set_paused`, `synth pause_all_sfx` / `stop_all_sfx` are used by the tut_pause screens. Check that each one maps to the existing hooks, without leaving the Playing screen.
- Start button in `tut_script`: App's in-game Start must reach the UI's `BUTTON_DOWN_MSG` (as for the `game` screen), not the engine's own pause.

### Step 9: Trainers (new `engine/src/ui/tutorial_trainers.{h,cpp}`)
- Add `SelectHost::padState(int p, uint32_t& heldFrets, bool& strumDown)`. App fills it from InputState: the five fret keys and the strum keys as a *level*, not an event. Lefty doesn't matter (frets, not d-pad).
- `GuitarTune` and `HopoTrain`, written to §5/§6 exactly (edge detection, 2000 ms window via the UI clock, fade step 4 frames per poll).
  - Scene access through the `tut_script` Panel (`setFrame`, `setShowing`).
  - Sfx via `Synth` (play/stop/isPlaying by desc name `s101xx`/`s2060x`; `miss_chink` sequence).
  - Script calls `interp.callFunc("fret_played", {f+1})`, `callFunc("hopo_hit", {})`, `vo_.play("tuning_hit_vo")`.
- The poll rate is the UI frame rate, while the original polled at 60 Hz. The 4-frame fade step is per poll, so scale it by `dt/16.67` [I].

### Step 10: Menu and controller
- `tutorials` screen: already script-driven. Check `tut_1/2/3.btn` disabled states and `FOCUS_MSG` blurbs.
- §2.5: make `main_tutorial.btn` usable on keyboard (get_controller "guitar" in guitar mode, or `tutorial_access`).

### Step 11: Verification
- Add `--tutorial N` (dev aid) that boots into `tutorials`, selects N and logs `ui: goto_screen` + `tutorial: state X`.
  Use GD01_UILOG for `play_vo`, dropped VO and task waits.
- Autoplay can pass the song lessons:
  - T1.2–T1.5 by hitting.
  - T3.2/T3.3b need autoplay to use HOPOs (no strum) so `hopo` fires.
  - T2.3 needs the dev whammy (`GD01_WHAMMY`). T2.4 needs autoplay to deploy SP.
- The trainers need scripted input. Add `GD01_TUTORIAL_INPUT` with a sequence of (ms, frets, strum) or a "trainer autopilot" in debug builds.
- Screenshot each lesson's overlay (existing screenshot hook) and compare against the box sizes `TUTn_m_BOX`.
- Timeouts: run with no input and check that "LESSON TIMED OUT!" appears and the next lesson starts (97 % / 60 s).

---------------------------------------------------------------------------------------------------
## 10. Open questions [I]
- The exact TaskMgr time base (UI ms vs panel-local), and whether threads advance on the frame they are created.
- The `loaded` field (BeatMatch loader done flag) is not traced.
- Whether `miss` vs `pass` exactly match overstrum vs sweep, and which watcher path sends `hopo` (likely in FUN_00263728 when hit via HOPO).
- The meaning of the three `s101xx` pitches per fret (detuned → tuned assumed).
- The original strum detection reads the strum bar bits (DUp/DDown). On a joypad this would be the d-pad, so the original effectively needs a guitar.
- Correct `docs/audio_sfx.md` ("Unused content: … s10101..s10115 and s20601..05") once implemented.
