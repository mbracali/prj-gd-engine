# Guitar Hero: Metallica (Xbox 360): findings

Each line **[C] address / data** (confirmed on our disc) or **[I]**
(inferred). Addresses are in our `default.xex` (title ID 41560830).

## Executable

- XEX2, title ID 41560830, media ID 2E0C8E5F, base 0x82000000, entry
  0x821B8BF8 [C: XEX headers].
- Ghidra: 30,337 functions after `FixXexFunctions.java`, 9,246 strings,
  94,894 data references [C: export of 2026-11-10].

## Data

See `formats.md` (CHNK, padded paks, QB struct items, type 0x1C).

## Front end

- UI state machine and descs: see `ui.md` (task_menu prefixes [C 0x82025698
  .. 0x820256E4, 0x823090D8 .. 0x8230A7C0, 0x82309028]).
- `default_lag_settings.xenon`: gem_lag_ms -45, input_lag_ms -61 (GH3's
  xenon_gem_offset / input_offset were -66) [C data].
- Health tables per instrument and difficulty (beginner .. expert) [C data].

## Audio

- Song banks: key generator [C 0x820E24F8], called for "%s_%d", n = 1..3,
  by [C 0x820E6948] (opening "music/%s"); previews by "a%s_preview.fsb"
  [C 0x820E27E8] (another path, not done).

## Multiplayer (2026-11-23 battle-mode deep dive)

Local multiplayer (co-op career, Face-Off, Pro Face-Off, Battle) is modelled
in `game/match.cpp` + `game/player.cpp`; the two-player HUD in `render/hud2p`
and the gh4 HUD in `ui/hud_widgets` / `ui/song_hooks`. The MP mode-select
offers FACE-OFF, PRO FACE-OFF, BATTLE, CO-OP [C data `menu_mp_select_mode.qb`
checksum list `[$dont_care,$p2_faceoff,$p2_pro_faceoff,$p2_battle,$p2_coop]`].
`--selftest` auto-plays co-op / face-off / battle per chart.

### BUG (fixed): Battle mode never started -- wrong power-up set

- Original: `battlemode_powerups` (`scripts\guitar\guitar_battle.qb`) on our
  disc holds **nine** power-ups, by `name`: `Lightning`, `DifficultyUp`,
  `DoubleNotes`, `PowerUpSteal`, `LeftyNotes`, `BrokenString`, `WhammyAttack`,
  **`Mines`** (`scr=battle_mine_attack`), **`blackout`** ("Hit The Lights",
  `scr=battle_blackout_attack`). GH3's eighth, **DeathLick**, is gone and
  Metallica has **no battle sudden death** (no `death_lick` / `sudden_death`
  in `guitar_battle.qb`). `max_num_powerups=3`, `battle_do_or_die_attack_scale
  =1.0`, `battle_*_drain_offset = {0,1500,3000,4500}` [C data, our disc].
- Port discrepancy: `match.h` hard-coded GH3's eight-attack enum
  (`...,kWhammy,kDeathLick,kAttacks=8`) and `BattleData::load` returned
  `powerups.size()==kAttacks(8)`. With Metallica's nine entries `load` returned
  false, so `Match::start` failed and **Battle mode could not begin at all**
  (err "the battle tables ... are missing"). The self-test silently skipped it
  (0 battles / 0 attacks / 0 KOs). The `kDeathLick=7` enum slot also collided
  with Mines (index 7), and index 8 (blackout) fell through to no-op.
- Fix: attacks now mapped **by `name`** into a canonical enum
  (`...,kWhammy,kMines,kBlackout,kAttacks=9`), `BattleData::load` fills a
  nine-slot table and requires every attack present (unknown names skipped,
  not fatal). The seven shared attacks (indices 0-6, byte-identical to GH3)
  are unchanged. `startAttack` adds `kMines` (victim mine window) and
  `kBlackout` (victim highway-dark window + Easy `params.easy_drain`); the
  DeathLick sudden-death select line and the DeathLick fire-bolt/heartbeat are
  removed. `song_hooks` iterates all nine, drops the DeathLick heartbeat, and
  plays `GH_SFX_BattleMode_Mine_Release_P<n>` for Mines.
  Files: `game/match.h`, `game/match.cpp` (`BattleData::load`, `select`,
  `fire`, `startAttack`, `battleFrame`), `ui/song_hooks.h/.cpp`,
  `tests/selftest_play.cpp` (start failure now hard-fails).
- Test: `--selftest` battles 0 -> **102 (2745 attacks, 7 KOs)**, `charts
  failing 0`, co-op/face-off unchanged. (31 FAILED are pre-existing missing
  DLC `.fsb.xen` audio, unrelated.)
- Still to do (grounded, handed off -- touches note-track / highway render):
  **Mines** place mine gems on the victim via `FillInputArrayForBattle
  player=.. attack=mine_attack` and cost health when strummed -- the gem
  placement + strum damage are not yet reproduced (only the card/drain/HUD
  book-keeping + window are). **blackout** darkens the victim's highway
  (`GHM_BattleMode_HitTheLights_*`, `blackout_highway`) -- the Match sets the
  `blackoutUntil` window; the highway renderer must read it to go dark.

### BUG (confirmed, NOT fixed -- separate lobby/flow subsystem): GD10-071

- Local multiplayer cannot be reached by a player: the MP lobby advances
  boot -> main menu -> `uistate_select_controller` -> `uistate_select_mp_mode`
  -> `uistate_character_hub`, then **never proceeds to `uistate_setlist`** when
  both players ready up (Start + P2 Start). Reproduced with
  `--walktest battle` (and faceoff/coop go through the same hub). The gh4
  band-mode lobby (`ui_band_mode.qb`, `ui_character_hub.qb`) ready->proceed
  transition is not driven. Script-driven flow bug, distinct from the in-game
  rules above; the dev hook `GD10_DEV_GAME_MODE` bypasses the lobby. Left for
  routing (flow/front-end), not fixed here to avoid a risky blind flow change.
## Career mode (RE 2026-11-23)

Deep-dive of the original career flow (data in `qb.pak.xen`:
`scripts\guitar\guitar_progression*.qb`, `...\menu\career_menu_flow.qb`,
`...\menu\states\layouts\ui_game_mode.qb`,
`...\ui_singleplayer_character_hub.qb`, `...\cas\ui_character_selection.qb`;
dumped with `tools/qbscript.py dis` and `tools/qb.py`). The career is
**script-driven**: the engine interprets the disc's QScript; the port's C++
supplies natives and the atoms engine. All claims below are [C scripts /
data] unless marked [I].

### Original progression model (differs from the GH3 model the code assumes)

- Metallica career is **gig/tier + star based**, not GH3's "win N songs".
  Each part has a `GH4_Career_<part>_Progression` atom list with only three
  atoms: `set_initial_states` (`Progression_Career_<part>_Init`),
  `<part>_career_gig1_complete` (`Progression_Career_Gig_Complete`, condition
  `scr Progression_Career_Check_Gig_Complete {gig=1}`) and
  `end_of_first_update`. No `depends_on` atom chains, no per-difficulty
  `required` arrays, no encore/boss atoms — so the port's `atoms.cpp`
  difficulty-`required`/encore/boss/relock paths are **dormant** for gd10.
- **Song complete** = `GetGlobalTags <song_checksum> params=stars` with
  `stars > 2` at any of the 5 `difficulty_index` values, i.e. **>= 3 stars at
  any difficulty** (`Progression_Career_Check_Gig_Complete`). A gig completes
  when every song in `tier<gig>.songs` is complete.
- **Tier unlock** = cumulative stars: `progression_check_for_total_stars`
  returns true when `tier.stars <= progression_total_stars_unlocked`;
  `progression_get_total_stars` sums each song's best-difficulty stars over
  all tiers (minus boss songs). `GH4_Career_Guitar_Songs` has `num_tiers = 9`,
  `prefix = 'career_guitar'`, per-tier `stars`/`encore_stars` thresholds
  (tier2 6, tier3 16, tier4 26, tier5 36, tier6 50, ...).
- Per-song-per-difficulty tag key: `format_globaltag_song_checksum` =>
  `FormatText '%s%p%d' s=<songname> p=<part text_nl> d=<difficulty text_nl>`
  hashed (`addtostringlookup`). Writes (`Progression_SongWon`,
  `update_song_star_rating` -> `player<i>_status.stars`, min 3) and reads
  (gig check) use the same script, so the checksum is self-consistent.
- Entry: main menu CAREER -> `main_menu_select_career` (`change game_mode =
  $p1_career`) -> `uistate_game_mode`; SINGLE -> `game_mode_select_career`
  (no `$band`: `game_mode=$p1_career`, 1 player), BAND (`$band`:
  `game_mode=$p2_career`, co-op, `uistate_band_mode`).

### BLOCKER: career mode is currently unreachable in the port (out of scope)

- `ui_create_game_mode` sets `first_character_hub_enter = 1` on every career
  entry. After instrument+difficulty the flow pushes
  `UIstate_singleplayer_character_hub`; `init_singleplayer_character_hub`
  only sets `first_character_hub_enter = 0` (and proceeds, returning `$true`)
  when `GetGlobalTags <savegame> last_singleplayer_character != none`.
  Otherwise it spawns `character_hub_continue_to_selection`, which waits for
  `menu_change uistate_character_selection`.
- `uistate_character_selection` focus runs `new_custom_char_focus` ->
  `generate_random_appearance` / `cas_queue_add_request` (CAS,
  `scripts\game\cas\cas_build_queue.qb`). `last_singleplayer_character` is set
  **only** by CAS paths (`choose_character_to_play`,
  `exit_save_changes`/`exit_skip_save`); `reset_globaltags` sets it to `none`.
- The port **does not implement CAS** (confirmed `tests/walk.cpp:1205` "the
  port builds no CAS characters yet, cas_queue never empties"; the `cas_*`
  scripts have no native support). Observed with `engine/build/linux-video`
  (video build reusing the repo's ffmpeg prefix), `--walktest career`:
  `uistate_select_difficulty` -> `UIstate_singleplayer_character_hub` ->
  `uistate_character_selection` (focus hits `cas_queue_add_request`) ->
  `menu_back uistate_select_difficulty`, looping; the walk never reaches the
  setlist/song. `--walktest quickplay` passes on the same build.
- Consequence: because the **first-ever** career entry can never set
  `last_singleplayer_character` without CAS, all of single-player career
  (hub, gig board, venue, setlist, the progression above, cash rewards,
  autosave) is unreachable. This is a **character-creation/selection**
  dependency (another agent's domain), not a defect in the career logic.
- Candidate minimal unblock (for the CAS/character agent to decide, NOT done
  here): on career entry with no custom character, seed
  `last_singleplayer_character` to a premade character (the script already
  falls back to `judy` via `profile_exists`), analogous to the existing "The
  Glitches" band stand-in; needs a premade singleplayer character that the
  port renders, and `cas_queue_new_character_profile` to no-op cleanly.

### Port notes (not player-visible bugs; for cleanup/other agents)

- `ui/glitches.cpp` `ensureGlitches()` (+ its `unlockList`, GH3-era names
  `GH3_Career_Songs`, `careertier`, `num_songs_to_progress`,
  `encore_unlocked`, `boss_unlocked`) is **dead code** — no caller; the port
  unlocks The Glitches via the original `unlock_all_profiles` /
  `purchase_all_profiles` scripts (`front_end.cpp:265`). Those GH3 names are
  not in gd10's data.
- `script/natives_core.cpp` `GetGlobalTags`/`SetGlobalTags` (lines 433/459)
  ignore the `savegame` and `all_active_players` params; fine for the single
  profile (read/write collapse to one namespace, so the gig star-check stays
  consistent), but would matter for band/multiplayer careers that write per
  savegame (note for the multiplayer agent).
- `SetProgressionMaxDifficulty` is a no-op stub (`natives_flow.cpp:67`); not
  exercised by the gig model above.
- `ui/front_end.cpp:84-98` forces `game_mode=p1_career` on the main-menu
  CAREER choose. The original `main_menu_select_career` already sets it; the
  C++ sets it before the game_mode screen's Single/Band choices are built
  (timing), so it is belt-and-suspenders, not required for correctness.
## Venue lighting (2026-11-23)

The venue lighting model (group lights -> stage/band/crowd shader registers,
blend modes, spotlights, light volumes, the dark-venue question GD10-040) is
audited faithful; `render.md` is the reference. The core selection/SH helpers
Function_824D7220 / 8240F560 / the SH weights are VMX128 and do not decompile
(`halt_baddata` / decompile-failed), so the port follows the reconstruction
there. Ruled out as non-bugs: group intensity (`groupScale` = 1 -- the
`SetLightGroupIntensity` command is only in the dev `lighttool.qb`, never the
runtime; no zone defines a per-group intensity, default 1) [C]; `Venue_Pulse*`
(the note-reactive housing-scale pulse) -- vestigial, wired to no Metallica
housing [C].

Fixed -- the light-show mood processors (`render/venue_lights.cpp`
`enterMood` / `applyNote` / new `cyclesOnSnapshotChange`). The processor of a
mood/performance entry resolves through `LightShow_SharedProcessors` (the only
table `LightShow_SetProcessors` registers; no zone has a per-venue
`<zone>_lightshow_processors`) [C qb.pak; all_dis.txt ~44584]. Per processor
`screnter`/`screvent`: default_generic = GenericMood_Enter / _Event, Poor =
Poor_Enter / _Event (no cycle on event), Blackout/Flare = *_Enter (save=false)
/ empty, Strobe_Generic = **GenericMood**_Enter / _Event (NOT the orphaned
`LightShow_Strobe_*`, which nothing references), Proc_*Intro = GenericMood_Enter
/ nullscript. A mode entry with no inline `processor` defaults to
default_generic [C].
- Bug A: the port read `processor` inline and did nothing when absent, so the
  medium/good generic moods (intro, wash, prelude, exposition, verse, chorus,
  ...; ~40% of every zone's entries carry snapshots but no processor) never
  played or cycled. Original: default_generic cycles on enter and on
  snapshotchange (notes 57/58).
- Bug B: the port modelled Strobe_Generic as a special strobe (SetTime
  disabled, driven by note 60 `strobetoggle`, intensity forced to 1.0) from the
  orphaned scripts; the shared table maps it to GenericMood, so strobe cycles
  through its snapshots on snapshotchange and `strobetoggle` is ignored (no
  processor consumes it) [C].
- Bug C: the port cycled Proc_*Intro (z_donnington's "One") on snapshotchange;
  their screvent is nullscript, so they cycle on enter only.
  Objective (venuetest, swings the rock meter through poor/medium/good; counts
  snapshot changes; identical at 60/240 fps): z_forum/entersandman 158 -> 751,
  z_mop/MasterOfPuppets 574 -> 1534. `GD10_LIGHTSHOW_LEGACY=1` restores the old
  behaviour.

Open (grounded, not fixed): Blackout_Enter / Flare_Enter append
intensity=0.25 specularintensity=0.25 only in z_soundcheck(_practice) /
z_training / z_viewer (non-quickplay venues) [C lightshow.qb]; the port does
not, so those venues go fully dark on those moods. `resetcycleonmoodchange`
(notes 87/88) is not modelled.
## Character animation / collision / physics audit (2026-11-23)

Deep RE + objective re-verification of the band performance animation,
accessory collision and ragdoll physics. **Verdict: faithful; no groundable
behavioural defect found.** One instrumentation addition (penetration
measurement). Details and evidence below.

### Collision shapes — faithful, no "Other", no gap

Parsed the virtual fixups of every shipped Havok-5.5 ragdoll in
`PAK/global_rag_skel` (20+ `.rag`, LZSS-unpacked). **Every rigid body's shape
is `hkpCapsuleShape` or `hkpConvexTranslateShape -> hkpBoxShape`** — no body
decodes as `Shape::Other`, so no accessory loses its collision proxy. The box
-> capsule conversion in `data/havok.cpp` `Pack55::readShape` is faithful for
the thin accessory rods (e.g. box half-extents (0.089, 0.010, 0.010) -> capsule
r = 0.011 ≈ the box thickness); it is not inflating flat panels. The 4.6 path
(`Pack`) keeps real `Shape::Box`, but gh4 uses only the 5.5 path. [C: data,
`rag55.py` enumeration of the object classes/shapes across all band `.rag`.]

### Ragdoll constraint parse — correct (self-consistent)

Some `.rag` genuinely tie an accessory body to a non-skeleton-ancestor anchor
(`Rob_original`: `Bone_ACC_Head_09` -> `Bone_Ankle_R`; the generic
`Rocker_Male`: every ACC bone -> `Bone_Ankle_R`), while others are sensible
chains (`Kirk_original`: `Bone_ACC_Head_09` -> `_08` -> `_07`). Because the
**same** `Pack55` entity offsets (ci+0x14 / ci+0x18) yield Kirk's correct
chain, the parse is proven correct and the odd parents are genuine shipped
data, not a mis-read. `band_ragdoll.cpp init()` deliberately keeps such a body
animated (not simulated) so the solver does not drag it to the ankle
(commit 9ea5f13 / GD10-044). The keyframed tip then rigidly rides its simulated
parent via `pose()` re-parenting — a benign result. **[I] UNCERTAIN:** the
original Havok runtime's exact handling of these constraints is VMX128/SIMD and
not decompilable from the static export; the port's choice is reasonable and
left unchanged.

### Physics — faithful and stable (objective runtime evidence)

`--walktest band_motion` (offscreen, real Quickplay song, Metallica band) and
`--chartest` both pass:
- **Penetration (the recurring hair/cloth bug class) is zero during play.** A
  new measured residual-penetration trace (deepest overlap of a dynamic
  accessory capsule in a keyframed body capsule, after the contact passes)
  reads **0.0000 m at every continuously-simulated step**. The only non-zero
  readings are at `ragdoll_markforreset` frames (song start / mocap-clip
  boundaries; |v|=|w|=0): the accessory is placed at its animated rest pose
  without a contact solve (Havok `setPoseModelSpace` at rest [C 823FE8C8 /
  826BCEE0]), so a thin accessory that lies on the body (e.g.
  `Bone_ACC_Head_05` resting on `Bone_Chest`, or `Bone_ACC_Legs_04` at
  `Bone_Knee_L`) shows up to ~0.11 m overlap against the fat body capsule
  (Rob's chest r=0.19) for that one frame, then the next step resolves it to 0.
  This matches the original (setPoseModelSpace + contact solver on following
  frames). The contact resolver itself takes the worst steady-state overlap
  from ≤0.017 m (before passes) to 0.0000 m (after).
- Band poses **plausible** (limbs within bind length, accessories on joints,
  hands within 10 cm of IK targets) at 6.2 s and 12.1 s, and **identical at 60
  vs 240 fps** (frame-rate-independent).
- Ragdoll self-test passes (a segment hung from a keyframed head keeps its cone
  and joint).
- Physics state healthy: |v| < 1 m/s, |w| ≲ 7.5 rad/s, constraint gap < 0.035 m,
  cone exceeded by ≤6° transiently (one-step-solve tolerance) — no overshoot /
  explosion (contrast the pre-GD10-044 176 rad/s bug).
- Gravity is correct: model-space `R^T·(0,-9.8,0)/scale` [C 82253B10:8200E4E4,
  823FF100]; solver params `4ITERS_MEDIUM` (tau 0.6, damp 1, 4 iterations,
  checkAndFixConstraint 0.05/0.8) grounded to the XEX addresses in
  `band_ragdoll.h`.

### Animation — faithful

Sampler (`data/anim.cpp`): nlerp rotation + linear translation with correct
`upper_bound` key search; clip selection (tempo/skill/speed), blend nodes,
difference anims, IK (two-bone + hinge), twist/split helper constraints,
facial loop and gh4 DrumKit per-hit difference anims are all grounded to prior
addresses and produce the plausible, frame-rate-independent poses above. No
pops / wrong-clip / retarget / timing defects surfaced objectively.

### Change made

`render/band_ragdoll.cpp` + `.h`: added a **measured penetration trace** to the
`GD10_RAGDOLL_LOG` diagnostic (worst dynamic-vs-keyframed capsule overlap per
step; before/after the contact passes; with `.rag` body names). Logging only,
gated behind the env var — no behaviour change. It is the objective instrument
`docs/guidelines/usual-bugs.md` requires for the hair/cloth bug class, which was
previously absent. Build: Linux video build (`GD10_FFMPEG_DIR`), `band_motion`
walk + chartest pass.
