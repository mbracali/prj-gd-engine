# Guitar Hero III (Xbox 360): venues, cameras, light show

How a song's venue is chosen, what a zone pak holds, how GH3 cuts its
cameras and runs its light show, and how gd04 draws it (M5). **[C]**
confirmed in the data or the disassembled scripts (file named), **[I]**
inferred. Code: `engine/src/data/venue.*` (the zone), `render/venue_camera.*`
(cuts), `render/venue_lights.*` (light show), `render/venue_view.*` (drawing),
`render/venue_stage.*` (the three for a song), `tests/venuetest.*`.

Sources: the QB scripts (`tools/qb.py` / `tools/qbscript.py` dumps of
`qb.pak.xen` and the eight zone paks) and the zone data; no executable
disassembly was needed for this milestone (the C side of the cut and light
functions is marked [I] where it matters).

## 1. Which venue

- `LevelZones` (`scripts\guitar\guitar.qb`): `load_z_<venue>` -> `{zone,
  name, Title, bg}`; eight playable venues in this order: z_artdeco (Ye olde
  Royal Odeon), z_budokan (Kaiju Megadome), z_dive (Mitch's Moose Lounge),
  z_hell (Lou's Inferno), z_party (Backyard Bash), z_prison (Shanker's
  Island), z_video (Video Shoot), z_wikker (Desert Rock Festival); plus
  `$debug_only` z_soundcheck, z_credits, z_viewer [C].
- The global `current_level` (default `load_z_artdeco`) names the level;
  `get_level_prefix` maps it to the zone name [C].
- Quickplay: `quickplay_start_song` (`quickplay_menu_flow.qb`) draws a random
  index below `get_total_num_venues` (venues whose global tag
  `venue_<name>.unlocked` is 1) and sets `current_level` [C].
  `setlist_choose_song` first restores `g_last_venue_selected` [C].
- Career: `Progression_Init` / `Progression_UnlockTier` set
  `current_level = <tier_global>.tier<N>.level` [C]; the career and face-off
  venue menus (`menu_venue.qb`) set it from the poster chosen [C].
- `start_song` -> `Load_Venue` (`guitar_gems.qb`):
  `SetPakManCurrentBlock map=zones pak=LevelZones.<level>.zone`, i.e.
  `DATA/COMPRESSED/ZONES/<zone>.pak.xen` [C].
- gd04: `VenueStage::chooseZone` reads `current_level` (so the menus' choice
  is used); `--song` keeps the default `load_z_artdeco`; `GD04_VENUE=<zone>`
  overrides (development aid).

## 2. A zone pak  [C data, all eight]

`z_dive.pak.xen` as the example:

| Entry | What |
|---|---|
| `zones\z_dive\z_dive_scripts.qb` | setup / startup scripts, encore and boss transitions, `<zone>_lightshow_mapping`, the snapshot lists per performance |
| `zones\z_dive\z_dive_cameras.qb` | the camera arrays `z_dive_cameras[_<kind>][_2p]` |
| `zones\z_dive\z_dive_snp.qb` | `Z_Dive_snapshots`: the light snapshots |
| `zones\z_dive\z_dive_sfx_scripts.qb` | sounds |
| `z_dive.tex` + `z_dive.scn` (+ `.hkc`) | the venue's textures and geometry (collision) |
| `z_dive.qb` (.nqb, parent the .scn) | `z_dive_NodeArray`: every object, waypoint |
| `z_dive_gfx.*` (flags 4) | a second scene + node array: the lights, light housings, particles |
| `models/real_crowd/*.skin`, `stagepeds/*.skin` | crowd and stage people (skins) |
| `.wav` | encore / intro crowd sounds |

### Node arrays

Each node: `name`, `pos`, `angles`, `class` (`levelgeometry`, `levelobject`,
`gameobject`, `waypoint`, `levellight`), `Type`, flags `$createdatstart`,
`$absentinnetgames`, `$RenderToViewport`, `createdonprogress = <node flag>`,
`createdfromvariable = <node flag>`, `lightgroup`, `triggerscript`, and per
class more (lights: `rotationquat`, `lightcolor`, `intensity`, `attenstart`,
`attenend`, `hotspot`, `field`, `specularintensity`) [C data].

- **Sectors and nodes**: a scene sector's key is its node's name. Sectors
  with flag 0x20 (levelobject nodes, e.g. 0x80040827) are in the node's
  local space (their bounds sit around 0); the others (levelgeometry,
  0x80042807) are already in world space [C data: every venue].
- **Angles**: the rotation is `q = qy(a.y) * qz(a.z) * qx(a.x)` (Hamilton
  product, radians) [C data: the level lights' `rotationquat` equals it,
  error < 1e-12 for the 11 lights with a z angle, and the y-x lights].
- **World**: y up, metres; the stage faces +z (the crowd) [C data].
- **Visibility**: a node shows when `$createdatstart`, or when its
  `createdonprogress` / `createdfromvariable` node flag is on [I: the node
  manager is C code]. Flags: `LS_ALWAYS`, `LS_3_5_PRE/POST`,
  `LS_ENCORE_PRE/POST` (`Progression_SetProgressionNodeFlags`), the
  performance / mood state flags (`lightshow_statenodeflagmapping`:
  `ls_perf_poor`, `ls_perf_medium_good`, `ls_mood_blackout`, ...) [C].
  gd04 sets LS_ALWAYS, LS_3_5_PRE, LS_ENCORE_PRE [I: the non-encore values].
- Waypoints `<zone>_TRG_Waypoint_<member>_Start` place the band
  (`get_start_node_id`: `'%s_TRG_Waypoint_%a_start'`; one player:
  `guitarist`) [C].

## 3. Cameras

### Conventions

- A camera (and a light) looks down its local **+z** with +y up, and screen
  right is its local **-x** [C data: projecting each camera's look-at target
  (the member's start waypoint + chest height) through its `Quat` lands at
  the camera's `screenoffset.x` for the cameras of all eight venues, e.g.
  dive -0.43 / -0.54, party 0.42 / 0.42; a `Quat` of 180 degrees about y on
  the crowd side looks at the stage].
- `Quat = (x, y, z)`, w = sqrt(1 - x^2 - y^2 - z^2) [I: w >= 0].
- `FOV` is horizontal, degrees [C: the viewer camera uses `sethfov hfov =
  $camera_fov`; I for the cut cameras].
- `LockTo = $world` or a band member: `pos` / `Quat` are then in that
  member's frame (its start waypoint's position and angles) [C data: the
  same projection test holds for LockTo cameras]. `lookat` /
  `lookatbone` / `screenoffset` keep the member framed (`screenoffset.y`
  is screen-down positive) [C data]; gd04 has no characters yet, so it uses
  the authored `Quat` (the framing as authored) [I].

### Arrays

`set_defaultcameracut_perf` (`guitar_cameras.qb`) looks up, for the prefix
(`cameras`, `cameras_solo`, ...) and each performance (`good`, `normal`,
`poor`): `<zone>_<prefix>_<perf>`, `<zone>_<prefix>`,
`default_<prefix>_<perf>`, `default_<prefix>` (`_2p` first in two-player)
[C]. Entry: `{name, Type (closeup, mid, LongShot, stage, ...), includenodes,
controlscript, params {cam1, cam2, ...}, dof, $CrowdOff}`; without a
`controlscript` the entry itself is the pose (`Default_Cameras_*`) [C data].

### When a cut happens  [C scripts, `guitar_cameras.qb`]

- `create_cameracuts` (`start_gem_scroller`) starts with prefix `cameras`;
  the song's transition (`Transition_SelectTransition`) runs its
  `Transition_CameraCut` at its time 0: `cameras_fastintro` (3 s) or
  `cameras_intro` (8 s), `length = transition time` [C
  `guitar_transitions.qb`].
- At the transition's end (`$EndWithDefaultCamera $SyncWithNoteCameras`): a
  note camera within 2 s -> `CameraCuts_EnableChangeCam enable=false` (wait
  for it); else `CameraCuts_SetArrayPrefix prefix='cameras' $changenow` [C].
- `cameracuts_iterator` walks `<song>_cameras_notes` ({time, note, length})
  and runs `CameraCuts_NoteMapping` [C]: 77 / 78 enable / disable changes;
  79 `cameras`; 80 `cameras_intro`; 81 / 90 `cameras_solo`; 82 / 110 dolly;
  83 zoom; 84-87 `cameras` with Type closeup / mid / LongShot / stage; 88
  singer; 89 drummer; 91-94, 103 orbit (by name); 95 guitar closeup; 96
  starpower; 97 bassist; 98 walk; 99-102 vertical; 104 singer closeup;
  105-109 special; 111-113 zoom slow / fast / out; 114 pan; 115 / 116 boss /
  player closeup; 117 stagedive. Each `$changenow` with length > 200 ms makes
  the next shot last exactly the note's length.
- `CameraCuts_CalcTime`: the note's length, else 3..8 s (`random_range`)
  extended to the next downbeat - the next `<song>_lightshow_notes` note 58
  after it (`CameraCuts_GetNextDownbeat`).
- `CameraCuts_WaitScript`: the shot ends at that song time, or at once on a
  `$changenow` - only while changes are enabled.
- `CameraCuts_StartCallback` picks the next entry: the one named
  `CameraCuts_NextName`, else of type `CameraCuts_ForceType`, else not of
  the last type; `GetRandomValue` among them; the array by
  `CameraCuts_Performance` [C]. How `CameraCut_GetArraySize` treats
  `includenodes` (only when the guitarist's target node is listed) is [I].
  `CameraCuts_Performance` is set by C code (`cameracuts_setparams` has no
  script caller) [C]; gd04 uses the rock meter's bands of `get_skill_level`
  (< 0.66 poor, > 1.33 good) [I].

### Shots  (`controlscript`)

| Script | Motion [C scripts; the morph curve "smooth" is I] |
|---|---|
| none / StaticCamControl | the pose |
| TwoCam (489 entries) | cam1, then a morph to cam2 over the shot |
| HandCam (334) | the pose; LongShot: 0..100 < 25 FOV -> 62, > 95 -> 78; mid: < 5 -> 64, > 85 -> 82, over the shot |
| HandCamZoom2 | after 2 s FOV 50 (0.6 s), +0.1 s 53 (0.25 s), +0.03 s 51 (0.3 s); then CalcTime |
| QuickZoom | cam1; after 2 s a 0.75 s morph to cam2; then CalcTime |
| Pan | cam1; after 2 s a 1 s morph to cam2; then CalcTime |
| Orbit (81) | the pose, orbiting about y by `3.141 * orbitangle / 180` over the shot |
| OrbitAndMorphTwoCam (44) | TwoCam plus the orbit (`DisableOrbit1/2`) |
| ThreePos / FourPos / SplineMorph | `ccam_splinemorph` through the keyframes over the shot |
| SpecialStepMove | stepped cam1 -> cam2 (gd04: as TwoCam) |
| `<zone>_camera_encore` / `_boss` | venue scripts (gd04: their first camera) |

Hand-held sway: `CameraCuts_SetHandCamParams` (motionx amplitude 0.034,
period base 0.013, multiples 1 3 4; motiony 0.04 / 0.017) [C data]; gd04
turns it into a small yaw / pitch sway [I: units].

### gd04

`CameraDirector` runs all of the above as a function of the song clock:
every note and every cut is processed at its own song time (several per
frame if needed), the random numbers come from a per-song seed, and the
performance at a cut is the one the 60 Hz step at that time set
(`PerfTimeline`). A timed cut within one game frame before a camera note
is folded into the note's cut [I: the original shows a one-frame shot or
none]. `--venuetest` checks the result is the same at 60 and 240 fps.

## 4. Light show  [C scripts, `scripts\fx\lightshow.qb` + the zone files]

- `lightshow_iterator` walks `<song>_lightshow_notes` with
  `time_offset = gem offset + lightshow_offset_ms (100)` [C]; gd04 fires a
  note 100 ms early [I: the sign].
- `LightShow_NoteMapping` [C]: 76 intro, 75 verse, 74 chorus, 73 solo, 72
  blackout, 71 flare, 70 strobe (moods, `LightShow_SetParams`); 69..61
  colour overrides (green, red, blue, yellow, white, magenta, off, orange,
  purple; colours in `lightshow_coloroverrides`); 60 strobe toggle; 58 / 57
  snapshot change (with / without snapshot positions); 56 pyro; 53..40 blend
  time 1.0 .. 0.0 s; 39 default (`lightshow_defaultblendtime` 0.15 s).
- `LightShow_InitEventMappings`: `<zone>_lightshow_mapping` (per mood and
  performance: a processor and a snapshot list), `<zone>_snapshots`,
  starting `performance=medium mood=intro` [C].
- Processors (`LightShow_SharedProcessors`) [C]: Poor_Generic (one snapshot
  on entry, ignores changes), *_Generic moods (a snapshot on entry and on
  every snapshot change), Blackout / Flare (one snapshot on entry), Strobe
  (blend time off; toggles between the strobe list's blackout and the saved
  snapshot at intensity 1.0).
- A snapshot entry sets one light: `lightchecksum`, r g b (0..255),
  `intensity`, `specularintensity`, `FarAttenStart/End`; others move light
  housings (`housingchecksum`, pos, Quat, volume / projector colours) [C
  data]. gd04 blends each light from its current value over the blend time
  [I], cycles a list in order [I], mixes override colours by
  `lightshow_coloroverrideblend` 0.4 except `LightShow_ColorOverrideExcludeLights`
  [I: the meaning of 0.4].
- Lights by group: geometry nodes have a `lightgroup` (none, band, Crowd,
  Periph, Fuji, ...) and so do lights [C data]; gd04 lights a mesh with the
  lights of its node's group [I]. `_VC_` lights look like the vertex-colour
  bake's own lights and are not applied at run time [I].

## 5. Shading (approximate)

GH3's materials name a shader template (`+0xa0`, e.g. 0xe0ab9772 on 160 of
the venues' materials, 0x1f7a935a, 0xa17d40ed) with texture slots that
differ per template [C data]. gd04 draws them all with one program
(`VenueView::programDesc`, uniform blocks GdVenueFrame / GdVenueLights /
GdVenueMat):

- diffuse = the material's first colour texture (class byte 0; some
  templates put a DXN normal map, class 1, first) [C data: the class byte;
  I: the slot choice];
- lightmap = the material's last texture, sampled with the mesh's last
  texture coordinate set, when the mesh has two sets [C data: 2048x2048
  single-level DXT1 maps, or a white 32x32 placeholder];
- colour = diffuse x lightmap x (vertex colour + 0.6 x the group's lights
  (ambient; directional, point and spot with N.L and a linear fall-off
  between attenuation start and end; spot cone from hotspot / field)) [I:
  the formula and the 0.6; GD04_VENUE_VC / GD04_VENUE_DYN scale them];
- material words [I]: `+0xe8` top byte = alpha cut-off (0x64, 0x80, 0xc8 ...);
  `+0xd8` = blend mode (0 opaque, 1 additive, 2 subtract, 3 blend), `+0xd4`
  the draw order (0..3), `+0xcc` bit 3 = blended; additive surfaces unlit.
- No culling (strip winding not checked), no fog, bloom, depth of field or
  shadows.
- `global_ad_tex.pak` holds the `ad_*` sign textures [C data].
- The crowd's cards (`<zone>_crowd_models[].textureasset`, the
  `tw_billboard0N` placeholders that `CreateViewportTextureOverride`
  replaces with a render of the crowd models, `create_crowd_models` in
  `guitar_crowd.qb`) [C] are hidden until the crowd exists.

## 6. Not done yet

Band members and their animation (the look-at targets), the crowd (3D peds
and the cards), light volumes, flares and housings (the `_gfx` game
objects), particles and pyro, animated level objects (fans, hub caps, the
doll: their trigger scripts), screen effects (`dof`, `ScreenFX_*`), the
encore / boss / win / lose transitions and their cameras, two-player arrays,
LODs.

## 7. Test

`gd04 --venuetest <disc>` (headless: `SDL_VIDEO_DRIVER=offscreen`): for every
venue (or `GD04_VENUE`), with slowride's notes (`GD04_VENUETEST_SONG`), the
whole song's cuts, camera and lights are simulated at 60 and at 240 fps (the
60 Hz steps plus one advance per frame, a rock meter swinging through the
three bands) and compared every 250 ms of song time; then
`GD04_VENUETEST_FRAMES` (240) frames are drawn at each rate from 15 s;
`GD04_VENUETEST_SHOTS=<dir>` saves a screenshot in the middle of the first
eight shots, `GD04_VENUETEST_LOG=1` prints every cut.
