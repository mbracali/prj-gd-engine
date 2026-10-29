# Guitar Hero III (Xbox 360): venues, cameras, light show

How a song's venue is chosen, what a zone pak holds, how GH3 cuts its
cameras and runs its light show, and how gd04 draws it (M5). **[C]**
confirmed in the data or the disassembled scripts (file named), **[I]**
inferred. Code: `engine/src/data/venue.*` (the zone), `render/venue_camera.*`
(cuts), `render/venue_lights.*` (light show, housings), `render/venue_view.*`
(drawing, housings, volumes), `render/gh3_shader.*` + `render/xenos_glsl.*` +
`data/matlib.*` (GH3's shaders), `render/crowd.*`, `render/particles.*`,
`render/venue_stage.*` (all of it for a song), `tests/venuetest.*`.

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
  is screen-down positive) [C data]; gd04 turns the authored `Quat` so the
  live bone of the band member sits at the screen offset (M6, docs/band.md
  "Cameras") [I].

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

## 5. Shading: GH3's own shaders

Each scene material names its template (`+0xa0` = qbkey of the template's
name, e.g. D_3ChkBox_Pass0 on 58 of z_dive's materials, D_N_S_4ChkBox,
D_1VertexColorOnly, Billboard_Fast, Wibble_Pos_Simple, Flare; the band's
SkinShader, HairShader, D_N_S_Character_1SeperateChnl) in the material
library (docs/formats.md "Material library") [C data]. gd04 draws every
material with that template's Default technique: the Xenos microcode
translated to GLSL (`render/xenos_glsl.cpp`, `render/gh3_shader.cpp`), with

- the material's constsA / constsB as the vertex / pixel shader's c0.. and
  its textures as samplers s0.. in order [C data: the constant tables];
- the engine constants c128..c255 at GH3's registers: the D3D-convention
  matrices (view space x right, y up, z = the camera's +z; projection depth
  0..1, converted to OpenGL's clip depth when the shader exports it),
  g_Time, g_viewport [C: the registers; I: g_distanceLODScaler (1, 0, 0, 0)
  = full detail, g_shLightShadowIntensity 1];
- vertex inputs by their D3D semantics: the meshes declare the shadow
  map's coordinates as TEXCOORD4 or TEXCOORD5 (z_dive: 124 meshes
  texc0 + texc5, 92 texc0 + texc4) and the templates fetch them by that
  index [C data: the declarations (0x823B9A28) and the shaders' fetch
  tables];
- textures GH3 binds itself (g_sampShadowProjection, the smoke of the light
  volumes) white [I].

What the shaders do is then GH3's [C: the microcode], e.g. D_3ChkBox_Pass0:
colour = diffuse x m_psPass0MaterialColor x vertex colour x (ambient
(c176) + SH(n) x lerp(1, shadow map, c137) + N.L x key colour (c178) x
lerp(1, shadow map, c178.w)); the "lightmap" is the material's last
texture, m_sampShadowMap, sampled with texcoord set 1, a shadow term on the
dynamic lights, not a light map.

The light registers are the light manager's output (C code; its VMX128
routines, e.g. Function_822B6E08, are not disassembled by Ghidra), so gd04
fills them from the group's lights as the light show sets them [I]: the
group's ambient lights summed (c176); the strongest directional or spot
light reaching the mesh's centre the key light (g_dirLight0Dir = its
direction of travel, Color, SpecColor = colour x specularintensity); the
next three their specular colours (c181 / c183 / c185); every other light
(and those three) projected onto L1 spherical harmonics as a clamped
cosine (1/4 + n.l / 2) into c188..c191; point and spot lights fade linearly
between their attenuation start and end, spots by hotspot / field.
`specularintensity` comes from the node and the snapshots [C data].

Render state: blend mode from `+0xd8`, alpha test reference from `+0xe8`'s
top byte, draw order `+0xd4`, blended when `+0xcc` bit 3 [I, as in M5; the
templates' own render states are not decoded]. Back faces are culled
(Direct3D's default, counter-clockwise) [I: checked on all eight venues;
this fixes the stage-rear cameras, e.g. z_dive's Stage_Rear01 stands behind
the back wall at z = -10.2 and showed its back faces (M5 bug); GD04_CULL=0
turns it off]. Templates that do not translate (loops: three water
templates) fall back to the M5 approximation; GD04_GH3_SHADERS=0 forces it.

### Dynamic shadows

The band casts shadows onto the venue: opaque venue meshes draw with
their template's DynamicShadowReciever technique, whose vertex shader
projects the view-space position with g_ViewToLightProjection0 / 1
(c144 / c148, two cascades) and whose pixel shader takes four taps of
g_sampShadowMap0 / 1 (s14 / s15) around the pixel, blends the "not
deeper than the map" results bilinearly (g_shadowMapSize c143 = the maps'
sizes), fades between the cascades by the camera distance
(g_shadowMapConstants c141.zw), maps the result with g_shadowLevel
(c142.zw) and keeps the darker of it and the static shadow map [C: the
microcode]. Sizes: shadow_map_size (1024, 512), styles shadow_big 1024 x
1024 / shadow_small 512 x 512 depth only; SetDynamicShadowIntensity 0.35
[C data tod_profiles.qb, tod_manager.qb]. gd04 [I]: one 1024 x 1024 map
for both cascades, orthographic (14 m square, 60 m deep) around the band
from the band group's strongest directional light pointing down, the
band's opaque meshes as casters, light = 0.35 + 0.65 x lit (as dark as the
reference captures' shadows); the characters do not receive.
GD04_NO_SHADOWS=1 turns it off.

### LOD

Every node carries `lod_dist1` / `lod_dist2`: 150 / 151 m or 400 / 401 m in
all eight venues [C data]; gd04 does not draw a node's meshes beyond
lod_dist2 from the camera [I: the band between is a fade or a lower LOD;
no venue geometry is that far]. The ReducedLOD techniques are not used [I].

## 6. Crowd, light housings, volumes, flares, particles

### Crowd  [C scripts `create_crowd_models`, `update_crowd_model_cam`, `crowd_play_adjusting_random_anims`]

`<zone>_crowd_models` lists a few peds (Model `Real_Crowd\Crowd_Ped_01.skin`
= the zone pak entry qbkey("models\Real_Crowd\Crowd_Ped_01.skin"), on the
GH3_Ped skeleton, `obj_security_creation_params`), each created at (-500,
-200 - 100 x index, 0), facing -z (Quat (0, 1, 0)) or `roty`, in light group
Crowd (the zones' Crowd lights sit at x = -500), with the clapping hands
(global.pak's Crowd_HandL/R_Clap.skin). Each has a camera (hfov 20) into a
viewport (style imposter_rendering, 128 x 256 with mipmaps, or
imposter_rendering_highres, 256 x 512, for at most six peds on Xenon
[C: tod_profiles.qb Default_TOD_Manager.styles]) whose texture replaces
the 64 x 64 card texture `textureasset` (tw_billboard0N, CreateViewportTextureOverride): all
of the venue's crowd card quads (Billboard_Fast) show these renders. The
camera: the venue camera's direction turned about y by 25 x its x
component, 3.5 m from the ped's position + (0, 1, 0), every frame [C; I:
it stands on the venue camera's side looking along that direction; the
render is drawn upside down as Direct3D's texture v runs down].

The peds dance `Crowd_Profiles.<name>.anim_set.idle.<skill>.<speed>`
(perm_anims.pak's GH3_crowd_* clips): skill from the rock meter (bad /
normal / good), speed from the tempo (the band's thresholds), a random
entry, played to its end; blend Crowd_BlendTime_TempoChange (0.67 s) when
the speed changed (and the first time), _Bad (0.5) when bad, else _Slow /
_Med / _Fast (0.6 / 0.4 / 0.2) [C]. gd04 (`render/crowd.*`) runs this on
the song clock with a per-ped seed [I: GetRandomValue's generator].
Not done: the lighters / fists (Crowd_SetHand), the stage diver, the
security and dancer stage peds (stagepeds/*.skin), 3D crowd members other
than the imposters.

### Light housings, volumes, flares  [C data: the _gfx node arrays, the snapshots]

Game objects with a `lightvolumetype` (ConeImpostor 149, flareonly 68,
volume 9 over the eight venues) carry a Model in ZONES/global.pak
(`LightHousings\GO_LightHousing02\GO_LightHousing02.mdl`: the housing, its
lens and a Flare quad), `startradius`, `endradius`, `innerradius`, `range`,
`volumedensity`, `volumecolor`, `projectorcolor`, `projectortype`. The
snapshots move them (`housingchecksum`, pos, Quat in the same convention as
the nodes' angles [C: z_dive LH_HotSpot01's Quat equals its angles'
quaternion]) and set the radii, range, density and colours [C]; gd04 blends
them like the lights [I].

- Housings draw with their models' templates at the live transform (World
  per draw).
- Flares: the Flare template's vertex shader builds a camera-facing quad
  from POSITION0 (corner) and POSITION1 (centre) with m_vsSizeScale; its
  occlusion pass (m_vsOcclusionPass, m_vsOcclusionSizeScale, the alpha
  scales) is the engine's occlusion query, not done (flares always show)
  [C: the microcode; I: the engine side]. gd04 tints the flare by the
  housing's volume colour (the snapshots' orange / green / red flares, 0 =
  off) [I].
- Volumes (ConeImpostor): the _Cone_Impostor template on a camera-facing
  quad from the lens along the housing's -z (the flare quads sit at -z):
  vertex = g_SpotlightPosition - z x range x g_SpotlightDir + (x x start
  radius + y x end radius) x g_Rotated_Vector; the pixel shader shapes the
  beam by the chord through the cone at each pixel (sqrt(R^2 - side^2)),
  three planar smoke lookups and g_ConeColor [C: the microcode]. The mesh
  ((+-1, 0, 0, 1) near, (0, +-1, 1, 0) far), g_SpotlightInfo (inner
  radius / end radius, 1), g_ConeColor (volume colour x density x 0.25),
  the smoke (white) and its UV scroll are gd04's [I]. Not done: the
  `volume` type (_Raytrace_cone templates), projectors.

### Particles and pyro  [C scripts; I: the emitter model]

Particle systems come from the zone's scripts: `CreateParticleSystem_fast
pos params_script` with a GP_* struct (scripts\fx\particles\*.qb:
emitrangedims, emit_target, anglespread, force, liferange, emit_rate,
lifetime, emitnum, emitdelaystart, sizerange, speedrange, alignwithpath /
pathfollowtime, color (RGBA word), particlecolor (a gradient), the quick
material's diffusetexture (an .img of ZONES/global.pak), blendmode,
alphacutoff) [C data]. Triggers [C scripts]: the light show's note 56
(LightShow_PyroEvent: `<zone>_lightshow_mapping.pyro_script performance =
...`, e.g. z_artdeco_pyro's six spark pots when good), and the performance
changes (`<zone>_lightshow_mapping.transitions.performance`, e.g.
z_dive_performance_changed spawning Z_Dive_Create_GoodParticles: the
cobra and dancer sparks every 1-3 s while good). gd04 (`render/particles.*`)
runs those scripts on its QScript VM stepped at 60 Hz of song time and
draws each particle as a function of its system's start and seed: a burst
of emitnum, then emit_rate a second for `lifetime` seconds; start in the
emitter box (half extents), direction within anglespread of emit_target,
speed in speedrange, constant force, size from sizerange's start to end
(+ a random share of each's second number), colour = color x the
particlecolor gradient over the life, camera-facing quads stretched back
along the velocity over pathfollowtime with alignwithpath [I: the whole
model]. Drawn with gd04's own textured, additive / blended program [I: the
particlequickmaterial templates translate (414 of 417) but their engine
constants (g_Particle_Vec / g_Particle_Tex) are not known]. Not done: bounce,
spline knots, flipbook animation (quadanimationfps), `burn`, wind.

### Not done yet

Animated level objects (fans, hub caps, the doll: their trigger scripts),
screen effects (`dof`, `ScreenFX_*`, bloom: the reference captures show
both), the characters receiving shadows, the flares' occlusion queries, the light manager's own light selection (VMX128 code),
the templates' render and sampler states, the encore / boss / win / lose
transitions and their cameras, two-player arrays.

## 7. Test

`gd04 --venuetest <disc>` (headless: `SDL_VIDEO_DRIVER=offscreen`): for every
venue (or `GD04_VENUE`), with slowride's notes (`GD04_VENUETEST_SONG`), the
whole song's cuts, camera and lights are simulated at 60 and at 240 fps (the
60 Hz steps plus one advance per frame, a rock meter swinging through the
three bands) and compared every 250 ms of song time; then
`GD04_VENUETEST_FRAMES` (240) frames are drawn at each rate from 15 s;
`GD04_VENUETEST_SHOTS=<dir>` saves a screenshot in the middle of the first
eight shots, `GD04_VENUETEST_LOG=1` prints every cut.
