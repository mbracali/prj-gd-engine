# gd-game-06: decisions

Each entry: what, why, how to undo.

## D1. Discovery uses gd-game-04's tools in place (2026-11-17)

What: the disc was extracted and checked with `gd-game-04/tools/*.py` run on
`gd-game-06/extracted` (read-only use, nothing copied into gd-game-06 yet);
only the five Ghidra scripts (FixXexFunctions, ExportAnalysisXex, DumpCFuncs,
DumpAsm, ReadF) were copied into `gd-game-06/tools/ghidra_scripts`, unchanged
except their `@category`. Why: GD06-001 writes no engine code; the copy of
the engine and tools is M1's decision (D2). Undo: none needed.

## D2. Recommended (owner to approve): copy gd-game-04's engine, no shared library (2026-11-17)

What: M1 copies gd-game-04's tracked `engine/`, `tools/`, `build_commands/`,
`local_build.sh` into gd-game-06, renamed gd04 -> gd06 / GD04 -> GD06 /
user-facing names -> Guitar Hero: Aerosmith, version 0.0.0 (as gd-game-10's
D1), including gd04's audio handling (Aerosmith's song banks use GH3's
container), then changes only what Aerosmith needs (the diff list).

Why:
- The XEX is GH3's with small changes (97% of functions pair by size and
  callees, every C function name kept, FORMATS.md) and every data format but
  the `.ska` keys parses with GH3's decoders, so a copy works almost at once.
- A shared "GH3 engine" library for gd04 and gd06 would mean refactoring the
  released gd04 (its tests, its packages) and touching gd-game-04. The
  repository's pattern is one engine per game folder (gd-game-10 started the
  same way) with the common backends in gd-engine-core.
- Cost: fixes in gd04's copy do not reach gd06 by themselves; the copy's
  comments keep GH3's [C] addresses, which are [I] for gd06 until re-found.

Audio: gd06 keeps gd04's song-bank reader (data/fsb.*, audio/song_stream.cpp,
audio/sfx.cpp) as-is, since Aerosmith's banks use GH3's container; the per-bank
key is derived the same way (M3 confirms it on Aerosmith's banks). streamall and
in-pak sounds play as in gd04.

Diff list (what gd06 changes in the copy, by milestone):
- M1: names / title (Guitar Hero: Aerosmith, title ID 41560819 in
  `iso_signatures`, save folder gd06), the `.ska` rotation keys
  (FORMATS.md), the chart's aux part, every GH3 address in comments marked
  for re-check.
- M2: main menu (`menu_main.qb` is new), `ui_frame`, `ui_physics`,
  `ui_exploding_text`, the 9 new C functions, movie subtitles (AO movies),
  the Xbox sign-in natives.
- M3: Quickplay's mode select (`menu_quickplay_select_mode.qb`), co-op
  quickplay, the difficulty analyser screen, confirm the song-bank key on
  Aerosmith's banks.
- M4: six new venues, the Aerosmith band (per-venue singer / drummer rigs,
  tempo anim events, per-member animation data), Aerosmith entrance /
  DMC intro transitions, new particles (pyro, fireworks, Tesla lightning,
  star-power bones / wings), `SetLightMapOverlayParams`.
- M5: career with opening acts, Aerosmith unlock and encores, the movies
  between tiers, Joe Perry's boss battle, Bonus songs, the store / Vault,
  achievements, no co-op career.

Undo: if the owner prefers a shared library, M1 extracts gd04's
engine-independent parts first (with gd04's tests passing) and gd06 links it.

## D3. gd06's `.ska` decoder diverges from gd04's in two places (2026-11-17, M1)

What: in the copied `engine/src/data/anim.cpp` (and `tools/anim.py`) gd06 changes
two things from gd04, for Aerosmith's data only:
- the compressed rotation-key scale is **1/32768** (gd04/GH3: 1/16384);
- a new **multi-key uncompressed-float track** (`flags & 0x10000000` + `0x4000`)
  is parsed as `(0, x, y, z)` entries (Q: xyz + derived w; T: xyz translation),
  frames assigned by index.

Why: the XEX rotation-key decoder is byte-identical to GH3's, so the scale is
the only thing the compressed data takes; the objective check is decisive
(1/32768 -> 0 of 8.16M keys over 1.0, max exactly 1.0; 1/16384 -> 48% fail). The
float track is a genuinely new Aerosmith clip kind (GH3 only ever had a true
single pose); the layout and the `0x4000` discriminator are confirmed on all 7
clips on the disc. Both are documented in docs/FORMATS.md "Animations".

Cost / open: for the float track, frame **timing** (keys are placed at index
0,1,2,... as a provisional) and w's **sign** are not yet derived -> M3/M4. Only
7 clips, all in `perm_anims.pak`, are affected; the compressed scale fix is the
30,333-file one. gd04 is untouched (its 1/16384 is correct for GH3's data).

Undo: restore the two gd04 values in `anim.cpp`/`anim.py` (formattest then fails
on 30,333 `.ska`).

## D4. `IsStandardGuitarControllerPluggedIn` answered false in the port (2026-11-17, M2)

What: gd06 registers the GH3-era C native `IsStandardGuitarControllerPluggedIn`
(XEX 0x8210aa28, cfunc 0x828ecc68) to return **false** by default, with
`GD06_STD_GUITAR_LEGACY=1` forcing the on-disc **true**. In
`engine/src/ui/natives_game.cpp` next to `IsGuitarController`.

Why: Aerosmith's boot is the first on a GH3-engine game to gate on this native.
Its **new** `check_for_any_input` (scripts\guitar\menu\menu_press_any_button.qb,
not GH3's) branches on it inside the press-any-button loop; GH3's own boot
script never calls it, so gd04 never registered it and the copied engine had no
handler -- the unregistered call wedged the flow on `bootup_press_any_button_fs`.
The native detects a physically-attached GH-branded guitar controller. The port
has none (the player's keyboard or pad is mapped as their instrument), so false
is the literal answer; the decisive, objective check is the walk: false reaches
`main_menu_fs` cleanly with the "using your guitar controller" intro
(`bootup_using_guitar_controller_fs`) correctly skipped -- the console's own
outcome for a valid player -- while true wedges the flow in headless. This is
new behaviour (gd04 has no equivalent pattern), so it carries the
`GD06_STD_GUITAR_LEGACY` switch for anyone comparing against console hardware.

Undo: set `GD06_STD_GUITAR_LEGACY=1`, or change the native's default to true
(boot then requires a real guitar-controller device and wedges keyboard/pad play).

## D5. Aerosmith's 9 new C functions answered offline (2026-11-17, M2)

What: the nine QScript C functions new in Aerosmith's XEX (FORMATS.md
"The executable", cfuncs.tsv) are registered in
`engine/src/ui/natives_flow.cpp`, all answered offline (OPEN_QUESTIONS 4, as
GH3's / gd04's Live path is): RefreshXUserSigninInfo, ClearXUserSigninInfo,
WriteSongDataToFile, DeleteAllSongDataFromFile, ResetScoreUpdateReady,
DeleteAllTeslaEffectObjects, SetLightMapOverlayParams -> no-op (true);
IsStarEntry, IsUserMusicPlaying -> false. Rationale per name in the code
comment. The sign-in / score ones are no-ops because the port has no Xbox Live
and keeps scores through its own save path (M3); the Tesla / light-map ones are
venue FX stubbed until M4; the two predicates are false (no Live leaderboards,
no Xbox custom soundtrack).

Why: faithful offline answers that unblock boot without inventing Live/online
behaviour the port does not have. ClearXUserSigninInfo (boot's
create_press_any_button_menu) and ResetScoreUpdateReady (create_main_menu) are
on the M2 path; the rest are reached later and registered now so they never
log UNHANDLED.

Undo: remove the registrations (the names then log UNHANDLED again); revisit
WriteSongDataToFile / DeleteAllSongDataFromFile / ResetScoreUpdateReady when M3
wires scores, and the venue natives when M4 does venues.

## D6. The chart's aux part is not a playable part (2026-11-17, M3)

What: gd06 does NOT wire `<song>_song_aux_<Diff>` to a playable Part; chart.cpp
reads only the parts it names (guitar / rhythm / guitarcoop / rhythmcoop), so
the aux sections parse and are skipped. The decision is recorded in chart.cpp
and OPEN_QUESTIONS 6.

Why: the aux track has real, distinct note data per difficulty (+ its own Star /
StarBattleMode phrases), but it is never assigned to a player. The GH3 engine
builds the gameplay section from player_status.part; the only part-selection UI,
menu_choose_part.qb, offers LEAD/RHYTHM (guitar/rhythm) or GUITAR/BASS and never
aux; quickplay sets part=guitar (menu_main.qb); no script assigns part=aux and
the XEX has no "_aux" string literal. It is an auxiliary, non-interactive track
(most likely an on-stage band-member animation / authoring reference). Making it
a Part would be dead, untested code and a guess at its role.

Undo: if M4's band work shows the aux track drives a band member's animation,
read it there (not as a player Part); if some mode is ever found that plays it,
add the Part then (none exists in p1_career / p1_quickplay / p2_faceoff /
p2_pro_faceoff / p2_battle / p2_coop or the boss battle).

## D7. The 7 tag-setup "arithmetic on none" warnings are documented, not fixed (2026-11-17, M3)

What: the once-per-script "UNHANDLED arithmetic on ..." lines from
setup_songtags / venuetags / unlocks / setlisttags / character_tags
(guitar_globaltags.qb) are left as-is (benign log lines), not silenced.

Why: each builds a `globaltag_checksum` accumulator by QB key-concatenation
(`checksum = checksum + <name>`); the first add has a none/key left operand,
which vm.cpp kAdd does not implement for keys -- it logs once and returns none,
then the flow continues. gd04's vm.cpp is byte-identical here and ships with
working saves and top-scores, so the warnings are cosmetic; the accumulator
feeds a combined checksum, not the per-tag saves (which use explicit tag names).
The M3 walk reaches toprockers -> autosave -> setlist with 0 black frames and
top scores cleared then rewritten, so the save path works. Implementing GH3's
key+key concatenation in the VM would remove the lines but is a shared-pattern
behaviour change that would diverge gd06 from the proven gd04 VM and would need a
GD06_*_LEGACY switch plus broad validation; not warranted for M3.

Undo: if the combined checksum is ever needed, add key+key concatenation to
vm.cpp kAdd (CRC of name(a)+name(b), registered in the name table) behind a
GD06 LEGACY switch, and re-check every script that does key arithmetic.

## D8. M3 is a GD06_VIDEO=ON build; FFmpeg prefix reused from gd04 (2026-11-17, M3)

What: playing a song needs the XMA audio decoder, which lives behind
GD06_VIDEO=ON (FFmpeg). M3 builds a separate dir `engine/build/linux-video`
with -DGD06_VIDEO=ON -DGD06_FFMPEG_DIR pointing at gd-game-04's prebuilt FFmpeg
7.1.x prefix (gd-game-04/engine/build/ffmpeg/linux/ffmpeg-prefix; same version,
gd06 has no prefix of its own yet). The GD06_VIDEO=OFF M1/M2 build at
engine/build/linux is left intact for fast format/boot checks.

Why: with GD06_VIDEO=OFF the song cannot start (falls to quickplay_fail_song_fs)
and all audio is silent; M3's gate (a song to results, with audio) requires the
decoder. Reusing gd04's FFmpeg prefix avoids a from-scratch FFmpeg build.

Undo: for a self-contained gd06 release build, run gd06's own build_ffmpeg
(local_build.sh / build_linux_portable.sh) to produce its prefix; M6 does this.

## D9. The menu 3D backdrop loads beyondbeautiful, not GH3's slowride (2026-11-17, M4)

What: `App::prepareMenuBackdrop` (app.cpp) now loads the song **beyondbeautiful**
for the live 3D venue behind the main menu (at the default venue z_Fenway via
`chooseZone`), instead of GH3's hardcoded `slowride`. `GD06_MENU_SONG=<id>`
overrides the song; `GD06_MENU_BACKDROP_LEGACY=1` forces `slowride` back (the
M2/M3 behaviour: the song isn't on the disc, so no backdrop loads).

Why: beyondbeautiful is Aerosmith's menu song — `create_main_menu` runs
`change { current_song = $beyondbeautiful }` (menu_main.qb). GH3's slowride is
not on Aerosmith's disc, so the backdrop load logged "not on the disc:
slowride_song.pak" and skipped every boot (the menu showed its 2D art alone).
With beyondbeautiful it loads z_Fenway in ~1.8 s, the boot walk still passes, and
the menu is visually identical (the 2D band art + logo cover the live venue, as
in GH3 — the live 3D venue is a gd04 **port feature**, since GH3's own
`create_main_menu_backdrop` is also 2D art). This is a behaviour change (the 3D
backdrop venue path now actually runs on boot), hence the LEGACY switch.

Undo: `GD06_MENU_BACKDROP_LEGACY=1`, or restore `"slowride"` in app.cpp.

## D10. HandCamSmooth camera determinism left for a follow-up (2026-11-17, M4)

What: `--venuetest` FAILS the 60-vs-240fps camera check on z_MaxsKC (43.7 s) and
z_JPPlay (87.3 s); the active shot is `CameraCuts_HandCamSmooth`. gd06 does NOT
fix it in M4 (documented, not changed).

Why: HandCamSmooth is a real VM script (guitar_cameras.qb) that gd04's
CameraDirector runs through the generic `Ctl::Script` / `ccam_domorph` morph path
(venue_camera.cpp line 339). That morph is not perfectly frame-rate-independent
for a **mid-song** VM camera shot — GH3 only used VM camera scripts for
intro/encore/boss, so the divergence never showed up in gd04's venuetest and the
shared path was never hardened for it. The band tracking (`aim()`) is
deterministic and is not the cause. The divergence is small and the venue renders
correctly at the real/target frame rate, so it is non-fatal; a proper fix touches
the shared Ctl::Script morph (all games) and warrants a GD06_*_LEGACY switch plus
revalidation — out of scope for M4's "venues + band core, proof, defer the finer
items".

Undo / do-later: make the ccam_domorph start-pose capture and VM stepping purely
a function of song time (as the note/cut processing already is), behind a
GD06_*_LEGACY switch, and re-run `--venuetest` at 60/240 fps on all six venues.

## D11. Aerosmith's career runs on gd04's generic engine unchanged; M5 is data + walks (2026-11-17, M5)

What: GD06-006 (M5 career and modes) needed NO engine changes to the career /
atoms / transition / VM code. Aerosmith's career is entirely data- and
script-driven (GH3_Career_Songs, GH3_Career_Progression, guitar_progression.qb,
career_menu_flow.qb) and the copied gd04 engine (`script/atoms.cpp`, the flow
manager, `Transition_Play`) runs it as-is. The Aerosmith-specific pieces are all
QScript the VM already runs:
- 6 tiers (num_tiers = 6), each tier's two opening-act songs unlocked first
  (defaultunlocked = 2); winning both runs Progression_CheckIntroSongsComplete
  (2 songs > 2 stars) -> Progression_TierAerosmithUnlock, which sets the NEW
  transition `current_transition = $PREAEROSMITH`.
- GuitarEvent_SongWon_Spawned has a PREAEROSMITH branch (Transition_Play is
  generic / data-driven: Default_cameras_preaerosmith, Default_PreAerosmith_
  Transition) -> `preaerosmith_win_song` -> career_aerosmith_confirmation_fs
  ("AEROSMITH!" entrance) -> start_aerosmith_encore plays Aerosmith's song.
- The tier's songs complete (Progression_CheckSongComplete, tier_size songs
  > 2 stars) -> preencore -> career_encore_confirmation_fs -> the encore song
  -> Progression_TierComplete (careertier<N>.complete, the AO_short_<N+1>
  completion movie, the next tier + venue unlocked).
- Tier 6 puts Joe Perry first: career_tier6_bossunlock -> bosscomplete, the
  boss song `bossjoe` ($boss, Boss_Joe_Props) reached from tier 6's set list.
- No co-op career (GH3's coop_career flow is gone); co-op is a MULTIPLAYER
  quickplay mode (below).

Why: the XEX is GH3's (`GH3_xp1`) and the career data parses with gd04's
decoders; a copy runs it. Confirmed by `--walktest career_tier` (a full tier 1:
two opening acts -> AEROSMITH! -> Aerosmith song -> a fourth song -> ENCORE! ->
the encore -> careertier1.complete = 1 -> tier 2 / z_MaxsKC) and `--walktest
boss` (The Glitches -> tier 6 -> bossjoe -> "JOE PERRY WANTS TO BATTLE!"), both
with screenshots read. No unhandled native in either log.

Undo: none (no engine behaviour changed).

## D12. GD06_WALK_FULLSTARS: a walk reports a 5-star career win (2026-11-17, M5)

What: a walk forces a song to "win" at ~20 s of song time (walkEndMs_), which on
a long Aerosmith chart scores far below the full chart's base_score and so earns
too few stars to count toward progression (CheckIntroSongsComplete /
CheckSongComplete need > 2 stars; a short GH3 song like slowride happened to
clear it in 20 s, dreampolice does not). `GD06_WALK_FULLSTARS=1` makes a walk's
won single-player song report score = base_score x 2.8 + 1 (a five-star score,
exactly what GH3's own `setlist_debug_completesong` writes), in
`App::songResult()`, gated on `walk_` + the env + `m.players() == 1`. The career
/ boss walks are run with it so a tier progresses deterministically without
playing every song in full.

Why: a test affordance, not a behaviour change; ordinary play is untouched (no
env, or outside a walk). Without it a career walk stalls at the set list after
the intro songs (the tier never unlocks Aerosmith's set). The 2.8x factor and
the 3/4/5-star thresholds are GH3's own (Progression_SongWon: >= base x 2 is 4,
>= base x 2.8 is 5; a won song is at least 3).

Undo: drop the env (the walk then needs real full-length play to earn stars);
remove the three lines in songResult() to delete the affordance entirely.

## D13. The modes' two-player controller select: P2 joins before both confirm (2026-11-17, M5)

What: the mp modes (face-off, pro face-off, battle, co-op quickplay) are reached
through MULTIPLAYER -> `mp_select_controller_fs`. The walk's two-player setup
(`twoPlayers_`, the split keyboard) gives two devices, but the screen only
continues once BOTH slots are ready (p1_ready / p2_ready). The mode walks now
drive it in the order the screen expects: player 2 presses Down to join the P2
side, then player 2 confirms, then player 1 confirms (`Op::p2(D), Op::p2(C),
Op(C)` with small delays), instead of GH3/gd04's `C, P2D, P2C`. With that,
`--walktest faceoff` passes end to end and the other modes route into their
flows.

Why: a test-harness ordering fix for the port's two-player controller
assignment, not an Aerosmith content change. The MULTIPLAYER mode menu
(menu_mp_select_mode.qb) offers FACE-OFF (p2_faceoff), PRO FACE-OFF
(p2_pro_faceoff), BATTLE (p2_battle) and, offline, CO-OP (the fourth item,
mode = p2_quickplay -> select_coop -> the coop_quickplay flow); the online-only
p2_coop item is hidden offline. Main-menu MULTIPLAYER is item 2 ({D, D, C}), not
GH3's item 3, because Aerosmith drops CO-OP CAREER.

Undo: restore `C, P2D, P2C` (the modes then stall on the controller select in
the port).

## D14. gd06 has its own FFmpeg prefix and launcher module recipe (2026-11-17, M6)

What: M6 builds gd06's OWN FFmpeg prefix (via gd06's `engine/build_common.sh`
`build_ffmpeg`, FFmpeg 7.1.5, the same `--disable-everything` + bink/xma decoder
flags as gd04/gd10) instead of reusing gd04's (D8's leftover), and registers
gd06 as a launcher game with a `build_module_06` recipe that is a copy of
`build_module_10` (GD06/gd-game-06, the gd06_module cmake target, FFmpeg per
system). gd06 is marked `ready_to_play: true`; its version stays 0.0.0 and
completeness 0 (owner decides any bump).

Why: every launcher build must carry the module of every ready game, built from
that game's own sources and FFmpeg (module_build_key / the self-contained
release). gd06's banks use GH3's container and its videos are Bink, exactly like
gd04/gd10, so the same FFmpeg configuration serves it; a copy of gd10's recipe
is the repository's one-engine-per-game pattern (D2). The registration lines are
gd06's own: `module_systems_06` and `build_module_06` in build.sh, a dedicated
`06)` FFmpeg arm in verify_bundle.sh (added as its own case arm so no other
lane's line changes), and `ready_to_play: true` in game-info.yaml;
`games.yaml` and the `iso_signatures` (title ID 41560819) were already in place
from M1. Verified on a local Linux dev launcher: it lists and launches
Aerosmith to gameplay and loads the ISO by title ID (PROGRESS GD06-007).

Undo: set `ready_to_play: false` (gd06 drops out of builds and verify);
`build_module_06` / `module_systems_06` / the `06)` verify arm can be removed
together. The owner runs the full multi-platform package build separately.

## D15. "Everything very dark": the VENUE lighting is faithful, no venue fix (2026-11-17)

What: on the "whole scene much darker than real Aerosmith" report, the scene
lighting / brightness lane (venue light-show, lightmap, exposure/tonemap/gamma,
SetLightMapOverlayParams) was investigated and **left unchanged** -- no venue
brightness boost added.

Why (objective, data/code only, no emulator):
- Every lighting/render file (venue_view, venue_lights, venue_stage, screen_fx,
  gh3_shader, enhanced_fx, gl_device, core_device + headers) is **byte-identical
  to gd04** after name-normalisation (0 differing lines). The copy changed nothing
  on the lighting path.
- gd06's `--venuetest` stage luma matches gd04's GH3 venue under the same pipeline
  (both ~14-46 of 255 on lit cuts); not abnormally dark.
- screen_fx is identity on the venue: TOD bright_sat Brightness=1 (from qb.pak),
  DOF_Off strength=0 (skipped), bloom additive; `GD06_NO_SCREENFX` on/off differs
  <1% on the same frames. `global_brightness` = 0.5 + 0.1 x SE_Brightness, and
  SE_Brightness defaults to 5 -> 1.0 (identity).
- **SetLightMapOverlayParams is never called**: 0 of 17,838 compiled scripts on
  the disc reference its checksum (0x3c487938); the C func only reads a `cutofflow`
  param. The M4 no-op cannot darken anything -- lead closed.
- The only large uniform darkener is the shared 360 display-gamma ramp
  (displayGammaRamp, ~30% down in shadows; `GD06_GAMMA=0` lifts in-song full-luma
  42->54), but it is identical to gd04, is the deliberate 360-output emulation for
  every game, and has no console reference to call it wrong.
- The visible darkness is the BAND rendering as silhouettes (in-song highway
  shots: venue warm-lit, band near-black) -- the parallel agent's band/character
  lighting lane, not this one.

So a venue brightness change would diverge gd06 from the identical, released gd04
render path with no objective basis, against "match the original exactly" and
"objective checks, not eyeballing". The fix belongs in the band lane.

Undo / do-later: if a console-grounded reference ever shows the venue itself is
too dark, the in-lane lever is the 360 display-gamma ramp or a TOD bright_sat
scale, which would be a global look change behind a `GD06_*_LEGACY` switch and
re-validated across games.

## D6. chooseBand reads the song's band struct so Aerosmith appears (2026-11-17, band lane)

What: `data/band.cpp chooseBand` now follows the real `create_band` /
`create_guitarist_profile`: the single-player drummer / bassist / vocalist /
guitarist come from the song's band struct (`gh3_songlist_props.<song>.band`,
fallback `$default_band`) -- `Drummer` / `Bassist` / `vocalist_male` /
`vocalist_female` / `Guitarist` plus the `*_instrument` overrides -- and a
non-default band without `allow_player_selected_guitarist` forces its own
guitarist (Joe Perry, per-venue outfit/style). `render/band.cpp startWith` skips
a member whose model fails to load instead of aborting the whole band.

Why: GH3's chooseBand (copied at M1) only knew the generic session band
(`drummer` / `bassist` / `singer` profiles + the player's character), so every
Aerosmith song showed `Axel, Drummer, Bassist, Singer` and **Aerosmith -- the
title band -- never appeared on stage** (owner: "the main band is not even
appearing"). The data is decisive: each Aerosmith song's entry sets
`band = $aerosmith_band_<song>` (`Guitarist='JoeP'`, `Bassist='Aero_TomH'`,
`vocalist_male='aero_singer'`, `Drummer='aero_drummer'`), opening acts/quickplay
set `band = $default_band` (`allow_player_selected_guitarist`). Objective check:
the `band:` log goes `Axel, Drummer, Bassist, Singer` -> `JoeP, Aero_Drummer,
Aero_TomH, Aero_Singer` on makeit (and the real career encore), loads with 0
skips in all playable venues, and default_band songs are unchanged.

Switches (new behaviour): `GD06_BAND_STRICT_LEGACY=1` restores GH3's
abort-the-band-on-any-member-failure; `GD06_BAND_PLAYER_GUITARIST_LEGACY=1` keeps
the player's own character on the guitar even on an Aerosmith song.

Undo: restore the previous `chooseBand` tail (generic drummer/bassist/singer +
player guitarist) and the `if (!e.model) return false;` in startWith.
