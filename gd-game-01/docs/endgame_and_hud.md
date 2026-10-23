# GH1 (PS2, SLUS_212.24): end-of-song screens and the in-game HUD, a reimplementation spec

[C] means read from the decompilation, the ELF, or the data. [I] means inferred. Addresses are EE virtual addresses.
Sources:
- analysis/decomp/*.c, strings.tsv, vtables.tsv;
- extracted_dta/ghui/gen/{endgame,hud_panel,track_panel,game,multiplayer,splash}.dta, arena/gen/arena_game.dta, config/gen/{gh,scoring,campaign}.dta;
- scenes in extracted/ghui/*.gh and extracted/hud/gen/hud.rnd_ps2, plus track/gen/track.rnd_ps2.

Related docs: docs/ui_system.md (screens, panels, routing), docs/end_of_song.md (Player stat messages, game_over / game_outro timing), docs/fonts_and_text.md.

Contents:
- Part A: end screens.
  - A1: script flow (this file).
  - A2–A8: natives and scenes. A2 = §1 Campaign, A3 = §2 HighScoreDB, A4 = §3 StatusPanel, A5 = §4 winner photos / GuitarDisplay / Fade, A6 = §5 scenes, A7 = §6 locale, A8 = §7 consolidated flow.
- Part B: in-game HUD.
  - B1: the HudPanel and its meters.
  - B2: TrackPanel, mtv_overlay and intro timing.

Player stat messages used by the end screens (`player0::player score / percent_hit / longest_streak / num_stars / star_rating / percent_complete`) are specified in docs/end_of_song.md §2:
- stars: 5 if S≥3B, 4 if S≥2B, else 3;
- star_rating: `3_star` / `4_star` / `5_star`, localized to "***__" / "****_" / "*****" for the stars font;
- percent_hit: int(100·hits/gems).

# PART A: End screens

## A1. Script flow of the end screens (ghui/gen/endgame.dta, multiplayer.dta) [C unless noted]

`GAME_PANELS = (game arena track hud mtv_overlay)`. Every end screen keeps these panels up, so the venue, track and HUD panels stay loaded and drawn behind the results.
- In practice: the game panel is paused (`game set_paused TRUE` in game_outro / post_show), `hud set_showing FALSE` (game_outro), and the track is hidden or exploded.
- The venue keeps animating with characters and the camera.
- All end screens have `(allow_back FALSE) (animate_transition FALSE)`, and all except post_show / multi_compete have `(in_game TRUE)`. In-game screens swallow input from player ≠ 0 in single player.

### Entry (arena_game.dta `game_outro`, see docs/end_of_song.md §3)
- Won: `meta set won_campaign TRUE` if `career && campaign final_song`, then `ui goto_screen {if_else $auto_restart loading post_show}`.
- Lost: `ui goto_screen {meta get lose_screen}` (= `lose`; lose.dta is out of scope, see docs/ui_system.md §5).
- `GamePanel_v13` preloads the panel `endgame` (1P) or `multi_compete` (2P) when the game panel loads (end_of_song.md).

### post_show (panels GAME_PANELS fade; no focus panel)
enter:
1. `game set_paused TRUE`; `crowd_audio turn_off`.
2. `win_screen = meta.won_campaign ? (difficulty==easy ? win_easy : win_game) : {meta get win_screen}`.
   - win_screen is `endgame` in quickplay and career, and `multi_compete` in multiplayer (splash.dta setup_*).
3. `{ui screen $win_screen} delay_task 300 {play_sfx postshow_loop}`: the music loop starts 300 ms later, as a task on the *target* screen.
4. `ui goto_screen $win_screen` straight away. So post_show is a one-frame trampoline.

### endgame (panels GAME_PANELS endgame helpbar; focus endgame; helpbar fret1 "CONTINUE")
Panel `endgame` (new_gh_panel, clear_vram) enter:
- `endgame_song_data.lbl` ← `game get_song_text`; `endgame_diff_data.lbl` ← token `{game get_difficulty_sym}`.
- From `player0::player`:
  - `endgame_score_data.lbl` ← `sprint score`
  - `endgame_percent_data.lbl` ← `sprintf eg_percent_format percent_hit`
  - `endgame_streak_data.lbl` ← `sprintf eg_streak_format longest_streak` ("%i Note Streak!")
  - `endgame_review_data.lbl` ← token `star_rating` (3_star/4_star/5_star, localized to the star glyphs).
- `set_headline endgame_headline.lbl`:
  - Venue `basement`: `localize "headline_basement_star<num_stars>"`.
  - Otherwise a random one of `gen_headline_1` / `gen_headline_2`:
    - `idx = num_stars − 3`; `band = headline_format(profile_name(profile_slot))` (Title-cases it).
    - h1 = `"<ADJ> <NOUN> from <band> at <venue>"`; h2 = `"<band> <VERB> <venue> with <ADJ> <NOUN>"`.
    - ADJ ∈ ADJS[idx] (c/b/a = 3/4/5 stars), VERB ∈ VERBS[idx], NOUN ∈ (set show). Every word is localized. `random_elem` is uniform.
- Resets `$new_venue ""`, `$new_status 0`, `$new_cash 0`.
- Career only: `campaign finish_song {player0::player score} $new_venue $new_status`. If `$new_venue != ""`, `meta set continue_screen career`, so the post-game "continue" goes back to the career screen instead of the song list.
- BUTTON_DOWN, when the button is select (`synth play_sequence button_select`) [quirk below]:
  - Career: `$new_status` → `status`; otherwise `campaign has_cash_award` → `cashaward`; otherwise `goto_endgame_complete`.
  - Quickplay: `highscore`.
  - Returns kDataUnhandled.
- **Quirk**: the `if` body has three statements. `{if cond a b c}` runs all of them when cond is true, so only the select button navigates. [C]
- `hide_char_pix` is defined but never called.

### win_easy / win_game (career, final song won; `meta won_campaign`)
- **win_easy** (panel `win_easy`, plain `new_panel`):
  - enter: `continue_frame = ui frame + 6000`; sfx `beat_easy` and `crowd_win`; `we_bandname.lbl` ← profile name.
  - poll: once `ui frame > continue_frame`, the helpbar shows fret1 CONTINUE.
  - Select after 6 s → `endgame`.
- **win_game** (panels GAME_PANELS win_game endgame_guitar_display helpbar fade):
  - enter:
    - `fade start_fade 4000 (color 1 1 1) (fade_synth FALSE) (fade_out FALSE)`, a 4 s fade in from white.
    - `continue_frame = now + 6000`.
    - `endgame_guitar_display hide TRUE`; `win_screen1.view` shown, `win_screen2.view` hidden.
    - `wg_diff.lbl` ← `sprintf win_diff (localize difficulty)`.
    - sfx `beat_other` and `crowd_win`; `battleaxe_display FALSE`.
  - Select after 6 s:
    - On **Expert** without the battleaxe shown yet → `display_battleaxe`:
      1. `campaign unlock_global_item battleaxe`; `continue_frame = now + 6000`.
      2. Fade to white over 1000 ms.
      3. After 1000 ms: swap win_screen1 → win_screen2.view, `endgame_guitar_display hide FALSE`, `show_guitar 0 battleaxe ''`, then a 4000 ms fade in from white.
    - Otherwise `leave_screen`: fade to black 1000 ms, `button_select`, after 1000 ms → `endgame`.

### status (career, `$new_status != 0`)
- Panel `status` = `new_status_panel` with `(rnd_file {sprint "ghui/status_" {campaign status} ".gh"})`. The status value is read when the panel loads, *after* finish_song. `retain_last_focus FALSE`.
- enter: if `won_campaign && status==5 && easy`, hide `status_guitarhero_newvenue.view`. Easy cannot reach the later statuses, so there is no "new venue" teaser.
- The `(text …)` table is filled natively (see A2 StatusPanel).
- Screen enter: `play_sfx beat_venue`.
- Select: `campaign has_cash_award` → `cashaward`; otherwise `goto_endgame_complete`.

### cashaward (`force_exit TRUE`, so it re-enters for every award)
- enter: `ca_bandname.lbl` ← profile name; `show_next_award`; `animate_forever_30fps $this cashaward.view`.
- `show_next_award`:
  1. sfx `cash_award`; `campaign next_cash_award $amount $reason`.
  2. `ca_reason.lbl set_text $reason` (a locale token); `ca_amount.lbl` = `"$%d" amount`.
  3. `gross = int(amount / 0.05)`; `ca_grossnum.lbl` = `"$%d" gross`.
  4. Fees `ca_num1..7.lbl` = `"-$%d" int(pct×gross)` with pct = .26 .21 .09 .12 .15 .04 .08. These sum to 0.95, so the net is the 5 % "amount".
- Select: another award → `show_next_award` again (same screen); otherwise `goto_endgame_complete`.

### highscore (quickplay)
enter:
1. `hs_stats.lbl` ← `sprintf highscore_score_fmt song (localize diff)`.
2. `score = player0 score`; `index = highscores check_highscore score + 1`, i.e. 1-based rank, or 0 when it doesn't qualify.
3. `fill_scores index score`: rows i = 1..5 (`foreach_int $i 1 6`, end-exclusive). Row `index` gets `("" , score)`. The other rows take successive table entries `get_highscore slot` (slot 0..). So the new score is inserted and the old 5th place drops off.
4. `index==0` → `view_scores`: no active text entry, all `hs_name%d.lbl` shown, `hs_start.lbl` hidden.
   Otherwise `enter_score index`: `active_ten = hs_entry<index>.ten`; focus it; `hs_flush.tnm` frame 0; show only that row's text entry and hide its name label.
5. `flushed FALSE`; `update_help_bar`. While entering, the helpbar is `get_text_entry_help_text () $ten FALSE`; after the flush it is empty; otherwise fret1 CONTINUE.
- TEXT_ENTRY_MSG (START accepts): `highscores add (ten get_text) (player0 score)`.
- poll: when the active entry `is_done` and not yet flushed:
  1. Set flushed.
  2. `anim_task hs_flush.tnm 1000 0 17` (frames 0→17 over 1 s, the toilet-flush handle).
  3. sfx `flush`; update the helpbar.
  4. **After 7000 ms, `goto_endgame_complete` automatically.**
- BUTTON_DOWN select: if there is no active entry, or the entry is done → `button_select` and `goto_endgame_complete`. So the player can skip the 7 s wait once the name is in.

### goto_endgame_complete (global func)
- `career && won_campaign` → `stop_sfx postshow_loop`, `meta_loading_goto career`. This goes back to the career screen after the final win, skipping `complete`.
- Otherwise → `complete`.

### complete (panels GAME_PANELS complete helpbar; navigator vertical selsong/restart/quit; helpbar fret1 CONTINUE + strum UP/DOWN)
- enter: focus `comp_selsong.btn`. In career, if the song is not a store song, its text becomes `CONTINUE`.
- SELECT:
  - `comp_restart.btn` → `game restart`, `ui goto_screen game`.
  - `comp_selsong.btn` → `meta_loading_goto {meta get continue_screen}`: qp_selsong, sel_song, multi_selsong, or `career` if finish_song set it.
  - `comp_quit.btn` → `meta_loading_goto {meta get main_screen}`.
  - Always `stop_sfx postshow_loop`. In career, `meta setup_career`, which resets continue_screen to sel_song *after* the goto has read it.

### multi_compete (2P; panels GAME_PANELS multi_compete helpbar; no in_game flag)
- Screen enter: `game set_paused TRUE`.
- Panel enter:
  1. `crowd_audio turn_off`; postshow_loop after 300 ms.
  2. `winner = s0>s1 ? 0 : s0<s1 ? 1 : -1` (scores from `playerN::player score`; P2's is handicap-normalised).
  3. `winner_photo0/1.mesh` showing for the winner.
  4. `me_winner.lbl` = PLAYER_1_WINS / PLAYER_2_WINS / DRAW.
  5. Song, artist and difficulty labels.
  6. Per player: `me_score_dataN` = score, `me_percent_dataN` = `me_percent_format percent_hit`, `me_streak_dataN` = longest_streak (plain number), `me_starsN` = star_rating.
- Select → `complete`. There is no highscore or career step in multiplayer.

### Flow summary
```
game_outro(won) ─► post_show ─► [career final] win_easy|win_game ─► endgame
                              └► endgame | multi_compete
endgame ─select─► career:  status? ─► cashaward* ─► complete
                           (status → cashaward if has_cash_award; cashaward loops per award)
                  quickplay: highscore ─(name+flush, 7 s or select)─► complete
multi_compete ─select─► complete
goto_endgame_complete: career && won_campaign → meta_loading_goto career, else complete
complete: selsong → continue_screen | restart → game | quit → main_screen
```

## A2–A8. End-screen natives and scenes (section numbers below are this part's own)


[C] = read from decomp/ELF/data. [I] = inference. Addresses are EE vaddrs.
Player messages (`score`, `num_stars`, `star_rating`, `percent_hit`, `percent_complete`, `longest_streak`) are specified in
docs/end_of_song.md §2 and are not repeated here. In short: score is the raw score (×handicap for P2); stars are 3/4/5 at S ≥ 0/2B/3B;
`star_rating` gives the symbols `3_star`/`4_star`/`5_star`, whose locale strings are `"***__"`, `"****_"` and `"*****"`, drawn with the `label_stars` style.

---------------------------------------------------------------------------------------------------
## 1. Campaign natives (`campaign` object, handler `Campaign_v1` @0x1395e0)

### 1.1 Object layout
- **Campaign** (global `DAT_002edbe8`):
  - +0x0c: the campaign **config** (built by `FUN_0013a300`). [C]
  - +0x18: the profile array. [C]
  - +0x28: `profile_slot`. [C]
  - +0x30: the `needs_save` flag. [C]
  - +0x34: `all_access` (unlock-all). [C]
  - +0x38: `tutorial_access`. [C]
  - +0x40: the **CashAward list** (an STL list; its sentinel node pointer is at +0x44). [C]
- **Config** (`FUN_0013a300` @0x13a300):
  - +0 is `campaign.dta`.
  - +4 is the `(order …)` array: 6 venues, each `(venue song×5)`.
  - +8 is `store.dta (song …)`, the store/bonus songs. [C]
- **Per-difficulty career state** ("DiffState"), from `FUN_00138740(campaign, diff)`, which is `profile[slot]→FUN_0014f8e8(diff)`.
  `FUN_001387a0(campaign)` returns the state for the **current** difficulty `GameConfig(DAT_002edb88)+0x90`. [C]
  - +0: config. [C]
  - +0x10: **cash**. [C]
  - +0x14: a map symbol→Record (`FUN_00154ea8` find-or-insert, `FUN_00154ed0` find). [C]
  - +0x24: difficulty. [C]
- **Record** (one per song, venue or item symbol): [C]
  - +0x0c: owned/unlocked flag (venue unlocked, item bought).
  - +0x14: beaten flag.
  - +0x18: best score (int).
  - +0x1c: best stars (u8).

### 1.2 Messages (symbol strings 0x307448–0x307738) [C]

| msg | impl | returns |
|---|---|---|
| `all_access` / `set_all_access B` | +0x34 | int |
| `get_random_song` | FUN_00138620 | symbol |
| `num_profiles` | FUN_00137890 | int |
| `max_num_profiles` | constant **8** | int |
| `has_profile_name s editing` | FUN_001378c8 | bool |
| `set_profile_name s` | FUN_001379a8 | – |
| `profile_name i` | FUN_001379e8: the profile's name, or **"Someone"** (0x3073f8) when slot i is empty | string |
| `is_empty_profile i` | FUN_00137a30 | bool |
| `empty_slot` | FUN_00137a48 | int |
| `delete_slot i` | FUN_00137a80 | – |
| `profile_slot` | +0x28 | int |
| `set_profile_slot i` | FUN_00138c98 | – |
| `new_campaign` | FUN_001387a0→FUN_0013d5c8 | |
| `set_character_info` / `get_character_info` | FUN_001393b0 / FUN_00139338 | |
| `is_unlocked sym` | FUN_00137b00 (all_access, or Record+0xc at the current difficulty) | bool |
| `is_store_song sym` | FUN_0013b638(cfg, sym): sym ∈ `store.dta (song …)` list | bool |
| `foreach_venue $v {…}` | FUN_00138cf0 | |
| `finish_song score $new_venue $new_status` | **FUN_00138ec0** (§1.3) | 0 |
| `cheat_beat_song sym b` | FUN_00139278 | |
| `has_cash_award` | `list(+0x40)` non-empty | bool |
| `next_cash_award $amount $reason` | **FUN_00139068** (§1.4) | 1 if one was popped, else 0 |
| `status` | FUN_0013e7e8(curState) (§1.5) | int 0..7 |
| `get_cur_status_sym` | FUN_0013e8f8: symbol `status_%d` (0x307c68) | symbol |
| `get_status_progress diff` | FUN_00139440 (§1.5) | string |
| `mark_needs_save` / `needs_save` / `save_complete` | +0x30 = 1 / read / = 0 | |
| `add_cash n` | FUN_00139598 | |
| `cash` | curState+0x10 | int |
| `career_score` | FUN_0013ec68: Σ Record+0x18 (best score) over career songs of the current difficulty | int |
| `high_score sym` | FUN_0013ed50: Record(sym)+0x18 | int |
| `buy_item sym price` | FUN_00138538 | |
| `unlock_global_item sym` | FUN_001385b8: for diff 0..3, FUN_0013e788(state, sym, 0), which sets Record(sym)+0xc=1 when cash ≥ price, and the price is 0. So the item is unlocked at **all 4 difficulties** for free | 0 |
| `skip_tutorials`, `tutorials_done`, `do_tutorial`, `tutorial_access`, `set_tutorial_access` | +0x3c / FUN_001392b8 / FUN_001392f0 / +0x38 | |
| `update_difficulty` | FUN_00139538 | |
| `last_difficulty` | FUN_00138710()+0x48 | |
| `final_song` | FUN_00139568 → **FUN_0013eb20(curState, GameConfig+0x98 song)** (§1.6) | bool |
| anything else | returns kDataUnhandled (type 6) | |

### 1.3 `finish_song score $new_venue $new_status` (FUN_00138ec0 → FUN_0013dd80) [C]
The current song is `GameConfig+0x98` and the state is the one for the current difficulty. `R = Record(song)`; `oldStatus = status(state)`.
```
result = {first=0, new_venue="", new_status=0, awards=[]}
if !R.beaten:                                   # first time beaten (R+0x14)
    first = 1; R.beaten = 1
    v = FUN_0013e130(state, song)               # venue unlock, see below
    if v != "": new_venue = v; new_status = index of v in (order …)   # FUN_0013b770
best = R.best_score
if best < score:                                # strictly greater; all of the rest is inside this branch
    oldStars = best ? num_stars_of(best) : 0     # FUN_0011bdd0(score, player 0)
    newStars = num_stars_of(score)
    R.best_score = score                         # FUN_00154d08 (max)
    if oldStars < newStars: R.stars = newStars   # FUN_00154d20 (max)
    amount = 0
    if !is_store_song(song) && oldStars < newStars:
        amount = starCash(newStars) - (oldStars ? starCash(oldStars) : 0)
    s = status(state)
    if oldStatus < s:
        new_status = s
        amount += status_awards[diff][s]         # FUN_0013b9e0: elem s of the row (elem 0 is the difficulty key)
    if amount > 0:
        reason = "award_%d_star" % newStars  (+ "_status" if oldStatus < s)
        awards.push({amount, reason})            # one CashAward per finish_song
        state.cash += amount                     # FUN_0013e778
$new_venue = new_venue (symbol), $new_status = new_status (int)
campaign.cashAwards = awards                     # FUN_0029e900: list assign, so it REPLACES the old list
if first || awards non-empty: campaign.needs_save = 1
```
- `starCash(n)` (FUN_0013b920) = Σ of `cash star_awards <diff>` elements 1..n−2. Medium/Hard/Expert: 3★=100, 4★=300, 5★=650. Easy: all 0. [C]
- `status_awards` row, indexed by the new status 1..7:
  - Medium/Hard/Expert: 100, 250, 600, 1200, 2200, 4900, 0.
  - Easy: 0 for all six. [C]
- So on Easy no cash award is ever generated, and the cashaward screen never appears. [C]/[I]
- Venue unlock `FUN_0013e130(state, song)` [C]:
  1. Skipped for store songs.
  2. Take the song's venue `v = FUN_0013b550(cfg, song)`.
  3. If `venueComplete(v)`, let `n = FUN_0013b150(cfg, v, diff)` (the next venue in order; [I] it returns "" past the last venue for this difficulty).
  4. If n is valid and not already unlocked (`FUN_0013d638`), set `Record(n).owned = 1` (FUN_0013e368) and return n.
  5. Otherwise return `""`.
- `venueComplete(v)` (FUN_0013e220) [C]:
  - The test is `#beaten songs of v ≥ required(v)`.
  - required (FUN_0013b6c8) is **5 for the last venue** of the difficulty, otherwise `required_songs`: Easy 4, Medium 4, Hard 5, Expert 5.
- `numVenues(diff)` (FUN_0013a788) is **5 on Easy** (no arena) and 6 otherwise. [C]

### 1.4 CashAward list (FUN_00139068 `next_cash_award`) [C]
- If the list is empty, it sets `$amount = 0` and `$reason = 0` and returns 0.
- Otherwise it pops the front node `{int amount, Symbol reason}` and sets `$amount` (int) and `$reason` (symbol, type 5). It returns 1.
- The reason tokens are `award_3_star`, `award_4_star`, `award_5_star`, `award_3_star_status`, `award_4_star_status` and `award_5_star_status` (locale texts below).
- `award_god` exists in the locale but no code references it.

### 1.5 Status (FUN_0013e7e8) and progress [C]
```
status = #{ venue v among the first numVenues(diff) venues : Record(v).owned && venueComplete(v) }   # FUN_0013ed90
       + (allFiveStars ? 1 : 0)                                                               # FUN_0013e940
allFiveStars = every career song of those venues has Record.stars == 5 (FUN_0013ea80 tests ==5)
               && (diff == Easy || every store song has stars == 5)
```
- Range: 0..7 (Easy 0..6).
- Names: status_0..7 = NOVICE, ROCKER, YOUNG GUN, SHREDDER, PRO, GUITAR HERO, GUITAR LEGEND, GUITAR GOD.
- `get_status_progress diff` (FUN_00139440) [C]:
  - If no song has been beaten, it returns `""`.
  - Otherwise it returns `sprintf(localize status_progress, localize(status_<s>), beatenCount, totalCount)`, with the format "%s STATUS (%i OF %i SONGS)".
  - beatenCount (FUN_0013ee98) is the number of beaten songs and totalCount (FUN_0013f030) the number of songs, both over the first numVenues venues.

### 1.6 `final_song` (FUN_0013eb20) [C]
It is FALSE in any of these cases:
- the song is not in the career+store list (FUN_0013a8d8),
- it is a store song,
- it is already beaten.

Otherwise it is TRUE iff
- the song's venue is the **last** venue of the difficulty, and
- `beatenCount(venue) == required(venue) − 1`.

So it is TRUE when beating this song completes the career. arena_game.dta `game_outro` calls it **before** endgame's `finish_song`, and sets `meta won_campaign TRUE` when the mode is career.

### 1.7 `headline_format $in $out` (MetaPanel func FUN_0014cde0, registered by FUN_0014c270) [C]
- It copies the string (≤63 chars) and title-cases it: the first char is upper-cased.
- Every other char is upper-cased when the previous char is not alphanumeric, and lower-cased otherwise (FUN_0014cd90 is the separator test).
- The result is assigned to `$out`. endgame uses it to make the band name title case.

---------------------------------------------------------------------------------------------------
## 2. HighScoreDB (`highscores`; ctor FUN_001508c8, handler HighScoreDB_v1 @0x151298) [C]

### Storage
- A vector of `SongHighScores` records (0x104 bytes each), one per song symbol passed to the ctor.
- Each record holds `table[8 profiles][4 difficulties]`, and each table is an STL list of `{int score; String name(cap 9)}` (FUN_00150780 = `rec+4+profile*0x20+diff*8`).
- Defaults (FUN_00150548): every table is filled through `add` with localized `highscore_dummy_0..4` = Kendall 50000, Marc 40000, Kris 30000, Sinc 20000, Keith 10000.
- The active table (FUN_00150da0) is `song = GameConfig+0x98`, `profile = campaign+0x28`, `diff = GameConfig+0x90`. It is NULL if the song is unknown.

### Messages
- **`check_highscore score`** (FUN_00150e18 → FUN_001500b8):
  - It runs a binary search (FUN_002a2ad8) for the first entry whose `score < new`. Entries are kept in **descending** order.
  - It returns the **0-based rank** 0..4, or **−1** when every entry is ≥ score (or there is no table).
  - **Ties rank below the existing equal score.**
  - Script: `$index = rank+1`, so 0 means it did not qualify.
- **`get_highscore slot $name $score`** (FUN_00151148): slot 0 = top; it sets `$name` (string) and `$score` (int).
- **`add name score`** (FUN_00150e70 → FUN_00150160):
  - Same search. If the position is end (not a high score) **nothing is inserted**.
  - Otherwise it inserts before that position, and if the size is then > 5 it drops the last entry.
  - Always sets `campaign needs_save = 1`.
- The highscore screen calls `add` only on TEXT_ENTRY_MSG. Until then, `fill_scores` shows the new row merged at `$index` with an empty name.
  `foreach_int $i 1 6` gives 1..5 (exclusive end, [I] from the 5 rows).

---------------------------------------------------------------------------------------------------
## 3. StatusPanel (`new_status_panel`, registered FUN_00152e38; vtable 0x2f8898) [C]
UIPanel base with overrides:
- **Load** (`StatusPanel_v13` @0x152e70):
  - Runs the normal panel load. The rnd_file is the script expression `"ghui/status_" {campaign status} ".gh"`, evaluated at load (after `finish_song` ran in endgame enter) [I: evaluated per load].
  - Starts an async load of **`ghui/image/status_<character>0.png`**, where character = `PlayerConfig(0)+8` (metal, punk, alterna, classic, hiphop, nu_metal, hair_metal, grim). Stored at +0x80.
- **IsLoaded** (`StatusPanel_v15`): base loaded && png loaded.
- **FinishLoad** (`StatusPanel_v16` @0x152f30):
  1. Base finish_load.
  2. Find the Tex `status_char1.tex` in the `ui` namespace and set its bitmap to the loaded png (FUN_001d5fd0), then free the file.
  3. Call FUN_00153018 to fill the texts.
- **Text fill** FUN_00153018 [C]:
  - Looks up the panel typedef `(text (status<N> entry…))` with N = campaign status (`status%i`).
  - For each entry `(band|char (label "x.lbl") (token t)?)`:
    1. `str = band` → `campaign profile_name {profile_slot}`; `char` → `localize(PlayerConfig(0).character)`, e.g. hair_metal → "Izzy Sparks".
    2. If `(token t)` is present, `str = sprintf(localize t, str)`.
    3. Find label `x.lbl` in `ui` and `set_localized_text str` (FUN_0011ef40: RndText SetText + fit + layout).
  - The (text) table in endgame.dta covers status1..7. With no `status0` entry, it is shown only when `$new_status` ≠ 0.
- Script `enter`: if `won_campaign && status==5 && Easy`, hide `status_guitarhero_newvenue.view` (on Easy the career ends at GUITAR HERO, so there is no "NEW VENUE: arena").
- The `status_charN.mesh` pictures in each scene are drawn with `status_char1.tex` [I: materials not decoded].

---------------------------------------------------------------------------------------------------
## 4. GamePanel winner photos, GuitarDisplayPanel, FadePanel

### Winner photos
- GamePanel_v15 (IsLoaded, 0x105270), once the result panel (endgame or multi_compete, preloaded at +0x1a8) is loaded and no tutorial is running, calls FUN_00104fd0. [C]
- FUN_00104fd0 loads `ghui/image/photo_<character_i><r>.bmp` for each player i, with r = random int 0..3 (FUN_0024a1d0(0,4) [I: exclusive]). [C]
- GamePanel_v16 (FinishLoad, 0x105408) puts them into `ui::winner_photo<i>.tex`. [C]
- Used by endgame's `endgame_photo.mesh` (winner_photo0.tex) and multi_compete's `winner_photo0/1.mesh`. [I: mesh→tex binding not decoded]

### GuitarDisplayPanel (win_game)
Already specified in docs/select_panels.md §2:
- `hide B`.
- `show_guitar 0 battleaxe ''`: shows the battleaxe with skin 0, using the axe placer/spin.
- In the endgame config, `guitar_setup_endgame.cam` is swapped in on Enter.

### FadePanel `fade` (FadePanel_v1 @0x112cb8, start_fade FUN_00112dd8/FUN_001129a0) [C]
- Message: `start_fade ms [(color r g b)] [(fade_synth B)] [(fade_out B)]`. Defaults: colour 0,0,0 with a=1, fade_synth TRUE, fade_out TRUE.
- It sets active=1, start=now, dur=ms, and alpha=0.
- **Poll** (v10 @0x112a98):
  - `t = clamp((now−start)/dur, 0, 1)`; `alpha = fade_out ? t : 1−t`.
  - When fade_synth is set, the synth master volume = `1−alpha`.
- **Draw** (v11 @0x112c00): while active, a full-screen rect in `(r,g,b,alpha)`.
  - The fade stays active after it completes: fully opaque for fade_out, transparent for fade-in.
- A Load (v13) resets active=0.
- The `fade` panel's script enter shows `press_any_button.lbl` only in attract mode.

---------------------------------------------------------------------------------------------------
## 5. Scenes (decoded with tools/ui_panels.py + rnd_classes; positions are panel-local XZ)

Styles come from ghui/config.dta, e.g. label_serif_black = serif font, black. Align codes: 0x11 = TopLeft, 0x12 = TopCenter, 0x14 = TopRight, 0x21 = MiddleLeft, 0x22 = MiddleCenter, 0x24 = MiddleRight, 0x41 = BottomLeft, 0x42 = BottomCenter, 0x44 = BottomRight.
Every scene uses `ui.cam` (pos y=−768, near 400, far 1000, fov 45°) and `ui.env` (ambient 1).

### endgame.gh (panel `endgame`)
- Draw order: endgame.view → `ui.cam, ui.env, endgame_wall.mesh, endgame_poster.mesh, endgame_layout.view, endgame_tape.view, endgame_light.mesh`.
- endgame_layout.view (rotated about 4°) draws: `endgame_title.view` (scale 1.5, z −200), `endgame_stats.view`, `endgame_songscore.view` (scale 0.8), then `endgame_win_alterna.mesh`, `endgame_win_classic.mesh` and `endgame_photo.mesh` (winner photo).
  - `endgame_win_punk.mesh` (referenced by `hide_char_pix`) does not exist, and `hide_char_pix` is never called.

| label | style | align | default / script value |
|---|---|---|---|
| endgame_title.lbl | label_impactor_black | 0x22 | "SONG COMPLETE!" (static) |
| endgame_song_data.lbl | serif_black | 0x11, width 500 | `game get_song_text` |
| endgame_diff_data.lbl | serif_black | 0x14 | `set_text {game get_difficulty_sym}`, giving EASY/MEDIUM/HARD/EXPERT |
| endgame_score.lbl / _data | serif_black, caps / fit 150×50 | 0x22 | "Score" / `sprint score` |
| endgame_review.lbl / endgame_review_data.lbl | serif_black / **label_stars** | 0x22 | "Review" / `star_rating`, giving `***__` etc. |
| endgame_percent.lbl / _data | serif, caps / fit 65×40 | 0x21 / 0x11 | "Notes Hit" / `"%d%%"` percent_hit |
| endgame_streak.lbl | serif_black | 0x24 | token `eg_streak` (**not in the locale**, so the raw token would show [I]) |
| endgame_streak_data.lbl | serif_black | 0x12 | `"%i Note Streak!"` longest_streak |
| endgame_headline.lbl | serif_black, fit 570×100, leading 0.76 | 0x22, width 320 | headline (below) |

**Headline** (`set_headline`):
- If the venue is `basement`, use `headline_basement_star<N>`:
  - 3: "Community upset over loud music, parties"
  - 4: "Police break up disturbance at 34 Winship Street"
  - 5: "Full-scale riot breaks out in area neighborhood"
- Otherwise pick at random between two forms, where `i = stars−3`, `$band` is the title-cased profile name and `venue` is the localized venue name:
  - gen_headline_1: `"<ADJ[i]> <Set|Show> from <band> at <venue>"`.
  - gen_headline_2: `"<band> <VERB[i]> <venue> with <ADJ[i]> <Set|Show>"`.

Word tables:
- ADJ c (3★): Solid, Confident, Lively. ADJ b (4★): Impressive, Tight, Exciting. ADJ a (5★): Stellar, Amazing, Brilliant, Outstanding.
- VERB c: Entertains, Stimulates, Stirs. VERB b: Delights, Thrills. VERB a: Lights Up, Ignites, Stuns, Electrifies.

Venue names: basement "34 Winship St", small_club "Freak Pit", big_club "Red Octane", theatre "Republik Theater", fest "Toxic Summer Tour", arena "The Garden".

Note: in quickplay, profile_name of an empty slot is "Someone".

### win_easy.gh (panel `win_easy`, plain new_panel)
- A letter on a wall. `we_bandname.lbl` (label_hand_pencil, jitter) is set to the profile name, over "Dear ________________ ,".
- The other labels are static:
  - "Congratulations. You have beaten the game on the easy setting. Now check out the medium or hard career for:"
  - The list items "More Songs!", "The Ultimate Venue!", "Unlock Shop crammed with extras!", "Secret Characters!", "Loads of Guitars!" and "Bonus Underground Songs!".
  - "Your journey to Rock Stardom has just begun." and "Yours in rock,".
- No anims.

### win_game.gh (panel `win_game`)
- win_game.view draws `ui.cam, win_game.env, wg_wall.mesh, win_screen1.view, win_screen2.view`.
- `win_screen1.view` → win_text.view draws:
  - wg_congrats1 "Congratulations!" (rokk_black)
  - wg_congrats2 "Your legendary show will be talked about for years." (impactor_grey)
  - wg_diff `sprintf(win_diff "You beat Guitar Hero on the %s difficulty", localize diffsym)` (impactor_black, 0x42)
  - wg_congrats3 ("Rock Stardom beckons...") is not in any draw list.
- `win_screen2.view` (initially hidden) draws:
  - wg_screen2_2 "THE BATTLE AXE" (rokk)
  - wg_screen2_3 "NOW AVAILABLE ON THE CHANGE GUITAR SCREEN"
  - wg_screen2_5 "For beating guitar hero on expert The Rock Gods bestow the ultimate accolade:"
  - plus the guitar_display panel's 3D battleaxe.

### highscore.gh (panel `highscore`)
- highscore.view draws `ui.cam, ui.env, hs_bathroom.view (urinal/walls, scale 2.6), hs_zoom.view, hs_flush.tnm, hs_start.lbl`.
- hs_zoom.view is tilted and contains:
  - `hs_title.lbl` "Top Rockers" and `hs_stats.lbl` (= `sprintf("%s / %s", song, localize diff)`).
  - 5 row views `hs_scoreN.view` at z = 80, 30, −25, −75, −125. Each row has `hs_numN.lbl` "N.", `hs_scoreN.lbl` (score), `hs_nameN.lbl` (name) and `hs_entryN.ten` (TextEntry style `high_score`, at the same position as the name label).
- All labels use `label_hand_pen`, align 0x21.
- `hs_start.lbl`: "Press START button to accept name and continue"; it is hidden in view mode.
- **hs_flush.tnm** animates `hs_handle.mesh` rotation keys at frames 0, 1, 5, 7, 9, 11, 13, 17 and 100 (the handle wiggle). The script plays frames 0→17 over 1000 ms on name accept, plus sfx `flush`, then waits 7000 ms and calls `goto_endgame_complete`.

### cashaward.gh (panel `cashaward`; the screen has force_exit, so the panel re-enters for each award)
- cashaward.view draws `ui.cam, ui.env, ca_wall, ca_stickers2, ca_stickers1, ca_layout.view, ca_stamp, ca_light`.
- ca_layout.view (scale 1.1, rot about 2°) draws `ca_paper.view, ca_fees.view, ca_intro.view, ca_highlight.view`.
- ca_intro:
  - `ca_bandname.lbl` (hand_pencil) = profile name.
  - `ca_reason.lbl` (serif, 0x11, width 600) = `set_text reason`, which localizes the award_* text.
  - "Gross profit:" and `ca_grossnum.lbl` = `"$%d" int(amount/0.05)`.
- ca_fees: the fee lines and their percentages:

  | label | text | share |
  |---|---|---|
  | ca_blurb1 | "Manager's Fee" | 26% |
  | ca_blurb2 | "Promotion" | 21% |
  | ca_blurb3 | "Roadies" | 9% |
  | ca_blurb4 | "Security" | 12% |
  | ca_blurb5 | "Lawyers" | 15% |
  | ca_blurb6 | "Bribes" | 4% |
  | ca_blurb7 | "Catering" | 8% |

  - The amounts `ca_num1..7.lbl` = `"-$%d" int(pct*gross)`. These are the 7 fees and sum to 95%.
  - "YOUR CUT:" `ca_amount.lbl` = `"$%d" amount` (fit 75×42).
  - "Spend your hard-earned cash at the Unlock Shop."
- **ca_highlight.mnm**: a MatAnim on `ca_highlight.mat` with a MinMaxLoop filter 0..10 (loop). The stage rotation is 0 for frames 0–5 and π for frames 5–10, a step flip.
  `animate_forever_30fps $this cashaward.view` runs `anim_task cashaward.view 604800000ms 0→18144000`, i.e. 30 frames/s. So the highlight circle flips 180° every 1/6 s.

### complete.gh (panel `complete`)
- complete.view draws a wall, junk, a poster, comp_background.view, and comp_button.view (scale z 0.7, z −71) with three ButtonEx (style `song_complete`, box 120×40):
  - `comp_selsong.btn` "New Song" (z −71). In career, unless it is a store song, its text is set to CONTINUE.
  - `comp_restart.btn` "Replay" (z −106).
  - `comp_quit.btn` "Exit" (z −141).
- Navigator: vertical selsong/restart/quit (wraps).

### multi_compete.gh (panel `multi_compete`)
- Draws `winner_photo0.mesh` / `winner_photo1.mesh`. Only the winner's is shown; neither on a draw.
- `me_winner.lbl` text, chosen by comparing the two scores (equal scores give DRAW):
  - PLAYER_1_WINS "PLAYER 1 WINS!"
  - PLAYER_2_WINS "PLAYER 2 WINS!"
  - DRAW "DRAW!"
- The song, artist and difficulty labels are set from the game.
- Two columns, P1 left (align 0x21/0x11) and P2 right (0x24/0x14), each with:
  - `me_score_dataN`: sprint score.
  - `me_percent_dataN`: "%d%%".
  - `me_streak_dataN`: sprint longest_streak, a plain number.
  - `me_starsN`: star_rating, `***__` etc. Style **serif_black**, not label_stars [C], so it shows literal asterisks/underscores [I].
- Static labels: SCORE, NOTES HIT, RATING, LONGEST NOTE STREAK, PLAYER 1, PLAYER 2.
- Panel enter: `crowd_audio turn_off`, then postshow_loop after 300 ms.
- Select → `complete`.

### lose.gh (panel `lose`, for reference)
- `lose_title.lbl` "SONG FAILED".
- `lose_song_data.lbl` (song), `lose_diff.lbl` "DIFFICULTY:", `lose_diff_data.lbl`.
- `lose_progress_data.lbl` = `"%d%% complete"` percent_complete.
- Buttons: RETRY / NEW SONG / QUIT (impactor_white, 140×40, z −52/−85/−118).

### status_1..7.gh (panel `status`)
- Every scene has:
  - `status_char1.tex` (swapped to the character picture) and a background `status_<x>_bg.png`.
  - A labels/char-mesh view. Labels marked "NEW STATUS" use `label_setlist_1`, plus a localized `status_N` label (rokk).
  - "NEW VENUE" plus a venue-name token label: small_club, big_club, theatre, fest, arena. These hold **symbols localized at load**, e.g. "Freak Pit".
- Script-filled labels, from the `(text …)` table:

| status | file / bg | filled labels |
|---|---|---|
| 1 ROCKER | status_rocker_bg | status_rocker_headline1 = "ROCKERS OF THE WEEK: <band>" |
| 2 YOUNG GUN | status_young_bg | status_young_bandname = "EXCLUSIVE INTERVIEW WITH <band>" |
| 3 SHREDDER | status_shredder_bg | status_shredder_headline2 = "<band> are changing the face of live rock" |
| 4 PRO | status_pro_bg | headline1 "<band> GOES PRO!", headline2 `"<band> is on a path to greatness... and we want a piece of that."` |
| 5 GUITAR HERO | status_guitarhero_bg | venue1 "<band> to crash the Garden", headline1 and headline5 = character name; `status_guitarhero_newvenue.view` (NEW VENUE + arena) |
| 6 GUITAR LEGEND | status_legend_bg + tag_gw | status_guitarlegend_charname = character name; no NEW VENUE |
| 7 GUITAR GOD | status_guitargod_bg | headline3, status2 and headline2 = character name; headline5 "Departs <band> citing musical differences"; headline7 "<band> Drummer"; no NEW VENUE |

- Static flavor text is in the scenes, e.g. "Student Apathy Reaches New Low", "GOES SOLO!", "\"I Wanna Live Forever\"".
- The `st_status.pic` PictureEx (style char_select_hiphop) exists in 2..7 but is not in any draw list [C for draw lists].

---------------------------------------------------------------------------------------------------
## 6. Locale strings used (ghui/eng/gen/locale.dta) [C]
| token | string |
|---|---|
| eg_score / eg_percent / eg_review | Score / Notes Hit / Review |
| eg_percent_format | `%d%%` |
| eg_streak_format | `%i Note Streak!` |
| eg_streak | missing |
| 3_star / 4_star / 5_star | `***__` / `****_` / `*****` |
| from / at / with / set / show | from / at / with / Set / Show |
| highscore_score_fmt | `%s / %s` |
| highscore_dummy_0..4 | Kendall, Marc, Kris, Sinc, Keith |
| HOW_TO_END | Press START button to accept name and continue |
| comp_selsong / comp_restart / comp_quit / CONTINUE | New Song / Replay / Exit / CONTINUE |
| win_diff | You beat Guitar Hero on the %s difficulty |
| win_congrats / win_legendary / win_stardom | Congratulations! / Your legendary show will be talked about for years. / Rock Stardom beckons... |
| easy/medium/hard/expert | EASY/MEDIUM/HARD/EXPERT |
| PLAYER_1_WINS / PLAYER_2_WINS / DRAW | PLAYER 1 WINS! / PLAYER 2 WINS! / DRAW! |
| me_percent_format | `%d%%` |
| award_3_star | Congratulations on the show and 3 star review. Here's your cut. Don't forget our agreement - great reviews earn you more cash. |
| award_4_star | Congratulations on the impressive show and 4 star review! Here's your cut. Pull out a 5 star performance for more dollars. |
| award_5_star | Congratulations on the killer show, amazing write-up, and perfect review score! Here's your cut. |
| award_N_star_status | Congratulations on the N star review, and for improving your rock star credentials! Enclosed is your cut of the new agreement. (5: "on a killer 5 star show") |
| status_rocker_week | ROCKERS OF THE WEEK: %s |
| status_young_gun_interview | EXCLUSIVE INTERVIEW WITH %s |
| status_shredder_changing | %s are changing the face of live rock |
| status_pro_goes / status_pro_piece | %s GOES PRO! / "%s is on a path to greatness... and we want a piece of that." |
| status_guitarhero_crash | %s to crash the Garden |
| status_god_depart / status_god_drummer | Departs %s citing musical differences / %s Drummer |
| status_0..7 | NOVICE, ROCKER, YOUNG GUN, SHREDDER, PRO, GUITAR HERO, GUITAR LEGEND, GUITAR GOD |
| status_progress | %s STATUS (%i OF %i SONGS) |
| characters | metal Axel Steel, nu_metal Pandora, punk Johnny Napalm, classic Clive Winston, hair_metal Izzy Sparks, alterna Judy Nails, hiphop Xavier Stone, grim Grim Ripper |

---------------------------------------------------------------------------------------------------
## 7. Consolidated end flow (script + natives)
1. `game_outro` (won):
   - If career and `campaign final_song` → `meta won_campaign TRUE`.
   - Then go to `post_show`, which pauses the game, turns crowd audio off and picks win_screen = won_campaign ? (Easy ? win_easy : win_game) : `meta win_screen`.
   - win_screen is `endgame` in quickplay/career and `multi_compete` in multiplayer. postshow_loop sfx starts after 300 ms.
2. win_easy / win_game:
   - Input is accepted after 6000 ms (`ui frame` + 6000); then the helpbar shows CONTINUE.
   - win_game on Expert runs `display_battleaxe` first:
     1. Unlock battleaxe (all difficulties).
     2. Fade to white over 1000 ms.
     3. Swap win_screen1 → win_screen2 and show the guitar.
     4. Fade in over 4000 ms, with another 6000 ms input lock.
   - On leaving: fade to black over 1000 ms, then go to endgame.
3. endgame enter:
   - Fills the labels.
   - In career: `finish_song score $new_venue $new_status`; if new_venue ≠ "", `continue_screen = career`.
   - Select (career): `$new_status` → status; else has_cash_award → cashaward; else goto_endgame_complete.
   - Select (quickplay): highscore.
4. status → cashaward (if any) → goto_endgame_complete.
   cashaward pops awards. In practice finish_song creates at most 1 per song, but the loop handles more.
5. highscore:
   - `rank = check_highscore(score)`.
   - If rank ≥ 0: TextEntry on row rank+1. On TEXT_ENTRY_MSG → `add name score`. When is_done: flush anim, then after 7 s goto_endgame_complete.
   - Otherwise select → goto_endgame_complete.
6. goto_endgame_complete:
   - Career && won_campaign → stop postshow_loop, `meta_loading_goto career`.
   - Otherwise → `complete` (New Song/CONTINUE → continue_screen; Replay → `game restart`, game; Exit → main_screen). In career, `meta setup_career` runs after any choice.

# PART B: In-game HUD, track panel, title overlay

## B1. HudPanel (`new_hud_panel hud`), the in-game HUD

### B1.1 Registration, scene, object layout
- Verb `new_hud_panel` (string 0x305eb8) is registered by FUN_00131cf8 → factory FUN_00131ca8 (alloc 0xc0) → ctor FUN_00131d30, which calls the UIPanel ctor FUN_00205870 and stores the singleton in **DAT_00344c70** (the global HudPanel; StarPower code reads it, see B1.6). [C]
- Script (ghui/hud_panel.dta): `(object_name hud) (rnd_file hud/hud.rnd)`. UIPanel::Load remaps `*.rnd` to `hud/gen/hud.rnd_ps2` (FINDINGS "UI panels"). `extracted/hud/gen/hud_multiplayer_hud_fix.rnd_ps2` is a near-copy that nothing references. [C] for the remap, [I] that the fix file is unused (no string or script names it).
- Vtable HudPanel @0x2f7ef0 (vtables.tsv). Overridden slots: [C]

| slot | addr | role |
|---|---|---|
| 1 | 0x132e08 `HudPanel_v1` | Handle (native messages, B1.2) |
| 3 | 0x132650 `HudPanel_v3` | Enter = UIPanel Enter, then **reset** (FUN_00132680) |
| 10 | 0x132858 `HudPanel_v10` | Poll (B1.3) |
| 11 | 0x132c08 | Draw = UIPanel Draw (draws the RndDir) |
| 14 | 0x132510 `HudPanel_v14` | Unload: frees all meter objects |
| 16 | 0x131e70 `HudPanel_v16` | FinishLoad: builds the meter objects (below) |

- Fields: +0x80 player count; +0x84 vector<HudMsg*>; +0x94 vector<StarMeter*>; +0xa4 vector<ScoreMeter*>; +0xb4 CrowdMeter* (rock meter, 1P only); +0xb8 LeadMeter* (2P only); +0xbc `youwin.mesh`. [C]
- FinishLoad (HudPanel_v16 @0x131e70), with the `ui` namespace pushed: [C]
  1. `multi = GameConfig+0x8c` (the multiplayer flag).
  2. `hud_single.view` showing = !multi; `hud_multi.view` showing = multi.
  3. 1P: CrowdMeter (FUN_001370c0) → +0xb4; `youwin.mesh` → +0xbc and **hidden**. 2P: LeadMeter (FUN_00133540) → +0xb8.
  4. `n = FUN_00113590(GameConfig)` players. For i in 0..n-1 with **scene index k = multi ? i+1 : 0**:
     HudMsg(`"message"`, k) (FUN_00133140), ScoreMeter(`"score_meter_"`, k) (FUN_00133fe0), StarMeter(`"star_meter_"`, k) (FUN_00136478).
     So 1P uses `message0 / score_meter_0 / star_meter_0`; 2P uses `…1` for P1 and `…2` for P2.
- HUD clock (FUN_00132bd8): `(*(float*)0x34425c == 0) ? DAT_00369304 : DAT_00344258`. 0x344258 is the game TaskMgr time block written each game poll (00100000.c:3841); 0x369304 is the UI time. All HUD timers below are in **ms** of this clock. [C] for the code, [I] for which clock each global is.
- Config (`config/gh.dta`): `(hud (crowd_meter (speed 0.9)) (star_meter (speed 0.9)) (lead_meter (max_lead 10000)) (message_show_time 3))`. [C]

### B1.2 Native messages (HudPanel_v1 @0x132e08)
Each handler returns 0 (handled). Anything else falls through to the GH/UIPanel base handler (`StatusPanel_v1` is the misnamed base, see docs/select_panels.md §0). [C]

| message | effect |
|---|---|
| `reset` | FUN_00132680: per player HudMsg reset (text hidden, anim stopped), StarMeter reset, ScoreMeter reset; CrowdMeter reset (target=0); LeadMeter reset (leader=-1, `multi_hud_arrow.tnm` frame 50). Then sends the **script** message `on_reset` to the panel. Enter also runs this. |
| `set_showing B` | the panel RndDir (+0x54) showing = B |
| `showing` | returns RndDir+0x34 (the showing flag) |
| `set_win_showing B` | `youwin.mesh` showing = B (1P only; the mesh is "You Rock!", 200×140 quad, youwin.mat) |
| `set_in_game B` | FUN_00132c68: per player `score_meter_k.view` showing=B; `star_meter_k.view` showing=B, and if !B also hide `star_meter_ready_k.view`; `rock_meter.view` showing=B (1P); `multi_hud_group.view` showing=B (2P) |
| `set_score_showing B` | FUN_00132d18: only `score_meter_k.view` showing=B, for each player |
| `show_message P token` | FUN_00132d88 → HudMsg[P].Show(localize token) (B1.4). P is the player index (0/1), not the scene index. |

Base methods that scripts also use: `set_paused` (UIPanel slot 7: a paused panel is drawn but not polled, so the whole HUD freezes; game.dta `on_set_paused` forwards it), `is_up`.

Script handlers (hud_panel.dta), `METER_END_FRAME = 480`: [C]
- `on_reset`: `hud set_showing {! {|| $attract_mode {tutorial is_tutorial_running}}}`, `hud set_in_game TRUE`, `hud set_win_showing FALSE`.
- `intro_start`: `$this reset`, `meters_slide_in.view set_frame 0`, `hud set_score_showing FALSE`.
- `intro_skip`: `$this reset`, `meters_slide_in.view set_frame 480`, `hud set_score_showing TRUE`.
- `slide_meter_in`: `hud set_score_showing TRUE`; `game anim_task meters_slide_in.view 500 0 480` (frames 0→480 over 500 ms); after 250 ms `play_game_sfx meter_slide`. It is sent from track_panel `do_extend_sequence` at +1500 ms.
- `star_power_ready P` (sent natively, B1.6): if `!$arena.game_over`: `hud show_message P star_ready` ("STAR POWER READY!"), `play_game_sfx_player sp_available sp_available_multi P`.

Every script message the game sends to `hud`:
- game.dta `intro_start` / `intro_skip` → `hud intro_start|intro_skip`; `on_set_paused` → `hud set_paused`.
- track_panel `do_extend_sequence` (+1500 ms) → `hud slide_meter_in`.
- arena_game.dta `game_won` → `hud set_in_game FALSE`, `hud set_win_showing TRUE`, +3000 ms (WIN_MESSAGE_MS) `hud set_win_showing FALSE`.
- `game_lost` → `hud show_message 0 song_failed` ("SONG FAILED!"), `hud set_in_game FALSE`, `hud set_score_showing FALSE`.
- `game_outro` → `hud set_showing FALSE` (and `set_win_showing FALSE` when won).
- system_script.dta / tutorials: `hud set_showing B`, `hud set_paused B`; cheats: `hud showing`, `hud is_up`, `hud intro_skip`.
- **There are no streak pop-ups in GH1** ("50 note streak" etc.). No locale token or ELF string exists for them; player.dta's `end_streak` handler is empty. `eg_streak_format "%i Note Streak!"` is only used on the endgame results screen. The only in-game HUD texts are `star_ready` and `song_failed`. [C]

### B1.3 Per-frame update (HudPanel_v10 @0x132858)
Order: [C]
1. The GHPanel/UIPanel poll (tasks, `poll` script, components, RndDir anim poll).
2. `t = HUD clock`. For each player i (Player = FUN_00106148(game,i)):
   - 1P: `CrowdMeter.Update(t, player+0x6c)`, where player+0x6c is the **displayed crowd rating 0..1** (crowd object at player+0x60; rating at +0xc, computed by FUN_0011d268, see FINDINGS rock meter).
   - `StarMeter.Update(t, energy = player+0x98, sm, deployed = player+0xa0)`. Player+0x88 is the StarPower sub-object: +0x10 energy 0..1, +0x14 enabled, +0x18 deployed, +0x50 config (ready_level at +0x10, multiplier at +0x14).
   - If the star meter's **displayed** value crossed ready_level this frame (`old_disp < ready && ready <= new_disp`), it sends the script message `star_power_ready i` to the hud panel.
   - `ScoreMeter.Update(t, {score = Player score msg (FUN_00116c80, handicapped), mult = player+0x20, streak = player+0x48, sp = player+0xa0})`.
     Player+0x20 = streak multiplier × (deployed ? 2 : 1) (FUN_00117160: `FUN_0010b450(streaks, streak) * FUN_0011cfa8(sp)`).
   - `HudMsg[i].Poll(t)`.
3. 2P: `LeadMeter.Update(t, score(P0), score(P1))`.

### B1.4 HudMsg (FUN_00133140; show FUN_001333b8; poll FUN_00133448; reset FUN_00133360)
- Objects: text `message%d.txt` (sprintf 0x306318), optional anim `message%d.tnm` (0x306348), and `show_time = hud.message_show_time × 1000 = 3000 ms`. [C]
- **Show(token)**: text = `localize(token)` (FUN_00238ce0, the locale table); text showing = TRUE. With a tnm (all three exist): start an AnimTask that plays **frames 0→100 at 30 fps** (queue entry DAT_00344c78 = {type 1, from 0, to 100}; AnimateTaskFPS with fps 0.03 frames/ms, so 3333 ms). Without a tnm: hide_time = t + 3000. [C]
- **Poll**: with the anim, advance it, and once the queue is empty hide the text. Without it, hide when `t > hide_time`. [C]
- AnimTask helper (FUN_00136ce8 ctor, FUN_00136da0 start (clears the queue, then pushes an entry), FUN_00136e50 poll, FUN_00137058 done, FUN_00136fb0 stop + set frame 0): entry type 1 = `AnimateTaskFPS(from,to)` with duration `|to-from| / 0.03` ms and frame `from + (to-from)*clamp01((t-t0)/dur)` (AnimateTaskFPS_v0 @0x1ee8d0, FUN_001ee9a0/9f0). Other entry types = a delay. [C]
- message0.txt: rokk.font, size 12, MiddleCenter, white, at (1,-50,50) under hud_single.view. message1/2.txt at x = ∓150. [C]
- message0.tnm has a ScaleOffset filter 0.75, so task frames 0→100 become key frames 0→75. Keys: [C]
  - scale 0 at f0 → 2.0 at f6 → 1.5 at f8 (a pop-in);
  - small jitter of position and rotation from f3 to f39;
  - then a fly-off upward: z 50 → 275 between f39 and f48 (task frames 52→64, i.e. about 1.73–2.13 s after Show). After that it sits off-screen until the task ends at 3.33 s and the text is hidden.
- message1/2.tnm have no filter: scale 0 → 1.2 (f6) → 1.0 (f8), and they fly sideways off-screen to x = ∓500 at f48 (1.6 s). The task still runs to frame 100 (3.33 s). [C]
- Quirk: `rock_needle.view`'s anim list also contains `message0.tnm`, so the rock meter's per-frame `set_frame` drives message0.tnm too. While a message is showing, the HudMsg poll runs later in the frame and wins; when no message shows, the text is hidden anyway. [C] for the data, [I] that this is harmless.

### B1.5 ScoreMeter (ctor FUN_00133fe0, alloc 0xa0; update FUN_00135b10; reset FUN_001359f8)
Scene objects (k = scene index): [C]
- `score_meter_%d.view` (+0x54);
- digit Mats from meshes `score_num_6.mesh … score_num_1.mesh` (vector +0x58 index 0..5, **index 0 = score_num_6 = the ones digit, rightmost at x=+60**; score_num_1 is leftmost at x=-61), then `score_mult_3.mesh` (index 6, left, shows "x") and `score_mult_2.mesh` (index 7, right, shows the number);
- textures `score_0..9.tex` (index 0..9), `score_none.tex` (10, blank), `score_x.tex` (11) (vector +0x68);
- streak bulb meshes `score_streak_1..10.mesh` (+0x78); bulb Mats `score_streak_0..4.mat` (+0x00) and `score_streak_glow_0..4.mat` (+0x10; loaded but not used by the 1P update path);
- `score_num_frame.mesh` mat (+0x88), `score_mult_frame.mesh` mat (+0x8c), `score_mult_glow.mesh` (+0x90);
- `score_colors.mnm` (+0x98), used as a **colour palette**: its key frames are read directly;
- 1P only: `score_shake.tnm` AnimTask (+0x9c).

Each digit, mult and bulb mesh gets its own clone of its material (FUN_001351e8 → FUN_001361d8), because they share score_digit.mat in the file. [I] that this is a clone, but per-mesh state requires it.

Update(t, {score, mult, streak, sp}): [C]
1. **Digits** (FUN_00135b90), only when the score changed:
   - `v = score % 1000000`. Set digit i to `v%10` for i = 0.. while v > 0. Set the remaining digits up to 5 to blank (`score_none`).
   - Score 0 shows a single "0" in the ones place, with the rest blank. There are no leading zeros.
   - Setting a digit sets stage-0 texture of the mesh's mat (mat+0x10 stages, stage+0x54 tex; FUN_00135a70).
2. **Streak bulbs** (FUN_00135c98(streak, mult, sp)):
   ```
   full = streak>0 && (streak>=30 || streak%10==0)
   m    = sp ? mult/2 : mult                 // undo the Star Power doubling
   lit  = streak<=0 ? -1 : full ? 9 : streak%10 - 1      // bulbs 0..lit are lit
   ci   = m - full;  if (ci==4) ci = sp ? 4 : 3         // FUN_00135c78
   bulb j (score_streak_{j+1}) material = j<=lit ? score_streak_{ci}.mat : score_streak_0.mat
   score_num_frame mat colour = score_colors.mnm colour at frame (sp ? 4 : 0)
   ```
   Effect: streak 1–9 lights 1–9 bulbs in colour 1. At 10 all ten are lit in colour 1 (the multiplier is now 2). 11–19 relight in colour 2, 21–29 in colour 3, and at 30+ all ten stay lit in colour 3.
   Bulb textures: streak_0 grey (off), 1 orange-yellow, 2 green, 3 purple, 4 cyan. `ci==4` cannot happen with the shipped `streaks (10 2)(20 3)(30 4)`. [I]
   Change detection: the update is skipped if streak, mult and sp are all unchanged. A streak above 30 is treated like 30 when the previous value was exactly 30.
3. **Multiplier** (FUN_00135df8(mult, sp)), only when mult or sp changed:
   - If mult increased and the shake task exists (1P), start `score_shake.tnm` (queue DAT_00344c88 = {1, 0→100000}). In practice this plays the 8-frame wobble at 30 fps and then holds the last key.
   - mult==1: both mult digits blank. Otherwise mult_3 = `score_x`, mult_2 = digit `mult`. The displayed value includes the SP ×2, so it shows 2,3,4,6,8.
   - `c = sp ? 4 : mult-1`. `score_mult_frame` mat colour = palette[c]. `score_mult_glow.mesh` is hidden when c==0. Otherwise it is shown and its mat colour is palette[c].
   - Palette (score_colors.mnm): f0 (1,1,1); f1 (1,.86,0) yellow; f2 (.56,1,0) green; f3 (.72,.5,1) purple; f4 (0,.75,1) Star Power blue.
4. Stores sp, then advances the shake task.
- Reset: Update with {0, mult 1, 0, 0}.
- `set_in_game` / `set_score_showing` toggle `score_meter_k.view`.
- 2P: score_meter_1/2.view only contain shells. FUN_00135678 (k==2) and FUN_00135290 (k==1) put copies of score_meter_0's digit and bulb meshes under them. [I] for the exact mechanism.

### B1.6 StarMeter (ctor FUN_00136478, alloc 0x2c; update FUN_00136928; reset FUN_00136800; set_in_game FUN_00136c18)
Fields: [0] target energy, [1] **displayed** energy, [2] deployed, [3] last t, [4] `star_meter_%d.view`, [5] `star_meter_fill_%d.view`, [6] `star_meter_ready_%d.view` (hidden at ctor), [7] optional AnimTask on `star_meter_shake_%d.view` (exists only for k=0), [8] hud.star_meter cfg, [9] speed = 0.9/1000 per ms, [10] `scoring.star_power.ready_level` (0.5). [C]

Update(t, energy, deployed). This runs only while `star_meter_k.view` is showing: [C]
```
dt = t - last; last = t; target = energy; dep = deployed
moving = false
if (disp != target) {                                    // FUN_00136b70
    step = 0.0009*|dt|; disp = disp<target ? min(disp+step,target) : max(disp-step,target)
    star_meter_k.view.SetFrame(disp*100)
    moving = !deployed
}
fill view: moving ? (show, SetFrame(t*0.03)) : (hide, SetFrame(0))
ready = disp >= ready_level || deployed                   // FUN_00136a40
if ready view hidden and ready: show it; if !deployed and shake task exists: start shake (0→100000 frames)
if ready view shown and !ready: hide it; stop shake (frame 0)
if ready view shown: SetFrame(t*0.03); advance shake task
```
- **Gameplay coupling**: Star Power can only be deployed when the HUD's displayed value has reached ready_level. FUN_0011c9a0 (can-deploy) = `sp.enabled && sp.energy >= ready_level && FUN_00132dd0(DAT_00344c70, player)`, and FUN_00132dd0 tests `StarMeter[p].disp >= ready_level`. The needle-style lag (0.9/s) therefore delays deploy and the "STAR POWER READY!" message. [C]
- Scene (1P): star_meter_0.view (amp tube) is inside the rock meter (same world matrix as rock_meter.view). Its frame 0..100 drives:
  - `amp_inside_star.mnm` (UV scroll of the star path),
  - `amp_tube_glow.msnm` (vertex morph of the fill tube),
  - `amp_tube_glow_hide.mnm` (tube alpha 0 until frame 14, 1 from frame 15),
  - `amp_inside_bar_path.tnm` (particle emitter x −80 → 85).
- star_meter_fill_0.view (frame = t×0.03, looping): particles `amp_inside_bar_path_0.part`/`.panm`, `amp_inside_bar_glow.mnm` (pulse looped 0–7.5), and `lightning.view` (8 lightning meshes whose tex keys cycle over 18 frames).
- star_meter_ready_0.view: `amp_tube_glow.mnm` (alpha 1→0.5, loop 15 frames).
- star_meter_shake_0.view: `meter_shake.tnm`, which wobbles **rock_meter.view** for 25 frames.
- 2P: star_meter_group_1/2.view hold star_meter_1/2 + ready_1/2.
- Reset: shows the meter and fill views, sets target=disp=0, sets frames 0, hides ready and fill, stops the shake, and restores the meter view's previous showing flag.

### B1.7 CrowdMeter = rock meter (ctor FUN_001370c0, alloc 0x2c; update FUN_00137338; set_in_game FUN_001373d8)
Fields: [0] target, [1] displayed, [2] last t, [3] `rock_meter.view`, [4] hud.crowd_meter cfg, [5] `rock_meter_warning.mnm`, [6] `rock_meter_normal.mnm`, [7] current mnm, [9] speed 0.0009/ms, [10] `warning_level` of the current difficulty from scoring.dta crowd (0.15 for all; FUN_00137088 → FUN_0010b4f8). [C]

Update(t, rating):
```
target = rating; dt = t - last; last = t
if (disp != target) { disp moves toward target by 0.0009*|dt|; rock_meter.view.SetFrame(disp*100) }
mnm = (target >= warning_level) ? normal : warning
mnm.SetFrame(t*0.03)
```
- Note that the warning test uses the target, not the displayed value. [C]
- rock_meter.view (anims driven by frame 0..100): [C]
  - `rock_needle.view` → `rock_needle.tnm`: needle rotation keys at 0/33/66/100 (quaternion y −0.325 → 0.309, about −38° to +36°).
  - `rock_light_{red,yellow,green}{,_front}.manim` and `rock_light(_front).manim` (the ROCK sign, rock_sign_2d.mat). Zones: red lit for frames 0–33, yellow 33–66, green 66–100. For example, rock_light_green goes from 0.65 to 0.9 at frame 66, and the sign colour is reddish, then yellow, then green.
- `rock_meter_warning.mnm` flashes rock_face_2d.mat from (0.8,0.2,0.2,α0) to white, looping 15 frames (0.5 s). `rock_meter_normal.mnm` = constant white. `rock_meter_starpower.mnm` (blue flash) exists but no code references it. [C]/[I]
- There is no separate reset besides target=0 (FUN_00137330). The displayed value keeps its old value until the next update. The player's crowd rating starts at 0.5 (FUN_0011d3f8(0.5)).

### B1.8 LeadMeter (2P, ctor FUN_00133540; update FUN_00133de0; reset FUN_00133d88)
- Objects: `multi_hud_group.view`, `multi_hud_arrow.tnm` / `_left.tnm` / `_right.tnm`, `multi_hud_logo.mnm`, `multi_hud_middle_glow.mnm`, `multi_hud_glow_left/right.mesh`, `multi_hud_lens.mesh`, Mats `multi_hud_lens{,_left,_right}.mat`, and `hud.lead_meter.max_lead` = 10000. [C]
- Update(t, s0, s1): [C]
  - `leader = s0==s1 ? 0 : s0>s1 ? 1 : 2`. On change: logo and middle_glow mnm frame = leader; glow_left shown iff leader==1; glow_right shown iff leader==2; lens mesh mat = lens/lens_left/lens_right[leader].
  - `f = 50 ± 50*min(1, |s1-s0|/10000)`: minus when s1−s0 < 1 (P1 leads), plus otherwise.
  - If f is 0 or 100, play `multi_hud_arrow_left` or `_right.tnm` at t×0.03 (a 10-frame wobble loop). Otherwise `multi_hud_arrow.tnm` frame = f.
- Reset: leader = −1, arrow frame 50.

### B1.9 Scene layout (hud/gen/hud.rnd_ps2) [C]
- `hud.view` draws `hud.env` (ambient black, no lights), `ui.cam`, `hud_multi.view` and `hud_single.view`.
  - ui.cam: perspective, fov 0.785 rad (45°), near 400, far 1000, z_range 0–0.02 (drawn in front of the world). Its world matrix is translation (0,−768,0), so it looks down +Y onto the XZ plane like all GH UI scenes.
- hud_single.view children:
  - `rock_meter.view` at (277.7, −4.4, −132.3), lower right, rotated about −40°. It holds the frame, needle, lights, ROCK sign, warning face and the star (amp) tube.
  - `score_meter_0.view` at (−276.4, −4.4, −157.7), lower left, rotated: the shell, 6 digits, the multiplier, 10 streak bulbs and the frame.
  - `message0.txt`, `youwin.mesh` at (0,0,−60) with scale (2,1.1), and `star_meter_shake_0.view`.
- hud_multi.view: score_meter_1/2.view at x = ∓362/349, z = 145; star_meter_group_1/2.view at x = −365/345, z = 170; `multi_hud_group.view` at (−3, 0, 221) (centre lead meter); message1/2.txt.
- **Slide-in** `meters_slide_in.view` (no geometry). Its frame 0..480 drives these TransAnims (frame 480 = resting pose):
  - `meter_slide_in.tnm` (rock meter x 360 → 232 (f220) → 250 (f260) → 235 (f480), with a rotation overshoot: a swing-in from off-screen right);
  - `score_slide_in.tnm` (score meter x −340 → −241.8 (f220) → −260 → −236.8, mirrored rotation);
  - 2P: `score_slide_in1/2`, `star_slide_in1/2` and `multi_hud_slide_in.tnm` (P1 x −394 → −225 → −255 → −225 → −235 from frame 100 to 480, P2 mirrored; the lead meter drops from z 300 to −105).
  - All use Hermite position splines. The script plays 0→480 in 500 ms.

### B1.10 Summary: HUD element ← Player state
| element | source | mapping |
|---|---|---|
| score digits | `player score` (handicapped) | 6 digits, ones at score_num_6, blank leading digits, mod 1e6 |
| multiplier "xN" + frame colour + glow | player+0x20 (includes SP ×2), sp flag | N hidden at 1; colour index sp?4:N−1 |
| 10 streak bulbs | player+0x48 current streak | see B1.5 |
| score_num_frame colour | sp flag | white, or blue (0,.75,1) in SP |
| rock needle and lights | crowd rating (player+0x6c), smoothed 0.9/s | frame = disp×100 |
| rock face red flash | rating < warning_level (0.15) | looping 0.5 s |
| SP tube fill | SP energy (player+0x98), smoothed 0.9/s | frame = disp×100 |
| SP fill sparks and lightning | disp ≠ energy and not deployed | looping |
| SP ready glow + meter shake | disp ≥ 0.5 or deployed | glow loop; shake once on becoming ready (not deployed) |
| "STAR POWER READY!" | disp crosses 0.5 | `star_power_ready` script → show_message |
| "SONG FAILED!" | arena game_lost | show_message 0 song_failed |
| "You Rock!" | arena game_won | set_win_showing TRUE for 3000 ms |
| 2P lead arrow | score difference | ±10000 maps to full deflection |

### B1.11 What our engine draws now
`engine/src/render/highway.cpp` `Highway::drawHud` draws a placeholder 2D HUD: text score, "xN", streak count, a bar rock meter, an SP bar and a "STAR POWER READY" text. None of hud.rnd is used. Replacing it means loading `hud/gen/hud.rnd_ps2` as a panel scene with `ui.cam` and implementing B1.3–B1.8, with the slide-in and messages driven by the script handlers above. The engine's Player must also expose the *displayed* SP value to gate deploy, as the original does (B1.6).

## B2. TrackPanel, per-player track, mtv_overlay, game-panel intro timing


[C] means read from the decompile, the ELF, or the data. [I] means inferred.
Sources: analysis/decomp, strings.tsv, vtables.tsv, extracted/track/gen/track.rnd_ps2, extracted/ghui/mtv_overlay.gh,
config/gen/track_graphics.dta, ghui/gen/{game,track_panel,hud_panel}.dta, arena/gen/arena_game.dta, config/gen/player.dta.
The helper `scratchpad/dumpobj.py <file> [names|classes…]` decodes View, TransAnim, MatAnim, ParticleSysAnim, Text and Cam records with tools/rnd_classes.

---------------------------------------------------------------------------------------------------
## 1. TrackPanel (`new_track_panel`, script name `track`, object_name `track_panel`)

### 1.1 Class and layout
- Registration is FUN_0015ae60, which registers the verb `new_track_panel` (0x30d570) with factory FUN_0015ae10 (size 0xbc). The ctor is FUN_0015ae98 (UIPanel ctor FUN_00205870). [C]
- Ctor fields [C]:
  - +0x80 = config `track_graphics`.
  - +0x88 = `track_graphics.multiplayer.use_dual_perspective` (TRUE in data).
  - +0x84 = 0.
  - +0x8c..0x98 = vector<Track*>, one per player.
  - +0x9c..0xa8 = vector of per-player surface textures.
  - +0xac..0xb8 = vector of surface FilePath loaders.
- Vtable at 0x2f8b70 [C]:

  | slot | function | meaning |
  |---|---|---|
  | 1 | TrackPanel_v1 0x15c0e0 | Handle |
  | 3 | ArenaPanel_v3 (via 0x15b2d0) | Enter (UIPanel base) |
  | 10 | TrackPanel_v10 0x15bae0 | Poll |
  | 11 | TrackPanel_v11 0x15bba8 | Draw |
  | 14 | TrackPanel_v14 0x15ba08 | Unload |
  | 15 | TrackPanel_v15 0x15b000 | IsLoaded |
  | 16 | TrackPanel_v16 0x15b2f0 | FinishLoad |

- **Load / IsLoaded** (v15 → FUN_0015b0b8) [C]:
  - For each player i, it reads `track_surface` from that player's character config through FUN_0010a710 and starts an async load of that bitmap (FUN_0011d620).
  - The paths are in characters.dta: `track/surfaces/<metal|nu_metal|punk|classic|alterna|hair|hiphop|grim>.bmp`.
  - **So the highway surface texture depends on the character.**
- **FinishLoad** (v16) [C]:
  - Turns each loaded bitmap into a Tex (FUN_0011d750), stored at +0x9c[i].
  - Looks up `ui::track_anims.view` (0x30d5a8), stored at +0x84.
  - Then runs the UIPanel base finish_load, which sends the script `finish_load` (→ `set_showing FALSE`, `set_track_out`). No tracks exist yet, so both are no-ops.
- **Poll** (v10) [C]:
  - If +0x84 is set: `track_anims.view.SetFrame(now*0.03)`.
    - That view only animates `gem_bonus.mnm` and `gem_bonus2.mnm`, the star-power "bonus gem" spin.
    - Each has a ScaleOffset ×1.5 filter and a MinMaxLoop 0..200.
    - The shared bonus-gem material therefore spins continuously at 45 fps-equivalent.
  - Then `FUN_00159708(now, track)` for every Track, then the UIPanel poll.
  - `now` = DAT_00344258 = **song time in ms** (the game clock struct written by the GamePanel, §3). [C]
- **Draw** (v11): `FUN_00159590(track)` for each track. [C]
- `set_paused` (UIPanel slot 7) stops Poll, so a paused game freezes every track anim.

### 1.2 Messages handled natively (TrackPanel_v1 @0x15c0e0) [C]

| message (string addr) | impl | semantics |
|---|---|---|
| `set_showing B` (0x30d5d0) | FUN_0015c048 → FUN_00159888 | Per track: root view +0x28 `set_showing B` (vt+0x24). |
| `showing` (0x30d5e0) | FUN_001598b8 | Returns track[0].root(+0x28).showing (+0x34). |
| `toggle_nowbar` (0x30d5e8) | FUN_0015bc30 | Global DAT_002edc58 ^= 1. Per track, gems(+0x18) → smasher set (+0x180) FUN_00164c78: show/hide the 5 smasher views and the 3 particle view sets. Cheat only. |
| `toggle_surface` (0x30d5f8) | FUN_0015bcd8 | DAT_002edc5c ^= 1. Shows/hides the surface object (+0x20), beat lines (+0x30), lightning (+0x44) and rails mesh (+0x24). Cheat only. |
| `toggle_gems` (0x30d608) | FUN_0015bd80 | DAT_002edc60 ^= 1. Shows/hides the gem view (+0x34). Cheat only. |
| `set_smasher_glowing i B` (0x30d618) | FUN_0015be28 → FUN_00159ca8 → FUN_0015e450 → FUN_00164bc0 | Fret i, or all frets when i=-1: smasher mesh material = B ? `pressed` mat `gem_smasher_<color>.mat` : `normal` mat `gem_smasher.mat` (FUN_00167500). |
| `pop_smasher i` (0x30d630) | FUN_0015bed0 → FUN_0015e470 → FUN_00164b30 | Fret i, or all when -1: start `gem_smasher.tnm` on that smasher at the current song time (FUN_001675f8). |
| `dual_perspective` (0x30d640) | – | Returns +0x88. |
| `set_dual_perspective B` (0x30d658) | FUN_0015c000 | +0x88 = B. If tracks exist and GameConfig.multiplayer, it calls reset (rebuilds the tracks). |
| `set_frame f` (0x30d670) | FUN_0015bf68 → FUN_00159860 | Per track: `extend_track.tnm.SetFrame(f)` (track+0x3c). See §1.5. |
| `reset` (0x30d680) | FUN_0015b7d0 | Destroys all Track objects and builds new ones (FUN_0015b5d0: `new Track(i, surfaceTex[i], dual)`, FUN_00157b50, size 0xc0). Then sends `on_reset` (0x30d5c0) to itself; the script runs `player_mgr foreach_player_matcher $m {$m on_new_track}`. |
| `explode` (0x30d688) | FUN_0015b980 → FUN_001598c8 | Per track: win explosion, §1.6. |
| anything else | – | Falls through to the UIPanel/GHPanel handler and the script. |

`set_smasher_glowing` has native callers too [C]:
- PlayerMatcher_v11 @0x109608: fret down → glowing TRUE.
- PlayerMatcher_v12 @0x109670: fret up → FALSE.
- FUN_001084f0, the script `refresh_track_buttons`, sets every fret to `held_mask & (1<<i)`.

So "glowing" is the **held-fret colour**. The smasher is grey (`gem_smasher.mat`) when not held and coloured (`gem_smasher_<slot>.mat`) while held. [C]
- The rim always gets `now_ring_<slot>.mat`, assigned once in the GemSmasher ctor FUN_001669d0 (mats from `track_graphics.smasher.mats`: normal gem_smasher, pressed gem_smasher_%s, ring now_ring_%s; slot names from `slot_colors green red yellow blue orange`). [C]
- Engine note: track_renderer.cpp uses `now_ring.mat` when not held and `now_ring_<c>.mat` when held. The original always uses the coloured ring. [C]

### 1.3 The Track object (per player; ctor FUN_00157b50; vtable 0x2f8b30; handler Track_v1 @0x159d70; script name `track`)

Fields [C]:

| offset | contents |
|---|---|
| +0x10 | config track_graphics |
| +0x14 | player index |
| +0x18 | gem manager (FUN_0015d208, 0x1bc). Its +0x180 is the smasher set (FUN_001643b0 → five GemSmasher FUN_001669d0, plus particle sets) and +0x188 is the bonus flag. |
| +0x1c | beat-line manager (FUN_0015a4b8) |
| +0x20 | surface object (FUN_0015c540: copies of `track_surface.mesh`, `track_lane_lines.mesh`, `track_surface.tex/.mat/.mnm`, using the per-character texture) |
| +0x24 | `ui::track_side_rails.mesh` |
| +0x28 | root View (new) |
| +0x2c | track-graphics View, added to the root's draw list |
| +0x30 | beat-line View |
| +0x34 | gem View |
| +0x38 | overlay View |
| +0x3c | `extend_track.tnm` (FUN_0015a2c8, retargeted with FUN_001dd008 to +0x54's transform) |
| +0x40 | side-rail state anim (FUN_001654b0) |
| +0x44 | copy of `lightning_track.view` (FUN_00158b90), parented under +0x2c, hidden at start |
| +0x50 | lightning start time |
| +0x54 | per-track camera (FUN_00158fd0: copy of `track.cam`; `cam (near_plane 50)(far_plane 200)(z_start 0)(z_end 0.1)`) |
| +0x74 / +0x78 | exploding flag / explode start time |
| +0x7c | `track_explode.view` |
| +0x80 | `track_surface.mat` |
| +0x84..0x94 | per-frame player state struct, §1.4 |
| +0x98 | `track_speed[difficulty]` (FUN_00163780: Easy/Medium 1.0, Hard/Expert 1.4) |
| +0x9c | `alpha_dist` 40 |
| +0xa0 | `horizon_y` 110 |
| +0xa4 | `track_fade.tex` |
| +0xa8 | `track_fade.mat` |
| +0xac | `track_add.mat` |
| +0xb0 / +0xb4 | fade x/y |

Track_v1 messages [C]:
- `free_camera B [flag]` and `free_camera_active` (0x30d038/0x30d048): debug camera.
- `on_miss_phrase gem_id` (0x30d060), sent by the player.dta `phrase_miss` handler as `{track on_miss_phrase $gem_id}`. It goes to FUN_0015e778:
  - From that gem to the end of its star phrase, every gem not yet hit or missed gets state 0 (normal look) unless bonus mode is on.
  - +0x18c = "phrase fully reverted" flag.
- `set_bonus_gems B` (0x30d070), sent by player.dta star_power `start_using` (TRUE) and `stop_using` (FALSE). It runs FUN_00159b88:
  - If B, set +0x50 = now.
  - `lightning(+0x44).set_showing B`.
  - FUN_0015e610: every gem in the visible window [+0x24,+0x28) that is not hit/missed gets state 2 (**bonus gem look**) when B. When !B it reverts to state 1 (star) if it still belongs to an intact phrase, or 0 otherwise.
  - Smasher-set burn/particle mode follows the flag (FUN_00164d00). +0x188 = B.
  - Also FUN_0015ec28 when B [I: re-skins tails].
- `set_frame f` (0x30d080): `extend_track.tnm.SetFrame(f)`. Scripts call this with `game anim_task track 800 2000 2025` in player.dta `start_using`, i.e. **the per-track camera shake when Star Power is deployed** (§1.5).

### 1.4 Per-frame track update (FUN_00159708, called from the TrackPanel Poll with now = song ms) [C]
1. If exploding: `track_explode.view.SetFrame((now-t_explode)*0.03)`.
2. Gem manager update FUN_0015dbc8(now, gems, &state). Beat lines FUN_0015aa40(now).
3. Scroll, FUN_001597f8: `d = now*track_speed`.
   - `surface.mnm.SetFrame(d)`. track_surface.mnm moves the UV by −1 V per 100 frames, looped 0..100.
   - The beat-line view and the gem view are translated by (0, −d, 0).
   - So **world units = ms × track_speed** along +Y. The engine already does this.
4. If the lightning is showing: `lightning.SetFrame((now - t_bonus_start)*0.03)`.
   - lightning_1.mnm is an 8-texture flipbook (`lightningB_01..08.tex`), each texture held 3 frames, looping over 24 frames (= 0.8 s).
   - lightning_2.mnm is the same with a +12-frame offset.
5. Side rails FUN_001657d8: `state = (sp_ready?2:0) + (warning?1:0)`. It selects `side_rails_{none,warning,star,warning_star}.mnm` (table @0x30f6b0) and plays it at `now*0.03`. All four target `track_side_rails.mat` and loop 0..15 frames (0.5 s):

   | anim | colour |
   |---|---|
   | none | white, alpha 0 (rails glow invisible) |
   | warning | pulses (1,1,1,0) → (1,.2,.2,1) (red) |
   | star | (.3,.8,1,1) → (1,1,1,1) (blue/white) |
   | warning_star | red ↔ blue |

   The state comes from Player poll FUN_00116498 → PlayerMatcher FUN_001085d8 → FUN_00159560 (track+0x84..0x94) [C]:
   - [0] `warning` = FUN_0011d378 = `!multiplayer && crowd.rating(+0xc) < crowd.warning_level(+0x1c)`. The field names are [I]; the comparison is [C].
   - [1] `sp_ready` = FUN_0011c9a0 = SP enabled and `meter >= ready_level` and FUN_00132dd0 (deploy allowed).
   - [2] = player+0xec, [3] = player+0xc0, [4] = player+0xcc (used by FUN_0015e610: when [4]==1, star gems are not re-starred) [I].
   - **So the rails glow red when the rock meter is in the warning zone, and blue when Star Power can be deployed. This is track-side, not HUD.**
6. Root view `+0x28` anim update (vt+0x14 of its anim interface, frame 0).

### 1.5 The extend (roll-out) and shake animation `extend_track.tnm` [C data]
- Target: the per-track camera +0x54, a copy of track.cam with fov 0.541, near 50, far 200.
- trans keys: y = **+64.0 at frame 0 → −63.218 at frame 1440**. x = 0.197 and z = 17.942 are constant. Spline over 2 keys; linear is a fine approximation [I].
- rot keys: constant quaternion (−0.052,0,0,0.999), about 6° pitch, from 0 to 2000. Frames 2000..2025 hold the **shake**:

  | frame | quaternion |
  |---|---|
  | 2002 | (−.044,.004,.004,.998) |
  | 2004 | (−.048,−.003,0,.999) |
  | 2007 | (−.052,−.001,−.002,.998) |
  | 2010 | (−.050,.002,−.004,.998) |
  | 2014 | (−.053,−.001,−.002,.998) |
  | 2019 | (−.052,0,0,.999) |
  | 2025 | (−.052,0,0,.999) |

- Filter: ScaleOffset 1/0 (identity).
- The saved track.cam position is y = −63.155, i.e. the "out" state.
- Script use:
  - `set_track_in` → frame 0. The camera is 127 units further down the track, so the highway is off-screen or empty.
  - `set_track_out` → frame **1920** (TRACK_END_FRAME), the final camera (frames > 1440 clamp to the last key).
  - `animate_track_out` = `game anim_task track_panel 1000 0 1920`. The camera reaches its final position after 1440/1920 × 1000 = **750 ms** and holds for the remaining 250 ms.
  - `animate_track_out_fast` is the same over 600 ms; `animate_track_in_fast` reverses it after a 500 ms delay. Tutorials only.
  - Star Power deploy: `game anim_task track 800 2000 2025`. That is 25 frames of shake spread over 800 ms, on that player's track only.
  - Debug `barf_cheat` does the same on every beat via track_panel over 400 ms.
- `anim_task obj ms from to` sets the object's frame by linear interpolation over ms (UI system doc) [I: exact easing linear].

### 1.6 Win explosion (`track_panel explode`, sent by arena `game_won`) [C]
FUN_001598c8, per track:
- Hide the track-graphics view (+0x2c). That hides the surface, gems, smashers and lightning.
- `track_explode.view.SetFrame(0)` and `set_showing TRUE`. Add it to the root draw list, and for player 0 also to the root's transform children.
- Set exploding = 1 and t_explode = now.
- From then on the Poll does `track_explode.view.SetFrame((now - t)*0.03)`.
  - The view carries a **ScaleOffset ×2.5** filter, so the child anims run at 75 frames/s.
  - The pieces' tnms (`track_explode01..20.tnm` with 101 keys each, `track_splinter01..05.tnm`) span frames 0..100. **The explosion lasts about 1.33 s.**
  - Particles: `track_explode_01.pnm` emits at rate 80 over frames 8→16. `track_explode_2/3.part` also play.
- The view draws with its own `track_explode.env` and `track_explode.lit`. FUN_00158db8 removed `track.cam` from its draw list, so it uses the per-track camera.
- While exploding, Draw first calls FUN_0015cc88(surface, track_surface.mat). This puts the per-character surface texture on `track_surface.mat`, so the debris pieces show the player's track texture [I: the purpose].
- At the same time arena `game_won` runs `game set_realtime TRUE`, the track-explode sfx, and `beatmatch set_paused TRUE`. The song clock stays in realtime mode, so `now` keeps running and the explosion animates [I].
- `game_lost` instead runs `track_panel set_showing FALSE`, so the track disappears instantly.

### 1.7 Smashers (GemSmasher FUN_001669d0; set FUN_001643b0) [C]
- Each of the 5 smashers clones `gem_smasher.view`, with a copy of `gem_smasher.mesh` (+0xc) and `smasher_rim.mesh` (+0x10).
  - It is placed at the lane x from FUN_00163828(slot), the same lane positions the engine uses.
  - Its anim list (+0x14) is [`gem_smasher.tnm`, `gem_smasher_hit.tnm`] (FUN_00167230 called in that order).
- **pop** (FUN_001675f8, used by `pop_smasher` and by `miss`):
  - Plays `gem_smasher.tnm` from t=now at 30 fps (FUN_001676c0: `SetFrame((now-t)*0.03)`).
  - Keys: z 0.001 at frame 0, **0.71** at frame 1 (33 ms), back to 0.001 at frame 5 (167 ms). Spline.
- **hit** (FUN_00167530, via PlayerMatcher_v3 `hit_gem` → FUN_0015e200 → FUN_00164988):
  - Plays `gem_smasher_hit.tnm` with the same timing but a peak of z **0.35**.
  - Starts the particle set for each fret in the gem's mask: `smash_normal.view`, `smash_star.view` or `smash_bonus.view`.
  - Selection (FUN_00166628): bonus mode → index 2, else star gem → 1, else 0.
  - When the last argument == 3, it also plays `smash_combo.view` and `smash_combo_lightning.view` per fret [I: the meaning of 3 is perhaps the multiplier/combo state].
- **miss** with a fret (PlayerMatcher_v4 `miss` @0x109218 → FUN_0015e2f8 → FUN_00164ae8) pops that smasher with the big 0.71 jump.
- `pop_smasher -1` stops (resets) all five.

### 1.8 Horizon fade (FUN_00158838 + Draw FUN_00159590) [C mechanics, I purpose]
- Config `fade normal (x 256)(y 216)`; in multiplayer `(128,236)` / `(384,236)`. x is centred by subtracting texW/2.
- Before drawing the root view it calls FUN_001a0b80(track_fade.tex, x, y). This most likely copies the framebuffer (the venue) under the horizon into `track_fade.tex`.
- After the track is drawn it blits two screen-space quads with that texture: `track_fade.mat` (alpha gradient) and `track_add.mat`. Then it draws the overlay view (+0x38).
- The effect: the far end of the track fades into the background. This is a PS2 trick; a PC port can use an alpha ramp near horizon_y on the surface instead. `alpha_dist` 40 is the related distance.
- The materials `smash_flamelight.mat`, `star_base.mat` and `smash_flamelight_starcollect.mat` get `+0x80=1` and `flags|=2` [I: render-state tweaks].

### 1.9 Multiplayer layout (FUN_00159418 / FUN_001592e8) [C]
- `normal`: track_offset (8.85,0,0), negated for player 0 (P1 on the left, P2 on the right), and track_scale 0.85.
- `dual_perspecitve` (sic, the string at 0x30d000; the data is the default because use_dual_perspective is TRUE):
  - track_scale 0.76.
  - Each camera gets a screen rect `x = 0 (P1) / 0.5 (P2), w 0.5, h 1`.
  - FUN_001b1df8(near, far, fov*0.5).

### 1.10 Script layer (track_panel.dta), on top of the natives
- `intro_start` / `intro_skip`: `reset` (new tracks), `set_showing (! $attract_mode)`, `set_track_in` (frame 0).
- `do_extend_sequence`, triggered by arena `extend_track`. Times are relative to that moment, i.e. song time −2000 ms (§3):

  | +ms | action |
  |---|---|
  | 0 | `animate_track_out` (1000 ms, cam final at +750) + TRACK_EXTEND_SFX |
  | 1300 | nowbar_1 sfx, `pop_smasher 0`, `set_smasher_glowing 0 TRUE` |
  | 1400 | nowbar_2 sfx, `pop_smasher 1`, `set_smasher_glowing 1 TRUE` |
  | 1500 | nowbar_3 sfx, `pop_smasher 2`, `set_smasher_glowing 2 TRUE`, **`hud slide_meter_in`**, **`mtv_overlay show_overlay FALSE`** |
  | 1600 | nowbar_4 sfx, `pop_smasher 3`, `set_smasher_glowing 3 TRUE` |
  | 1700 | nowbar_5 sfx, `pop_smasher 4`, `set_smasher_glowing 4 TRUE` |
  | 2000 | `pop_smasher -1` (stop all pops) |
  | 2500 | `refresh_track_buttons` (glow = actually-held frets) |

  So during the intro each fret button jumps up and lights in its colour from green to orange, one every 100 ms. All five stay lit until +2500 (song time +500 ms), then show the real held state.

### 1.11 Track vs HUD responsibilities

| belongs to the **track** (track/gen/track.rnd_ps2, TrackPanel/Track natives) | belongs to the **hud** panel (hud/gen/hud.rnd_ps2) |
|---|---|
| highway surface (per-character texture), lane lines, side rails + **rails glow** (red warning / blue SP-ready) | score, multiplier, streak counter |
| beat/half-beat/bar lines | rock meter (needle, lights) |
| gems, tails, star/bonus gem looks, `gem_bonus` spin (`ui::track_anims.view`) | star power meter / bulbs |
| smashers (pop, hit bump, held colour), rims, smash particles, flame lights, burn tails | HUD messages (`show_message`: star_ready, song_failed), youwin |
| SP **lightning** on the track and the SP camera shake | meters slide-in (`meters_slide_in.view`) |
| roll-out at intro (extend_track.tnm), win **explode**, hide on loss | |
| horizon fade, finishline.part | |

### 1.12 What our engine already draws (engine/src/render/track_renderer.cpp, read-only) and the gaps

Already drawn:
- surface (scrolling V), side rails mesh, lane lines, bar lines
- smashers with a held colour
- gems, tails, star look, HOPO tops, misses
- the hit flare `smash_flamelight.mesh` (starcollect mat in SP)
- `lightning_track.view` while SP is active
- the static `track.cam`

Gaps for 1:1:
1. Intro **roll-out**: animate the camera y from +64 to −63.218 per extend_track.tnm, frames 0..1440 of 1920 over 1000 ms, starting at song time −2000. The track also starts hidden: `set_track_in` until then.
2. **SP camera shake**: extend_track.tnm frames 2000→2025 over 800 ms on deploy.
3. **Smasher pop/hit bounce** (0.71 / 0.35 z over 167 ms), the intro pop/glow sequence, and the always-coloured `now_ring_<c>.mat` rims.
4. **Bonus gems** while SP is deployed: every unplayed visible gem gets state 2 (gem_bonus mesh/mats with the spinning gem_bonus.mnm and gem_bonus2.mnm).
5. **Side-rail glow** anims (warning red / SP-ready blue, 0.5 s pulse).
6. **Lightning anim**: the 8-frame texture flipbook at 30 fps from deploy time. The engine draws it static.
7. **Win explosion** (track_explode.view at 75 fps, 1.33 s, plus particles), and on loss **hide** the track.
8. **Per-character surface texture** (characters.dta `track_surface`).
9. **Smash particle views** (smash_normal/star/bonus, combo).
10. Horizon fade and multiplayer dual-perspective layout.

---------------------------------------------------------------------------------------------------
## 2. `mtv_overlay` panel (song-title overlay, the "MTV" lower-third)

### 2.1 Definition (game.dta)
`new_gh_panel mtv_overlay (object_name mtv_overlay)`. There is no rnd_file, so the scene is `ghui/mtv_overlay.gh`.
- `enter`: if a tutorial is running, `show_overlay FALSE`; otherwise `setup_text`.
- `setup_text`:
  - If `campaign is_store_song (game get_song)` (bonus/store song): show `mtv_unlock_song_id.view` and hide the campaign view. line1 = song title, line2 = artist.
  - Otherwise: show `mtv_campaign_song_id.view`. line1 = song, line2 = `localize mtv_made_famous` = "As made famous by", line3 = artist.
  - `set_line prefix i text` writes **both** `<prefix><i>.lbl` and `<prefix><i>_shadow.lbl` with `set_localized_text` (verbatim).
- `show_overlay B`: `$this set_showing B` (GHPanel set_showing → RndDir showing, GHPanel_v1 0x142b20) and `$this set_paused (! B)`.

### 2.2 Scene contents (decoded; 15 objects) [C]
- **The scene has no TransAnim, MatAnim or other animation objects.** `mtv_overlay.view` has no anim entries. The overlay therefore **pops on and off**; there is no slide or fade. The only "animation" is the show/hide timing below.
- `ui.cam`: world (0,−768,0), identity rotation, fov 0.785, near 400, far 1000. This is the same UI camera convention as the other panels: x and z are screen units, roughly 640×480 centred [I].
- `ui.env`: ambient 1,1,1, no fog.
- `mtv_overlay.view` draws ui.env, ui.cam, then the campaign view, then the unlock view. Both views have world scale (0.6, 0.6, 0.5) at (−289.491, 0, 206.983). In the file, campaign has showing=TRUE and unlock has FALSE.
- Labels:
  - Style `label_impactor_mtv` has colour (.9,.9,.9). `label_impactor_mtv_shadow` has colour (0,0,0).
  - Template `label_impactor_mtv.txt` (resources.rnd_ps2): font `impactor_mtv.font`, which is the impactor2.tex sheet with 42×28 cells, space 0.04 and mat blend SrcAlpha, zmode Force. Size 30, caps.
  - The LabelEx `caps` flag is TRUE, so the text is upper-cased.
  - World transforms (x scale, z scale) and LabelEx alignment, leading and wrap width:

| label | world x | world z | scale x/z | align | leading | wrap |
|---|---|---|---|---|---|---|
| mtv_campaign_line1 (title) | −289.491 | 191.983 | 0.84 / 1.4 | 0x41 bottom-left | 1.0 | 1000 |
| mtv_campaign_line1_shadow | −286.491 | 189.483 | 0.84 / 1.4 | 0x41 | 1.0 | 1000 |
| mtv_campaign_line2 ("As made famous by") | −289.491 | 184.483 | 0.6 / 1.0 | 0x21 middle-left | 1.0 | 1000 |
| mtv_campaign_line2_shadow | −287.691 | 182.983 | 0.6 / 1.0 | 0x21 | 1.0 | 1000 |
| mtv_campaign_line3 (artist) | −289.491 | 176.983 | 0.84 / 1.4 | 0x11 top-left | 0.8 | 600 |
| mtv_campaign_line3_shadow | −286.491 | 174.483 | 0.84 / 1.4 | 0x11 | 0.8 | 600 |
| mtv_unlock_line1 (title) | −289.491 | 191.983 | 0.84 / 1.4 | 0x41 | 1.0 | 1000 |
| mtv_unlock_line1_shadow | −286.491 | 189.483 | 0.84 / 1.4 | 0x41 | 1.0 | 1000 |
| mtv_unlock_line2 (artist) | −289.491 | 194.483 | 0.84 / 1.4 | 0x11 | 0.8 | 600 |
| mtv_unlock_line2_shadow | −286.491 | 191.983 | 0.84 / 1.4 | 0x11 | 0.8 | 600 |

  - Shadows are offset (+3, −2.5) in world units, or (+1.8, −1.5) for the small campaign line2. Each shadow is drawn **before** its label (the view draw order lists the shadow first).
  - The text sits in the top-left of the screen. The title grows upward from z≈192, "As made famous by" is centred on z≈184.5, and the artist grows downward from z≈177 and wraps at 600 units.
  - The view lists the labels as transform children, but each label's parent is itself. **Use the world matrices above.** [C data; I how the parenting resolves]

### 2.3 Show/hide timeline (normal career/quickplay song, not attract or tutorial)
These are times relative to the GamePanel `intro_start`, at song time **−10000 ms** (§3). The `game delay_task` delays run on the game panel's TaskMgr [I: they tick on the UI/real clock, which equals song time during the realtime intro].

| song time | event |
|---|---|
| −10000 | `intro_start` → `mtv_overlay show_overlay FALSE` |
| −9000 | `game delay_task 1000` → `show_overlay TRUE`. The overlay appears instantly while the intro camera shot plays. |
| −2000 | arena `extend_track` → track_panel `do_extend_sequence` |
| −500 | (+1500) → `show_overlay FALSE`, at the same moment as `hud slide_meter_in` |

- So **the overlay is visible for 8.5 s** (song time −9000 to −500).
- With `intro_skip` (tutorials/cheat) nothing re-shows it. Panel enter runs setup_text with the scene in its loaded showing state; tutorials hide it on enter.
- After a `restart` the game panel re-runs Enter → intro_start, so the same timeline repeats.
- Because `show_overlay FALSE` also pauses the panel, nothing on it polls while hidden. That doesn't matter, since it has no anims.

---------------------------------------------------------------------------------------------------
## 3. GamePanel natives: song clock, intro, extend_track, music_start

### 3.1 Song clock (GamePanel_v10 Poll @0x105808) [C]
- `DAT_00344258` (an 0x30-byte struct written via FUN_00106a28/FUN_00106a70) is the **game time**: [0] ms, [2] ticks, [4] beats.
- In "realtime" mode (+0x94) it advances from the CPU Count register:
  - `ms = elapsed + base(+0x9c)`
  - `ticks = base_ticks(+0xa4) + (ms - base_ms(+0xa0)) / ms_per_beat(+0x98) * 480`
- Otherwise it comes from the BeatMatch audio clock (FUN_0010e1f0 / +0x80 +0x14).
- The time never goes backwards (FUN_00106a70 clamps).

### 3.2 Enter / start
- GamePanel_v3 (Enter) @0x1055b8 resets the clock to 0 (FUN_00104a38) and sets +0xa8 = 1 ("start pending"). `restart` and `start_song` also set +0xa8. [C]
- The next Poll calls FUN_00105b80:
  - **Normal** (`skip_intro` +0x88 == 0):
    1. `D = $camedit.INTRO` shot `(duration)`. That is **10000 ms in every venue's camera.dta** (basement, small_club, big_club, theatre, fest, arena, small_club_multi).
    2. Clock = −D ms (ticks = −D/ms_per_beat·480), realtime mode on (FUN_00106190(1)).
    3. Send **`intro_start`** to the game panel script.
    4. The script then runs `arena_panel start`, `arena intro_start`, `track_panel intro_start`, `hud intro_start`, `mtv_overlay show_overlay FALSE`, and `delay_task 1000 {mtv_overlay show_overlay TRUE}`.
  - **skip_intro**: send `intro_skip`, then start the music immediately (FUN_00105640).
  - If `start_paused` (+0x8c): FUN_00106168(1) pauses.
- The "real_time" key of the intro shot is read but only used for the warning "Intro shot is not real-time." [C]

### 3.3 `extend_track` [C]
- `track_extend_ms` is a **script property** of the game panel: `(track_extend_ms -2000)` in game.dta.
- The ctor FUN_00104798 reads it by sending the message `track_extend_ms` to itself; the result is stored at +0xb4.
- Each Poll: `if (prev_ms < +0xb4 && +0xb4 <= now_ms)` → send **`extend_track`** to `arena` (0x363748).
- arena_game.dta handles it as `{track_panel do_extend_sequence}`. **It fires once at song time −2000 ms**, i.e. 8000 ms after intro_start.

### 3.4 Music start [C]
- Poll: `if (!started(+0xac) && now_ms > -25.0)` → FUN_00105640:
  - start the song audio for every player (FUN_0010e160)
  - switch the clock back to audio-driven (FUN_00106190(0))
  - set +0xac = 1
  - send **`click_start`** to the game panel script, which forwards `arena click_start`
- **`music_start`** to arena is sent by the BeatMatch beat tracker (BeatMatch_v1 @0x10e458, string 0x300198) when its **measure counter changes to 2** (`if measure == 2`) [C for the comparison; I that the field is the measure index].
  - Along with it come the `beat`/`beats_left`/`measure` data vars and the arena `beat` message on every beat change.
  - arena_game.dta `music_start`: crowd_upto_norm sfx, crowd excitement, `ui music_start`.
- Song text events: FUN_00106348 walks SongDB+0x4c (tick, symbol) pairs and sends each symbol to arena as its tick passes (e.g. crowd_normal_tempo, crowd_half_tempo). [C]

### 3.5 Full intro timeline (song ms; non-tutorial, non-attract)

| ms | event |
|---|---|
| −10000 | intro_start. Track reset + `set_track_in` (camera far, highway not visible). HUD `meters_slide_in` frame 0, score hidden. mtv overlay hidden. Venue intro camera (10 s shot). |
| −9000 | mtv overlay shown |
| −2000 | extend_track: track roll-out begins (1000 ms; camera settles at −1250) |
| −700, −600, −500, −400, −300 | smashers 0..4 pop + light up, nowbar_1..5 sfx |
| −500 | `hud slide_meter_in`: score shown, `meters_slide_in.view` 0→480 over 500 ms, meter_slide sfx at +250. mtv overlay hidden. |
| −25 | audio starts, click_start |
| 0 | all smasher pops reset |
| +500 | smasher glow = held frets |
| later | music_start when the beat tracker reaches measure 2 |
