# gd-game-06 progress log

## 2026-11-17: BAND lane — Aerosmith (the title band) now takes the stage (uncommitted)

Bug (owner, high priority): "the main band is not even appearing." Root cause
found by running the real paths offscreen (scratch XDG_DATA_HOME) and reading the
`band:` log + screenshots BEFORE/AFTER.

- **The band always spawned and drew** on every real path (quickplay, --song,
  career) and in every venue — all 6 venues have the four
  `Z_<zone>_TRG_Waypoint_<role>_Start` nodes, so placement was never the issue.
  What was wrong: on **Aerosmith's own songs it was the generic GH3 session band**
  (`Axel, Drummer, Bassist, Singer`) instead of **Aerosmith** — Aerosmith, the
  title band, never appeared.
- **Root cause:** `chooseBand` (data/band.cpp) was GH3's: it took the drummer /
  bassist / singer from the fixed `drummer` / `bassist` / `singer` profiles and
  the guitarist always from the player's character. It **ignored the song's
  `band` field.** The real `create_band` / `create_guitarist_profile` read
  `gh3_songlist_props.<song>.band` -> a band struct (`$default_band` for opening
  acts / quickplay; `$aerosmith_band_<song>` for every Aerosmith song) and pull
  `Drummer` / `Bassist` / `vocalist_male` / `vocalist_female` / `Guitarist` (+
  `*_instrument`) from it; a non-default band without
  `allow_player_selected_guitarist` forces its own guitarist (Joe Perry, per-venue
  outfit/style). [C scripts create_band, create_guitarist_profile; data
  default_band, aerosmith_band_*.]
- **Fix:** rewrote `chooseBand`'s single-player drummer/bassist/vocalist/guitarist
  selection to follow create_band: resolve the song's band struct (fallback
  `default_band`), read each member profile from it, honour
  `allow_player_selected_guitarist` (JoeP forced, per-venue style: Nipmuc 2 /
  MaxsKC 3 / Fenway 4 / Nine_Lives 5 / JPPlay 6) and `*_instrument` overrides.
  default_band path (opening acts, quickplay, credits) is unchanged.
- **Objective before/after (the `band:` log, same song/venue):**
  - `--song makeit` @ z_Nipmuc: BEFORE `Axel, Drummer, Bassist, Singer` ->
    AFTER `JoeP, Aero_Drummer, Aero_TomH, Aero_Singer`.
  - real **career** tier 1 (walktest career_tier): opening acts dreampolice /
    alltheyoungdudes keep `Axel, Drummer, Bassist, Singer`; the Aerosmith encore
    **MakeIt** -> `JoeP, Aero_Drummer, Aero_TomH, Aero_Singer`.
  - Aerosmith band loads with **0 skips** in all 5 playable Aerosmith venues
    (Nipmuc, Fenway, MaxsKC, Nine_Lives, JPPlay — JPPlay/hof checked too).
  - regression: `--song dreampolice` ($default_band) still `Axel, Drummer,
    Bassist, Singer`.
  Screenshots LOOKED AT (Read): BEFORE the stage carried the generic band; AFTER
  Joe Perry (lead, per-venue outfit), Joey Kramer on the kit, Tom Hamilton and
  Steven Tyler are on stage and animated (shots in scratch, not in the repo).
- **Safety net:** a single member whose model fails to load no longer blanks the
  whole band (render/band.cpp `startWith` skips it and keeps the rest;
  `GD06_BAND_STRICT_LEGACY=1` restores GH3's abort-on-any-failure). No member
  actually failed on this disc.
- Files (uncommitted): `engine/src/data/band.cpp` (chooseBand + `<SDL3/SDL_stdinc.h>`),
  `engine/src/render/band.cpp` (non-fatal member load). Built GD06_VIDEO=ON in
  `engine/build/fix-band` (-j2), no errors. Switches: `GD06_BAND_STRICT_LEGACY`,
  `GD06_BAND_PLAYER_GUITARIST_LEGACY` (force the player's character even on
  Aerosmith songs).
- Still off / not mine: the venues are dark at some cuts (LIGHTING lane); the
  singer's per-song face-anim pak (`singer_anim_pak`) and per-song instrument
  "random_*" tables are not yet wired (the base per-venue rig loads); the Rhythm
  slot (Aero_BradW, co-op's 2nd guitarist) is single-player-skipped as in GH3.

## 2026-11-17: "everything very dark" — VENUE/LIGHTING lane investigated, no in-lane bug found (uncommitted, NO code change)

Scope of this pass: scene lighting / brightness only (venue light-show, lightmap,
exposure/tonemap/gamma, SetLightMapOverlayParams). Band/character lighting is a
PARALLEL agent's lane and was not touched. Objective, data/code-only (no emulator).

Findings (all objective):
- **The venue brightness is FAITHFUL; the venue/light code is byte-identical to
  gd04.** Diffed every lighting/render file (venue_view, venue_lights, venue_stage,
  screen_fx, gh3_shader, enhanced_fx, gl_device, core_device, all `.h`) gd04 vs
  gd06 after name-normalisation: **0 differing lines** (core_device's only diff is a
  doc-comment "gd-game-04"->"gd-game-06"). So nothing in the lighting path was
  changed by the M1 copy.
- **Objective luma matches the GH3 baseline.** `--venuetest z_Nipmuc` (offscreen,
  scratch XDG, dreampolice) stage-region mean luma per camera cut ranged ~2..44 of
  255 (bright cuts ~28-44, mood/blackout cuts ~2-14); gd04's own `--venuetest` on a
  GH3 venue (z_artdeco) under the identical pipeline gave the same range (~14-46).
  gd06 is not abnormally dark vs the engine gd04 ships.
- **screen_fx is effectively identity on the venue** — not a darkener. The TOD
  `Default_TOD_Manager` bright_sat is Brightness=1/Contrast=1/sat=1 (identity, read
  straight from qb.pak); DOF is `DOF_Off` strength=0 (skipped); bloom is additive.
  Apples-to-apples `--venuetest` with `GD06_NO_SCREENFX` on vs off on the SAME
  frames: within ~1% (e.g. 008689 full 43.85 vs 43.78). (An earlier 40% gap was an
  artefact of comparing different camera poses via `GD06_VENUETEST_SEQ`.)
- **`global_brightness` (viewport-0 bright_sat) is identity at the default.** Its
  script `TOD_Proxim_Update_Global_Brightness` sets Brightness = 0.5 + 0.1 x
  SE_Brightness; SE_Brightness defaults to 5 on the disc -> 1.0. The port not
  applying it = the console default, not a darkening.
- **SetLightMapOverlayParams is a confirmed dead end (closes the M4 lead).** Its C
  func (0x822755c0) only reads one param (`cutofflow`, key 0x1cdb0bf2). Scanned
  **all 17,838 compiled scripts** on the disc for its call checksum (0x3c487938):
  **0 calls**. It is never invoked, so stubbing it as a no-op cannot darken
  anything. DeleteAllTeslaEffectObjects is star-power teardown, not brightness.
- **No missing TOD "sun"/level-ambient.** `tod_manager_create_perm_lights` only
  does SetDynamicShadowIntensity 0.35 (already honoured) + SetModelLightsFadeTime;
  the `tod` sun_rgb/lev_rgb are not turned into scene lights by GH3's engine, so
  nothing is being dropped there.
- **The one big uniform darkener is the shared 360 display-gamma ramp**
  (gl_device.cpp displayGammaRamp: sRGB-decode then Rec.709-encode, ~30% down in
  shadows/low-mids). `GD06_GAMMA=0` lifts the in-song frame full-luma 42->54 and
  stage 26->37. But this ramp is byte-identical to gd04, is the deliberate
  360-output emulation applied to EVERY game, and there is no console reference
  (emulator banned) to say it is wrong -- changing it is a global look change, not
  an Aerosmith fix.

Where the darkness actually is: in both in-song highway shots (`--walktest
quickplay`, GD06_WALK_SHOTS, Read) the VENUE reads with appropriate warm stage
light, but the BAND members render as near-black SILHOUETTES (side/back-lit cuts).
That is the parallel agent's band/character-lighting lane. The venue lighting in
this lane is faithful and matches gd04.

Decision: **no code change made** (DECISIONS D15). Fabricating a venue brightness
boost would diverge gd06 from the identical, released gd04 render path with no
objective justification and against "match the original exactly" /
"objective-checks-not-eyeballing". The owner-visible fix belongs to the band lane.
Screenshots looked at (scratch, not in repo): venuetest z_Nipmuc cuts, in-game
highway.bmp / highway2.bmp.

## 2026-11-17: GD06-007 M6 launcher module + registration (done, uncommitted)

- **gd06 has its OWN FFmpeg prefix now (DECISIONS D14, closes D8's leftover).**
  Built with gd06's own `engine/build_common.sh` `build_ffmpeg` (FFmpeg 7.1.5,
  `--disable-everything` + `--enable-decoder=bink,binkaudio_rdft,binkaudio_dct,xma1,xma2`
  + `--enable-demuxer=bink` — identical version/flags to gd04/gd10, XMA for the
  song audio and Bink for the videos) into
  `engine/build/ffmpeg/linux/ffmpeg-prefix` at -j2. M3/M4/M5 reused gd04's
  prefix; gd06 is now self-contained. The build stamp matches what
  `build_module_06`'s linux branch computes, so a later release reuses it.
- **The launcher module `gd-game-06.so` is built** (`gd06_module` target,
  `-DGD06_MODULE=ON -DGD06_VIDEO=ON -DGD06_FFMPEG_DIR=<gd06's prefix>`, -j2):
  10.7 MB, **exports `gd_game_run`**, 0 gd06 compiler warnings (the one
  array-bounds warning is OpenAL in gd-engine-core, shared/cosmetic). The
  CMake module target (OUTPUT_NAME gd-game-06, the gd_module_shim force-include)
  was already in place from the M1 copy — no CMake change needed.
- **gd06 registered as a launcher game (exact files/lines):**
  - `gd-engine-launcher/scripts/build.sh`: **`module_systems_06="linux
    steamdeck windows macos android"`** (next to 05/10) and **`build_module_06`**
    (a copy of `build_module_10` with GD06/gd-game-06 — same FFmpeg-per-system
    build, cmake `gd06_module`, ships gd-game-06.so + av libs + the FFmpeg/glslang/
    RtMidi licences + gd-engine-core runtime).
  - `gd-engine-launcher/scripts/verify_bundle.sh`: a dedicated **`06)`** arm in
    the per-game FFmpeg check (avformat/avcodec/avutil present + its
    `FFmpeg-LICENSE.LGPL-2.1.txt`), added as its own arm so no other lane's line
    is touched.
  - `gd-game-06/game-info.yaml`: **`ready_to_play: true`** (so every launcher
    build must carry gd06's module). Version kept **0.0.0**, completeness **0** —
    left for the owner (no unasked bump).
  - **Already present, confirmed:** `games.yaml` lists `gd-game-06/game-info.yaml`;
    `game-info.yaml` `iso_signatures` has `xex_title_id: "41560819"` (xdvdfs) —
    the launcher matches the disc by title ID from this, no games.yaml
    `extra_iso_signatures` needed.
- **VERIFIED on a local Linux dev launcher (the two must-never bugs checked).**
  Built the launcher binary and staged a dev layout (gd-game-06.so + av libs in
  `gd-engine-games/gd-game-06/`, the Aerosmith ISO in `original_isos/`; no
  manifest → the launcher's development-run path, which still carries gd06's
  module):
  - **Lists and matches:** `--list` → `gd-game-06  Guitar Hero: Aerosmith  0.0.0
    yes  yes  <the Aerosmith ISO>` — **MODULE=yes** (no "launcher without a
    module") and the **disc auto-matched by iso_signatures / title ID 41560819**
    (no "launcher can't load ISOs").
  - **Launches to gameplay:** `--play gd-game-06 --iso <disc> --exit-after --
    --walktest quickplay` (offscreen, dummy audio) → the launcher loaded the
    module (`gd06 0.0.0 started`, window made for Vulkan), **loaded the ISO**
    (`disc: ... Aerosmith ... disc: 64 songs`), played **dreampolice** (z_Nipmuc)
    to **quickplay_win_song_fs** with the song hooks firing (hit_note 20,
    star_hit_note 3, crowd sfx), walk PASSED, **exit code 0** after 64.2 s, module
    unloaded cleanly back to the launcher.
  - **Screenshot LOOKED AT (Read the PNG; GD06_WALK_SHOTS/highway.bmp → PNG,
    offscreen, scratch XDG_DATA_HOME; not in the repo):** the real Guitar Hero:
    Aerosmith in-song highway rendered **from the launcher** — the 5-lane
    fretboard with Aerosmith's skull / star / winged highway art, green gems
    streaming, a note struck with the orange flame hit effect, the score "282"
    and the star-power multiplier ring at left, the ROCK meter dial at right,
    over Nipmuc High with the silhouetted band (singer at the mic, guitarists)
    under warm stage light. Not a crash / placeholder / GH3 screen.
  - **verify_bundle.sh exercised:** a staged minimal gd06 bundle (trimmed
    games.yaml, manifest.json, recomplauncher.json, the module + av libs +
    licence) → "complete (1 game module with their libraries)"; a negative check
    (hide libavcodec) correctly FAILS, so the new `06)` arm is live.
- **Changes are gd06-only / gd06's own launcher lines; no other lane touched.**
  Only gd-game-06's rows in the backlog. All build output is in git-ignored
  `engine/build/` and `gd-engine-launcher/build/`. Work left **UNCOMMITTED**
  (shared branch).
- **Left for the owner:** the full multi-platform release/package build
  (`build.sh all` — memory-heavy, other lanes active, the owner triggers it) and
  the known M4/M5 FX follow-ups (HandCamSmooth 60/240 fps determinism D10, the
  AO_short between-tier completion-movie frame, the finer store / Vault items +
  the achievement list enumerated, the mp battle/boss BATTLE-start past the timed
  helper, the legacy GH3-content test scenarios).

## 2026-11-17: GD06-006 M5 career and modes (done, uncommitted)

- **Aerosmith's career runs on gd04's generic engine with ZERO career-flow
  engine changes (DECISIONS D11).** The career is all data / QScript
  (GH3_Career_Songs, GH3_Career_Progression, guitar_progression.qb,
  career_menu_flow.qb) and the copied gd04 atoms / flow-manager / Transition
  engine runs it. M5 was data archaeology + test-harness (walk) adaptation + a
  walk-only star affordance; no new natives, no engine behaviour change.
- **`--walktest career_tier` PASSES a full tier 1 -> tier 2** (built
  GD06_VIDEO=ON, run GD06_WALK_FULLSTARS=1): boot > CAREER > a new band > MEDIUM
  > character > hub > venue (z_Nipmuc) > set list > **Dream Police (Cheap
  Trick)** > newspaper > cash > set list > **All the Young Dudes (Mott the
  Hoople)** > [the 2 opening-act songs complete the intro:
  Progression_CheckIntroSongsComplete -> Progression_TierAerosmithUnlock sets
  the NEW `current_transition = PREAEROSMITH`] > **career_aerosmith_confirmation_fs
  ("AEROSMITH!")** > start_aerosmith_encore plays **makeit** (Aerosmith) >
  newspaper > set list > **unclesalty** > [tier songs complete] >
  **career_encore_confirmation_fs ("ENCORE!")** > start_encore plays
  **DrawTheLine** (the encore) > newspaper (**careertier1.complete = 1**) > cash
  > **career_select_venue_fs = tier 2 / Max's Kansas City (z_MaxsKC)**. 5 songs
  started, 5 ended; no unhandled native.
  - Objective structure (GH3_Career_Songs): **6 tiers**, each an opening act's
    2 songs (defaultunlocked = 2) then Aerosmith's set then an Aerosmith encore;
    tiers by venue: z_Nipmuc, z_MaxsKC, z_Fenway, z_nine_lives, z_JPPlay, z_hof;
    the opening acts are Cheap Trick / Mott the Hoople (t1), Joan Jett / The
    Kinks (t2) etc.; `AO_short_<N>` movies between tiers (Bink + subtitles);
    **tier 6 is Joe Perry's boss battle** first (career_tier6_bossunlock ->
    bosscomplete), then the set and encore. No co-op career (D11).
  - A career walk forces a ~20 s win, too few stars to count toward progression
    on a long song; **GD06_WALK_FULLSTARS=1** reports a 5-star score
    (base_score x 2.8 + 1, as GH3's setlist_debug_completesong does), a walk-only
    test affordance (DECISIONS D12), so the tier progresses deterministically.
- **`--walktest boss` PASSES: Joe Perry reached.** The Glitches (every tier
  complete) > EASY > the tier-6 venue (z_hof) > the set list's **bossjoe**
  ("walk: setlist on bossjoe (tier 6, song 2)") > **career_battle_help_fs =
  "JOE PERRY WANTS TO BATTLE!"** (Boss_Joe_Props, the battle-gems helper, Joe
  Perry's character art). Reaching the boss is the gate.
- **All four modes selectable (DECISIONS D13), reached through MULTIPLAYER.**
  The MULTIPLAYER "CHOOSE MODE" menu lists **FACE-OFF, PRO FACE-OFF, BATTLE,
  CO-OP** (menu_mp_select_mode.qb; offline the CO-OP item is mode = p2_quickplay
  -> the coop_quickplay flow; the online-only p2_coop is hidden). Main-menu
  MULTIPLAYER is item 2 ({D, D, C}; Aerosmith drops CO-OP CAREER). The two-player
  controller select needed the port order (P2 joins with Down, then both
  confirm). Walks: **faceoff** PASSES to the results, **proface** PASSES to the
  results (one shared difficulty), **battle** PASSES to the two-player "BATTLE
  MODE!" help (its BATTLE arms after a timed helper animation, as the boss's
  does), **coop** PASSES into coop_quickplay_select_difficulty_fs (co-op
  quickplay is new, reached via MULTIPLAYER). No unhandled native in any.
- **Screenshots LOOKED AT (Read the PNGs; offscreen render, GD06_WALK_SHOTS,
  scratch XDG_DATA_HOME; BMP->PNG; not in the repo):**
  - **career_tier_setlist** — the real Aerosmith tier screen: torn-paper set
    list, SETLIST + THE VAULT tabs, "1. GETTING THE BAND TOGETHER", "Dream
    Police — CHEAP TRICK, 1979", "All the Young Dudes (cover)* — MOTT THE
    HOOPLE, 1972", "*COVER OF LISTED BAND'S HIT".
  - **career_aerosmith** — the "AEROSMITH! / LET'S ROCK" entrance confirmation
    (raised fists, a guitarist on stage under lights, a US flag).
  - **career_encore** — the "ENCORE!" confirmation (raised fists, a guitarist
    raising his guitar, the venue's EXIT sign).
  - **career_tier2** — tier 2's venue select: a "max's kansas city presents
    Aerosmith" poster with the real Aerosmith band photo (progression reached
    tier 2).
  - **boss_help** — "JOE PERRY WANTS TO BATTLE!" with Joe Perry's character art
    (black hair, white shirt, a pink/white Les Paul) + the battle-gems rules.
  - **mp_controller** — MULTIPLAYER "SELECT CONTROLLER" (two-player assignment).
  - **coop_mode_menu** — MULTIPLAYER "CHOOSE MODE": FACE-OFF / PRO FACE-OFF /
    BATTLE / CO-OP.
  - **battle_help** — the two-player "BATTLE MODE!" help over an Aerosmith venue.
  Every one is the real Aerosmith career / mode screen, not a GH3 screen.
- **Content wiring.** The **43 songs** are in `gh3_songlist` (31 career across
  the 6 tiers in GH3_Career_Songs, 11 bonus via GH3_Bonus_Songs / Bonus_
  progression, the Kings and Queens credits version); the career walk played 5
  of them and quickplay/venue work already played others. The **store / Vault**
  is reachable (THE VAULT tab on the set list; store_fs sells characters /
  guitars / finishes / songs through gd04's generic store). **Achievements**:
  the game defines Achievement_Atoms / Achievements_SongWon, but those only
  award Xbox Live achievements (WriteAchievements) — not applicable to the
  offline port, the same faithful no-op as gd04 (modes.md GD04-013).
- **Changes are gd06-only and small:** `engine/src/app.cpp` (the
  GD06_WALK_FULLSTARS affordance in `songResult()`), `engine/src/tests/walk.cpp`
  (the `career_encore`/`career_tier` tier walk rewritten for Aerosmith's flow;
  `boss` -> bossjoe / tier 6; the mp mode nav {D,D,D,C}->{D,D,C} and the
  controller-select order; `coop` repurposed to co-op quickplay via MULTIPLAYER;
  the battle walk terminal at its help). Built the GD06_VIDEO=ON binary at
  `engine/build/linux-video` (-j2). Work left UNCOMMITTED (shared branch); only
  gd-game-06.
- **Left for M6 / follow-up:** the AO_short_<N> between-tier **completion-movie
  frame** capture (walks skip movies unless GD06_WALK_MOVIES=1; the movies exist
  and play, M2 proved Bink playback) — capture one with GD06_WALK_MOVIES or
  `--playmovie AO_short_2`; the finer **store / Vault items** (per-category
  Aerosmith unlockables) and the **achievement list** enumerated against the
  game's definitions; the mp **battle / boss BATTLE-start** past the timed
  helper-ready animation (reached, just not auto-started in the walk); the GH3-
  specific legacy test scenarios still carrying GH3 content (`boss_devil`,
  `boss_helper`, `boss_keys`, `glitches`, `venues`, `vault` reference GH3 songs
  / 8 tiers / bosses — they do not break the build but need Aerosmith song ids
  and the 6-tier layout).

## 2026-11-17: GD06-005 M4 venues and band, core (done, uncommitted)

- **All 6 Aerosmith venues render behind the highway with the band, on gd04's
  venue/band system unchanged.** The system is generic: `venue::venueZones`
  reads Aerosmith's `LevelZones`, `VenueStage::chooseZone` reads `current_level`
  (default `load_z_Fenway`), `venue::load` reads the zone pak — no GH3 venue name
  is hardcoded on the render path (the `z_artdeco` special-cases in
  venue_stage.cpp are encore-stance / lighter details that simply never match an
  Aerosmith zone). Nothing in the engine needed changing to make the venues draw;
  only two song **ids** were GH3-specific (below).
- **`--venuetest` over the six venues** (new default song `dreampolice`; GH3's
  `slowride` isn't on Aerosmith's disc): every venue loads with **0 missing
  textures**, all meshes drawn with GH3's shaders (0 approximated), per-venue
  camera arrays named by zone (GUITARIST / SINGER / DRUMMER / GUITAR / STAGE /
  MID / LONGSHOT / ZOOM …), **73 cuts + 351 light-show snapshot changes** each,
  band-shadow acne **0.3–0.5 %** of lit texels (gate < 10 %; the two-cascade
  shadow fix carries over). Objective result: z_Nipmuc 275 meshes/26 lights,
  z_MaxsKC 353/38, z_Fenway 374/27, z_nine_lives 398/18, z_JPPlay 358/32,
  z_hof 305/23. **4 of 6 are frame-rate-independent** (993 checkpoints identical
  at 60 vs 240 fps). **z_MaxsKC (43.7 s) and z_JPPlay (87.3 s) FAIL** the camera
  60-vs-240fps check — the active shot is `CameraCuts_HandCamSmooth`, a real VM
  script (guitar_cameras.qb) that runs through the `Ctl::Script` / `ccam_domorph`
  morph path; its morph is not perfectly frame-rate-independent for a **mid-song**
  VM camera shot (GH3 only used VM camera scripts for intro/encore/boss, so the
  pattern never surfaced in gd04). Non-fatal: the venue renders correctly at the
  real/target frame rate; `aim()` (band tracking) is deterministic and is not the
  cause. Left for a follow-up (shared Ctl::Script code; would want a
  GD06_*_LEGACY switch).
- **Screenshots LOOKED AT (Read the PNGs) for all 6** (offscreen render,
  GD06_VENUETEST_SHOTS, scratch XDG_DATA_HOME; BMP→PNG; not in the repo):
  - z_Nipmuc (**Nipmuc High**): blonde guitarist + ginger singer on a wooden
    stage, dense crowd with raised hands, amps, warm beams; a second shot the
    drummer mid-stroke behind a full kit. Real Nipmuc, band animated.
  - z_MaxsKC (**Max's Kansas City**): singer at the mic (studded belt), 2
    guitarists (one a black Les Paul), neon "…sas city" sign, orange club
    footlights, pink/blue stage lights, crowd. Real Max's, band animated.
  - z_Fenway (**The Orpheum**): singer + guitarist silhouetted under teal
    spotlight beams and a neon guitar/sign — theatrical light show.
  - z_JPPlay (**Half-time show**): drummer in a **distinct stadium rig** (cap,
    tank, shorts — per-venue drummer rig) mid-stroke + guitarist + arena crowd,
    magenta truss lights.
  - z_nine_lives (**Moscow**): blue blackout mood, singer + guitarist, stars on
    the stage floor, Russian set shapes.
  - z_hof (**Hall of Fame**): drummer behind the kit, teal rim light, wall
    light stripes.
  Every one is the real Aerosmith venue with the band — not a GH3 venue, not an
  empty stage, no crash.
- **The band works unchanged:** guitarist, singer (at the mic), drummer (per-
  venue rig, animated arms/twist) and bassist are placed at their
  `<zone>_TRG_Waypoint_<member>_start` waypoints, skinned and animated on the
  song by gd04's body/hand/performance animation (no Aerosmith-specific band
  code needed for the core). The chart's **aux track is not needed** for on-stage
  animation — the body loop + strum/fret/finger + `<song>_performance` drive it
  fully (OPEN_QUESTIONS 6 / D6 stand; revisit only if a specific missing motion
  is found).
- **Menu 3D backdrop fixed (D9).** app.cpp hardcoded GH3's `slowride`, absent on
  the disc, so the 3D menu backdrop never loaded (logged "not on the disc" and
  skipped; the 2D menu art rendered alone). Now it loads **beyondbeautiful** (the
  song `create_main_menu` sets as `current_song`) at the default venue z_Fenway:
  the boot walk logs "menu backdrop: z_Fenway, ready in 1805 ms", the walk still
  passes, and the menu screenshot is the real Aerosmith main menu (band art +
  winged logo + CAREER/QUICKPLAY/… , Xbox LIVE greyed) with **no regression** —
  the 2D art covers the live venue as in GH3. `GD06_MENU_SONG=<id>` overrides;
  `GD06_MENU_BACKDROP_LEGACY=1` restores slowride (the M2/M3 no-backdrop state).
  Note GH3's own `create_main_menu_backdrop` is also 2D art — the live venue
  behind it is a gd04 **port feature**, not an on-disc 3D menu.
- **Test song defaults** changed slowride → `dreampolice` in venuetest.cpp and
  chartest.cpp so `--venuetest` / `--chartest` run out of the box on Aerosmith
  (test-only; GD06_VENUETEST_SONG / GD06_CHARTEST_SONG still override).
- **No missing / UNHANDLED venue natives** during a song (checked the full log).
  The new FX are scripted zone data the generic engine already runs when
  triggered, but a plain quickplay venuetest never fires them: encore **fireworks**
  + **pyro fountains** (z_hof/others' `*_Encore_Firework*` / `Pyro_*` nodes) and
  star-power **Tesla** bolts (`*_GFX_TRG_TeslaNode_*` exist in the zones; gd04's
  default star-power case already shoots Tesla bolts from a zone's TeslaNodes).
  `SetLightMapOverlayParams` is still a no-op (D5) and is **not** called from any
  zone script — a guitar-FX light-map overlay to implement later with
  DeleteAllTeslaEffectObjects. Left for M4b/M5: these FX under their real
  triggers (needs an encore / star-power test), star-power wings/bones, the
  Aerosmith entrance / DMC-intro transitions, and the opening-act bands at their
  waypoints.
- **Changes are gd06-only and small:** `engine/src/app.cpp` (menu song, env
  switches), `engine/src/tests/venuetest.cpp` + `chartest.cpp` (default song),
  `engine/src/tests/walk.cpp` (the boot walk saves `menu.bmp` once the backdrop
  posed). Built the GD06_VIDEO=ON binary at `engine/build/linux-video` (-j2).
  Work left UNCOMMITTED in the working tree (shared branch); only gd-game-06.

## 2026-11-17: GD06-004 M3 one Quickplay song to results, with audio (done, uncommitted)

- `gd06 --walktest quickplay` PASSES end to end (built with GD06_VIDEO=ON, see
  below): `bootup_press_any_button_fs > main_menu_fs > quickplay_select_difficulty_fs
  > quickplay_setlist_fs > song > song_over > quickplay_win_song_fs >
  quickplay_toprockers_fs > quickplay_setlist_fs`; songs started 1, ended 1;
  0 black frames; loading screens held >= 2.37 s. The walk's song is
  **dreampolice** (Cheap Trick, the quickplay setlist's first song), venue
  z_Nipmuc (Nipmuc High), band Johnny/Drummer/Bassist/Singer.
- **Quickplay flow = GH3's, unchanged.** Aerosmith's single-player quickplay is
  identical to GH3 (quickplay_menu_flow.qb: quickplay_select_difficulty_fs ->
  quickplay_setlist_fs -> quickplay_play_song_fs -> win/fail). main_menu_flow.qb
  `select_quickplay` -> quickplay_select_difficulty_fs (menu_main.qb
  main_menu_select_quickplay sets part=guitar). The new
  `menu_quickplay_select_mode.qb` (create_select_quickplay_mode, SINGLE QUICKPLAY
  / MULTIPLAYER CO-OP) is NOT the song path -- it is the OPTIONS > Top Rockers
  high-score viewer (options_menu_flow.qb options_top_rockers_select_mode_fs).
  Co-op quickplay is its own flow (coop_quickplay_menu_flow.qb) reached via
  MULTIPLAYER. The only flow fix M3 needed: the main menu drops CO-OP CAREER, so
  QUICKPLAY is item 1 -- walk nav main_menu_fs {D,D,C} (gd04) -> **{D,C}**
  (walk.cpp). The other quickplay-path walks (results_art, back, pause,
  gamepads, ...) still carry gd04's {D,D,C} and need the same one-line fix when
  their milestones run them (noted, not changed here).
- **AUDIO confirmed on Aerosmith's banks (this also closes GD06-008's M3 check).**
  gd04's FSB reader (data/fsb.cpp, per-bank key "5atu6w4zaw", bit-reverse per
  offset) decodes every Aerosmith bank: `fsb.py check` 61 banks, 648 samples, 0
  real problems (the one flagged line is a streamall stem-name truncation, menu
  sfx, as on GH3). dreampolice.fsb.xen: 5 stems (crowd/guitar/preview/rhythm/song),
  44100 Hz stereo, 10,949,888 samples = 248.3 s. Objective playback check: the
  walk with GD06_AUDIODUMP produced a NON-SILENT song mix -- peak 0.736, RMS
  0.0629, 26.9 % non-zero float samples (vs a GD06_VIDEO=OFF build: exactly 0,
  silent, no XMA decoder); SFX bus also non-silent (peak 0.497, RMS 0.048).
  Lines up with the chart: dreampolice Expert lead ends at 239.0 s, inside the
  248.3 s audio (fade-out after the last gem). The song hooks fired for real
  (hit_note 20, star_hit_note 3, whammy, 46 fretbars, 44 drum kicks, crowd
  events), so the stems are time-aligned to the chart.
- **XMA needs FFmpeg: M3 is a GD06_VIDEO=ON build.** The song cannot start
  without the XMA decoder ("song: cannot start <song>: built without FFmpeg");
  the GD06_VIDEO=OFF M1/M2 build drops to quickplay_fail_song_fs. Configured a
  separate build dir `engine/build/linux-video` with
  -DGD06_VIDEO=ON -DGD06_FFMPEG_DIR=<gd04's prebuilt FFmpeg 7.1.x prefix>
  (reused gd-game-04/engine/build/ffmpeg/linux/ffmpeg-prefix, same version;
  gd06 has no prefix of its own yet). Full build 304 files, -j2, ~5 min. The
  M1/M2 GD06_VIDEO=OFF build at engine/build/linux is left intact.
- **Highway screenshot LOOKED AT (Read the PNG): real Aerosmith in-song
  highway.** Captured ~14 s into the song (after the 8 s intro camera and the
  highway rise; a 6 s shot caught only the intro/crowd camera). The GH3-engine
  5-lane fretboard with Aerosmith's winged/skull highway art, green gems
  streaming, a note struck with the flame hit effect, the score ("282") and
  star-power multiplier ring at left, the ROCK meter dial at right, over
  Nipmuc High with the band (singer at the mic, a silhouetted guitarist with a
  Les Paul) under warm stage light. Gameplay (gems, HOPO/scoring, rock meter,
  star power) is live -- gd04's gameplay runs unchanged on Aerosmith's data.
  (BMP via GD06_WALK_SHOTS, offscreen render, scratch XDG_DATA_HOME; not in the
  repo.)
- **The aux chart part is NOT a playable part (GD06-004 / OPEN_QUESTIONS 6
  resolved).** Every chart adds `<song>_song_aux_<Diff>` (+ `_aux_<Diff>_Star` /
  `_StarBattleMode`) with real, distinct note data (dreampolice Expert: lead 658,
  rhythm 1030, guitarcoop 0, aux 630; dreamon: lead 619, rhythm 338, aux 181 --
  aux is never a copy of another part). But no player is ever assigned it: the
  GH3 engine builds the gameplay section from player_status.part, the only
  part-select UI (menu_choose_part.qb) offers LEAD/RHYTHM or GUITAR/BASS, never
  aux; quickplay sets part=guitar; no script sets part=aux and the XEX has no
  "_aux" literal. So it is an auxiliary (non-interactive) track -- most likely an
  on-stage band-member animation / authoring reference -- revisit with the band
  in M4 if useful. chart.cpp reads only the sections it names, so aux parses and
  is skipped; no Part / "_aux" infix added (documented in chart.cpp).
- **The 7 tag-setup warnings are cosmetic and shared with gd04 (documented, not
  changed).** setup_songtags / venuetags / unlocks / setlisttags / character_tags
  build a `globaltag_checksum` accumulator by QB key-concatenation
  (`globaltag_checksum = globaltag_checksum + <name>`); the first add is
  none/key + key, which the VM (vm.cpp kAdd) does not implement for keys -- it
  logs "UNHANDLED arithmetic on ..." once per script and returns none, then the
  flow continues. gd04's vm.cpp is byte-identical here and ships with working
  saves/top-scores, so these are benign; the accumulator feeds a combined
  checksum, not the per-tag saves (which use explicit names -- proven by the walk
  reaching toprockers -> autosave -> setlist with 0 black frames and top scores
  cleared/rewritten). Implementing GH3's key+key concatenation in the VM would
  remove them but is a shared-pattern behaviour change (would need a GD06 LEGACY
  switch and broad validation); deferred, not needed for M3.
- Left for M4: the menu 3D backdrop still hardcodes GH3's `slowride`
  (app.cpp:935; Aerosmith's menu song is `beyondbeautiful`). Left as-is on
  purpose -- switching it activates the 3D menu-backdrop venue path that M4 owns
  and risks a visible menu regression; the 2D menu renders correctly and the
  bad song id just logs + skips the backdrop. Also M4: full venue/band
  validation (the in-song venue already renders via the copied engine), the
  aux track's possible band-animation role, per-song face anims.
- No runtime behaviour was changed that needs a GD06_*_LEGACY switch (the walk
  nav + the shot action are test-only; the chart/aux change is documentation).
  Work left UNCOMMITTED in the working tree (shared branch); only gd-game-06
  touched.

## 2026-11-17: GD06-003 M2 boot to the main menu (done, uncommitted)

- `gd06 --walktest boot` reaches Aerosmith's main menu:
  `bootup_sequence_fs > bootup_press_any_button_fs > bootup_do_memcard_sequence_fs
  > main_menu_fs` (the `bootup_using_guitar_controller_fs` intro is skipped,
  correct for the port -- no GH guitar hardware). Walk passes; no unhandled
  native CALLS in the log (the 7 "UNHANDLED arithmetic on none/key" lines are
  the M1 tag-setup warnings -- setup_songtags / venuetags / unlocks /
  setlisttags / character_tags -- not native errors; noted for M3).
- Screenshot LOOKED AT (Read): the real Guitar Hero: Aerosmith main menu -- the
  winged GH-Aerosmith logo, the band artwork (Steven Tyler, Joe Perry with the
  white Les Paul, drummer, bassist, young guitarist), the items CAREER (focused),
  QUICKPLAY, MULTIPLAYER, TRAINING, **Xbox LIVE greyed** (offline, correct),
  OPTIONS, and the SELECT / UP/DOWN / GAMEPADS help bar. Not a crash/placeholder.
  (BMP via GD06_SCREENSHOT offscreen, scratch XDG_DATA_HOME; not in the repo.)
- Real M2 blocker: Aerosmith's **new** `check_for_any_input`
  (scripts\guitar\menu\menu_press_any_button.qb) gates the press-any-button flow
  on `IsStandardGuitarControllerPluggedIn` -- a GH3-era C native that GH3's own
  boot script never called, so gd04 never registered it. Unregistered it wedged
  the flow. Answered **false** (the port has no GH-branded guitar hardware; the
  player's keyboard/pad is their instrument), which reaches `main_menu_fs`
  cleanly with the guitar-controller intro correctly skipped -- the console
  outcome for a valid player. `GD06_STD_GUITAR_LEGACY=1` forces the on-disc
  "true" for console comparison. In `engine/src/ui/natives_game.cpp` next to
  IsGuitarController (DECISIONS D4).
- The 9 new Aerosmith C functions (FORMATS.md / cfuncs.tsv), all registered in
  `engine/src/ui/natives_flow.cpp`, answered offline (OPEN_QUESTIONS 4):
  RefreshXUserSigninInfo / ClearXUserSigninInfo (sign-in cache; no Live, no-op;
  ClearXUserSigninInfo is called on the boot path by create_press_any_button_menu),
  WriteSongDataToFile / DeleteAllSongDataFromFile / ResetScoreUpdateReady
  (per-song score bookkeeping; no-op, kept by the port's own save path, M3;
  ResetScoreUpdateReady is called at the top of create_main_menu),
  DeleteAllTeslaEffectObjects / SetLightMapOverlayParams (new venue FX /
  light-map overlay; no-op, M4), IsStarEntry / IsUserMusicPlaying (no Live
  leaderboards / no Xbox custom soundtrack in the port; false).
- menu_main.qb builds its menu items (create_exploding_text / create_UI_frame /
  ui_physics.qb) as ordinary screen elements -- the engine's menu_objects handles
  them, no new element type, no "UNHANDLED composite object member".
- Left for M3/M4: the 3D menu-backdrop venue still hardcodes GH3's `slowride`
  (app.cpp:935 "menu backdrop: not on the disc: slowride_song.pak"); Aerosmith's
  menu song is `beyondbeautiful` (create_main_menu) -- a gd04 leftover, 3D
  backdrop venue work belongs to M4. The menu itself renders correctly without it.
- Build: `cmake --build build/linux --target gd06 -j2` (GD06_VIDEO=OFF),
  `free -g` >= 16 G before each build, one binary at a time. Work left
  UNCOMMITTED in the working tree (shared branch).

## 2026-11-17: GD06-002 M1 data layer (done)

- Copied gd-game-04's tracked engine into gd-game-06, renamed throughout:
  `engine/` (281 files), `tools/` (the Python decoders + `FindBytes`/`FindConst`
  Ghidra scripts; the discovery Ghidra scripts were already here), the 8
  `build_commands/`, `local_build.sh`. gd04 -> gd06, GD04 -> GD06, Gd04 -> Gd06
  (identifiers, env vars `GD06_*`, include guards, CMake project/target `gd06`,
  `gd-game-04` paths -> `gd-game-06`, the Android package `...gd06` and
  `GD06Activity`, the `res/gd06.*` icons). gd04's audio handling kept intact
  (`data/fsb.*`, `audio/song_stream.cpp`, `audio/sfx.cpp`, the per-bank key) -
  Aerosmith's banks use GH3's container (DECISIONS D2).
- Identity: version `0.0.0` (CMake, game-info.yaml), user-facing name
  "Guitar Hero: Aerosmith" (`app_ids.h` kDisplayName/kWindowTitle, `main.cpp`,
  `app.cpp` on-screen, `disc.cpp` messages, `app_icon.cpp` .desktop, macOS
  bundle name), save folder `gd06` (kPrefApp), title ID **41560819** in
  `game-info.yaml` `iso_signatures`. The engine identifies the disc by
  `qb.pak.xen`, not the title ID; it reads the data paks, not the XEX, so the
  base/entry addresses (0x82000000 / 0x8213A0B0, unchanged from GH3's base)
  are not on the engine boot path - they live in game-info / Ghidra docs.
- `.ska`: two differences found and fixed (docs/FORMATS.md "Animations").
  (1) compressed rotation-key scale 1/32768 (GH3: 1/16384) - the 30,333 the
  kickoff flagged; objective check over the whole disc: 0 of 8,164,872 keys
  with |xyz| > 1, max |xyz| exactly 1.0. (2) a new multi-key uncompressed-float
  track (`flags & 0x10000000` + `0x4000`, 7 clips in perm_anims): 16-byte
  `(0, x, y, z)` entries, parsed with w derived (Q) / xyz translation (T),
  frames by index (provisional -> M3/M4). Fixed in `anim.cpp` and `anim.py`.
- Chart's new aux part (`<song>_song_aux_<diff>`) noted in `chart.cpp`: the
  reader names only the sections it uses, so the aux sections parse as QB and
  are skipped cleanly; wiring to a `Part` is M3.
- Gate: built the `gd06` Linux target (CMake/Ninja, GD06_VIDEO=OFF, -j2) and
  ran `gd06 --formattest <Aerosmith .iso>`: all files parse, 0 problems
  (see the agent report / commit for the figures). Audio reader compiles
  unchanged. Work left UNCOMMITTED in the working tree (shared branch).

## 2026-11-17: GD06-001 discovery (done)

- Disc extracted into `gd-game-06/extracted/` (806 files, 4.67 GB) with
  gd04's `xdvdfs.py`; name table `extracted/names.tsv` (41,234 names).
- `default.xex` imported into `gd-game-06/ghidra/gha.gpr` exactly as gd04
  (XEXLoaderWV, FixXexFunctions, DumpCFuncs, ExportAnalysisXex; 6 GB heap,
  7 minutes): 33,178 functions, all decompiled, 8,282 strings, 85,940 data
  refs, 1,401 C-function table entries in `ghidra/analysis/`.
- Compared with GH3 (gd04's rip and analysis): every format but the `.ska`
  rotation keys parses with gd04's decoders; the XEX is GH3's build
  (`GH3_xp1`) with ~97% of functions pairing by size and callees and 9 new C
  functions; content: 6 new venues, 43 songs, Aerosmith's band, a career
  with opening acts and encores, Joe Perry as the boss, no co-op career.
  Details: `docs/FORMATS.md`, `agents/CONTEXT.md` "The game".
- Recommendation: copy gd04's engine, including its audio handling
  (DECISIONS D2); owner questions in OPEN_QUESTIONS.md.

Agent time: about 45 minutes (extraction, Ghidra, comparison, docs).

## Plan (milestones; estimates are agent time)

Estimates come from measured agent time on the closest precedent:
gd-game-10 went from a copy of gd04's engine to a Quickplay song played to
the results screen in about 2.7 h (11-10 23:50 -> 11-11 02:32) on a newer,
more different engine (gh4). Aerosmith is GH3's own engine, so the early
milestones should be faster; the venues, band and career are new content
and carry most of the work.

| Milestone | What | Gate | Estimate |
| --- | --- | --- | --- |
| M1 data layer | Copy gd04's engine / tools / build files as gd06 (D2, audio handling kept), names and title ID, the `.ska` keys, the chart's aux part, `--formattest` over Aerosmith's disc | `gd06 --formattest <disc>`: all files parse; pak / qb / qbscript / anim checks 0 problems | 1-1.5 h |
| M2 boot to main menu | Boot movies and legal screens, the new `menu_main`, UI frame / physics / exploding text, the 9 new C functions, sign-in natives answered offline | `--walktest boot` reaches Aerosmith's main menu, screenshot; no unhandled native in the log | 1-2 h |
| M3 one Quickplay song | Quickplay mode select, set list, difficulty, a song on the highway to the results (song audio via gd04's handling), co-op quickplay selectable | `--walktest quickplay` plays one song to the results and back to the set list | 1-2 h |
| M4 venues / band | The six venues (scenes, lights, cameras, crowd, particles), Aerosmith's members with their per-venue rigs and per-song anims, DMC, the entrance / DMC-intro transitions, star-power effects | `--venuetest` over the six venues; every band member animated in a song | 4-6 h |
| M5 career | Tiers with opening acts, Aerosmith unlock, encores, movies with subtitles between tiers, Joe Perry's boss battle, Bonus songs, store / Vault, cheats, saves, face-off / pro face-off / battle | a career walk through tier 1 into tier 2; boss battle walk | 3-5 h |
| M6 module / release | Launcher module (build_module_06, games.yaml, iso_signatures with title ID 41560819), packages on every system, verify_bundle, ready_to_play (version only when the owner asks) | every bundle verified; the launcher runs gd06 on Linux | 1-2 h |

Total: about 11-18 h of agent time, spread over several runs. Builds at -j2
with one build at a time while another agent shares the machine.
