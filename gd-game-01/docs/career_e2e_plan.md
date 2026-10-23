# Career end to end: original behaviour vs gd01, and the gap list

Scope: everything a player meets in Career, from picking a difficulty to beating the final song:
tiers, unlocks, encores, cash, the store, bonus songs and videos, the final win, Battle Axe, saving,
and what Quick Play / Multiplayer see as unlocked.

- [C] means confirmed from the decompilation (`analysis/decomp/*.c`), `analysis/strings.tsv` / `vtables.tsv`, or the decoded scripts in `extracted_dta/`.
- [I] means inference.
- Rules already written down in `docs/career.md`, `docs/endgame_and_hud.md` and `docs/select_panels.md` are only summarised here. This file adds a check of every rule against our code, plus the gaps.

Engine files checked (read-only):
- `engine/src/game/campaign.{h,cpp}`
- `engine/src/ui/career_objects.{h,cpp}`
- `engine/src/ui/ui_system.cpp`
- `engine/src/ui/select_panels.cpp`
- `engine/src/ui/game_panels.cpp`
- `engine/src/app.cpp`

---------------------------------------------------------------------------------------------
## 0. Verdict

The career **rules** (Campaign, CampaignState, cash, unlocks, status, final song, save) are ported
correctly and match the decomp line by line (§2). The original screen scripts run unchanged.

These things still stop the career from working fully end to end:

| # | Gap | Impact | Size |
|---|---|---|---|
| **G1** | **StatusPanel (`new_status_panel`) is not implemented.** Its `rnd_file` is a script expression that is never evaluated, so no scene loads. The character picture and the `(text ...)` fill are also missing. | The tier-complete / new-status newspaper screen after a song comes up **blank**. This is the original's only "new venue unlocked" announcement. | M |
| **G2** | **`video_provider get_video_file` is missing.** | Bought bonus videos cannot play: Options → Bonus Material → video passes no file to `play_movie`. | S |
| **G3** | **The career song list cache ignores the selected venue.** | When you change venue in sel_venue, the newest-tier filter and the footer stay as they were for the old venue. | S |
| **G4** | **Guitar select:** `guitar_red_octane` is not filtered out, and the quickplay flag (multiplayer means "unlocked in any difficulty") is ignored. | An extra guitar shows up. In multiplayer, guitars and skins bought in another difficulty are missing. | S |
| **G5** | **Multiplayer character select** checks the current difficulty instead of "any difficulty". | Grim Ripper / Izzy bought in a Medium+ career can be locked in multiplayer. | S |
| **G6** | **FINAL_WIN gating differs:** the original requires difficulty ≠ Easy and does not check the mode. | On Easy we play the FINAL_WIN camera and guitarist clip, but the original plays the normal WIN ones. | XS |
| G7 | Small things: `cheat_beat_song` 5★ rule, `set_career_venue` availability under unlock-all, stale comments and backlog rows. | Debug paths and documentation. | XS |
| G8 | CreditsPanel (`new_credits_panel`) is not implemented. | Options → Credits is empty. It is **not** part of career: GH1 never rolls credits after the career. | M (optional) |

There is **nothing to build for encores**: GH1 has none (§1.3).

There is **no explicit "career complete" flag or screen** to build either:
- completion is derived from the item flags;
- the win screens are `win_easy` / `win_game` (`complete.gh` is the ordinary after-song menu, and a won career skips it).

---------------------------------------------------------------------------------------------
## 1. The original flow, step by step (with evidence)

### 1.1 Starting a career per difficulty [C]

1. `main` → `sel_difficulty` (`ghui/gen/career.dta:125-160`).
   - Each difficulty shows `campaign get_status_progress kDifficultyX` (`career.dta:88-116`, `FUN_00139440`).
   - The focus starts on `campaign last_difficulty`.
2. SELECT does `set_difficulty` and `campaign update_difficulty`.
   - If `campaign new_campaign` (that difficulty's `character == ""`, `FUN_0013d5c8`) → `sel_character_new` → `sel_guitar` → `goto_career`, which runs `set_character_info` (`FUN_001393b0`).
   - Otherwise → `get_character_info` → `career`.
3. Each of the 4 difficulties is an independent career, each with its own CampaignState (`docs/career.md` §1.4). Per difficulty it holds:
   - character, guitar and skin;
   - cash;
   - the 116 items (unlocked / beaten / score / stars).
4. `career` screen (`career.dta:161-303`):
   - It shows the character, skin, cash, band, `career_score`, the status symbol and the difficulty.
   - **The store button is disabled on Easy, and the cash labels are hidden there** (`career.dta:226-251`).
   - On enter it runs `meta setup_career` (`splash.dta:44-51`): main_screen=career, continue_screen=sel_song, win_screen=endgame, lose_screen=lose, mode=career.
   - ROCK → `autosave_goto sel_venue`.

Ours: the scripts run as-is. The natives are `career_objects.cpp:55-68` and `campaign.h:89`. **OK.**

### 1.2 Tiers, venues, unlocking [C]

Data is `config/gen/campaign.dta`: `order` (6 tiers × 5 songs), `required_songs` Easy 4 / Medium 4 / Hard 5 / Expert 5 (line 25), `cash` (lines 35-50).

- **Number of tiers:** 5 on Easy (no arena), 6 otherwise (`FUN_0013a788`).
- **Songs needed:** `required(diff, venue)` = 5 for the last tier of that difficulty, else `required_songs[diff]` (`FUN_0013b6c8`).
- **Tier complete:** `venueComplete` = beaten songs in the tier ≥ required (`FUN_0013e220`).
- **Unlocking the next tier:** on the first pass of a career song, if its tier is now complete, the next tier and its 5 songs unlock (`FUN_0013e130`, decomp `00130000.c:9185`).
- **Stars do not gate progress.** Any pass gives at least 3★.
- **sel_venue** (`career.dta:1489-1657`): `foreach_venue` enables or disables each button by `is_unlocked`. `game set_career_venue` (`FUN_00113c30`) focuses the newest unlocked venue, unless the career is complete.
- **sel_song** (`career.dta:1658-1936`):
  - It uses `song_provider` in career mode: the songs unlocked at the current difficulty, then "Bonus Tracks" (the store songs bought at this difficulty).
  - The `FUN_0013d698` filter hides the newest tier's songs while an older venue is selected.
  - The footer reads: "Beat %i/5 songs to continue", "Beat 5/5 songs to win", or "Play in the latest venue…".
- **Final song:** `final_song` (`FUN_0013eb20`) = a career song, not yet beaten, in the last tier of that difficulty, with beatenInVenue == required − 1.

Ours (all match): `campaign.cpp:124-131, 236-299, 278-284`; venue/song selection `ui_system.cpp:795-828, 973-993`. **OK, except G3.**

### 1.3 Encores [C]

GH1 (PS2 NTSC) has **no encore mechanic**:
- no "encore" string in the ELF (`analysis/strings.tsv`), in any decoded script, or in `ghui/eng/gen/locale.dta`;
- no encore screen or script;
- tiers end when the required number of songs is beaten.

Nothing to implement. Encores arrived with GH2.

### 1.4 After a song: endgame → status → cash award → complete [C]

`arena/gen/arena_game.dta:229-271` `game_outro`:
- On a win: if `mode==career && campaign final_song` → `meta set won_campaign TRUE`, then go to `post_show`.
- On a loss: go to the lose screen.
- `won_campaign` is cleared again on `loading` enter (`ghui/gen/loading.dta:58`).

`endgame.dta:1-42` `post_show`: `won_campaign ? (Easy ? win_easy : win_game) : endgame`.

`endgame` panel enter (`endgame.dta:279-303`), career only:
- runs `campaign finish_song $score $new_venue $new_status` (`FUN_00138ec0` → `FUN_0013dd80`, decomp `00130000.c:9036`);
- if a new venue opened, sets `continue_screen = career`.

On SELECT (`endgame.dta:306-333`):
- `$new_status` → `status`;
- otherwise `has_cash_award` → `cashaward`;
- otherwise `goto_endgame_complete`.

`status` (`endgame.dta:612-773`):
- StatusPanel with scene `ghui/status_<campaign status>.gh` and text table `status1..7`;
- plays sfx `beat_venue`;
- SELECT → `cashaward` or `goto_endgame_complete`.

`cashaward` (`endgame.dta:775-881`):
- shows one award per page (`next_cash_award`), with reason text = locale `award_<n>_star[_status]`;
- "gross" (amount / 0.05) and the seven fees are cosmetic.

`goto_endgame_complete` (`endgame.dta:44-65`): `career && won_campaign` → `meta_loading_goto career` (this **skips** `complete`); otherwise go to `complete`.

`complete` (`endgame.dta:1174-1260`, scene `complete.gh`):
- buttons CONTINUE/selsong, restart, quit;
- for career non-bonus songs the first button reads "CONTINUE".

Ours: these scripts run unchanged. `finish_song` is `career_objects.cpp:82-88` / `campaign.cpp:302-337`; cash award `career_objects.cpp:99-106`. **OK, except G1** (status screen).

### 1.5 Cash awards [C] (`campaign.dta:34-50`, `FUN_0013dd80`, `FUN_0013b920`, `FUN_0013b9e0`)

| | Easy | Medium / Hard / Expert |
|---|---|---|
| Star money (cumulative, first time reaching that many stars on a **career** song) | $0 | 3★ $100, 4★ $300, 5★ $650. An improvement pays the difference. |
| Status bonus when status rises to N | $0 | 1 $100, 2 $250, 3 $600, 4 $1200, 5 $2200, 6 $4900, 7 $0 |
| Bonus (store) songs | no star money | no star money |

- At most one award per song, reason `award_<stars>_star[_status]`.
- `has_cash_award` / `next_cash_award` empty the pending list.
- No code path produces `award_god`.

Ours: `campaign.cpp:146-151` (`starCum`) and `campaign.cpp:314-333` (status index `newStatus-1` into the row without the difficulty key equals the original element `newStatus`). **OK.**

### 1.6 Status [C] (`FUN_0013e7e8`, `FUN_0013e940`)

- status = complete+unlocked tiers, **+1** if every career song of that difficulty is 5★ (and on Medium+ every bonus song too).
- Names `status_0..7`: NOVICE … GUITAR GOD.
- On Easy the career ends at status 5 (GUITAR HERO). `endgame.dta:625-640` hides `status_guitarhero_newvenue.view` when `won_campaign && status==5 && Easy`.

Ours: `campaign.cpp:253-261`. **OK.**

### 1.7 The final tier and the final song win [C]

1. The last tier needs **5/5**. The song that takes it to 5/5 is `final_song`.
2. At game over the camera script runs `char_sys final_win_sequence` (`arena/gen/camera.dta:389-430`).
   - This is `FUN_00282818` → `FUN_00282870` (`00280000.c:1287-1330`):
     `($final_win debug var) || (campaign final_song && difficulty != Easy)`, **and** `arena.game_won`.
   - When true it plays `FINAL_WIN_GRIM` / `FINAL_WIN` at once (and the guitarist's final-win clip).
   - Otherwise it plays WIN after `WIN_CAMERA_DELAY`.
   - `FUN_00282870` is also used at character load (`00280000.c:499`) to choose which win animation set to load.
3. `game_outro` → `won_campaign TRUE` → `post_show`:
   - **Easy → `win_easy`** (`endgame.dta:355-436`): 6 s input delay, band name, sfx `beat_easy`.
   - **Medium/Hard/Expert → `win_game`** (`endgame.dta:447-610`):
     - 4 s white fade-in, label `win_diff` "You beat Guitar Hero on the %s difficulty", sfx `beat_other`;
     - **on Expert only**, the first select runs `display_battleaxe`: `campaign unlock_global_item battleaxe` (`FUN_001385b8`: unlocked in all 4 difficulties), white flash, then `endgame_guitar_display show_guitar 0 battleaxe ''` (axe placer and spin, endgame camera);
     - the next select fades to black and goes to `endgame`.
4. `endgame` → `finish_song`. The last tier is now complete, so status rises:
   - Easy 5, otherwise 6 (7 if everything is 5★);
   - this shows `status_5/6/7.gh`, then the cash award (status bonus $4900 on Medium+);
   - then `goto_endgame_complete` → `meta_loading_goto career`, which autosaves.
5. **No credits roll and no "career complete" screen.**
   - Credits are reachable only from Options (`ghui/gen/options.dta:36, 753-770`).
   - A completed career just shows the final status. The song list loses its footer (`status >= N`). sel_venue keeps the chosen venue (`FUN_00113c30`).

Ours:
- `app.cpp:1011-1016` (final win), `venue_camera.cpp:267`;
- win_easy and win_game run from the script;
- GuitarDisplayPanel endgame config `select_panels.cpp:290-300`;
- `unlockGlobalItem` `campaign.cpp:359-362`;
- FadePanel `game_panels.cpp:308-356`.

**OK, except G6** (Easy gating), **and G1** (the final status page is blank).

### 1.8 Unlocks across difficulties [C]

- Everything is **per difficulty**: tiers, songs, store purchases (guitars, skins, songs, characters) and cash. A Medium purchase does not unlock that item in the Hard career.
- **Global items:**
  - videos (`global=1`, `buy_item` unlocks them in all 4 difficulties, `FUN_0013e788`);
  - `battleaxe` (via `unlock_global_item` after the Expert win).
- **Beating Easy, Medium or Hard unlocks nothing outside that career.** Only finishing **Expert** gives the Battle Axe. Every other cross-mode effect comes from the "any difficulty" checks below.

| Consumer | Unlock test | Evidence |
|---|---|---|
| Career lists: songs, characters (`char_single` stepping), guitars, skins, venues | current difficulty (`FUN_00137b00`) | `docs/select_panels.md` §3.4, `FUN_00140d50` |
| Quick Play song list | tiers 1-2 always, plus any song unlocked in **any** difficulty (bonus songs included) | `FUN_00137d58` (`00130000.c:5325`) |
| Quick Play footer "Play career mode to unlock more songs" | hidden once some Medium..Expert career has status ≥ N−1 | `FUN_00118dd8` |
| Multiplayer guitars and skins (`input_guitar set_quickplay p TRUE`) | **any** difficulty | `SelectGuitarProvider_v9` → `FUN_00138240`, `FUN_0011a498` |
| Multiplayer characters | **any** difficulty | `FUN_0014e7e8` → `FUN_00137b28` d=0..3 |
| Options → Bonus Material videos | `campaign is_unlocked videoN` (global) | `options.dta:639-737` |

Ours:
- Quick Play is `ui_system.cpp:774-793`. **OK.**
- Multiplayer has **G4 / G5**.

### 1.9 New characters [C]

- Izzy Sparks (`hair_metal`, $5000) and Grim Ripper (`grim`, $8000) are **store items only** (`store.dta character`). They have no other condition (`characters.dta` has no unlock data). They are locked by default (`FUN_0013c5d0`: unlocked = !is_store).
- There is no way to buy them on Easy (the store is disabled there).
- Grim restricts the guitar list to `grim_guitar` (`SelectGuitarProvider_v9`), and his final win uses `FINAL_WIN_GRIM`.
- The other 6 characters are unlocked from the start.

Ours: `campaign.cpp:164-167`, `select_panels.cpp:463`. **OK.**

### 1.10 The store [C] (`career.dta:810-1470`, StorePanel_v1 @0x1535d0, `config/gen/store.dta`)

- **Screen 1:** five categories (guitar, skin, song, character, video). For each it shows a poster, a blurb and the price range (`store low_cost/high_cost`).
- **Screen 2:** items from these providers:

| Category | Provider | What it lists |
|---|---|---|
| guitar | `store_guitar_provider` | all 7 store guitars |
| skin | `store_skin_provider` | the 28 store skins whose guitar is unlocked |
| song | `song_provider` in store mode | 17 songs, with a preview |
| character | `character_provider` in store mode | hair_metal, grim (drawn unlit until bought) |
| video | `{game video_provider}` | video1..3 |

- Owned items show `st_sold.mesh`.
- **Buying:**
  - when `!is_unlocked item && cash >= price` → `campaign buy_item item price` (`FUN_00138538`: unlock, `cash -= price`, global items in all difficulties, needs_save always), refresh the cash, `push_screen store_bought` (`<category>_bought_blurb`), sfx `cash_register`;
  - otherwise sfx `button_error`.
  - There is **no yes/no confirm dialog**. `store_bought` is the only confirmation.
- Back from screen 1 → `autosave_goto career`, which saves.
- **Prices** (`store.dta`):

| Category | Prices |
|---|---|
| Guitars | es335 / xplorer / firebird / lespaul_special $600; corvus / moderne $700; eds $850 |
| Skins | $150 / $200 / $250 (eds_white $250) |
| Songs | fireitup, cheatonthechurch, cavemanrejoice, eureka $300; the other 13 $250 |
| Characters | hair_metal $5000, grim $8000 |
| Videos | video1 $350 (wavegroup.pss), video2 $325 (character.pss), video3 $325 (venues.pss) (`store.dta:230-244`) |

- **What a purchase does:**
  - guitar / skin / character: appears in the career selectors at that difficulty, and in multiplayer;
  - song: appears under "Bonus Tracks" in that difficulty's career list and in the Quick Play list (any difficulty). It plays with the `mtv_unlock_line` overlay (`game.dta:33-60`), earns no star money, and counts toward the status-7 "all 5★" rule on Medium+;
  - video: playable from Options → Bonus Material (`options.dta:630-752`: `play_bonus_video` → `meta play_movie {video_provider get_video_file videoN}`).

Ours:
- StorePanel `game_panels.cpp:290-303`; prices `campaign.cpp:65-76, 133-138`; `buyItem` `campaign.cpp:346-357`;
- providers `career_objects.cpp:208-270`; store song list `ui_system.cpp:771-773`;
- char_store lights `select_panels.cpp:112, 158`;
- `play_movie` `ui_system.cpp:1241-1244`.

**OK, except G2** (videos).

### 1.11 Saving [C] (`docs/career.md` §4)

- No separate "career completed" flag exists. Completion and status are recomputed from the per-item flags (unlocked / beaten / score / stars) plus cash and character info.
- `needs_save` is set by:
  - `finish_song` (first pass or an award);
  - `buy_item`;
  - `set_character_info` when it changes;
  - `add_cash`, name changes and high scores.
- `unlock_global_item` does **not** set it, but the `finish_song` that always follows it does.
- Saves happen at `autosave_goto` points: ROCK, store back, and every `meta_loading_goto` after a song.

Ours: JSON `career.json` (`campaign.cpp:397-536`), the memcard screens are mapped to it (backlog M3). **OK.**

---------------------------------------------------------------------------------------------
## 2. Rule-by-rule check of our port (all match unless listed in §3)

| Rule | Original | Ours | Status |
|---|---|---|---|
| Item list and initial flags (6 venues, 30+17 songs, 8 characters, 13 guitars, 39 skins, 3 videos; battleaxe locked) | `FUN_0013baf8..FUN_0013d128` | `campaign.cpp:154-178` | ✅ |
| numVenues / required / nextVenue | `FUN_0013a788` / `FUN_0013b6c8` / `FUN_0013b150` | `campaign.h:68`, `campaign.cpp:124-131` | ✅ |
| unlockNextVenue | `FUN_0013e130` | `campaign.cpp:287-299` | ✅ |
| finish_song (first, venue, stars, cash, status, award list replaced, save flag) | `FUN_0013dd80` / `FUN_00138ec0` | `campaign.cpp:302-337`, `career_objects.cpp:82-88` | ✅ |
| final_song | `FUN_0013eb20` | `campaign.cpp:278-284` | ✅ |
| status / allFiveStar / songsBeaten / careerScore | `FUN_0013e7e8`, `FUN_0013e940`, `FUN_0013ee98`, `FUN_0013ec68` | `campaign.cpp:253-275` | ✅ |
| buy_item / unlock_global_item | `FUN_00138538`, `FUN_001385b8` | `campaign.cpp:346-362` | ✅ |
| get_status_progress | `FUN_00139440` | `career_objects.cpp:109-117` | ✅ |
| Career list filter and footer | `FUN_0013d698`, `FUN_00118dd8` | `ui_system.cpp:795-828` | ✅ logic, ❌ cache (G3) |
| Quick Play list and footer | `FUN_00137d58` | `ui_system.cpp:774-793` | ✅ |
| set_career_venue / set_career_song | `FUN_00113c30` / `FUN_00113b80` | `ui_system.cpp:973-993` | ✅ (see G7) |
| Stars of a score (3/4/5 vs 2×/3× base) | `FUN_0011bdd0` | `app.cpp:273-276` | ✅ |
| win_easy / win_game / battleaxe / fades | `endgame.dta:355-610` | script + `select_panels.cpp:285-340`, `game_panels.cpp:308-356` | ✅ |
| StatusPanel | `StatusPanel_v13/v15/v16`, `FUN_00153018` | — | ❌ G1 |
| VideoProvider get_video_file | `VideoProvider_v1` @0x11bbe0 → `FUN_0011bb00` | — | ❌ G2 |
| Guitar provider filters | `SelectGuitarProvider_v9` @0x119ce0 | `select_panels.cpp:458-476` | ❌ G4 |
| Multiplayer character lock | `FUN_0014e7e8` | `select_panels.cpp:540-543, 590-597` | ❌ G5 |
| final_win_sequence | `FUN_00282818` / `FUN_00282870` | `app.cpp:1013-1016` | ❌ G6 |

---------------------------------------------------------------------------------------------
## 3. Gaps and exact implementation steps

### G1. StatusPanel (`new_status_panel`) [C]

**Original** (`docs/endgame_and_hud.md` §3; vtable 0x2f8898, ctor registered `FUN_00152e38`):

- **Load (`StatusPanel_v13` @0x152e70):**
  - evaluate `(rnd_file {sprint "ghui/status_" {campaign status} ".gh"})` **at each load**; this runs after `finish_song`;
  - start loading `ghui/image/status_<PlayerConfig(0).character>0.png` (the files exist: `extracted/ghui/image/gen/status_{metal,punk,...}0.png_ps2`).
- **IsLoaded (v15):** the scene and the png are both loaded.
- **FinishLoad (`StatusPanel_v16` @0x152f30):**
  - point the `status_char1.tex` bitmap at the png;
  - then run `FUN_00153018`, which goes through `(text (status<N> (band|char (label "x.lbl") [(token t)])...))` with N = `campaign status`:
    - `band` → `campaign profile_name {campaign profile_slot}`;
    - `char` → `localize(player 0 character)`;
    - with a token, the text becomes `sprintf(localize t, str)`;
    - then `x.lbl set_localized_text str`.
- The script's `enter` (hiding the Easy "new venue" view) and the BUTTON handlers are already in `endgame.dta`.

**Ours:**
- `new_status_panel` is registered as a generic panel (`ui_system.cpp:1682`).
- `PanelObj` stores `rnd_file` with `propValue` (`ui_system.cpp:25-30, 129`). A `{sprint ...}` command becomes a `Code` value, and `PanelObj::load()` reads it with `.str()`, which returns `"{...}"` (`script.cpp:145`). The scene file isn't found and nothing loads (`ui_system.cpp:150-160`).
- The panel also loads only once (`loaded_`, `ui_system.cpp:146-147`), and it is never reloaded for a different status.
- The `(text ...)` table and the character picture are not handled anywhere.

**Steps:**
1. In `PanelObj::load()` (`ui_system.cpp:146`): if `props["rnd_file"]` is `Value::Type::Code`, evaluate it with `ui_.interp_.eval(*code)` and use the resulting string. Remember the resolved file name.
2. For `new_status_panel` (keep the verb on `PanelObj`, or add a `StatusPanel` NativePanel in `game_panels.cpp` returned by `makeGamePanel`), on each `enter()`:
   - re-evaluate the file;
   - if it differs from the loaded one (or always, since this is cheap), drop `scene_` / `components_`, set `loaded_ = false`, and call `load()`.
   Doing this per panel keeps every other panel untouched.
3. After the scene loads, load `ghui/image/status_<character>0.png` (character = `playerCharacter(0)`, i.e. `GameObj players[0]->character`) and bind it to the scene texture `status_char1.tex`. Reuse the mechanism of `UiSystem::loadWinnerPhoto` (`ui_system.cpp:207`), which already swaps a texture on the endgame scene.
4. Text fill (port `FUN_00153018`):
   - N = `campaign status`;
   - find `props["text"]` → the entry `statusN`;
   - for each sub-entry: `str = band ? profile name : localize(character)`;
   - if it has `(token t)`, `str = sprintf(localize(t), str)` (a single `%s`);
   - send `set_localized_text str` to component `x.lbl`. Use the `label` string without changing it; the components are named with `.lbl`.
5. Run the fill before the script's `enter`, so `status_guitarhero_newvenue.view set_showing FALSE` still wins.
6. Check: after the 4th basement song on Medium, `status_1.gh` (ROCKER) shows "ROCKERS OF THE WEEK: <band>" and the character picture. After the final arena song, `status_6.gh` shows. On Easy the fest finale shows `status_5.gh` without the new-venue box.

### G2. `video_provider get_video_file` [C]

**Original:**
- `VideoProvider_v1` @0x11bbe0 handles `get_video_file sym` → `FUN_0011bb00` → `store.dta (video <sym> (file X))`, giving `wavegroup.pss`, `character.pss` or `venues.pss`.
- The other messages are the usual `get_text` / `get_symbol` / `list_length`.

**Ours:** `career_objects.cpp:267-269` registers a plain `ItemProviderObj` for `video_provider`. `get_video_file` is not handled (it falls through to `return false`), so `play_bonus_video` gets no file name and `meta play_movie` has nothing to play.

**Steps:**
1. `Campaign`: while parsing `store.dta` (`campaign.cpp:65-76`), also store `(file ...)` for videos in `std::map<std::string, std::string> videoFiles_`, plus an accessor `videoFile(sym)`.
2. `ItemProviderObj` (or a small `VideoProviderObj`): `if (msg == "get_video_file") return r = Value::string(c.videoFile(a[0].str())), true;`. The argument is a symbol.
3. Check that `hooks_.playMovie` resolves `videos/<file>` (it already does for `ghintro.pss`).
4. Check: buy video1 on Medium → Options → Bonus Material → video 1 plays. Also check that `bm_buy.lbl` hides once all three are bought.

### G3. The song list cache must follow the selected venue [C]

**Original:** the list is rebuilt from the provider every time it is used. `FUN_0013d698` reads the **current** GameConfig venue. Both the "newest tier hidden while an older venue is selected" filter and the footer (`FUN_00118dd8`) depend on that venue.

**Ours:**
- `ProviderObj::rows()` caches on `diff` only (`ui_system.cpp:723-729`). `venue` is read but not part of the key.
- `invalidate()` runs only on campaign changes (`ui_system.cpp:1544`).
- Example: unlock small_club, enter sel_song there (the list is built with cur = 1), go back, pick basement. The list still shows the small_club songs and the "Beat 4/5 songs to continue" footer, where the original shows the basement-era list and "Play in the latest venue to continue your career".

**Steps:** add `std::string builtVenue_` and `bool builtQuickplay_` next to `builtDiff_` (`ui_system.cpp:861`), and rebuild when `venue != builtVenue_` (career mode only). Alternatively, call `invalidate()` from `GameObj` `set_venue` / `set_career_venue` (`ui_system.cpp:965, 973`).

### G4. Guitar list filters [C]

**Original** (`SelectGuitarProvider_v9` @0x119ce0, decomp `00110000.c:6657`):
- character `grim` → `[grim_guitar]`;
- otherwise:
  - the list is `FUN_00137c18(campaign, 3)` (quickplay = 0: the current difficulty) or `FUN_00138240` (quickplay = 1: any difficulty);
  - then **both `grim_guitar` and `guitar_red_octane` are removed** (the strings at 0x302038 / 0x302048 are referenced only by this function).
- Skins (`FUN_0011a498`) use the current difficulty, or any difficulty when quickplay is set.

**Ours:** `select_panels.cpp:458-476`:
- filters only `grim_guitar`;
- always uses `host_.unlocked()`, which is current difficulty only (`ui_system.cpp:2070-2072`);
- `St::quickplay` is set (`select_panels.cpp:427`) but never read;
- `guitar_red_octane` is unlocked by default (not a store item), so it shows in every list.

**Steps:**
1. Add `bool unlockedAny(const std::string&)` to `SelectHost` / `UiSystem`, backed by `Campaign::isUnlockedAny` (`campaign.cpp:230`).
2. `guitarList(p)`: skip `grim_guitar` **and** `guitar_red_octane`; use `st_[p].quickplay ? host_.unlockedAny(g) : host_.unlocked(g)`. Do the same in `skins(p, g)`.
3. Update the stale comment at `select_panels.cpp:458-461` ("the campaign unlocks are not reimplemented…").
4. Keep the "Red Octane guitar" cheat (`$cheat_guitar kGuitarRedOctane`, `long_cheats.dta`) as an in-game model swap only. Not needed.

### G5. Multiplayer character lock = any difficulty [C]

**Original:** `MultiCharSelPanel` disables `msc_char_<c>.pic` and skips characters that are **not unlocked in any difficulty** (`FUN_0014e7e8` → `FUN_00137b28` with d = 0..3; `docs/select_panels.md` lines 400, 426).

**Ours:** `select_panels.cpp:540-543` and `590-597` use `host_.unlocked(c)`, which is the current difficulty (whatever the last career or quickplay difficulty was).

**Steps:** use `host_.unlockedAny(c)` from G4 in both places. `CharsysPanel::step` (`select_panels.cpp:200-206`, career) stays on the current difficulty.

### G6. FINAL_WIN gating [C]

**Original** (`FUN_00282870`): `final_win` (debug global) || (`campaign final_song` && GameConfig difficulty > 0); `FUN_00282818` also requires `arena.game_won`. The mode is **not** checked. On Easy the final song uses the normal WIN camera after the delay, and the normal guitarist win.

**Ours** (`app.cpp:1013-1015`): `won && mode == career && finalSong(...)`, with no difficulty check.

**Steps:**
- `bool finalWin = won && campaign_ && difficulty_ > 0 && campaign_->finalSong(difficulty_, songId);`
- Dropping the career check copies a quirk: the FINAL_WIN shot can also fire in quickplay when that song is the final career song of that difficulty.
- If you want to stay strictly 1:1, drop the mode check. Keeping it changes only that quirk.
- `won_campaign` itself (arena_game.dta) keeps its own mode check, and that check is correct.

### G7. Small items [C]

1. `cheat_beat_song` (`career_objects.cpp:89-98`): the original sets stars to 5 only when the score was raised **and** equals 1 000 000 (`FUN_0013e090`, `00130000.c:9160-9182`). Move the `stars = 5` inside the "score raised" branch. This is a debug path.
2. `set_career_venue` (`ui_system.cpp:973-981`): the original also requires `is_venue_available` (not `multi_only`) on the "keep current venue" path and while walking the venues. Add `isVenueAvailable(venue)`. This matters only with unlock-all, when `small_club_multi` could be the current venue.
3. Docs and backlog: `backlog.md` row "7i Campaign unlocks … ❌ Everything reports unlocked" is stale (row 9 is ✅). Once G1-G6 are done, mark 9c as done.

### G8. CreditsPanel (optional, not career) [C]

- `new_credits_panel credits` (`options.dta:753-770`, vtable `CreditsPanel` 0x2f8080, scene `ghui/credits.gh`, data `config/gen/credits.dta`) is only registered as a generic panel. Nothing feeds `credits.lst`.
- GH1 shows credits **only** from Options. Do **not** add a roll after the career.

---------------------------------------------------------------------------------------------
## 4. Walkthrough to sign off (after G1-G6)

Use a fresh profile. The in-game keyboard cheats `b` / `5` on sel_song call `cheat_beat_song` (no cash, no save) and speed things up. Real passes check the cash.

1. **Easy:**
   - beat 4 basement songs → `status_1` (ROCKER) page → no cash page → `complete` → CONTINUE goes to `career` (new venue);
   - sel_venue focuses small_club;
   - the store button is disabled and cash is hidden.
2. **Easy, fest 5/5:**
   - FINAL camera is **not** used (G6);
   - `win_easy` → endgame → `status_5` without the new-venue box → back to `career`, skipping `complete`;
   - get_status_progress reads "GUITAR HERO STATUS (25 OF 25 SONGS)".
3. **Medium:**
   - first 3★ pass → cash page $100; a 5★ pass of another song → $650;
   - the tier-complete pass adds the status bonus ($100 for status 1, reason `award_N_star_status`);
   - in the store, buy a $600 guitar → `store_bought` → back → autosave;
   - the guitar shows in sel_guitar_edit (Medium) and in multiplayer guitar select, but not in the Hard career.
4. **Store song:** buy `fireitup` on Medium. It appears under "Bonus Tracks" in the Medium list and in Quick Play. It plays with the "unlock" MTV overlay and pays no star money.
5. **Videos:** buy video1 → Options → Bonus Material → it plays (G2).
6. **Characters:** buy Izzy ($5000) and Grim ($8000). Grim is limited to `grim_guitar`. Both are selectable in multiplayer (G5).
7. **Expert arena 5/5:**
   - FINAL_WIN camera;
   - `win_game` → select → Battle Axe screen → select → endgame → `status_6` → cash $4900 (+ stars) → `career`;
   - battleaxe is selectable in every difficulty and in multiplayer;
   - `guitar_red_octane` never appears (G4).
8. **Venue list (G3):** switch venues on sel_venue mid-career. The song list and footer must change.
9. Restart the app. Everything above survives in `career.json` (cash, items, battleaxe, videos, character info, last difficulty).
