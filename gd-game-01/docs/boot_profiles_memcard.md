# GH1 (PS2, SLUS_212.24): boot flow, profiles, memory-card screens, help bar

Marks: **[C]** read directly from the code or scripts. **[I]** inferred, not proven.
Scripts are in `extracted_dta/ghui/gen/` (`splash.dta`, `mem_card.dta`, `error.dta`, `game.dta`, `pause.dta`, `options.dta`, `main.dta`)
and `extracted_dta/config/gen/system_script.dta`. Strings are in `extracted_dta/ghui/eng/gen/locale.dta`. The decomp is in `analysis/decomp/`.
All UI times are in **ms of `ui frame`** (`DAT_00369304`).

---------------------------------------------------------------------------------------------------
## 1. Boot flow, power-on to main menu

### 1.1 Native logo slideshow (before any script UI) [C]
`App::App` = `FUN_00100258` (string "harmonix" @0x2fdcc8 and its neighbours):
```
FUN_00152a90(splash)                 ; splash.minMs = 3000, count = 0
FUN_00152b38(splash, "pub")          ; ghui/gen/pub_splash.rnd_ps2      -> RedOctane logo
FUN_00214468 / FUN_001073b0 (Options) / FUN_001045c0     ; subsystem init while the logo is on screen
FUN_00152b38(splash, "mtv")          ; ghui/gen/mtv_splash.rnd_ps2      -> "MTV Music Television GAMES" logo
FUN_0011d920(0)
FUN_00152b38(splash, "harmonix")     ; ghui/gen/harmonix_splash.rnd_ps2 -> Harmonix logo
FUN_002094d8(ui)                     ; UIManager init: runs gh.dta (ui (create ... {ui goto_screen $first_screen}))
FUN_0014a3b0()                       ; MemcardMgr init (config/mc.dta)
FUN_00153820()
FUN_00152af0(splash, 2)              ; wait until the Harmonix logo has been up 3000 ms, then free it
FUN_0020a810(ui); main loop FUN_00100480
```
- `FUN_00152b38(splash, name)`:
  1. **Busy-waits** (`FUN_00152cb8`) until the previous logo has been up for `minMs = 3000`.
  2. Loads `ghui/%s_splash.rnd` (@0x30b5a8) and finds `splash.view`.
  3. Draws it for 2 frames (double buffer) and restarts the timer.
- So **each logo stays up max(3000 ms, the init work that follows it)**.
  - Fixed order: **RedOctane → MTV Games → Harmonix**. Each logo is one static textured quad in `splash.view`.
  - No fades: the screen holds the last frame until the next one is drawn **[I: no fade code found]**.
  - **Not skippable**: no pad input is read here.
  - The logo images are extractable (the Tex is embedded in each `.rnd_ps2`). I decoded them to PNG under the scratchpad `logos/` folder.
- There is **no separate legal or health-warning screen**. The legal lines are labels on the `splash` ("press any button") scene (§1.4).
- There is also **no "progressive scan" prompt**.

### 1.2 Script start [C]
- `gh.dta` `(ui (create … {meta set_defaults} {ui my_init} {if {! {app app_child}} {ui goto_screen $first_screen}}))`.
- `meta set_defaults` sets `$first_screen bootup_load`, `$autosave FALSE`, `$main_entered FALSE` and calls `meta setup_quickplay`.
- `goto_screen` is asynchronous, so `bootup_load` enters on the first main-loop polls, right after the Harmonix logo's 3 s.
- `test_game.dta` sets `$first_screen loading` (debug only).

### 1.3 Screen order
```
[logos: RedOctane 3s → MTV 3s → Harmonix 3s]
→ bootup_load            (memory-card check/load dialog, §3)
   … possible memcard sub-dialogs (create save / no card / caution / autosave_disable_check) …
→ cut_scene              (FMV videos/ghintro.pss)
→ splash                 (title + legal + "PRESS ANY BUTTON TO BEGIN"; attract after 60 s)
   ├─ num_profiles > 0  → chooseprof → (existing band) main
   │                                  → (empty slot)    nameprof → [autosave] → main
   └─ num_profiles == 0 → guitar_help → nameprof → [autosave] → main
```

| screen | panels | driven by | duration / exit | skippable |
|---|---|---|---|---|
| `bootup_load` | meta dialog helpbar (DIALOG_SCREEN, no anim) | `mem_card.dta` + natives `memcard get_info/load_data` | ≥1000 ms "checking", then the async load, then 1000 ms more → `cut_scene` (§3.3) | no (no buttons in the dialog) |
| `cut_scene` | none (`animate_transition FALSE`, `allow_back FALSE`) | `splash.dta`. On `TRANSITION_COMPLETE`: `{meta play_movie ghintro.pss}` then `{ui goto_screen splash}` | length of the movie; **blocking** native player | **yes**, see below |
| `splash` | meta splash (`player_num 0`, no anim) | `splash.dta` panel `splash` | until a button press, or 60 s → attract | any button except guitar L2 |
| `guitar_help` | meta guitar_help helpbar | `splash.dta` | until select (green fret/R2, or X/Select on a pad) | – (helpbar "CONTINUE") |
| `chooseprof` / `nameprof` | see §2 | | | |

**`meta play_movie f`** (`FUN_0014ccf0` → `FUN_0024edf8(0.0, "videos/%s", alloc, free)`) [C]:
- It only plays if `FUN_00224238()` (flag `DAT_002f1d74`) or `set_tool_movie` (`DAT_002edc34`) is set **[I: the flag means "running from disc"]**.
- The player runs its own blocking loop: no UI polling or drawing, and the script continues only after the movie ends.
- The loop exits at end of stream, or when `elapsed > minSec*1000` (minSec = 0, so immediately) **and** pad 0 has a button in mask `0x842` pressed.
  Mask bits are kPad 1 / 6 / 11, which means **R2 (green fret), X, or Start** skip it at any time.
- The frame-pacing timer uses NTSC/PAL rates (RCNT0 target 0x20d / 0x271).
- The duration of `ghintro.pss` (16,023,556 bytes) was not measured, because the movie is not extracted **[I: ≈30–60 s]**.

### 1.4 Splash / "press any button" [C]
- `splash.gh` labels:
  - `splash_press_start.lbl` "PRESS ANY BUTTON TO BEGIN"
  - `splash_legal.lbl` "©2005 RedOctane Inc. ALL RIGHTS RESERVED"
  - `splash_legal2.lbl` "Game engine code ©2005 Harmonix Music Systems, Inc."
  - `splash_legal3.lbl` "Developed by Harmonix Music Systems. Protected by US Patent number 6,429,863."
  - `splash_legal4.lbl` "RedOctane® is a registered trademark…"
  - `splash_legal5.lbl` "Guitar Hero (TM) is a trademark of RedOctane, Inc."
  - Other objects: poster `splash.bmp` on a brick wall, and `splash_legal.view`.
- Panel `splash`:
  - `finish_load`: `splash.view set_frame 0`.
  - `enter`:
    1. `anim_task splash.view 3333 0 100`, an intro animation of frames 0→100 over **3333 ms**.
    2. `$enter_time = ui frame`.
    3. `$attract_mode FALSE`, `$entering_attract_mode FALSE`.
    4. `synth play_sequence meta_lick`.
  - `BUTTON_DOWN` (pad 0 only, `player_num 0`):
    1. If `meta is_any_button`, `play_sequence button_select`.
    2. `campaign num_profiles > 0` ? `goto chooseprof` : `goto guitar_help`.
    - Input is **not** blocked during the 3.3 s intro anim. Input is only dropped while the UIManager is in a transition (§4).
  - `poll`: if not entering attract and not in a transition, and `ui frame − $enter_time > 60000` (`DELAY_BEFORE_ATTRACT_MS`), and not `$disable_attract_mode`, it calls `enter_attract_mode`.
    `$disable_attract_mode` is never set by any shipped script.

### 1.5 Attract mode [C]
- `enter_attract_mode` (`splash.dta`):
  1. `$entering_attract_mode TRUE`.
  2. `meta setup_quickplay`.
  3. `game set_song {campaign get_random_song}`. This is a random song, re-rolled until it is a career song or an unlocked store song (`FUN_00138620`).
     Before any profile is picked, the profile is slot 0 (Campaign ctor sets +0x28 = 0).
  4. `game set_difficulty kDifficultyExpert`.
  5. `game set_quickplay`, which applies the song's quickplay venue, character and guitar.
  6. `$attract_mode TRUE`.
  7. `ui goto_screen loading` → `game`.
- `game` screen (`game.dta`) when `$attract_mode`:
  - **enter**:
    - `$enter_time = ui frame`; `$leaving FALSE`; `$started_fade FALSE`.
    - `track_panel set_showing FALSE`: the highway is hidden. `track_panel intro_start/intro_skip` also do `set_showing {! $attract_mode}`.
    - `player0::player_matcher set_auto_play TRUE`: the **CPU plays the song perfectly** **[I: auto_play hits every gem]**.
    - The HUD is hidden (`hud on_reset`: `set_showing {! (|| $attract_mode tutorial)}`).
    - Panel `fade` enter: `press_any_button.lbl set_showing $attract_mode`, which shows "PRESS ANY BUTTON TO ROCK" (locale `press_any_button`).
    - The result is a **demo of venue + band + song audio with no highway and no HUD**, plus the prompt.
  - **BUTTON_DOWN**: `meta is_any_button` → `$leaving = now`. Start does **not** pause in attract.
  - **poll**:
    1. If not leaving and `now > 120000` (`ATTRACT_LENGTH_MS`), set `$leaving = now`.
    2. Once leaving, call `fade start_fade 2000` (`FADE_LENGTH_MS`) once.
    3. When `now > leaving + 2000`: `synth stop_all_sfx`, `game set_paused TRUE`, `goto finish_fade`.
- `finish_fade` (panels fade) enter calls `meta_loading_goto splash`:
  1. `meta_loading` (animated loading panel).
  2. On TRANSITION_COMPLETE, `autosave_goto splash`.
  3. `splash`. Its enter resets `$attract_mode FALSE` and the 60 s timer.
- Any button during attract → a 2 s fade → back to splash. It does **not** go straight to profile select.
- If the song ends before 120 s **[I]**: `arena_game` outro → `post_show` → `{meta get win_screen}` = `endgame`. The attract scripts do not handle this. In practice songs are longer than 120 s.

---------------------------------------------------------------------------------------------------
## 2. Profile screens (splash.dta, error.dta, options.dta)

### 2.1 Common pieces [C]
- `PROFILE_PANEL_COMMON` (used by chooseprof, loadprof and delprof):
  - Vertical navigator over `cp_band0..7.btn` (wraps; focus starts on `cp_band0.btn`).
  - `set_up_bands`: for i in 0..7 (`MAX_NUM_PROFILES 8`), `is_empty_profile i` ? `set_up_new_profile btn` : `btn set_localized_text {campaign profile_name i}`.
  - `FOCUS_MSG` stores `button_idx` = 0..7.
- `campaign` natives (see docs/career.md §2.1): `num_profiles`, `max_num_profiles`(8), `is_empty_profile i`, `profile_name i`,
  `profile_slot`, `set_profile_slot i` (also copies that profile's options into Options), `set_profile_name s` (sets needs_save),
  `has_profile_name s editing` (case-insensitive; skips the current slot when editing), `empty_slot`, `delete_slot i`.

### 2.2 `chooseprof` (screen: meta chooseprof helpbar; `allow_back FALSE`, `animate_transition FALSE`; helpbar `fret1 SELECT, strum UP/DOWN`) [C]
- Enter: `anim_task chooseprof.view 1000 0 100` (1 s intro), then `set_up_bands`. Empty slots read **"new band"** (`new_band`).
- `SELECT_MSG`:
  1. `campaign set_profile_slot idx`.
  2. Existing band → `goto main`. No save, because needs_save is unchanged.
  3. Empty slot → `nameprof set back_screen chooseprof`, `can_go_back TRUE`, `is_editing FALSE` → `goto nameprof`.
- There is no back button (you cannot return to splash).

### 2.3 `guitar_help` [C]
- Panel `guitar_help` (`player_num 0`), scene `guitar_help.gh`. Title "USING THE GUITAR CONTROLLER", with 5 steps (plug in, put on, playing position, "Continue (Green button) Back (Red button)", "START button", "Up/Down (Strum bar)").
- Screen: `allow_back FALSE`, no anim, helpbar `fret1 CONTINUE`.
- `BUTTON_DOWN` with `meta is_select_button`:
  1. `nameprof set can_go_back FALSE`, `is_editing FALSE`.
  2. `button_select` sfx.
  3. `goto nameprof`.
- It has no timer. It is shown **only when there are no profiles at all**, which means first boot or a fresh or declined save.

### 2.4 `nameprof` (screen object `nameprof`: panels meta nameprof helpbar, focus `profile.ten`; `allow_back FALSE`; `animate_transition $main_entered`) [C]
- `animate_transition $main_entered` **[I]**: the value is read once when the screen is built, before `set_defaults`, so it is probably always FALSE.
- Scene text:
  - "NAME YOUR BAND" (`name_band`).
  - "This is also your SAVE GAME name".
  - "Press START button to accept name and continue".
  - "You can edit this name later from Data Settings in the Options menu".
  - TextEntry style `band_name`: up to 20 characters from `ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 !?.'`.
- `init`: `profile_slot -1`, `initial_text 0`.
- `enter`:
  1. `$main_entered ? turn_off_ambient : reset_ambient`.
  2. `anim_task nameprof.view 1000 0 100`.
  3. If `initial_text` is set, `profile.ten set_text` it (rename case), then clear it.
- The helpbar is driven by script (`get_text_entry_help_text`):
  - `fret1 NEXT`, if the user can scroll.
  - `fret2` is either BACK (empty text and `can_go_back`) or DELETE.
  - `strum UP/DOWN`.
  - It is re-sent on TRANSITION_COMPLETE (which also does `profile.ten resume_input`), on every SELECT_MSG, and on SCREEN_BACK.
- `TEXT_ENTRY_MSG` (START accepts):
  - `""` → `ui push_screen error_no_profile` ("NO BAND" / "Please enter a BAND NAME.").
  - `has_profile_name name is_editing` → `push_screen error_duplicate_profile`.
  - Otherwise:
    1. If `profile_slot != -1`, `campaign set_profile_slot profile_slot`, then reset it to -1.
    2. `campaign set_profile_name name`, which sets needs_save.
    3. `button_select` sfx.
    4. **`autosave_goto main`**.
- `BUTTON_DOWN` back with empty text and `can_go_back`:
  1. `profile_slot -1`.
  2. `backwards_anim`.
  3. `button_back` sfx.
  4. `goto {get back_screen}`.
- Error screens (`error.dta` `ERROR_SCREEN_HANDLERS`) are pushed on top: panels `dialog`, helpbar `fret1 CONTINUE`. Select on button1 → `ui pop_screen`.
  The other error screens are `error_max_profile` ("NO FREE BANDS") and `error_cant_delete_profile`.

### 2.5 Exact first-boot flows [C]

**A. No save on the card, or no card**
1. Logos → `bootup_load` sets **`$autosave TRUE`**.
2. Card branches:
   - Card present but no save: the load fails → `bootup_create_save_check` "save not found":
     - Yes (default) → `bootup_create_save`. This writes a **save with 8 empty profiles**, then `$mc_overwrite TRUE` and `save_complete` → `cut_scene`.
     - No → `bootup_caution` ("Continue without saving?", default No):
       - Yes → `autosave_disable_check` ("Do you want to disable the Autosave feature?", default Yes). Yes sets `$autosave FALSE`, No keeps it TRUE. Either way → `cut_scene`.
       - No → retry `bootup_load`.
   - No card: `bootup_nocard` ("start without saving?", default No):
     - Yes → `autosave_disable_check` → cut_scene.
     - No → retry.
3. `cut_scene` → `splash` → any button → `num_profiles == 0` → **`guitar_help`** → **`nameprof`** (slot = current = 0, `can_go_back FALSE`).
4. START with a valid name:
   1. `set_profile_name` (slot 0).
   2. `autosave_goto main`.
   3. If `$autosave` is set: the **`autosave`** screen → `main`. Otherwise straight to `main`.

**B. Existing save**
1. `bootup_load` → `load_data` OK → `$mc_overwrite TRUE`, wait 1000 ms → `cut_scene` → `splash`.
2. Any button → `num_profiles > 0` → **`chooseprof`**.
3. Choices:
   - Existing band: `set_profile_slot i` → `main`, with no save screen.
   - "new band" slot: `set_profile_slot i` → `nameprof` (`can_go_back TRUE`, back to chooseprof) → `set_profile_name` → `autosave_goto main` → autosave → main.

Message order for a new band:
- From chooseprof: `campaign set_profile_slot i`, then at TEXT_ENTRY `campaign has_profile_name s FALSE`, then `campaign set_profile_name s`, then (autosave) `memcard save_data autosave $mc_overwrite` → `campaign save_complete`.
- From guitar_help: `set_profile_slot` is not called; slot 0 is used.

### 2.6 Profile actions in Options → Data Settings (`data_settings`; vertical rename / autosave / newprof / saveprof / loadprof / delprof) [C]
Helpbar: default `fret1 CONTINUE, fret2 BACK, strum UP/DOWN`; on `ds_autosave.btn` it is `fret1 ON/OFF …`. Enter: `autosave.tog set_state $autosave`.
`options` disables `op_data.btn` while `$disable_save`, which is set by the unlock-all and crowd-meter cheats.

| button | flow |
|---|---|
| rename (`rename band`) | nameprof with `back_screen data_settings`, `can_go_back TRUE`, `initial_text = profile_name(profile_slot)`, `is_editing TRUE` → TEXT_ENTRY → `autosave_goto main` |
| autosave | `autosave.tog toggle`; `$autosave = tog state` (no save) |
| new band | if `num_profiles < 8`: if `needs_save` → `new_profile_warning` ("You have unsaved data…", continue/cancel), otherwise `goto_nameprof_for_new_profile` (`profile_slot = campaign empty_slot`, `can_go_back TRUE`, back data_settings). If all 8 slots are full → `push_screen error_max_profile` |
| save band | `$mc_save_success_screen main`, `$mc_save_failed_screen data_settings` → `memcard_save_confirm` |
| load band | `$mc_post_load_screen loadprof`, `$mc_load_failed_screen data_settings` → `memcard_load_confirm` |
| delete band | `pre_delete_confirm` |

- `loadprof` (screen anim TRUE, `allow_back FALSE`; helpbar SELECT/UPDOWN): empty slots are disabled and blank. Select → `set_profile_slot i` → `main`.
- `delprof` (anim TRUE; helpbar SELECT/BACK/UPDOWN): empty slots are disabled.
  - Select on the **current** slot → `push_screen error_cant_delete_profile`.
  - Any other slot → `delete_confirm.selected_slot = i` → `delete_confirm`.
  - Back → `backwards_anim` → `data_settings`.

---------------------------------------------------------------------------------------------------
## 3. Memory-card screens (mem_card.dta + native `memcard`)

### 3.1 Native `memcard` (MemcardMgr, handler `MemcardMgr_v1` @0x14b368) [C]
Card file: `BASLUS-21224/data`. One 0x1e000-byte (122,880 B) buffer at 0x344fb8 holds all 8 profiles and the high scores (career.md §4).

| method | sync? | semantics |
|---|---|---|
| `get_info` | sync | card status from `FUN_00225b18(port 0)`: 0 ok, −101 no card, −102 unformatted, … (`FUN_0014b768`) |
| `load_data obj` | **async** (`FUN_0014ad08` → worker `FUN_0014ad38`, done `FUN_0014ae00`) | status must be 0 or −103. Opens the dir and reads `data` (0x1e000 bytes). On 0, `campaign` is deserialized (`FUN_00138c30`). Then `memcard_result(result, 0)` is sent **to obj** |
| `save_data obj overwrite` | **async** (`FUN_0014a650` → `FUN_0014a730`, done `FUN_0014a908`) | Serializes campaign first (`FUN_00138b90`). If the dir or file is missing (or a reset flag is set), it re-creates the dir and `icon.sys` and needs **free ≥ 0x33000 B** (else −106, and the message carries `$space_free`). If the file exists and `overwrite` is 0, it returns **−120 kMCFileExists**. Otherwise it writes the file |
| `format obj` | async (`FUN_0014b068`) | formats only if status is −102. If the card is already formatted, it returns −121 |
| `space_needed` | sync | 0x33000 |
| `space_available` | sync | free bytes, or 0 on error |
| `slot` | sync | `"1-A"` when a multitap is present, otherwise `"1"` (`FUN_0014b2f0`) |
| `check_save_game` | sync | 0 if the save file exists, otherwise an error |
| `localize tok` | sync | looks up `tok_ps2` in the locale table and substitutes `{SLOT}` (→ slot) and `{REQ_SPACE}` (→ `"%dKB"`, i.e. "204KB"). The dialog body text always comes from this |

Result codes (`mem_card.dta`): `kMCNoError 0, kMCNoCard −101, kMCNotFormatted −102, kMCDifferentCard −103, kMCReadWriteFailed −104,
kMCCorrupt −105, kMCNotEnoughSpace −106, kMCGeneralError −110, kMCFileExists −120, kMCAlreadyFormatted −121`.
`kMCDamaged` is **not defined**, so those switch arms never match: a script bug in the original.
Message: `MEMCARD_RESULT_MSG = (memcard_result ($result $space_free))`, delivered straight to the screen object that called the method.

### 3.2 Dialog panel [C]
- Every memcard screen is `DIALOG_SCREEN`: panels `meta dialog helpbar`, focus `dialog`, `allow_back FALSE`, **`animate_transition FALSE`**. That means instant cuts.
- Panel `dialog` (`force_exit TRUE`, so it re-enters on each screen; enter calls `reset_ambient`):
  - Scene `dialog.gh`: title `dl_title.lbl` (style `label_error_red`), message `dl_message.lbl` (wrap 580), and two buttons `dl_button1/2.btn` on a vertical navigator.
  - `setup b1 b2 default msg title`: an empty `''` button is hidden and disabled; `default` = button1/button2/none sets the initial focus.
    It sets the helpbar to `fret1 SELECT` if button1 shows and `strum UP/DOWN` if button2 shows.
  - `message msg title` = `setup '' '' none …`: **no buttons and an empty helpbar** (progress screens).
  - `set_message tok` changes only the body text.
- Titles: `mc_title_loading` "loading...", `mc_title_saving` "saving...", `mc_title_caution` "caution", `mc_title_error` "memory card error", `mc_title_nosave` "save not found".

### 3.3 Progress screens and timing [C]
All three below are non-interactive. Each phase is a `delay_task 1000` on the screen, or the async op time.

| screen | phase 1 (≥1000 ms) | phase 2 (op time) | phase 3 on success (1000 ms) | then |
|---|---|---|---|---|
| `bootup_load` | title "loading...", body `mc_checking` "Checking status of memory card… Do not remove the memory card, reset, or switch off console." (if `get_info` = no card → `bootup_nocard` at once) | `load_loading` "Loading Guitar Hero data. Do not remove memory card…" + `load_data` | **body unchanged** (still `load_loading`), `$mc_overwrite TRUE` | `cut_scene` |
| `bootup_create_save` | "saving...", `mc_checking` | `bootup_create_save` "Creating Guitar Hero save file. Do not remove…" + `save_data $mc_overwrite` | `bootup_create_success` "Guitar Hero data created successfully.", `campaign save_complete`, `$mc_overwrite TRUE` | `cut_scene` |
| `autosave` | "saving...", `mc_checking` | `save_saving` "Saving Guitar Hero data. Do not remove memory card…" + `save_data $mc_overwrite` | `save_success` "Save successful", `save_complete`, `$mc_overwrite TRUE` | `$mc_save_success_screen` |
| `memcard_load` | "loading...", `mc_checking` (no card → `memcard_load_error` with `mc_nocard_error`) | `load_loading` + `load_data` | `load_success` "Guitar Hero data loaded successfully.", `$mc_overwrite TRUE` | `$mc_post_load_screen` (= `loadprof`) |
| `mc_format` | "caution", `mc_checking` | `format_card` "Formatting… Do not remove…" + `format` | (none) | `$mc_format_done` |

- Minimum time on PS2: about 2 s plus the card I/O (a real card takes about 0.5–2 s for 120 KB **[I]**).
- On failure, phase 3 is skipped and the result goes straight to an error dialog.
- For the PC port: show phase 1 ("checking") and phase 3 (success) for **≥2 s each** instead of 1 s, and phase 2 for as long as the file I/O takes.
  - The "Do not remove the memory card" warning text is in phases 1 and 2.
  - Rewording `{SLOT}`/"memory card (8MB)(for PlayStation®2)" is up to you. `localize` resolves `<tok>_ps2`, so a PC table could use `<tok>_pc`.

### 3.4 Prompt / error dialogs (all wait for input; the default focus is shown as *) [C]

| screen | buttons | text token | button1 → | button2 → |
|---|---|---|---|---|
| bootup_create_save_check | yes* / no | bootup_create_save_check (title "save not found") | bootup_create_save | bootup_caution |
| bootup_nocard | yes / no* | bootup_nocard | autosave_disable_check | bootup_load (retry) |
| bootup_save_nocard | yes / no* | bootup_nocard | autosave_disable_check | bootup_create_save |
| bootup_nospace | yes / no* | bootup_nospace | bootup_caution | bootup_load |
| bootup_corrupt_data | yes* / no | bootup_corrupt_data | bootup_create_save (`$mc_overwrite TRUE`) | bootup_caution |
| bootup_different_card | yes* / no | different_card | bootup_create_save | bootup_caution |
| bootup_replace_save_check | load* / replace | bootup_replace_save_check | `$mc_overwrite FALSE` → `$mc_load_screen` (bootup_load) | `$mc_overwrite TRUE` → `$mc_replace_screen` |
| bootup_caution | yes / no* | bootup_caution | autosave_disable_check | bootup_load |
| autosave_disable_check | yes* / no | autosave_disable_check | `$autosave FALSE` → cut_scene | `$autosave TRUE` → cut_scene |
| bootup_format_check | yes / no* | unformatted_card | `$mc_yes_screen` (mc_format) | `$mc_no_screen` |
| bootup_caution_format_cancelled | yes / no* | bootup_caution_format_cancelled | bootup_caution | `$mc_overwrite FALSE` → bootup_create_save |
| retry_cancel | retry / cancel* | `$mc_retry_cancel_text` | `$mc_retry_screen` | `$mc_cancel_screen` |
| retry_continue | continue* / retry | `$mc_retry_continue_text` (e.g. save_error) | `$mc_continue_screen` | `$mc_retry_screen` |
| save_different_card | yes* / no | different_card | autosave | `$mc_save_failed_screen` |
| save_nocard | yes / no* | no_card_error ("continue without saving?") | `$mc_yes_screen` (= failed screen → continue) | `$mc_no_screen` (= autosave, retry) |
| save_nospace | yes* / no | no_space_error | `$mc_yes_screen` (continue) | `$mc_no_screen` (retry) |
| replace_save_check | yes / no* | file_exists_error ("Overwrite previous data?") | `$mc_overwrite TRUE` → autosave | `$mc_overwrite FALSE` → failed screen |
| save_unformatted | yes / no* | unformatted_card | mc_format | mc_no_format_warn |
| mc_no_format_warn | continue / retry* | no_format_warn | `$nfw_continue_screen` | `$nfw_retry_screen` |
| memcard_save_confirm | yes / no* | mc_save_confirm | `$mc_overwrite TRUE` → autosave | `$mc_save_failed_screen` |
| memcard_load_confirm | continue / cancel* | mc_load_confirm ("you will lose your current game progress") | memcard_load | `$mc_load_failed_screen` |
| memcard_load_error | yes / no* | `$mc_load_text` (mc_nocard_error / mc_card_removed_error / mc_corrupt_error / mc_nodata_error) | memcard_load (retry) | `$mc_load_failed_screen` |
| pre_delete_confirm | continue / cancel* | pre_delete_confirm ("progress must be saved first") | success→delprof, fail→data_settings, → autosave | data_settings |
| delete_confirm | yes / no* | delete_confirm | `campaign delete_slot sel`, success and fail → data_settings, → autosave | delprof |
| new_profile_warning | continue / cancel* | mc_new_profile_warning | goto_nameprof_for_new_profile | data_settings |

Result routing in `bootup_load` (after `load_data`):
- `0` → OK.
- `−101` → bootup_nocard.
- `−105` → `$mc_overwrite TRUE` → bootup_corrupt_data.
- Anything else (no save file, unformatted, …) → `$mc_overwrite FALSE` → bootup_create_save_check.

Result routing in `bootup_create_save`:
- `−101` → bootup_save_nocard.
- `−106` → bootup_nospace.
- `−120` → bootup_replace_save_check.
- `−102` → bootup_format_check (with the `$nfw_*`/`$mc_*` vars set).
- `−103` → bootup_different_card.
- Anything else → retry_continue (`save_error`; continue → bootup_caution).

Result routing in `autosave`:
- `−103` → save_different_card.
- `−101` → save_nocard.
- `−106` → save_nospace.
- `−120` → replace_save_check.
- `−102` → save_unformatted.
- Anything else → retry_continue (continue → `$mc_save_failed_screen`).

Result routing in `memcard_load`:
- `−101` → card removed.
- `−105` → corrupt.
- `−103` → silently retries `load_data`.
- Anything else → `mc_nodata_error`.

### 3.5 When the memory-card screens appear in normal play [C]
`autosave_goto S` (`system_script.dta`): `if (campaign needs_save && $autosave && !$disable_save)` → `$mc_save_success_screen = $mc_save_failed_screen = S` → `autosave`. Otherwise it goes straight to S.

needs_save is set by:
- `set_profile_name`
- `set_character_info` (when it changed)
- `finish_song` (first beat or a cash award)
- add_cash, buy_item
- tutorial progress
- `highscores add`
- **Options volume setters** (`FUN_00107618/658/698` set campaign+0x30)

Call sites, i.e. the places the "saving..." sequence shows up:
1. **Boot**: `bootup_load` always runs, so "Checking…" → "Loading…" shows on every boot. On first boot `bootup_create_save` follows if accepted.
2. **New or renamed band name accepted** → autosave → `main`.
3. **Career menu "Let's Rock"** (`cm_letsrock` → `autosave_goto sel_venue`). This saves if you just picked or changed the character/guitar (new career) or anything else is pending.
4. **Leaving the store** (`autosave_goto career`).
5. **Leaving Game Settings** after moving a volume slider (`autosave_goto options`).
6. **After every song** that changed something: every `meta_loading_goto` goes through `meta_loading` (TRANSITION_COMPLETE → `autosave_goto next`). This covers:
   - complete/endgame → selsong/quit
   - lose → selsong/quit
   - pause → quit
   - career campaign win (`goto_endgame_complete` → career)
   - tutorials end → main
   - attract exit → splash
   In practice it saves after a career song that was a first clear or gave cash, and after a quickplay high-score entry.
7. **Manual** in Data Settings:
   - save band: confirm → autosave → **main** on success, data_settings on failure.
   - load band: confirm → memcard_load → loadprof → main.
   - delete band: pre_delete_confirm → autosave → delprof → delete_confirm → autosave → data_settings.

If the player declined the save at boot (`$autosave FALSE`), only the manual paths save.

### 3.6 Caveats [C/I]
- A screen's `delay_task` lives in that screen's TaskMgr (`GHScreen_v3` @0x2085f0 ticks `FUN_00242740(ui frame, this+0x10)`). It only ticks while the screen is current or stacked.
  In `bootup_load`'s no-card branch the 1000 ms task is still queued after the goto. **[I]** For the port, clear a screen's tasks on Exit.
- `bootup_save_nocard` reuses the `bootup_nocard` text. The token `bootup_space_check_failed` and others are unused.

---------------------------------------------------------------------------------------------------
## 4. Help bar (HelpBarPanel, `new_helpbar_panel`) and its transitions

### 4.1 Objects [C]
- `new_helpbar_panel helpbar (max_labels 4)(max_buttons 4)(button_spacing 40)(strumbar_spacing 80)(text_spacing 25)` (`splash.dta`).
  - ctor `FUN_00149360` builds 4 LabelHBElement (from resource `help_bar.txt`), 4 ButtonHBElement (`help_bar.mesh`) and 1 StrumBarHBElement (`help_bar_strum.mesh`). Their vtables are @0x2f8580 / 0x2f8540 / 0x2f8500.
  - The global pointer is `DAT_002edbf8`.
- `ghui/helpbar.gh` contains only Tex/Mat for `hb_fret1/fret2/start/strum` (images `helpbar_*.png/bmp`), two placeholder meshes (`help_bar_starting.mesh`, `help_bar_strumbar.mesh`), `meta.cam`, `helpbar.view` and `ui.env`.
  - **No animation objects** (no TransAnim/MatAnim/EnvAnim). The help bar is never animated or faded.
- Enter (`HelpBarPanel_v3` @0x14a080): UIPanel::Enter, then the two placeholder meshes are **hidden** and their world positions are stored as the start point of the row (+0xc0) and of the strum row (+0xd0).
- `set_display ((elem token) …)` (`HelpBarPanel_v1` → `FUN_00149d88`) rebuilds the element list. elem ∈ `fret1 fret2 strum start`:
  - fret1/fret2/start: a button icon then a label.
  - strum: starts at the strumbar position, draws the strumbar icon and then a label.
  - Each element's x advances by `spacing + element width` (`FUN_00149b98`).
  - `()` gives an empty bar.
  - The text is localized from the token.

### 4.2 What sets the contents (GHScreen) [C]
`FUN_00143a68(screen, key)`:
1. Look up `(helpbar …)` in the screen's typedef, then `(key elems)`, falling back to `(default elems)`. Call `set_display` on the result.
2. If the screen has **no `(helpbar …)` block, the contents are left unchanged**. Dialog screens and nameprof rely on that and set the contents from script.

Called from:
- **Screen Enter** (`GHScreen_v6` @0x143170): key = empty symbol, so `default` is used.
- **TRANSITION_COMPLETE for this screen** (`FUN_001439d0`): key = the name of the focus panel's focused component.
- **FOCUS_MSG from the focus panel** (`FUN_00143940`): key = `$new_focus` (e.g. `ds_autosave.btn` → `fret1 ON/OFF`).
- These handlers return kDataUnhandled, so script handlers run afterwards and can override. nameprof does this on TRANSITION_COMPLETE, SELECT and SCREEN_BACK. `dialog setup` also calls `set_display` directly.

### 4.3 Visibility rule: this is why the legend disappears on select [C]
Drawing (`GHScreen_v4` @0x143588 = screen Draw):
```
helpbar.drawnThisFrame = 0                      ; FUN_00149ff0
UIScreen::Draw(screen)                           ; every panel's Draw (slot 11) in panel-list order;
                                                 ; HelpBarPanel_v11 only sets drawnThisFrame = 1 (draws nothing yet)
if (helpbar.drawnThisFrame)                      ; i.e. 'helpbar' is in this screen's (panels …)
    FUN_00149ff8(helpbar):
        UIPanel::Draw (helpbar.view; placeholders are hidden)
        if (helpbar RndDir showing && UIManager.state == 0)      ; DAT_00369350 = ui+0x80
            for each active element: element.Draw()              ; icons + labels + strumbar
```
1. The legend shows only on screens that list the `helpbar` panel. It is always drawn **on top of** that screen's other panels, whatever its position in the list.
2. **The legend is hidden while the UIManager transition state ≠ 0**:
   - state 1: `goto_screen` called, old screen exiting, new panels loading.
   - state 2: new screen entered, in-anim running.
   - state 3: `pop_screen`.
   It is a hard cut with no fade, from the frame where `goto_screen` / `push_screen` / `pop_screen` is called (usually inside the SELECT_MSG handler) to the frame after `TRANSITION_COMPLETE_MSG` is sent (state → 0 in `FUN_00209ec0`).
3. On reappearance the contents are already the new screen's:
   - `default` is set at Enter, during state 2 while still hidden.
   - It is refreshed for the focused component exactly when TRANSITION_COMPLETE fires.
   So the first visible frame shows the right legend.
4. A SELECT that does not change screen (the autosave toggle, slider select, store toggles) never hides the legend. A ButtonEx flashing its `selected` colour for 10 frames is unrelated.
5. The help bar panel itself is not exited between two screens that both list it (shared panel), so nothing else changes.
   On a screen without `helpbar` (splash, cut_scene, loading, game, meta_loading, error_* popups) there is simply no legend.

### 4.4 Timing of the gap [C]
Duration hidden = `(old.animate_transition ? 500 : 0)` + panel-load wait + `(new.animate_transition ? 500 : 0)`, plus about 1–2 polls of latency.
- In state 1 the switch waits until the new panels are loaded **and** the old screen's out-anim is done (`GHScreen_v7`: busy while `ui frame < exitStart+500`, `FUN_00143290`).
- In state 2 it waits until the new in-anim is done (also 500 ms).
- Typical menu→menu (both `animate_transition TRUE`, the default): **≈1000 ms**, during the 500 ms camera/ambient slide out + 500 ms slide in.
- Memory-card dialogs, chooseprof, guitar_help, splash: `animate_transition FALSE`, so the gap is only the load frames (≈1–3 frames when the panels are resident).
- Input (`button_down/up`) is also dropped while state ≠ 0, so the hidden legend matches the window when no input is accepted.

### 4.5 Port recipe
```
draw(screen):
    draw panels in order;
    if 'helpbar' in screen.panels and helpbar.showing and ui.state == IDLE: draw legend on top
on goto/push/pop: ui.state = BUSY  -> legend vanishes instantly
on Enter(new): legend.set(screen.helpbar.default) (if the block exists)
on TRANSITION_COMPLETE(new): legend.set(screen.helpbar[focused] or default); ui.state = IDLE -> legend appears instantly
on FOCUS change: legend.set(screen.helpbar[new_focus] or default)
```
- No fade or slide is needed. A 500 + 500 ms transition gives the ~1 s "gone while moving, back on the new screen" look.
- Keep a pushed popup's legend behaviour **[I]**: the stacked screen under it still draws its helpbar. The global contents are whatever was last set, e.g. the error popup's `default (fret1 CONTINUE)`.
