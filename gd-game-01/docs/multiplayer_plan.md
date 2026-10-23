# GH1 two-player multiplayer (face-off): spec and implementation plan

[C] = read from the decompilation (`analysis/decomp/*.c`), the ELF, or the shipped data/scripts.
[I] = inferred. Port decisions that have no PS2 counterpart are marked **[port]**.
Sources: `extracted_dta/ghui/gen/{multiplayer,main,splash,quickplay,career,game,hud_panel,track_panel,endgame,pause}.dta`,
`extracted_dta/config/gen/{scoring,beatmatch,player,track_graphics,gh,cheats_funcs}.dta`, `extracted_dta/arena/gen/*`,
`extracted_dta/charsys/gen/{guitarist,charbase}.dta`, `extracted_dta/venues/small_club_multi/gen/camera.dta`, the MIDI files in
`extracted/songs/*/*.mid`, and the existing docs (cited by section). Engine paths are relative to `engine/src/`.

GH1 has one multiplayer mode, **face-off**. Both players play the **same difficulty**. They get the **same chart split into
alternating sections**, each on their own highway. There is **no failing**, the venue is always **small_club_multi**, and the higher
(handicap-normalised) score wins.

---------------------------------------------------------------------------------------------------
## 1. Menu flow

### Original
```
main ─multiplayer─► multi_sel_character ─both done─► multi_sel_guitar ─both done─► multi_selsong
     ─SELECT─► multi_seldiff ─SELECT─► (meta setup_multiplayer) loading ─► game
game ─game_outro─► post_show ─► multi_compete ─select─► complete
complete: selsong → multi_selsong | restart → game | quit → main
```
- **main** [C] `main.dta:65`: `main_multiplayer.btn` → `ui goto_screen multi_sel_character`.
  - Its poll (`main.dta:80-85`) disables the button while `{game is_missing_multi_controller}`.
  - That check is `FUN_00113910`: it returns 0 only if pad 0 **and** pad 1 are both connected (`FUN_00221800(i)+0x20`).
  - `reset_player_settings` (`main.dta:118-130`) runs on every entry to main and sets player 2 to `classic` with guitar index 0. Player 1 is set to `metal`, guitar 0, medium, venue basement.
- **multi_sel_character** [C] (`multiplayer.dta:1-227`, docs/select_panels.md §4):
  - Uses the MultiCharSelPanel. Each pad scrolls its own cursor. The two players can never pick the same character (FUN_0014e4a0 skips the other player's).
  - The `char_multi` CharsysPanel shows both 3D characters at `char_multi0/1.placer`.
  - `chars_selected` → `multi_sel_guitar`.
- **multi_sel_guitar** [C] (`multiplayer.dta:252-547`):
  - Per-pad guitar, then skin selection (`multi_input_guitar`, `(multiplayer TRUE)`).
  - Unlocks are checked with `set_quickplay p TRUE`, i.e. a guitar unlocked in **any** difficulty counts (docs/select_panels.md §3).
  - When both players are done: `set_guitar 0/1` writes PlayerConfig `set_guitar` / `set_guitar_skin`, then `goto multi_selsong`.
- **multi_selsong** [C] (`multiplayer.dta:571-604`):
  - The `sel_song` panel with `(sel_song_career FALSE)`. The panel's enter then does `song_provider set_quickplay TRUE` (`career.dta:1670-1680`), so the song list is the **quickplay list**.
  - SELECT → `multi_seldiff`. Back → `multi_sel_guitar`.
- **multi_seldiff** [C] (`multiplayer.dta:548-570`):
  - The plain `sel_difficulty` panel: **one difficulty for both players**.
  - SELECT → `meta setup_multiplayer`, then `loading`, then the panel's `set_difficulty`.
  - Evidence that the difficulty is shared:
    - GameConfig has a single `+0x90` difficulty.
    - SongDB::LoadSong loads one difficulty (docs/end_of_song.md §1).
    - Every Track reads `track_speed[GameConfig+0x90]` (`00150000.c:5176`).
    - multi_compete prints one `{game get_difficulty_sym}`.
- **setup_multiplayer** [C] (`splash.dta:24-33`):
  - `main_screen main`, `continue_screen multi_selsong`, `win_screen multi_compete`, `lose_screen lose`, `mode multiplayer`.
  - **`game set_venue small_club_multi`** (fixed venue, no venue choice), `game set_multiplayer TRUE` (GameConfig+0x8c).
  - Unlike quickplay's `qp_diff`, it never calls `game set_quickplay`. So the song's `(quickplay (character)(guitar)(venue))` block is **not** applied, and the players keep their picks.
- **Menus accept any pad** [C] (docs/ui_system.md: screen `player_num` defaults to −1). In-game screens swallow player ≠ 0 only in single player (`ui_system.md:141`). In MP, either player can drive song/difficulty select and the pause menu.

### Engine status
- `ui/ui_system.cpp` GameObj:
  - `multiplayer` → 0 and `is_missing_multi_controller` → 1 (lines 947-950), so the MP button is always disabled.
  - `get_player_config 0/1` exists (PlayerConfigObj ×2). MultiCharSelPanel and the 2-slot GuitarSelectPanel exist (`ui/select_panels.cpp:291,408,513`).
- `UiSystem::buttonDown(button, guitar)` always routes `player_num 0` (`ui_system.cpp:2170-2180`). The second pad cannot drive the per-player panels.
- `hooks_.startGame` passes only `players[0]` (`ui_system.cpp:1846-1860`).

### Steps
1. [C] GameObj (`ui/ui_system.cpp`):
   - Add `bool multiplayer`, set by `set_multiplayer B` (also from the `game multiplayer` getter).
   - `is_missing_multi_controller` = `!(input.padConnected(0) && input.padConnected(1))`.
   - `is_missing_controller` = pad0 missing, or (multiplayer and pad1 missing). Use a new `Hooks::padConnected(int)`.
   - `set_venue` already stores `small_club_multi`.
   - Keep `set_quickplay` untouched in MP (the script never calls it).
2. `UiSystem::buttonDown/Up(button, guitar, int player)`: pass the pad slot through `route(... player_num ...)`. All native panels already take `player`.
3. Hooks: replace `startGame(index, diff, venue, character, guitar)` with a `GameSetup` struct:
   - `{song, difficulty, venue, multiplayer, players[2]{character, guitar, skin}}`.
   - `App::startSong` builds 1 or 2 players from it.
4. The `meta` object must keep `mode`, `win_screen` and `continue_screen` from `setup_multiplayer`. They are already generic script variables [I: verify that `meta set/get` round-trips].

---------------------------------------------------------------------------------------------------
## 2. Controller assignment

### Original
- [C] Player i reads **pad port i**:
  - PlayerMatcher+0x14 is the player index.
  - `FUN_00108260` builds the matcher's controller from `game get_controller <index>` (`FUN_001137c0`). That returns `guitar` if the pad is a guitar (`FUN_002218e0`), else `lefty_joypad` if lefty is set for that pad (`FUN_001075e8`), else `joypad`. It re-creates the controller whenever the type changes (hot-swap).
- [C] `game_settings` has separate lefty toggles `gs_left_p1.btn` / `gs_left_p2.btn` (docs/slider.md:275).
- [C] Losing either pad in MP: `game.dta` poll → `pause_controller` with text `controller_loss_msg_multi` (`pause.dta:131-160`). Resume stays disabled until `is_missing_controller` is false again.
- [C] Start from **either** pad pauses (`game.dta:177-186`, no player check).

### Engine status
`input/input.cpp`:
- Merges all pads and the keyboard into one event stream. `InputEvent` has no player/device index.
- `ControllerMode` is per event, but App keeps one `player_`.

### Steps [port]
1. `input/input.h`: add `int player` to `InputEvent`, and a slot table `std::array<SDL_JoystickID,2> slots_`.
   - The first pad connected takes slot 0, the second slot 1.
   - On removal the slot is freed and not compacted, as with a PS2 port.
   - Expose `bool padConnected(int slot)` and `ControllerMode slotMode(int slot)`.
2. Keyboard fallback:
   - The keyboard is **slot 0 when fewer than one pad is in slot 0**. Otherwise it acts as the player of the lowest empty slot, so one pad plus the keyboard can play 2P.
   - Optional split-keyboard dev map behind `--kb2p`: P1 on keys 1-5 + Up/Down + Space; P2 on F8-F12 (or J K L ; ') + RShift strum + RCtrl SP.
   - For `is_missing_multi_controller`, a slot held by the keyboard counts as connected.
3. Per-slot lefty: map `lefty_joypad` per slot from the options toggles. The joypad fret order flips [I: exact lefty mapping from beatmatch_controller.dta].
4. `App::onAction`: route gameplay events to `players_[e.player]`; route UI events with `e.player`. Start from any slot pauses.

---------------------------------------------------------------------------------------------------
## 3. Chart format: how the parts alternate

### Original [C]
- There are **no separate P1/P2 tracks**. The one `T1 GEMS` track carries **player-section notes per difficulty**:
  - note `0x45 + 12·diff` → P1
  - note `0x46 + 12·diff` → P2
  - That is **69/70 Easy, 81/82 Medium, 93/94 Hard, 105/106 Expert**.
  - Evidence: `FUN_00262358` in DifficultyParser (`00260000.c:1425-1460`).
- Parser behaviour:
  - Section note-on sets bit (note−0x45−12·diff) in the running mask `parser+0x68`. Note-off clears it.
  - Any fret slot whose note-on has the **same tick** as the section note gets the new mask too, so a section that starts on a note includes that note.
  - A fret note-on snapshots the current mask into its slot (`FUN_002619d0`, `00260000.c:1080-1090`). The gem's mask is that of the slot whose note-off emits it, stored at gem record +0x20 / GameGem byte +0xe (`FUN_0025b1f0`-path line `00250000.c:7220`).
  - The mask is only maintained when `parser+0x18 > 1`. That field is set from SongData+4 = **number of players** (`FUN_00260d10` → `FUN_002627f8`). **In single player every gem's mask is 0.**
- Ownership test `FUN_0025b688(mask, p)` = `mask == 0 || (mask >> p) & 1`.
  - Mask 0 means **both players** play the gem (no section note held).
  - Mask 3 also means both (overlapping sections).
- The shipped charts use it. Checked with a note-on scan of `extracted/songs/*/*.mid`:
  - evenrats: 56 P1 / 55 P2 sections per difficulty.
  - smokeonthewater: 103/102.
  - spanishcastlemagic: 32/32.
  - All four difficulties have the same section bars.
  - Sections alternate every bar or few bars, with frequent overlaps at the hand-over bar.

### Engine status
`game/chart.cpp:51-55` already parses rel 9/10 into `Gem::players` (always, not only in MP). Nothing reads it.

### Steps
- Keep the parse. In 1P, treat `players` as 0 (all owned) by passing `playerIndex = -1` / `numPlayers = 1` to the consumers below. That is equivalent to the original zeroing the mask [C equivalence].
- Add `bool Gem::ownedBy(int p) const { return players == 0 || (players >> p & 1); }`.

---------------------------------------------------------------------------------------------------
## 4. Player rules in face-off

### Original
- **Judgment**, PlayerMatcher watcher in the `00263xxx–00264xxx` range [C]:
  - A gem the player does not own is never hittable. The strum path (`FUN_00263248`, check at `00260000.c:2194`) needs `!hit && ownedBy(p)`. Otherwise it goes to the overstrum path `FUN_002641e0`.
  - **So strumming during the opponent's section is an overstrum**: the streak breaks, with the usual 50 ms overstrum guard (`+0x5c + 50`).
  - HOPO taps need the previous gem to have been hit by this player (`FUN_002639b8`, line 2530). So the first note of a section is never a HOPO.
  - Passing an unowned gem calls sink slot `+0x3c` (`FUN_00264660`) instead of the miss sink `FUN_002642e8` (`FUN_00263b20`, lines 2586-2600). **No miss, no streak break, no crowd hit** [C path; the slot-meaning name is I].
- **Counts** [C]: `percent_hit` denominator = the gems owned by p (`FUN_0010c608`). A chord counts as 1.
- **Score handicap** [C] (docs/end_of_song.md §1–2):
  - `B[p]` = base score over owned gems (`MultiplayerAnalyzer_v2` @0x131bd0).
  - `score` (P1) = raw. `score` (P2) = `(int)(raw × B[0]/B[1])` (`FUN_00116c80`, `SongDB+0x40`).
  - Stars use the handicapped score against `B[p]`.
- **No fail** [C]:
  - `FUN_0011d3b8` (lose test) and `FUN_0011d378` (warning test) both return false when GameConfig+0x8c is set (`00110000.c:8905-8925`).
  - The crowd rating still updates on every hit/miss (`FUN_001171b8`). Only `lose` is suppressed. There is no rock meter on screen (§5).
- **Song end** [C]: only player index 0 can call `win` (`FUN_00117290`), at `now > end_ms`. One `game_over` → `game_won` for both.
- **Star power** per player, same rules (§7).

### Engine status
- `game/player.h/.cpp`: one `Player(song, difficulty, rules, mode, noFail)`. It judges every gem. `noFail` exists (`player.cpp:328`). There is no handicap and no ownership filter.
- `app.cpp:137` holds a single `player_`.

### Steps
1. `Player(const Song&, Difficulty, const Rules&, ControllerMode, int playerIndex, int numPlayers, bool noFail)`. Store `owned_ = [i] gem.ownedBy(p)` (all true when numPlayers == 1).
2. In the watcher functions (`nearestGem`, `watcherStrum`, `hopoAttempt`, `pendingCheck`, `sweep`):
   - Unowned gems are not candidates for hits.
   - In `sweep`, an unowned gem that passes advances `sweep_` with **no** `onMiss`.
   - A strum whose only candidate is unowned → `overstrum`.
   - HOPO requires `lastHit_ == i-1` (already true in the code) and gem i-1 owned. The first follows from the hit requirement.
3. Totals: `notesTotal()` = owned count. `baseMaxScore(p)` = Σ over owned gems (end_of_song.md formula). Put it in `game/chart.cpp` as `int baseMaxScore(const Chart&, int p)`.
4. `int displayScore() const` = `p == 0 ? score_ : int(float(score_) * handicap_)`, with `handicap_ = float(B0)/float(B1)` passed in by App. Use it for the HUD, results, stars, is_doing_badly and the lead meter.
5. noFail = `multiplayer || opt_.noFail`. The warning flag used by the rails is `!multiplayer && rating < warning` (`TrackRenderer` reads `player.rockMeter()`; add `bool warning()` to Player).
6. `App`: `std::array<std::unique_ptr<Player>,2> players_; int numPlayers_`.
   - Only `players_[0]` triggers the song-end win.
   - `Autoplay` per player (cheat `cycle_multiplayer_auto_play`, `cheats_funcs.dta:451-500`: states none / P1 / P2 / both).

---------------------------------------------------------------------------------------------------
## 5. Multiplayer HUD

### Original [C] (docs/endgame_and_hud.md B1.1–B1.9)
- HudPanel FinishLoad:
  - `hud_single.view` hidden, `hud_multi.view` shown.
  - **No CrowdMeter (rock meter)**, no `youwin.mesh`. A LeadMeter (FUN_00133540) instead.
  - Per player i, the scene index is k = i+1: `message1/2.txt`, `score_meter_1/2.view`, `star_meter_1/2.view` (and `star_meter_group_1/2`, `star_meter_fill_k`, `star_meter_ready_k`).
- Layout (hud.rnd_ps2):
  - score_meter_1/2 at x = −362 / +349, z = 145.
  - star_meter_group_1/2 at x = −365 / +345, z = 170.
  - `multi_hud_group.view` at (−3, 0, 221): the centre **lead meter**, called the tug meter here.
- Poll (HudPanel_v10), per player:
  - StarMeter.Update(energy, deployed).
  - ScoreMeter.Update(handicapped score, multiplier incl. SP ×2, streak bulbs, sp flag).
  - HudMsg poll.
  - `star_power_ready i` script message when the displayed SP level crosses 0.5.
  - Then `LeadMeter.Update(t, score(P0), score(P1))`.
- **LeadMeter** (FUN_00133de0, `gh.dta hud lead_meter max_lead 10000`):
  - `leader = s0==s1 ? 0 : s0>s1 ? 1 : 2`.
  - On a leader change: `multi_hud_logo.mnm` and `multi_hud_middle_glow.mnm` frame = leader. `multi_hud_glow_left.mesh` shown iff leader==1, `_right` iff leader==2. `multi_hud_lens.mesh` mat = `multi_hud_lens{,_left,_right}.mat`[leader].
  - `f = 50 ∓ 50·min(1, |s1−s0|/10000)` (minus when P1 leads).
  - At f == 0 or 100, play the wobble loop `multi_hud_arrow_left/right.tnm` at t·0.03. Otherwise `multi_hud_arrow.tnm` frame f.
  - Reset: leader −1, arrow frame 50.
- `set_in_game B` toggles score/star meters per player and `multi_hud_group.view`.
- `show_message P token` uses HudMsg[P], e.g. `star_ready` per player (`hud_panel.dta:48-56`). `set_win_showing` does nothing visible in 2P (no youwin mesh).
- Slide-in (`meters_slide_in.view` 0→480 over 500 ms):
  - `score_slide_in1/2`, `star_slide_in1/2`, `multi_hud_slide_in.tnm`.
  - P1 x −394 → −225 → −255 → −225 → −235, frames 100..480. P2 is mirrored.
  - The lead meter drops from z 300 to −105.
- `extracted/hud/gen/hud_multiplayer_hud_fix.rnd_ps2` is not referenced; use `hud.rnd_ps2` [I].

### Engine status
`ui/game_panels.cpp` HudPanel:
- Single only. It forces `hud_single.view` on and `hud_multi.view` off (line 76-77).
- Object names are hard-coded `*_0`, `message0`, `rock_meter.view`.
- `Hooks::playerState(int p, …)` and `starMeterShown(p, level)` are already per player.

### Steps
1. HudPanel: `int n = multiplayer ? 2 : 1`. Build per-player sub-objects `ScoreMeterUi`, `StarMeterUi`, `HudMsgUi` with a scene index `k = multi ? i+1 : 0`, i.e. a `"score_meter_%d"` format. Move the current single-player code into these structs unchanged.
   - Create the CrowdMeter only in 1P. Create a new `LeadMeterUi` only in 2P, implementing the formula above.
2. `set_in_game`, `set_score_showing`, `show_message P`, `reset` and the slide-in: loop over players with k. `multi_hud_group.view` replaces `rock_meter.view` in 2P.
3. `star_power_ready i` → `ui_->gameEvent("hud", "star_power_ready", {i})` per player. The sfx uses `play_game_sfx_player … $player_num` with pan −1/+1 (docs/audio_sfx.md:375). Check that `player_pan` is implemented.
4. `playerState(p)` must return `displayScore()` (handicapped).

---------------------------------------------------------------------------------------------------
## 6. Two tracks side by side

### Original [C] (docs/endgame_and_hud.md B2 §1.3, §1.8, §1.9; render_gems_dof.md)
- TrackPanel `reset` builds one `Track(i, surfaceTex[i], dual)` per player (FUN_0015b5d0).
  - The **surface texture is per player's character** (`characters.dta track_surface`, FUN_0015b0b8).
  - Each Track has its own gem manager, smashers, beat lines, lightning, extend camera (`track.cam` copy) and explode view.
- Track ctor, when GameConfig.multiplayer (`00150000.c:5196-5204`), with `track_graphics.multiplayer`:
  - `use_dual_perspective TRUE` (the data default) → **FUN_001592e8**:
    - Scale the track-graphics view by **0.76** (`dual_perspecitve track_scale`).
    - Camera screen rect `x = 0` (P1) / `0.5` (P2), `y 0`, `w 0.5`, `h 1` (cam +0x314..+0x320).
    - Camera fov field = `fov × 0.5` (`FUN_001b1df8(near, far, fov*0.5)`).
    - Projection `FUN_001b1eb8`: `x = 1/tan(fov/2)`, `y = 1/(tan(fov/2)·aspect)` with `aspect = (h/w)·screenAspect`.
    - Net result [I]: each half-screen sees the single-player framing at the same pixel scale (the half fov offsets the half-width viewport), shrunk by 0.76, and centred in its half.
  - Without dual perspective → FUN_00159418: one shared full-screen camera, track offset **(±8.85, 0, 0)** (negated for P1, so P1 is on the left), scale 0.85. Cheat `toggle dual perspective` (`cheats_funcs.dta:540-550`).
- **Tails: 20 segments in MP** (30 in 1P), segment length 120/N (`FUN_001633d8`, `00160000.c:2024-2029`).
- **Horizon fade**: `track_graphics fade multi` → (128,236) for P1 and (384,236) for P2, instead of (256,216) (FUN_00158838).
- Smasher set (FUN_001643b0, `00160000.c:2846-2851`): `combo2_angle` (12°) for `smash_combo_lightning.view` applies only in 1P. In MP the angle is 0.
- Hit-effect views are deep-copied for players after the first (docs/hit_fx.md:39).
- Gem list per track (`FUN_0015d840`, `00150000.c:8946-9015`):
  - Every chart gem gets a record. **Unowned gems get fret mask 0**, so nothing is drawn [I: a 0-fret record draws nothing], and no HOPO flag.
  - So each highway shows only that player's notes. During the opponent's section the highway is empty except beat lines.
- Script `track_panel` messages (`pop_smasher`, `set_smasher_glowing`, `set_frame`, `explode`, `set_showing`) apply to **every** track. Per-player `track` messages (`set_bonus_gems`, `on_miss_phrase`, the SP shake `game anim_task track 800 2000 2025`) go to the player's own track through the player namespace (PlayerMgr `with_player_namespace`).

### Engine status
`render/track_renderer.*`:
- One instance. It draws with `track.cam` at full-screen aspect (`track_renderer.cpp:154`).
- `kRows = 30` (line 47).
- Surface texture fixed. No per-player ownership.
- The Renderer has no sub-viewport API (`render/renderer.h:44-52`).

### Steps
1. `Renderer`: add `pushViewport(float x, float y, float w, float h)` / `popViewport()` in normalised units of the current (wide) viewport. Return the sub-aspect.
2. `TrackRenderer(renderer, data, int playerIndex, int numPlayers, bool dual)`:
   - Clone state per instance; App owns `tracks_[2]`. The scene can be shared read-only if `SceneRenderer`/`AnimPlayer` state is per instance. Simplest is two full instances; the memory cost is small.
   - `kRows` → member `rows_ = numPlayers > 1 ? 20 : 30`.
   - Surface texture override = the player's character `track_surface` bmp (load `track/surfaces/<char>.bmp` through `GameData`; also a 1P gap, endgame_and_hud.md §1.12 #8).
   - Gem loop: skip `!gem.ownedBy(p)` for drawing. The HOPO look must use the owned-previous rule: `hopo && prev owned`.
   - Dual: `pushViewport(p*0.5, 0, 0.5, 1)`. Camera from `track.cam` with fov × 0.5 and aspect = 2·screenAspect. Model scale 0.76 on the track-graphics root (apply to every mesh matrix and the gem/beat-line translations).
   - Non-dual fallback: full viewport, translate x ∓8.85, scale 0.85.
   - In MP, disable the smash_combo_lightning angle (0°).
3. `App` hooks: `trackMessage` fans out to all tracks. Per-player SP shake (`trackFrame`) needs a player index: give `game anim_task track …` from the player namespace the target `track<p>` [I: route by the `$player_num` in the handler's scope].
4. Horizon fade: if/when implemented, use the per-player fade centre.

---------------------------------------------------------------------------------------------------
## 7. Star power in multiplayer

### Original [C]
- Identical per-player StarPower objects (`scoring.dta star_power`: ready 0.5, deploy_rate 0.125, phrase 0.25, ×2, whammy …).
- Each player deploys independently with their own tilt/select.
- Phrases are the shared note-103-style phrases (rel 7) of the one chart. A player can only complete a phrase from the gems they own [I: the phrase check walks the gem list, and unowned gems are neither hit nor missed for that player; verify `FUN_0010c5d8` against masks].
- Handlers (`config/player.dta`) run in the player's namespace:
  - `start_using`: own track shake, own `set_bonus_gems`, `sp_deployed_multi` panned to the player, `crowd_upto_good`, and the player's guitarist `start_starpower`.
  - The sfx use the `_multi` variants: `sp_awarded_multi`, `sp_available_multi`, `sp_deployed_multi`, `sp_depleted_multi` (docs/audio_sfx.md:423-426).
- The deploy gate uses the HUD's displayed meter (FUN_00132dd0) per player.
- Side rails: blue when SP is ready, never red in MP (`FUN_0011d378` is false in MP).

### Engine status
SP exists for one player. `setHudStarLevel` exists. Single `guitaristStarPower` hook.

### Steps
- Per-player Player events: `App::handlePlayerEvents(p)` dispatches with `player_num p` and the `_multi` sfx when multiplayer.
- `guitaristStarPower(p)` → `venue_->startStarPower(p)`.

---------------------------------------------------------------------------------------------------
## 8. Venue, crowd, camera, lights, audio

### Original
- Venue is always **small_club_multi** [C] (`splash.dta:30`). It is `(multi_only TRUE)` in `arena/venues.dta:220-251`, so it never appears in career/quickplay.
  - `allow_walks FALSE`, `lose_teleport FALSE`, crowd sound bank `small_club_multi`.
- **Camera** [C]:
  - `load_scripts.dta:24-27` loads `camera_multiplayer.dta` instead of `camera.dta`. There are **no excitement-driven shot changes**.
  - intro_start/skip → `$camedit.MULTIPLAYER` (shot `multi01`, Cam_nt_np_close).
  - `active_players_changed` (`camera_multiplayer.dta:18-47`):
    - `$active_players` 0 or 3 → MULTIPLAYER (both).
    - 1 → `MULTIPLAYER_0` (`multi02`, hide_crowd, the P1 close shot).
    - 2 → `MULTIPLAYER_1` (`multi03`, hide_crowd).
  - Shot parameters: `venues/small_club_multi/gen/camera.dta`.
  - There is no game_over handler, so the shot stays at the end (docs/camera_endgame.md:188).
- **active_players** [C]:
  - `FUN_00258b20(now, songpos)` walks the difficulty gem list. For each gem whose start time ≤ now, if its mask ≠ the last sent mask (initially −1, `00250000.c:5662`), it calls listener slot +0x2c with the mask.
  - BeatMatch's listener `BeatMatch_v4` @0x10f110 sets `$active_players = mask`, calls `FUN_00109d90` → the guitarist's `FUN_00284058(mask)` for each player, and sends **`active_players_changed`** to the arena.
  - So the camera cuts **when the first gem of a new section reaches the now line**, not at the section note.
- **Crowd excitement** [C]: `ArenaPanel_v10` → `FUN_0010d370` uses **player 0's** crowd rating. The crowd keeps reacting to P1 only:
  - rating < boot threshold → Boot.
  - < 0.33 → Bad.
  - < 0.66 → Okay.
  - otherwise Great.
- **Lights** [C] `arena/venue.dta:121-140`: on downbeat after a section change, `set_lights_per_excitement kExcitementGreat` when `{game multiplayer}`. Venue lights are pinned to the Great state in MP.
- Crowd 3D: small_club_multi shows nothing below Great (docs/camera_walk_crowd_lighting.md:756). The crowd still uses the P1-driven excitement [C arithmetic in that doc].
- **Audio**:
  - The `_multi` sfx are panned −1 (P1) / +1 (P2) by `play_game_sfx_player` [C].
  - Guitar-stem unmute in MP uses `beatmatch.dta audio unmute (multiplayer_scale 0.33)(multiplayer_attenuation 1.5)(multiplayer_common_attenuation 1.5)` (dB; `FUN_002662b0` converts with 10^(−x/20)) [C config read]. The exact mix formula (UnmuteBeatMatchAudio, `0x264958..0x268328`) is **not decoded** [I].
  - Interim [I]: the guitar stem is audible if the owner(s) of the current section are not in a miss state. With both players, each player's miss attenuates by 1.5 dB.

### Engine status
- `render/venue_renderer.cpp` and `game/venue_camera.cpp` run `camera.dta` logic.
- No `active_players` event. `VenueScript::excitementFor(player_->rockMeter())`.
- `small_club_multi` loads as a venue, untested in MP.

### Steps
1. `game/song_pos` (new, small) or inside `App::updateGameEvents`: track `lastMask_ = -1`. Each frame, for owned-agnostic gems with `ms <= now` past the cursor, if `gem.players != lastMask_` → `venue_->activePlayersChanged(mask)` and set the arena variable `$active_players`. Run only in MP; in 1P masks are 0 and the original sends one event with 0 [C], which is harmless.
2. `VenueCamera`: when multiplayer, load `arena/gen/camera_multiplayer.dta` handlers instead of `camera.dta`:
   - Implement `intro_start`/`intro_skip`/`active_players_changed` by evaluating `$camedit.MULTIPLAYER*`.
   - Disable the excitement shot picker and the WIN/LOSE shots.
3. `VenueScript`: expose `{game multiplayer}` = true so venue.dta pins the lights to Great. Excitement stays from `players_[0]->rockMeter()`.
4. Mixer: per-channel gain from the rule above; mark it TODO until UnmuteBeatMatchAudio is decoded.

---------------------------------------------------------------------------------------------------
## 9. The guitarists

### Original [C]
- MyCharSys creates **`guitarist%d` per player** (`MyCharSys_v10` @0x280fc0, string 0x340c90), using that player's PlayerConfig character, guitar and skin. Player 1 is guitarist0, player 2 is guitarist1. The rest of the band is unchanged.
- **Placement** (`FUN_00281cd8`): with 2 guitarists, player k (1-based) stands on walk spot `k % 2`:
  - **P1 → walk_spot_02** (−17.3, 50.5, 21.3), screen-left [I: the camera looks +Y from the crowd].
  - **P2 → walk_spot_01** (36.2, 52.0, 20.7), screen-right.
  - Both face −Y (docs/camera_walk_crowd_lighting.md §2 table).
- **No walks** in MP: MyCharSys_v10 clears the enable bit on every kWalk* clip. small_club_multi also has `allow_walks FALSE`.
- **Hands/activity**: `FUN_00286298` sets guitarist+0x90 "player active" = `mask == 0 || mask & (1 << player)` on every `active_players` change. The idle player's hands stop playing (docs/guitarist_hands.md:111, 365).
- **Mood**: `is_doing_badly` (`charbase.dta:209-236`) in MP = `other.score − my.score > base_max_score(0)/15`, which selects kGuitarBad. `score_diff` is set at init (`charbase.dta:177-202`).
- **Game over** (`guitarist.dta:41-89`): each guitarist gets `set_game_over (my score >= max(score0, score1))`.
  - The winner plays kGuitarWin, the loser kGuitarLose. On a draw, both win.
  - `game_outro` fires when guitarist0's clip ends (docs/end_of_song.md §3); arena_game.dta guards it to once.
- Camera focus character: MULTIPLAYER shot keys `Left` → 0, `Right` → 1, else −1 (docs/camera_endgame.md:49).

### Engine status
`render/venue_renderer.h` has one `guitarist_` (`Band{guitarist, guitar}`) and one `GuitarWalk`. The start spot comes from `walk_->startSpot()` (`venue_renderer.cpp:164`).

### Steps
1. `VenueRenderer::Band` → `std::vector<Member>{archetype, guitar, skin}`; `guitarists_[2]`.
   - Place guitarist i at spot `(i+1) % 2` when there are 2. No `GuitarWalk` in MP, or construct it disabled.
   - Each guitarist gets its own GuitarAnimInfo (the same song hand events).
2. `activePlayersChanged(mask)` → `guitarist[i]->setActive(mask==0 || mask>>i&1)`. Hit/miss "active" per player comes from that player's events.
3. `isDoingBadly(i)` callback: in MP use the handicapped scores and `B0/15`.
4. `gameOver`: in MP compute per-guitarist `won_i = score_i >= max`. `game_outro` from guitarist0 only.
5. Camera "focus character" indices for the MULTIPLAYER shots (eyes, head snap) → guitarist 0/1.

---------------------------------------------------------------------------------------------------
## 10. End of song and the results screen

### Original [C]
- Win path as in 1P (no fail): `game_over` → `game_won`:
  - `track_panel explode` (both tracks), `hud set_in_game FALSE` (hides the score/star meters and the lead meter), `beatmatch set_paused TRUE`.
  - `game_outro` → `post_show` → `{meta get win_screen}` = **multi_compete**.
- GamePanel_v13 preloads `multi_compete` instead of `endgame`. GamePanel_v15/16 load `photo_<char_i><rand 0..3>.bmp` for **each** player into `winner_photo0/1.tex` (docs/endgame_and_hud.md §4).
- multi_compete (`multiplayer.dta:605-786`, docs/endgame_and_hud.md "multi_compete"):
  - `crowd_audio turn_off`; `postshow_loop` after 300 ms. The screen sets `game set_paused TRUE`.
  - `winner` = s0>s1 ? 0 : s0<s1 ? 1 : −1, using the handicapped `playerN::player score`.
  - `winner_photo<winner>.mesh` shown (neither on a draw).
  - `me_winner.lbl` = PLAYER_1_WINS / PLAYER_2_WINS / DRAW.
  - Song, artist, difficulty.
  - Per player: score, `me_percent_format` percent_hit, longest_streak, star_rating (asterisk text).
  - Select (`meta is_select_button`, any pad) → `complete`.
- `complete`:
  - selsong → `meta_loading_goto multi_selsong` (back in MP; characters and guitars are kept).
  - restart → `game restart`, same setup.
  - quit → main, which resets the player settings.
- Pause → quit goes to `continue_screen` = multi_selsong.

### Engine status
- `ui/ui_system.cpp:2183-2197` loads only player 0's photo into `winner_photo0.tex`.
- `PlayerStatsObj` registers only `player0::player`. `hooks_.playerStat(stat)` has no player index.
- `App` draws a fallback results screen for one player (`app.cpp:918-930`).

### Steps
1. `Hooks::playerStat(int p, stat)`. Register `player1::player` as a second PlayerStatsObj when multiplayer. `foreach_player` iterates both (`PlayerMgrObj`, `ui_system.cpp:531-550`).
2. `loadWinnerPhoto`: loop over players → `winner_photo%d.tex`.
3. `GamePanel` preload: `multi_compete` when multiplayer (the script handles the rest).
4. `App`'s non-UI fallback results: show both columns and the winner text.

---------------------------------------------------------------------------------------------------
## 11. What gets saved

### Original
- **Nothing MP-specific** [C for the scripts]:
  - multi_compete goes straight to `complete`. There is no `highscore` step (that is quickplay's endgame path, `endgame.dta:331`) and no `campaign` call (career is `meta setup_career` / `finish_song` only).
  - The player-1 high-score table and the career progress are untouched.
- `meta_loading_goto` passes through `meta_loading` → `autosave_goto` (`pause.dta:93-122`). If `$autosave` is on, the profile is written as usual with unchanged data [C flow; I that nothing dirty is saved].
- The character/guitar picks live in GameConfig PlayerConfig (RAM) and are reset at `main` [C `main.dta:118-130`].

### Steps
- Make sure the engine's result handling skips `Campaign` updates and `highscore` when multiplayer. Nothing new to persist.

---------------------------------------------------------------------------------------------------
## 12. Implementation order (files and classes)

1. **Input slots** (`input/input.{h,cpp}`): `InputEvent::player`, slot assignment, `padConnected`, keyboard fallback, optional `--kb2p` [port].
2. **UI plumbing** (`ui/ui_system.{h,cpp}`):
   - `buttonDown/Up(..., player)`.
   - GameObj `multiplayer` / `set_multiplayer` / `is_missing_*`.
   - `Hooks::GameSetup` and `playerStat(p, …)`.
   - `player1::player`, both winner photos.
   - Main's MP button then enables itself through the script.
3. **Rules** (`game/chart.{h,cpp}`, `game/player.{h,cpp}`):
   - `Gem::ownedBy`, `baseMaxScore(chart,p)`.
   - `Player(…, playerIndex, numPlayers, noFail)` with the ownership filter in the watcher, the owned-gem totals and `displayScore()` handicap. No-fail and no-warning in MP.
4. **App** (`app.cpp`):
   - `players_[2]`, `tracks_[2]`, per-player event dispatch with `player_num`.
   - Only P0 ends the song.
   - Per-player hit effects and SP.
   - `active_players` tracking.
   - The P0 rating drives excitement.
5. **Tracks** (`render/renderer.{h,cpp}` sub-viewport; `render/track_renderer.{h,cpp}`):
   - Per-player instance, dual-perspective camera (fov×0.5, rect half, scale 0.76).
   - 20 tail rows, ownership filter, per-character surface, combo angle 0.
6. **HUD** (`ui/game_panels.cpp` HudPanel): the per-player k-index refactor, LeadMeter, multi slide-in, no rock meter.
7. **Venue** (`render/venue_renderer.{h,cpp}`, `game/venue_camera.cpp`, `game/venue_script.cpp`, `game/guitar_walk.cpp`, `scene/character.*`):
   - 2 guitarists on spots 02/01, no walks, active mask.
   - MP camera script.
   - `{game multiplayer}` for the lights.
   - Game-over winner/loser.
8. **Results**: multi_compete (script-driven, needs the step-2 hooks) and the fallback screen.
9. **Audio**: panned `_multi` sfx; interim stem-mute rule; later decode UnmuteBeatMatchAudio.
10. **Self-test** (`--selftest`):
    - Load evenrats in MP with autoplay P1 only.
    - Assert that P2 misses 0 owned gems, that P1's percent denominator equals the owned count, and that the handicapped P2 score equals raw × B0/B1.
    - Assert that active_players changes coincide with section boundaries.

---------------------------------------------------------------------------------------------------
## 13. Open items to verify
- [I] A 0-fret gem record (an unowned gem) draws nothing on that player's track. Verify that FUN_0015df70/FUN_0015fa58 skip it, or render a "ghost".
- [I] StarPhrase completion with mixed-owner phrases (`FUN_0010c5d8` vs the gem mask).
- [I] UnmuteBeatMatchAudio mixing with `multiplayer_scale` / attenuations.
- [I] Lefty per pad (`lefty_joypad` mapping) and whether lefty also mirrors the highway (GH1: no evidence of mirroring found).
- [I] The exact `player_pan` values (docs/audio_sfx.md says −1/+1).
- [I] Which pad confirms in shared menus when both press on the same frame (the first event wins).
