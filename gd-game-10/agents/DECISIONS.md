# gd-game-10: decisions

Each entry: what, why, how to undo.

## D1. gd10 starts from a copy of gd04's engine (2026-11-10)

What: `engine/`, `tools/`, `build_commands/`, `local_build.sh` copied from
gd-game-04's tracked files, renamed gd04 → gd10, GD04 → GD10,
gd-game-04 → gd-game-10, user-facing names → Guitar Hero: Metallica.
The engine version was set to 0.0.0 (matching `game-info.yaml`; not a bump).
Why: the kickoff (section 1); gh4 is GH3's engine one generation later.
Caveat: the copied code's comments keep GH3's **[C] addresses**: they are
GH3 XEX addresses, not Metallica's, until re-confirmed (each is **[I]** for
gd10 until then).
Undo: delete the copy.

## D2. Kickoff owner questions: defaults for the proof of concept (2026-11-10)

What: the run prompt's defaults (kickoff "Owner's answers"). Undo: the owner
answers differently.

## D3. Disc files: CHNK or stored; pak entries may be GZ (2026-11-10)

What: `Disc::read` inflates CHNK chains and returns other DATA/COMPRESSED
files as stored (GH3's whole-file deflate is gone); `Pak` inflates GZ-wrapped
entries at load. Why: every file on the disc is one or the other [C data].
Undo: none needed (GH3 discs are not read by gd10).

## D4. The UI state machine's call order [I] (2026-11-11)

What: `menu_change` leaves the top (ui_destroy_) and pushes (ui_init_,
ui_create_); `menu_replace` and `menu_back` also deinit what they remove;
going back re-creates the uncovered state (ui_create_, not ui_return_);
`menu_add` keeps the state under it; `menu_back` honours `state` (back to
it; nothing when it is already the top) and `num_states`. After ui_create_
the menu named by focus_id / ui_task_menu_default_focus_id gets the focus.
Why: the XEX's task_menu (0x823090D8 .. 0x8230A7C0) only read in part; this
order runs every boot and Quickplay screen. Undo: ui/ui_states.cpp.

## D5. flowState() maps gh4 states for the shared port code (2026-11-11)

What: `FrontEnd::flowState()` returns the UI stack's top; uistate_mainmenu
and uistate_options keep GH3's names (main_menu_fs, options_select_option_fs)
so the port's help-bar items and Esc logic from gd04 work unchanged.
Undo: rename those uses to the gh4 names.

## D6. Sprite dims are a size, not a scale (2026-11-11)

What: GH3 turned dims on a textured sprite into a scale [C GH3]; gh4's descs
give dims (1280 x 720 on a 1024 x 512 picture) and scale (1, 1) and lay
children out in those dims, so gd10 keeps dims as the size [I]. Undo:
ui_system.cpp setTexture / applyProps.

## D7. FSB key: the XEX's generator (2026-11-11)

What: song banks are decrypted with the key 0x820E24F8 makes from the bank's
name ("<song>_<n>"), plain = bit-reverse(stored) XOR key [C]. Previews: the
same generator on "<song>_preview" (D81). Undo: none.

## D8. The song's pairs and stems [I] (2026-11-11)

What: bank 1 = drums (4 pairs), bank 2 = guitar, bass, vocals, bank 3 =
backing + crowd; guitar -> the guitar stem, bass -> rhythm, the rest -> song.
Why: `--songaudiotest entersandman` levels against the chart (vocals pair
silent until 1:10, bank 3's second pair silent). Undo: kGh4Pairs in
audio/song_stream.cpp, once the XEX's table is found.

## D9. Chart note flags [C] (2026-11-11, confirmed 11-11)

What: (time, packed) pairs: frets 0x1f, 0x20 open, 0x40 the note is a
hammer-on (not a flip): ProcessHammerOns (0x8214EE70, jam songs only)
writes the same bit: single fret (an open note a value of its own), not
the previous note's, within the first beat / default_hammer_on_measure_scale
[C]; on the disc 1267 of 1270 Enter Sandman expert notes match that rule
(the rest forced). Two entries at one time are one chord (frets ORed, the
longer length) [C data; I: the merge, the original keeps a length per
fret]. 0x0f80 / 0x2000 unused for now. Undo: game/chart.cpp,
game/note_track.cpp, game/player.cpp (lastHitMask_ of an open note).

## D10. The Glitches stand-in (2026-11-11)

What: on the first main menu the original's own scripts unlock_all_profiles
and purchase_all_profiles run, and the band is named The Glitches (band_info
name), unless GD10_NO_GLITCHES. Why: rule 14; gh4's save differs from GH3's
and gd04's ensureGlitches does not apply. Undo: ui/front_end.cpp.

## D11. Empty switch cases share the next body [I] (2026-11-11)

What: in `switch`, a matched case whose body is only its jump (then the
next case) runs the next non-empty body (`case p1_quickplay ... case
p4_quickplay <body>`). Why: otherwise one-player Quickplay never starts a
song. To confirm in the XEX's interpreter. Undo: script/vm.cpp kSwitch.

## D12. IsSongAvailable [I] (2026-11-11)

What: true for set-list songs whose first bank is on the disc, minus
tutorials, sync / debug charts and the jam session. Why: the native's rule
is not read yet; this hides the Death Magnetic DLC songs (no audio on the
disc) and test charts. Undo: ui/natives_gh4.cpp.

## D13. The debug highway stands in for gh4's highway (2026-11-11, done 11-11)

Done: the song draws gh4's own highway (highway_guitar1 layout, the CAS
highway skin, gems / buttons / tails from the script-built materials, D15).
The debug highway remains for data without the gh4 tables.

## D14. A descinterface element wraps its desc; the desc's root is its child 0 (2026-11-11)

What: `CreateScreenElement {type = descinterface desc = ...}` makes an
element with the call's own props (pos, just, dims, pos_anchor, z, alpha);
the desc's root element (main_menu_bg, helper_pill_container, hud_root's
tree) is built as its child 0, and alias / prop paths start there. Why:
setup_band_hud resolves `{hud_root child = 0}` as the HUD's root [C script];
make_menu_frontend and 20 other full-screen descs pass just = [center
center] without dims at pos (0, 0), which then moves nothing (applied to
the root it drew the 1280 x 720 picture centred on the top-left corner: the
"skull art over the menus" of the proof of concept). The XEX's
descinterface class is not read yet [I]. Undo: ui/ui_desc.cpp buildDesc /
descPath.

## D15. gh4 highway materials drawn as layered sprites [I] (2026-11-11)

What: guitar_material.qb builds each gem / button / whammy material from a
template (ImmediateMode_AlphaFade_UI_3Pass, AnimatedTexture_UI_3Pass,
WhammyBar_UI) with up to three grey textures (basic_gems.tex) tinted by the
gem set's colours (standard_gems / _stars / _fx) [C data]. MaterialLibrary
(MATL v3) is not decoded, so the passes are drawn in order, colour x
texture, alpha-blended (added for blendmode add), colours clamped to 1; the
whammy glow (m_glowcolor, m_glowintensity) is left out. Undo:
render/gem_materials.cpp, render/highway2d.cpp (gemGh4, matSprite).

## D16. The default guitarist's highway [I] (2026-11-11)

What: the highway skin is the character profile's appearance
CAS_Guitar_Highway desc_id -> CAS_Highways_List (highway_pak,
highway_texture) [C data]; with no character_id (a direct `--song`) the
first Preset_Musician_Profiles_Modifiable entry (James) is used. Undo:
ui/song_screen.cpp highwayTexture.

## D17. gh4's light set: which light takes which register [I] (2026-11-11)

What: the registers are gh4's [C data: constant tables; microcode]; which
light fills them is GH3's light set extended [I: gh4's is VMX128 code not
read]: key light = the first directional light (c161..c163), the next
three the specular slots and the SH, the first four point lights
g_PointLight (c176 + 4i, attenuation c248 / c249), the first five spot
lights g_SpotLight (c192 + 4i, c250..c253, the fifth's c211), cone from
hotspot / field as full angles; the rest into the SH as GH3's; ambient into
c160 and SH0 (no shader reads both). docs/render.md "Lights". Undo:
render/gh3_shader.cpp lightObject.

## D18. gh4's screen effects drawn with GH3's passes [I] (2026-11-11)

What: tod_profiles.qb's bloom2 / dof3 / bright_sat entries [C data] mapped
on the chain gd04 built for GH3: bloom2 = simple_bloom's passes (subtract
as the cutoff, colormodulate as the strength; saturation, thickness,
equation not applied), dof3 = the far blur in metres; noise2, uberscreenfx,
godray, vignette not drawn; GH3's chain order. docs/render.md "Screen
effects". Undo: render/screen_fx.cpp readEntry.

## D19. Lit and soft-edged particles [I] (2026-11-11)

What: a quick material with `lighting` multiplies its colour by the SH's
constant term + 4/17 of the key light at the system (clamped to 1);
`softedge` fades over softedgescale metres in front of the scene depth;
`max` caps the emit rate at max / mean life [C data: the flags; I: the
models]. Undo: render/particles.cpp (lit, softEdge, max), gh3_shader
spriteLight.

## D20. Spawn instance limits and the menu input block [I] (2026-11-11)

What: `SetSpawnInstanceLimits max = n management = ...`, first in a
spawned script or a handler: with n other runs of that script alive (also
runs that called it, with `checkwholecallstack`), ignore_spawn_request ends
the new one, kill_oldest ends the oldest [C names, scripts; I semantics:
0x8239A820 not read]. `SetButtonEventMappings block_menu_input` /
`unblock_menu_input`: the pads raise no menu events in between (GetButtons
Pressed still counts) [C scripts; I: what the XEX blocks]. Why: both were
no-ops, so a second press during ui_select_practice_mode_choose's loading
chose TRAINING again and left the tutorials list unfocused (the fuzz walk,
GD10-020). Undo: script/natives_core.cpp SetSpawnInstanceLimits,
ui/natives_flow.cpp SetButtonEventMappings, FrontEnd::handle.

## D21. No Xbox LIVE blade: it closes at once [I] (2026-11-11)

What: `NetSessionFunc func = showsigninui` spawns
sysnotify_handle_unpause_console, as if the system's sign-in blade opened
and was closed with no one signed in to LIVE; ui_signin_check's
wait_for_blade_complete then ends and the LIVE warning comes back with its
BACK [C scripts]. Why: Music Studio > GHTunes > Sign In waited forever on
an empty screen. Undo: ui/natives_flow.cpp NetSessionFunc.

## D22. No downloaded content folders: GetContentFolderState is free [I] (2026-11-11)

What: the port opens no content folder (OpenContentFolder false), so
GetContentFolderState returns contentfolderstate = free. Why: practice's
section Back (ui_select_song_section_back) waits for free and never went
back. Undo: ui/natives_flow.cpp GetContentFolderState.

## D23. No minimum time on the loading screen [C script] (2026-11-11)

What: Vm::kLoadingHoldMs is 0 (was 2 s, GD10-040 as gd-game-01..03).
gh4's destroy_loading_screen has no timer: the loading screen goes when the
loads are done [C script loading_screen.qb]. Why: the owner found the loads
too long (GD10-034); the hold made every song and zone load 2 s or more.
Undo: script/vm.h kLoadingHoldMs.

## D24. PC wording in the localized strings (2026-11-11)

What: every qs.pak string goes through pcstrings::pcQsText as it loads:
whole-string rewrites (gamer cards / tags, sign-in, guest, the restart
notice, the cheats warning without achievements), then rules: Xbox 360
Controller -> gamepad, Headset -> headset, the console -> the system,
storage device -> save location, the Dashboard -> free disk space; a string
still naming Xbox LIVE, the other console's network or gamer profiles says
"Online play is not available in GD-Engine." (gd-game-04's wording). Button
glyphs stay (guitar fret colours). `--scripttest` fails on any localized
string naming the console. Undo: script/pc_strings.cpp pcQsText, data/qs.cpp.

## D25. gh4's cheats in the Enhanced menu [C script] (2026-11-11)

What: Cheats & enhancers lists guitar_hero_cheats' entries that have
options (METALLICA COSTUMES, ALWAYS DRUM FILL, ..., HYPERGUITAR / BASS /
DRUMS, GEM / FLAME / STAR COLOR, VOCAL FIREBALL): a check box for two
options, a dropdown for more. Choosing one does what
ui_options_cheat_toggle does (guitar_hero_cheat_index, the user_options
tag cheat_index<i>, cheat_set_globals, exclude_option,
refresh_autokick_cheat) and unlocks the cheat (unlock_Cheat_<name>).
EXTRA LINE 6 TONES (not_toggle) and Superuser are not rows. Undo:
ui/port_enhanced.cpp gh4Cheats / setCheat.

## D30. gh4's band from band_builder's logic [I] (2026-11-11, characters agent)

What: data/band.cpp chooseBandGh4 builds the song's band as band_builder
does (the song struct's band, player 1's character on its part); a
RandomCharacter band gets a Modifiable preset rocker per part until the CAS
randomizer is in. Undo: data/band.cpp chooseBandGh4.

## D31. CAS parts read entry by entry from the raw CAS paks [I]

What: a header-only index of DATA/PAK/CAS (24+ paks, 375 MB); a part is the
key of its mesh's base name [C data]; how the engine looks parts up is [I].

## D32. Band_PlayClip mocap on the GH3 animator's body node [I]

What: the clip plays from its start frame on the body node; the arms on or
off as the clip struct says.

## D33. Band and crowd attach one member / ped a frame

What: textures and programs requested and settled a slice a frame (the
quickplay walk's load window: 808 ms worst before, 29 ms after).

## D40. gh4's screen effects through the uber pass [C shaders, I constants] (screen FX agent)

What: 824B8460 sets a flag per effect (bloom2 / godray 1, dof3 2, noise2 4,
bright_sat 8, vignette 0x10, inkblotch 0x20); 8251EC80 draws them in one
pass, sys_uberscreenfx with the _Uber_ScreenFX template plus _bloom1 _dof1
_noise1 _color1 _vignette1 _inkblot1 [C]. Order: DOF lerp, + bloom, colour
matrix (bright_sat after the bloom), noise, vignette, ink. The constants the
CPU writes (VMX128) are inferred: g_DOF0 = (nearEnd, nearEnd - nearBegin,
farBegin, farEnd - farBegin), g_Color = (rgb, intensity), bloom2's matrix,
blur widths, g_Random each frame, ink every intervala..intervalb s. Undo:
render/screen_fx.cpp endUber / readEntry.

## D41. Camera notes pick the TOD profile [C scripts]

What: camera notes 81..127 (CameraCuts_SetNextScreenFx) swap the list for a
Metal_*_tod_manager at the next cut, then the shot's DOF.

## D42. The vignette mask is generated [I]

What: sys_vignettemask names no texture on the disc: alpha x saturate((r -
inner) / (outer - inner)). ScreenFlash's FlashBS stays a pass after the uber.

## D43. Mocap / moment cameras: camera lock targets play camera anims [C scripts, data; I where marked] (2026-11-11, lead)

What: create_all_camera_lock_targets makes ghosts (Profile_Ven_Camera_Obj,
skeleton GH4_Camera) that momentcamera_playanim places at a node and animates;
a camera entry `lockto = <target> locktobone = bone_camera pos (0,0,0) quat
(0,0,0)` sits on that bone [C]. Band_PlayClip's clip struct `cameras[i] {name
anim}` plays on moment_cam_lock_target_<i+1> at <zone>_<name>, its
`drummercameras[i]` on drummer_mocap_lock_target_<i+1> at the drummer's start
(clipplaydrummercamera, drumCam_Params) [I: Band_PlayClip_CFunc's mapping;
the anim from the clip's start frame]; start_drummer_cameras cycles
Drum_Andy_StandardLP_C01 / _C02 [C]; <member>_mocap_lock_target_01 / 02 play
the member's body loop's <loop>_c01 / _c02 in step with it at his start
waypoint [I]. A target playing nothing at a cut is not picked [I] (its bone is
at the world origin: the old "stage floor / feet" shots). The clip structs are
read from the song pak (not in the globals at the venue's start).
Undo: render/mocap_camera.cpp, venue_stage.cpp setLockLookup / setFollow.

## D44. Transition camera anims [C scripts, data]

What: <zone>_<Intro|FastIntro|Encore|Boss>_Transition's ScriptTable
SpecialCamera_PlayAnim {obj anim} animates <zone>_<obj> (z_forum:
TRG_Geo_Camera_Performance_DRUM01, ven_forum_camera_intro) from the zone pak;
the intro's control script ccam_domorph locks to its bone_camera.
Undo: MocapCameras::addTransition.

## D45. CrowdOff hides the crowd models for the shot [C 0x822B4EA0, 0x822B4FC8]

What: CameraCuts_StartCallback: StructureContains (entry, CrowdOff) ->
enable_crowd_models_cfunc active = false. StructureContains searches the
entry's items and unnamed struct includes only, not `params`: a CrowdOff inside
params does not count. The crowd cards get an empty override (not drawn).
Undo: venue_stage.cpp `off`.

## D46. distfromlookat keeps the camera's distance to the framed bone [I]

What: the authored pos is distfromlookat from the bone in the authoring pose
[C data: e.g. z_tushino guitarist_closeup 2.83 m to bone_neck at 1.40 m];
the camera keeps that distance on the ray that frames the bone at its
screenoffset (the mocap moves members off their start nodes).
Undo: CameraDirector::aim `dist`.

## D47. CameraCuts_SpecialStepMove [C script]

What: cam1, then stepTime s: pos = cam1.pos + (cam2.pos - cam1.pos) c +
StepDir sin(c StepNum 360 deg), c 0..1; then cam2 over FinalRotTime
(smooth); ccam_setsmoothing not applied [I]. Undo: Ctl::StepMove.

## D50. gh4's light slots by the light's vertexlight / noSH flags [C] (2026-11-11)

What: 824D7220: point / spot lights take g_PointLight (4) / g_SpotLight (5)
slots only with `vertexlight` (flag 0x10); otherwise a point light is a
directional one shining from it (SH x attenuation, a slot while fewer than
four), a spot light SH only x clamp((P - pos) . dir, 0, 1); `noSH` (0x20)
keeps it out of the SH (a vertexlight light past its slots with noSH is
dropped). Flags from 8240F560: dynamic 0x4, shadow 0x8, vertexlight 0x10,
noSH 0x20. ExcludeLevel / ExcludeBand / ExcludeCharacters are read by no
code [C: no site builds their keys]. A point key light's direction from the
light to the object is GH3's [I]. Undo: render/gh3_shader.cpp lightObject,
lightIn (the venue argument).

## D51. An object's light groups: lightgroup, lightgroup2.. [C] (2026-11-11)

What: 823C9FD8 reads `lightgroup`, then "lightgroup%d" from 2 while
present (at most 32); a light serves the object through any. `empty`
names no group [I]. Undo: render/venue_view.cpp groupOf / groupsMore_.

## D52. gh4's gamma ramp is the identity [C] (2026-11-11)

What: 824558E8 builds ramp[i] = clamp(i x 256 x (0.6 x b + 0.7), 0, 65280)
with b = 0x82849280 = 0.5 (no script changes it): the identity. D25's
output curve stays as gd04's. Undo: none (no code change).

## D60. Talent skeletons by full name [C data]
What: band::Library keeps each global.pak .ske under its short and full name
(GH_Rocker_Male_James and GH_Rocker_Male_James_original). The 24 LZSS
skeletons of PAK/global_rag_skel decode (Neversoft LZSS, "LZSS" + u32 size)
byte-identical to the `_original` ones. Undo: data/band.cpp Library::load.

## D61. Facial anims from the anim struct member [C scripts]
What: setup_character_anims' facial_anims / facial_anims_default (the
anim struct member, the song's struct first), facial_anim_loop,
Band_ChangeFacialAnims (ExtendCRC fa_type; a struct that does not exist ends
the loop, the face keeps its last clip), Band_PlayFacialAnim. Face clips
animate bones 63..92 (Bone_Jaw .. bone_tongue, their bone mask); rotations
absolute, translations offsets from the bind [C data: offsets <= 1 cm].
Undo: render/band_anim.cpp facial / band_changefacialanims.

## D62. Song structs from the song pak [C data; I: load order]
What: the song's `<anim_struct>_<song>[_alt]` and Band_PlayClip clip structs
are looked up in the globals, else in the song pak's QB (the globals get
them only at load_songqpak, after the band is created). Undo:
data/band.cpp songAnimPart, render/band_anim.cpp Setup::songFiles.

## D63. initial_drummer_anim [C script; I: is_current_band_metallica]
What: with use_drummer_events = 0 the drummer cycles
Drum_Lars_StandardLP_NoTempo (Lars) / Drum_Andy_StandardLP_NoTempo; the
Metallica test is "the drummer's profile is LarsUlrich*". Undo:
render/band_anim.cpp init (drumLoop).

## D64. global_model_tex for the talents' missing textures [C data]
What: a material texture the part's .tex lacks is taken from
PAK/global_model_tex's dictionary (all 368 talent keys are there), decoded
once per disc at band load. Undo: data/band.cpp globalModelTextures.

## D65. gh4 accessory ragdolls [C data; I offsets]
What: the full body desc's `ragdoll` key is the .rag entry's short name in
PAK/global_rag_skel; "LZSS" packed Havok-5.5.0-r1 packfile (file version 5,
contents in section 2, __types__ empty). Fixed offsets: hkpPhysicsData
systems +0x0C; hkpPhysicsSystem rigidBodies +0x08, constraints +0x14;
hkpRigidBody shape +0x10, name +0x70, material friction / restitution
+0x84 / +0x88, motion +0xD0 (type +8, transform +0x10, centerOfMassLocal
+0x90, damping +0xB4 / +0xB8, hkUFloat8 velocities +0xBC/+0xBD,
inertiaAndMassInv +0xC0); hkpConstraintInstance data +0x0C, entities
+0x14 / +0x18, name +0x20; ragdoll atoms (transforms +0x10, motors +0xA0,
angFriction +0xF0, twist +0xFC, cone +0x110, planes +0x124, ballSocket
+0x138), limited hinge (angMotor +0xA0, angFriction +0xB4, angLimit +0xC0,
2dAng +0xD0, ballSocket +0xD4); atom type words checked. accessory_bones:
Rocker_accessory_bones (ACC_Head 01..09, Torso, Legs). The same simulation
as GH3's (render/band_ragdoll). Undo: data/havok.cpp Pack55,
render/band.cpp accessorySim gh4 branch.

## D66. Deform skeletons' mirror [C data]
What: Face_deform / GH_Male_Deform: Bone_Cheek_Lower_L's mirror index is
Bone_Cheek_R's while its mirror name is Bone_Cheek_Lower_R: the name wins.
Undo: data/anim.cpp parseSkeleton.

## D70. Crowd cards are quad lists by the mesh's primitive word [C data] (2026-11-11)

What: data/scene.cpp reads gh4 mesh +0x84 (was word68, raw): 13 =
D3DPT_QUADLIST, Mesh::quadList, triangles() splits each 4 indices into
(a, b, c), (a, c, d). Every other gh4 mesh has 4.
Why: on the disc's zones the 15 meshes with 13 are exactly the crowd
cards; each has a multiple of 4 indices (8 of them not a multiple of 3);
the cards are 4-vertex quads (positions2 = the quad's centre, the same for
its 4 vertices, Billboard_FastLit's pivot).
Undo: drop quadList; the cards draw as triangle-list shards.

## D71. One crowd atlas, a cell per ped [C scripts / data] (2026-11-11)

What: render/crowd.cpp makes one target the size of
imposter_rendering_highres_quad_base (1024 x 1024; >6 peds:
imposter_rendering_quad_base, 768 x 768), clears it once (alpha 0), draws
each ped into its rect (HRViewportParams / ViewportParams .rect x the atlas
size); texture(card) returns the atlas for every model's textureasset.
Why: create_crowd_models: CreateViewport crowd_base_viewport (texture
crowd_atlas0.dds) then per ped a viewport with ResourceViewport =
crowd_base_viewport, no_resolve_colorbuffer, style quad<n>; all models
share textureasset crowd_atlas0.dds; the cards' uv0 span u 0..0.74 in
0.25 steps, v 0..0.99 in two halves.
Undo: per-ped targets (only the first model's shows, stretched).

## D72. Ped facing 180 + roty, camera hfov crowd_ped_camera_fov [C script] (2026-11-11)

What: yaw = 180 + roty degrees (CreateFromStructure quat (0, 1, 0), then
obj_setorientation y = 180 + roty when roty != 0); hfov 21.
Why: create_crowd_models / guitar_crowd.qb (crowd_ped_camera_fov = 21).
Checked: z_forum's AUDIENCE_R (a camera in the crowd looking out) shows
faces, the intro wide view faces turned to the stage. [I: obj_setorientation's
sign as GH3's port had it; apply_correction (roty = 0) not known.]
Undo: yRot(roty).

## D73. gh4 pyro notes; node emitters with a lifetime; SafeCreate (2026-11-11)

What: render/particles.cpp: LightShow_NoteMapping's LightShow_PyroEvent
entries map a lightshow note to pyro_scripts.<type> (nullscript skipped),
run lightshow_offset_ms early with `performance` [C scripts lightshow.qb;
I: LightShow_GetPyroScript 821a3880 returns the mapping's pyro_scripts].
IsCreated / IsInNodeArray take the first unnamed item [C scripts
misclightutils.qb]. A particleobject node whose params have lifetime > 0
emits emit_rate for that long per Create and then counts as not created;
createdatstart ones start expired [I: the emitter's lifetime; the Top_1
moment SafeCreates them, which needs them gone].
Undo: the ring of Topsparks emits for ever; no gh4 pyro.

## D80. RemoveParameter and AddParam do their work (2026-11-12)

What: RemoveParameter {$x} drops x (named, or an unnamed flag) from the
calling script's parameters; AddParam name = n value = v [structure_name =
s] adds n = v to them, or to their struct s; se_setprops takes an unnamed
struct's fields (`se_setprops <s>`). Both natives were no-op stubs.
Why: user_control_helper_get_buttonchar removes all_buttons on the Xbox 360
(IsXenon), so every help pill shows the guitar glyph (\m0 -> \b4, the
green fret) and not "\b3\b4" (A + green); ui_setlist_scribbles cycles the
difficulty pop-up's highlight scribbles with AddParam + se_setprops, so the
focused difficulty had no highlight [C scripts; natives 0x823946C0 and
0x823980E8, whose bodies Ghidra cuts short: I the rest from the callers].
Undo: natives_misc.cpp (both stubs back), ui_system.cpp se_setprops.

## D81. The set list's song previews (2026-11-12)

What: SongPlayPreview filename = <prefix> plays DATA/MUSIC/<prefix>_preview
.fsb.xen (one stereo XMA2 sample, about 21 s, key from "<prefix>_preview"
as the song banks') once, from its start, on the music_setlist buss;
SongStopPreview stops it; SongIsPreviewPlaying is true until it ends.
SongCheckIfDownloaded is false (no downloads). No fade, loop or offset in
the native: setlist_songpreview_monitor plays it again when it ended, the
gig posters fade the buss (-100 dB over 1 s after 7 s), Menu_Music_Off
stops the menu music [C 0x8219DD60, 0x820E45E8 ("%s_preview"),
0x820E49F8 (buss 0x4A0087AE music_setlist, end callback 0x820E3F80),
0x82190010, 0x82190020, 0x8218FDD8]. The port plays it on the song mixer
while no song or practice preview has it [I].
Undo: song_preview.cpp's four natives, SongStream::openPreview.
