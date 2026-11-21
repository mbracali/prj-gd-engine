# gd-game-07: decisions

Each entry: what, why, how to undo.

## D1. Discovery uses gd-game-10's tools in place (2026-11-17)

What: the disc was extracted and checked with gd-game-10's Python tools (a
scratch copy in `agents/scratch/t/`) and gd-game-10's `gd10 --formattest`,
read-only; only gd10's Ghidra scripts were copied into
`gd-game-07/tools/ghidra_scripts`, unchanged except their `@category`.
Why: GD07-001 writes no engine code; copying the engine is M1's decision
(D2). Undo: none needed.

## D2. Copy gd-game-10's engine (owner approved 11-17), no shared library (2026-11-17)

What: M1 copies gd-game-10's tracked `engine/`, `tools/`, `build_commands/`,
`local_build.sh` into gd-game-07, renamed gd10 -> gd07 / GD10 -> GD07 /
user-facing names -> Guitar Hero World Tour, version 0.0.0, then changes only
what World Tour needs.

Why:
- Metallica is World Tour's engine one year later: 1,692 of World Tour's
  1,708 QScript C functions are in Metallica, paks / QB / scripts parse
  with no change, and the C++ decoders already read most of the disc
  (FORMATS.md). gd10 already plays the gh4 highway, drums, vocals, the
  band and venues.
- The same pattern as gd-game-10 (a copy of gd04) and gd-game-06 (a copy of
  gd04): one engine per game folder, shared backends in gd-engine-core. A
  shared "gh4 library" would mean refactoring gd10 while another lane fixes
  its bugs.
- Cost: gd10 fixes do not reach gd07 by themselves; Metallica's [C]
  addresses become [I] in gd07 until re-found in World Tour's XEX. The
  Metallica-only parts (music store, Metallica's band / set pieces) are left
  in but unused, or removed where they get in the way.

Diff list (what gd07 changes in the copy, by milestone):
- M1: names / title (Guitar Hero World Tour, title ID 4156081A in
  `iso_signatures`, save folder gd07), the compressed `.ska` rotation keys,
  the earlier scene header (.skin / .scn / .mdl), the older Havok packfiles,
  the `.wav` chunks, `.ske` / `.table` / `.img` / `.tex` leftovers.
- M2: World Tour's boot / main menu, the 16 World Tour-only C functions,
  sign-in natives offline.
- M3: Quickplay (single and band) to the results.
- M4: the 23 venues, the full create-a-rocker and the band.
- M5: career (solo per instrument + band), battles, store, cheats, saves.
- M6: music studio / GHTunes (`.jam`, the studio zones, drum-kit banks).
- M7: launcher module.

Undo: if the owner prefers a shared library, M1 first extracts gd10's
engine-independent parts (gd10's selftest passing) and gd07 links them.

## D3. Song audio: gd10's bank reader as copied (owner, 2026-11-17)

What: gd07 keeps gd-game-10's song-bank reader (the key derived as the XEX
does, gd-game-10 D7) as copied, so songs play from the player's own disc.
M3 confirms the key path on World Tour's banks. Why: owner's answer to
OPEN_QUESTIONS 1. Undo: replace the reader with a single silent entry point.

## D4. No commits or pushes by this lane (owner, 2026-11-17)

What: GD07 work stays uncommitted in the working tree; the agents sharing the
repository commit when needed. Why: owner's answer, several agents work on
the same branch. Undo: owner's call.

## D5. M1: the copy and World Tour's data layer (2026-11-17)

What: gd-game-10's tracked `engine/`, `tools/*.py|sh`, `build_commands/`,
`local_build.sh` copied from HEAD (not the working tree: the Metallica lane
had uncommitted edits) and renamed gd10 -> gd07, GD10 -> GD07 (the `GD07_*`
switches), gd-game-10 -> gd-game-07, "Guitar Hero: Metallica" -> "Guitar Hero
World Tour", engine version 0.0.0. gd10's backlog IDs in comments were put
back to GD10-NNN.

Caveats (as gd10's D1): the copied comments' [C] addresses are Metallica's
XEX, [I] for gd07 until re-found; their D-numbers and `docs/formats.md` /
`docs/FINDINGS.md` references are gd-game-10's files. "Metallica" in
comments describes where a finding came from. `engine/README.md` still
describes Metallica's content (to rewrite when gd07 is playable).

World Tour changes (all [C data: every file of the disc], `--formattest`
"all files parse"):
- `.ska` (data/anim.cpp): animated Q tracks packed back to back (no 16-byte
  alignment), keys by World Tour's decoder `decodeRotWt` [C 0x82445E88]
  (0x100: a u16 frame first; compressed x / y / z bytes; index keys =
  identity; standardkeyQ.bin is loaded byte-swapped [C 0x824459C0 ->
  0x82445730], so a byte component is the byte itself); animated T tracks are
  GH3's byte-coded keys (`decodePos`). Metallica's 16-aligned 8-byte keys are
  no longer read (gd07 reads World Tour only).
- Scenes (data/scene.cpp): 0x70-byte mesh records next to Metallica's 0x90
  (index block offset at +0x58, Metallica's +0x44.. at -0x18, +0x78.. at
  -0x20, no index block size word).
- Textures (data/texture.cpp): format 8_8 (0x0A) [I component order], class 1
  (normal map) lone .img, .tex hash tables of 9 bits.
- Format checks only (tests/format_checks.cpp): Havok 5.5 packfiles skip GH3's
  v4 walk; LZSS-wrapped .rag / .ske unpacked first; XMA2 .wav; .table word
  lists; volume slices sized by decoded format. Most also apply to Metallica's
  disc (gd10's own formattest fails on them: 2,799 there) - for the gd10 lane
  to pick up if wanted.

## D6. M2: the two VM rules World Tour's boot needs (2026-11-17)

- Struct literals merge an unnamed struct item (`{object = <object> <Data>}`):
  World Tour's ui_event_spawned builds the UI event's data so; without it the
  data held `_:struct` and the UI never saw `state` (the boot stopped at
  uistate_null) [C data: uistatemachine.qb; I: the rule, as scriptParams'].
  GD07_STRUCT_MERGE_LEGACY=1 undoes.
- AddParams keeps unnamed flags (`AddParams {$donthide}` in guitar_startup,
  read by its own `GotParam donthide`, so the legal screen stays up until
  boot_legal's 6 s wait ends) [C data: guitar.qb].
- The boot walk (tests/walk.cpp) has World Tour's first-boot steps:
  UIstate_band_name_logo and uistate_boot_guitar (both optional).

## D7. M2 / M3: what World Tour's menus and first song needed (2026-11-17)

- se_fittodims [C 0x82361010]: scale = dims / the element's size (x its
  scale with factor_start_scale), nothing when it fits and only_if_larger_*
  (default true); preserve_aspect_ratio takes the smaller factor [I].
- World Tour's _Uber_ScreenFX_* shaders read no framebuffer (no
  g_FramebufferSampler; Metallica's do): bloom1 = bloom, alpha 1; noise1 =
  noise x g_Color, alpha = intensity; bloom1_noise1 = their sum, alpha =
  intensity [C shaders]. The port copies the picture and adds the uber pass
  weighted by its alpha [I: render state not found]. Before, the pass replaced
  the picture (menus and songs were all film grain). GD07_UBER_BLEND_LEGACY=1.
- Aliases are dropped with their element (and its children): make_menu_frontend's
  DestroyScreenElement current_menu_anchor destroyed the new viewport_root
  when the alias was resolved by id, so the menu under it was rebuilt under
  the root and outlived its screen [C scripts; I]. GD07_ALIAS_LEGACY=1.
- `<menu> : menu_getselectedindex` -> selected_index (the menu's selected child).
- Walks: state names compare without case (World Tour: uistate_setlist_prompt);
  the HUD check waits 22 s (Default_Intro_Transition 20000 ms, Metallica 8000).
- Not done: World Tour draws menus (create_viewport_ui) into a viewport texture
  that replaces Alpha_texture*.dds on the sound stage; the port draws them
  over the screen at full size (the main menu's text too large).

## D8. World Tour's menus on the sound stage (2026-11-17)

World Tour draws its menus into textures shown on the 3D sound stage
(z_soundcheck), not over the screen [C scripts viewport_ui.qb,
menu_frontend.qb, ui_mainmenu*.qb, z_soundcheck_scripts.qb; C data]:
- create_viewport_ui: CreateViewport {style = viewport_ui_texture},
  CreateViewportTextureOverride {viewportid texture texdict}, a windowelement
  {viewport}. The port (ui/ui_system.cpp drawViewport, ui/menu_scene.cpp
  drawUiViewports) draws those windows into a 1280 x 720 target (cleared
  transparent, alpha written [I]) and VenueView uses it for every material
  texture of that key (main menu: Alpha_texture1.dds on Viewport_Plane;
  make_menu_frontend: Alpha_texture_drummer / _guitarist / _basist on the
  boards the stage's rockers wear). Not drawn there in a frame (the zone not
  in yet): on the screen, as before.
- Sectors: World Tour's sector flags are all 0; a LevelObject node's sector
  is in model space (placed at the node), levelgeometry in world space [C
  data: z_soundcheck's 299 sectors; songs' props too]. GD07_SECTOR_LOCAL_LEGACY=1.
- The menu scene moves level objects: obj_movetorelpos, obj_movetopos,
  obj_setposition, obj_stopmoving, obj_locktoobject, Obj_WaitMove
  (translation only, linear [I: as the songs']); Kill / Create put an object
  back where the level placed it [I] (UIResetPos runs on the press-start
  screen and the main menu; without it the props sank 3.46 per visit).
- Menus: position_children = false keeps the scripts' places; fit_major /
  fit_minor "fit content" scales the content into the dims, "fit content if
  larger" only down [I: the names] (GD07_FIT_LEGACY=1).
- Not verified: the props' lighting (all 41 zone lights on; the props look
  dark), the menus' depth of field (ApplyMenuTransitionDOF).

## D9. Why the stages were dark, and the band (2026-11-17)

Owner: the ports' scenes look darker than the originals. Found [C data /
microcode; I where noted]:
- Light show: World Tour's mood entries mostly name no `processor`
  (z_bayou intro / medium = {snapshots = [Intro_01]}); the port played a
  snapshot only for a named processor, so no snapshot ever played and every
  stage kept its base lights. LightShow_SharedProcessors' first is
  default_generic (cycle a snapshot on entering, the next on each 58
  snapshotchange note) [I: the default by its name]. GD07_LIGHTSHOW_DEFAULT_LEGACY=1.
- gh4 vertex lights: D_3ChkBox_Pass0 (and the gh4 lit templates) read each
  point light's attenuation from its fourth row (c179 / c183 / c187 / c191)
  and each spot's (attA, attB, coneX, coneY) from its fourth row, not GH3's
  c248..c253: the lights flagged vertexlight added nothing. GD07_GH4_LIGHTS_LEGACY=1.
  This is gd10's "Fed GH3's way they draw black" note.
- global_model_tex.pak's textures (_______Default_M_____.dds, 0xfd99221e,
  0xd729d3b1, ...) shared by the zones' materials: now read for a venue (only
  the ones its materials name). GD07_GLOBAL_MODEL_TEX_LEGACY=1.
- Checked and not the cause: the vertex colours (the props' 255, speakers'
  168..216), the ambient (the menu rockers get 1.0), gamma (the XEX uses no
  gamma surface formats; ApplyChangeGamma / GetGammaValues are debug C
  functions no script calls).
- The band: World Tour keeps cas_globalpak / cas_acc_bones / cas_deform in
  DATA/COMPRESSED/PAK (chunked), not DATA/PAK/CAS; the CAS archive now
  indexes them through the loaded pak (the presets' bodies are in
  cas_globalpak).
These rules are in the shared gh4 code, so gd-game-10 (Metallica) likely has
the same three lighting gaps.

## D10. Owner's bug pass: band, highway, the VM (2026-11-17)

Owner (after playing): band / characters, menus / UI and gameplay / highway
have bugs. Found and fixed [C scripts / data; I noted]:
- The band: World Tour builds it in script (create_band ->
  band_builder_create_band: players, CPU characters, random characters
  generated by cas_set_random_appearance); the port ran none of it and
  took Metallica's preset stand-in (a "Guitarist" preset drumming, Lars
  singing). app.cpp now runs the setup scripts before the song's venue
  loads and band::chooseBandGh4 reads band_builder_current_setup /
  band_builder_random_appearances. GD07_BAND_BUILDER_LEGACY=1.
- The VM, which the randomizer needs (and every other script):
  - array + array concatenates, unset + array is the array, struct + struct
    merges, array + value appends, key + key adds checksums, unset + key /
    number is that value [I: the rules from their callers]. GD07_ARITH_LEGACY=1.
  - A `$name` passed on in a struct literal (`{part_name = <arg> part_name}`)
    stays a name; only a precompiled inline struct's `$name` reads the
    global. GD07_KEYPTR_LEGACY=1.
  - `( <arg> x )` naming a global array reads the array only for one local
    alone in the parentheses, not for a member read
    (`( <arg> body_specific_parts . <arg> part )` stays the part's name).
  - New natives (natives_core.cpp, GD07_WT_NATIVES_LEGACY=1): Randomize,
    CheckModelBuilderLock, MathFloor, sqrt, GetRandomArrayElement,
    GetMaxPlayers, CreateIndexArray, NewGlobal, UpdateStructElement,
    RemoveComponent, AppendStruct, GetActualCASOptionStruct (the option's
    included structs expanded).
- The player's highway: the player's member from the band builder (a random
  appearance's CAS_Guitar_Highway), not always the first preset's (Axel).
- Tests: chartest / venuetest default to aggro (slowride / entersandman are
  not on World Tour's disc).
- Note: UNHANDLED lines print only with GD07_UILOG=1; a quickplay run had
  336 unique ones, now ~95 (memory / profiling / network stubs mostly) plus
  the "You Rock" exploding text's particle physics (applyforce,
  circleaabbcollisioncheck).

## D11. Owner's second bug list (2026-11-17)

Each rule has a GD07_*_LEGACY=1 switch back to the old behaviour [C: data /
code as noted; I: inferred].
- Band name screen: a descinterface covering its parent with a call `just`
  and a scale grows about that point (scale 1.3, just center: the panel and
  letter wheel centred) [C script ui_create_band_name_logo; I: the pivot].
  GD07_DESC_PIVOT_LEGACY.
- Band logo: the CAP layers composited on the CPU into the logo texture
  (CAS_Band_Logo_Details.textureasset) from cas_current_appearance's
  CAS_Band_Logo cap: decals from cas_captextures.pak by short name;
  x/y_trans, x/y_skew x 0.01, x/y_scale percent, rot milliradians, a x 2.55,
  colour = color_lookup_table.<c>.diffuse, flags 4 texture alpha only, 8 / 16
  mirror [C 0x823AB4D0 and its constants; I: the transform order, y down].
  Text layers not drawn yet. ui/cap_logo.cpp, GD07_CAP_LOGO_LEGACY.
- Menu stage nodes: IsInNodeArray / IsCreated answer from the zone (they were
  always false, so SafeCreate / SafeKill did nothing), Kill / Create
  {prefix}, CallScriptOnNode (each node's script spawned) [C scripts
  misclightutils.qb, z_soundcheck_*]. GD07_MENU_NODES_LEGACY.
- Main menu ONLINE and DOWNLOADS grey (128) and not focusable, as
  create_main_menu_elements' demo_mode_disable [C script]; Options'
  GUITARHERO.COM not made (owner). GD07_MENU_DISABLE_LEGACY,
  GD07_MENU_REMOVE_LEGACY.
- Focus colour stuck: GetScreenElementProps returns the font, so
  retail_menu_unfocus restores the item's font (the fire font stayed);
  material = null clears. GD07_GETPROPS_FONT_LEGACY.
- Band black in venues with a shadow light (z_Cathedral): World Tour's
  character receiver shaders read g_shadowLevel (c142) .xy (GH3's .zw); c140
  g_shadowDirWorldSpace set (towards the light [I]) [C: the shaders'
  microcode]. GD07_SHADOW_LEVEL_LEGACY.
- Music studio: ccam_domorph / PlayIGCCam read a pose given as an unnamed
  struct ({($jam_camera_props.<prop>)}), the control script's first pose at
  once, KillCamAnim {name} only that camera [C scripts jam_camera_script,
  do_jam_loading; I]. UI-only viewports of other styles (the band name,
  bandname_rendering) draw into their texture only, never on screen;
  IsAssetLoaded (zone / venue texture dictionaries) and
  IsTextureInDictionary answer yes [I]. GD07_POSE_STRUCT_LEGACY,
  GD07_IGCCAM_LEGACY, GD07_VIEWPORT_STYLE_LEGACY, GD07_ASSET_TEST_LEGACY.
- Mouse on the 3D boards (main menu, jackets, select controller): the
  pointer is mapped through the board mesh under it (VenueView::pickTexture:
  small textured meshes kept, projected with the live camera and node
  motion) to the board texture's point. GD07_POINTER_BOARD_LEGACY.
- Options' backdrop song: aggro (slowride is not on the disc).
- Head to Head needs two players (as the original): player 2 is enabled in
  GAMEPADS (Y on the main menu); with both, Select Controller -> mode works.
- Not done: the recording session (jam band, highway, sequencer:
  JamSession / BandManager natives), the band logo's text layers, the
  Select Controller board's icon layout (the 650 x 650 layout in a 450 x 450
  rotated body: placement not confirmed).

## D12. Owner's third report (2026-11-18)

- Main menu letters doubled / menus "zoomed out": a song-time IsCreated
  (app.cpp, the particles' named objects) replaced the menu stage's, so on
  the menu stage IsCreated was always false: SafeKill never hid the menu's
  moving plane (two planes, doubled letters) and SafeCreate re-made shown
  props, resetting their motion (the jackets left far from the camera). It
  now falls back to the menu stage's [C scripts misclightutils.qb].
  GD07_ISCREATED_CHAIN_LEGACY. Kill + Create still reset a prop
  (z_soundcheck_UIResetPos kills then creates every prop) [C script].
- Harbour water flat grey: the water template's microcode loops (Xenos
  LOOP_START / LOOP_END, aL-relative constants). The translator runs loops
  in its pc machine: the count / step from the shader's directional light
  constants (g_dirLight0Dir / 1Dir: 2 lights, stride 3) [I], relative
  constants without a0 use aL.
- The wait before a song: the intro transition is the zone's own
  (<zone>_Intro_Transition, 9900 ms in z_harbor / z_cathedral / z_hob),
  looked up before default_Intro_Transition (20000 ms) as
  Transition_GetProps does [C script]; read on the venue preload's worker.
  GD07_ZONE_TRANSITION_LEGACY.
- Flickering guitarist / drummer arms: the two-bone IK's elbow side flipped
  between frames (the animation's right arm is straight, so both turns
  reach the angle). The side kept: the animation's, or for a straight limb
  the rig's rest pose [I]. Measured with GD07_ARMLOG: 1702 bone jumps over
  0.1 m in 17 ms in 25 s of song -> 2. GD07_IK_SIDE_LEGACY.
- Not reproduced: clothes clipping into skin (close-ups of four random
  rockers, GD07_CAM_MEMBER, showed none); the parts carry no skin-hiding
  data and physique bone transforms are not applied to either.
- Dev aids: GD07_ARMLOG / GD07_ARMTRACE / GD07_IKTRACE / GD07_IK_FORCE,
  GD07_CAM_MEMBER=<i>,<m>[,<bone>], GD07_SKINLOG, GD07_BANDMATLOG,
  GD07_MATLIB_LITLOG.
