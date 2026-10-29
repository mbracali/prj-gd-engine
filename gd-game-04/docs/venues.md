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

### Star Power camera  [C: StarPowerOn Function_8211DA70, guitar_starpower.qb, guitar_cameras.qb]

In a one-player song StarPowerOn spawns do_starpower_camera: one game frame
later CameraCuts_AllowNoteScripts false (the camera notes are skipped) and
CameraCuts_SetArrayPrefix prefix='cameras_starpower' changetime=2500
(CameraCuts_ForceChangeTime: the shot playing ends once it has lasted 2.5 s,
whether changes are enabled or not); 7 s later prefix 'cameras' and notes
on again. Star Power's end runs Kill_StarPower_Camera (prefix 'cameras',
notes on). The walk camera (start_walk_camera, cameras_walk, the same with
max_walk_camera_cut_delay 1000) needs guitarist_walking (Band_WalkToNode),
not done.

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
  `intensity`, `specularintensity`, `shadowintensity` (its
  `FarAttenStart/End` are not applied: §5 "Lights" [C]); others move light
  housings (`housingchecksum`, pos, Quat, volume / projector colours) [C
  data]. gd04 blends each light from its current value over the blend time
  [I], cycles a list in order [I], mixes override colours by
  `lightshow_coloroverrideblend` 0.4 except `LightShow_ColorOverrideExcludeLights`
  [I: the meaning of 0.4].
- Lights by group: geometry nodes have a `lightgroup` (none, band, Crowd,
  Periph, Fuji, ...) and so do lights [C data]; which lights reach a mesh
  is the light manager's (§5 "Lights") [C].

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

### Lights  [C: the light manager, read with a VMX128 decoder over Ghidra's `.word`s]

The per-object registers c176..c191 are filled by 823DA990 (the object's
light set) and uploaded by 823DB168 before each draw (Function_822B6E08
only builds the shadow cascades c140..c148) [C]:

- **A light** (822BE020; defaults 822B7998): `Type` point / spot / ambient
  / PRT, anything else (`dir`, none) directional; `lightcolor` (0..1),
  `intensity` (default 1), `specularintensity` (1), `shadowintensity`
  (`shadowdensity`, 1), `attenstart` / `attenend` (7.5 / 15), `hotspot` /
  `field` (45); it shines along rotationquat x (0, -1, 0) (z_dive's
  Stage_Up lights below the stage point up); point and spot lights fade
  as clamp((attenend^2 - (d^2 + 0.0001)) / (attenend^2 - attenstart^2 +
  0.0001), 0, 1); flags `dynamic` (a candidate everywhere) and `shadow`
  (ranked x 50000). Its box: directional and spot lights a cube of half
  attenend around pos + dir x attenend / 2, the others attenend around
  pos (`boxcenter` / `boxcorner` replace them). "_VC_" lights are lights
  like the others.
- **Candidates** of an object at P (a mesh: its bounding sphere's centre;
  a model, i.e. a band member or ped: its origin): the lights whose box
  holds P plus the dynamic ones, of the object's light group, `band`
  lights for an `alt_band` object, and `defaultgroup` lights (no
  `lightgroup`) everywhere; at most 32 (822BB680).
- **Ranking**: luminance (0.299, 0.587, 0.114) . colour x intensity x
  attenuation (x 50000 for shadow lights), stable, descending.
- **Slots**: the first directional or point light is the key light
  (g_dirLight0Dir = its direction, from the light to P for a point light;
  g_dirLight0Color = colour x intensity, .w = shadowintensity;
  SpecColor = colour x specularintensity; not in the SH); the next three
  take g_dirLight1..3 (specular colour colour x specularintensity, the
  direction for slots 2 and 3 the light's own) and also go into the SH;
  further directional / point lights into the SH only. Slot colours are
  not attenuated.
- **SH** (823DA668): per light, with l the direction towards it, SH0 +=
  C x 4/17, SH1..3 += C x 8/17 x l (C: colour x intensity, x attenuation
  for point lights; spot lights always into the SH, x clamp((P - pos) . dir,
  0, 1), no cone). **Ambient** lights sum into c176 (colour x intensity).
- The light show's snapshots (light_setparams 821B4950) set r g b / 255,
  intensity, specularintensity, shadowintensity; FarAttenStart / End are
  not applied (ApplyLightFarAtten*Change are empty scripts) [C].
- The TOD's lev_red / green / blue are not read by the executable [C].

gd04 does exactly this (`gh3shader::lightObject`, `lightIn`,
`groupLights`); light group intensities (SetLightGroupIntensity) and the
model light fade (SetModelLightsFadeTime) are taken as 1 [I].


Render state (823DC180, over the effect pass) [C]: blend from `+0xd8` (0
ONE / ZERO, 1 SRC_ALPHA / ONE, 2 its reverse subtract, 3 SRC_ALPHA /
ONE_MINUS_SRC_ALPHA, 4 ZERO / SRC_ALPHA, 5 DST_COLOR / ONE, 6 DST_COLOR /
ZERO, 7 ONE / ONE, 8..12 colour-only variants; the zones use 0, 1, 3);
`+0xcc`: 0x08 cull none, 0x10 ZFUNC ALWAYS, 0x20 no z write, 0x40 no alpha
test (else the reference `+0xe8`'s top byte), 0x80 no stencil; draw order
`+0xd4` [I]. The samplers clamp per `+0xea + slot` (bit 0 u, 1 v, 2 w) [C,
applied when both u and v clamp]. Back faces are culled
(Direct3D's default, counter-clockwise) [I: checked on all eight venues;
this fixes the stage-rear cameras, e.g. z_dive's Stage_Rear01 stands behind
the back wall at z = -10.2 and showed its back faces (M5 bug); GD04_CULL=0
turns it off]. Templates that do not translate (loops: three water
templates, used by no scene on the disc [C data: all 1,300 zone materials and
the characters']) fall back to the M5 approximation; GD04_GH3_SHADERS=0 forces it.
Every scene material writes its stencil: func ALWAYS, z-pass REPLACE with
the material's `+0xdc` (0 or 1) unless `+0xcc` & 0x80 [C 823DC180]; the
bloom's material filter reads it (below).

### Screen effects  [C: 823DE658, the ScreenFX classes; render/screen_fx.*]

The venue viewport (bg_viewport) runs its ScreenFX instances in the order
the scripts added them, each on the previous one's output, in its own
A8R8G8B8 + D24S8 target (every pass clamps to 8 bits); the highway and HUD
viewports never see them [C]. enable_bg_viewport_properties ->
TOD_Proxim_Update_LightFX_Viewport: `venue_DOF` (the camera shot's `dof`,
a DOF_*_tod_manager, else DOF_Off), then Default_TOD_Manager's screen_FX:
`BnS` (bright_sat) and `sbloom` (simple_bloom) [C scripts].

- **dof** (824440F8; on when strength > 0): the colour box-downsampled to a
  quarter (_2D_DownSample4x: the mean of 16 texels), blurred by
  _2D_GaussianBlur7x7 H then V (rho = sqrt(blurradius / 3), the centre and
  three merged tap pairs, not normalised), then _2D_DepthOfFieldNear over
  the scene with SRC_ALPHA / ONE_MINUS_SRC_ALPHA: a = strength x
  saturate((z - frontdist x far) / ((backdist - frontdist) x far)), z the
  linear depth (m_vInvProjMatT), far the camera's far clip [C; I: far =
  camera_default_far_clip 500].
- **bright_sat** (8243E5F0): out = Mix(b (k (Sat(Hue(rgb)) - 0.5) + 0.5)),
  hue a rotation about (1,1,1) by hue x 2 pi, saturation with Haeberli's
  weights (0.3086, 0.6094, 0.0820), alpha = the red result
  (_2D_Tex_BS_NoViewport). The TOD's BnS has hue 1 (one turn): an identity
  pass that always runs [C].
- **simple_bloom** (8243D240), materialfilter 1 (the only use): the colour
  where the stencil is 1 (the dc = 1 materials: neon, signs, light lenses
  ...), box-downsampled to a quarter, blurred twice by _2D_GaussianBlur7x7H
  / V (seven taps -3..3, rho 1, normalised, times falloff1 = 5 the first
  time and 1 the second: a gain that saturates the halo, not a radius),
  added with SRC_ALPHA, ONE as B x bloom1 (0.35) x strength / 255
  (_2D_ComposeBloom0) [C]. materialfilter 0 would use _2D_Cutoff (the
  16-texel mean where its rgb sum exceeds color / 255) instead.
- ScreenFlash (z_prison only) and the luminance-adapted variants are not
  done.

gd04 runs those passes with GH3's own filter shaders (the ImmediateMode /
ImmediateModeAlt / ImmediateModeFilter techniques of the material library,
translated like the materials); GD04_NO_SCREENFX=1 turns them off. The
Enhanced Options' Bloom / Bokeh rows are separate port enhancements.

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
**Lighters** [C scripts guitar_crowd.qb, the songs' `<song>_performance`]:
12 songs (paintitblack, welcometothejungle, ...) call Crowd_StartLighters /
Crowd_StopLighters; in between crowd_monitor_performance checks every game
frame: not bad -> Crowd_AllSetHand right lighter (rhand_lighter: global.pak's
Crowd_HandR_Lighter.skin instead of the clap), Crowd_AllPlayAnim special
(each ped one game frame after the last: crowd_play_adjusting_random_anims
with the anim set's `special`, blend Crowd_BlendTime_Special) and
Crowd_ToggleLighters ON (unhide `<zone>_LIGHTER_Geo00..14`, the venue's
lighter geometry, created hidden by crowd_create_lighters except in
z_artdeco, moved by their trigger scripts); bad -> the clap, `idle`, OFF.
gd04 does this on the 60 Hz grid of song time [I: the frame grid].

### Stage peds and moving objects  [C scripts; render/stage_peds.*, render/particles.cpp]

- The zones' gameobject nodes with a skinned `Model`, created at start
  (StagePeds\SPed_*.skin: security guards, devil girls, the caged dancer,
  the hoop girl; Venues\*\Ven_*.skin: the art deco dragon and knights, the
  hell puppet and hammer) stand at their node on the skeleton of their
  profile's creation params (`SkeletonName`), lit by the profile's
  `lightgroup`. Their trigger scripts run on the node (gd04: the particles'
  VM): GameObj_PlayAnim / GameObj_WaitAnimFinished, obj_spawnscriptnow
  crowd_play_adjusting_random_anims (hero_get_skill_level_cfunc,
  get_anim_speed_for_tempo_cfunc), Obj_SwitchScript, hide / Unhide [C].
  The stage divers are hidden at the song's start (Crowd_StageDiver_Hide;
  Crowd_StageDiver_Jump has no caller) [C]. gd04 draws a ped once a clip
  played (blendduration -1: 0.3 s [I]).
- Node trigger scripts and the performance scripts move level objects:
  obj_rotx / roty / rotz {Speed degrees a second, angle}, obj_rotate
  {absolute, time}, Obj_WaitRotate, obj_movetopos / obj_movetorelpos /
  obj_setposition {time | Speed}, Obj_WaitMove, CallScriptOnNode {prefix,
  callBack_Script} (z_dive's fans and hub caps by the crowd's performance,
  z_party's globes, z_video's fans, z_hell's imps, z_prison's sirens, ...)
  [C scripts; I: the units, the Euler order, the pivot = the node]; the
  speakers' Venue_Pulse* scripts (AddPulseEvent on hitnote_<colour>) scale
  them to `amount` (1.12) for a game frame on each hit note of those
  colours, then back over `time` (0.1 s) [I: as Venue_PulseOnEvents' own
  delayed branch]. gd04 evaluates every motion as a function of song time
  (the scripts run on its 60 Hz steps) and draws the node's meshes with that
  transform as g_World.

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
- Flares (823C0698) [C]: the Flare template builds a camera-facing quad
  from POSITION0 (corner) and POSITION1 (centre) x m_vsSizeScale. Two
  occlusion queries a flare, read the next frame: a small quad
  (m_vsOcclusionPass 1: m_vsOcclusionSizeScale, 3..10 % of the flare) with
  colour writes off, first with ZFUNC ALWAYS (A), then depth tested (B);
  the flare is drawn without depth test at m_vsSizeScale = B / A (A = 0:
  not drawn), hidden nearer than NDC z 0.91 and faded in up to 0.93 (x
  sqrt((z - 0.91) x 50)). The occlusion scales the size, not the alpha.
  gd04 does this with GL occlusion queries (OpenGL ES: any samples, 0 or
  1) [I: the NDC cut with near 0.1, far 500], and tints the flare by the
  housing's volume colour (the snapshots' orange / green / red flares, 0 =
  off) [I].
- Volumes (ConeImpostor, Function_823B4B68) [C]: `flareonly` housings have
  none; a housing's beam starts at its position along its -z. Skipped
  while the camera is within endradius of the beam's axis (the camera's
  distance to the axis point P - D x |(P - C) . D|), faded in over the next
  metre (dens = volumedensity x min(q - endradius, 1)). VS: g_ConeSize
  (startradius, endradius, range, dens), g_SpotlightDir (the housing's
  +z), g_SpotlightPosition, g_Rotated_Vector = normalize(P' - C) x D,
  g_UVOffset = a fixed random offset per volume record, g_UVScale = 0 (no
  data sets smoketexcoordscale / smokevelocity); PS: g_SpotlightInfo =
  (innerradius, 1 - innerradius) (a share of the radius), g_ConeColor =
  volumecolor x dens (alpha dens). The mesh: (+-1, 0, 0, 1) near, (0, +-1,
  1, 0) far; the pixel shader: profile (1 - s, with a plateau inside the
  inner share) x the far fade x the chord through the cone (2 sqrt(R^2 -
  side^2), metres) x g_ConeColor x three planar lookups of the smoke
  texture (global_gfx.tex 0xa500318e, 128 x 128 grey noise) weighted by
  the camera z axis squared. Blend ONE / ONE, cull none, depth tested, no
  z write; drawn after the scene and before the screen effects (so
  bloomed). Defaults: density 0.5, start 0, end 10, inner 0, range 10.
  gd04 does all of this. Not done: the `volume` / `normal` types
  (_Raytrace_cone templates, a 20-segment circle mesh and the depth
  buffer: 9 objects), projectors (_Projector_StageOnly).

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
model]. Drawn with gd04's own program in the quick
material's state [C: 823B7768, 823DC180, the particlequickmaterial
shaders, which read no particle constants: the CPU builds world-space
quads; g_Particle_Vec / Tex belong to the GPU rain]: blendmode add 1,
subtract 2, blend 3, modulate 4, brighten 5, else 0 (opaque) by the
material blend table; `burn` = premultiplied (ONE / ONE_MINUS_SRC_ALPHA)
with a' = (1 - burnvalue) a, rgb x a x a'; alphacutoff the alpha test;
texturelayout 2x2 / 1x2 / 2x1 cells, cell = (int(age x quadanimationfps)
+ the particle's random) & mask (the random alone at 0 fps) [C 82427E08].
Not done: bounce, spline knots, wind, the size / rotation maths of GH3's
own quad builders (VMX128 code not decoded), lit quick materials.

### Not done yet

The characters receiving shadows, the effect passes' own render and
sampler states (gd04 uses the scene material's, which 823DC180 applies
over them), the `volume` light type and projectors, the ReducedLOD
technique [I: the venues fit inside lod_dist1, 150 m], the encore / boss / win / lose transitions
and their cameras, two-player arrays, the drum pulses (drumkick events),
ScreenFlash, the first branch of a `random(...)` of clip names (z_prison /
z_artdeco security, the caged dancer: gd04's VM yields no value for it, so
those peds wait for a later branch), walks (Band_WalkToNode, so no
cameras_walk).

## 7. Test

`gd04 --venuetest <disc>` (headless: `SDL_VIDEO_DRIVER=offscreen`): for every
venue (or `GD04_VENUE`), with slowride's notes (`GD04_VENUETEST_SONG`), the
whole song's cuts, camera and lights are simulated at 60 and at 240 fps (the
60 Hz steps plus one advance per frame, a rock meter swinging through the
three bands) and compared every 250 ms of song time; then
`GD04_VENUETEST_FRAMES` (240) frames are drawn at each rate from 15 s;
`GD04_VENUETEST_SHOTS=<dir>` saves a screenshot in the middle of the first
eight shots, `GD04_VENUETEST_LOG=1` prints every cut.
