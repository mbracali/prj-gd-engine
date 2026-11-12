# gd10 rendering: the original shaders, lights, screen effects

How gd10 draws a venue with Metallica's own material shaders. [C] = read in
the disc data or the XEX, [I] = our inference; decisions in
`agents/DECISIONS.md` (D17..D19). Code: `engine/src/render/` (gh3_shader,
xenos_glsl, venue_view, screen_fx, particles).

## The material library

`DATA/FXFILES/MaterialLibrary.bin.xen` is MATL v3 (758 templates, 3,845
shaders) [C data, `data/matlib.cpp`]. Every venue mesh draws with its
template's Default technique (Opaque meshes with a dynamic shadow:
DynamicShadowReciever), the Xenos microcode translated to GLSL as gd04 does
(`render/xenos_glsl.cpp`). Programs are compiled when the venue is built
(Default and the receiver variant) and the screen effects' passes when the
chain is set up, not in the first frame that draws them; how without a
stutter: "Loading without a stutter" below.
`GD10_GH4_SHADERS=0` (development aid) switches the library off: the
approximation draws.

## Why the shaders drew black

The shaders were fed GH3's register layout. gh4 moved the engine constants
[C data: the constant tables of all 4,981 shader records]:

| Register | gh4 | GH3 |
| --- | --- | --- |
| c64..c99 (pixel) | filter passes: m_filterSampleWeights c64, m_filterSampleOffsets c80, m_NearBlurParams c96.. | (material c0.., c168..) |
| c124 (vertex) | g_TextureProjection0 (4 rows) | c156 |
| c132 / c134 / c135 | g_TopviewRange / g_wetEffectParams / g_NearFar_Values | - |
| c137 | g_wibbleDirection | g_shLightShadowIntensity (wibble c164) |
| c138, c140..c155 | as GH3 (LOD scaler, shadow maps, inverse projection, g_clipPlanes c153, viewport, time) | |
| c160 | g_ambientLightColor | c176 |
| c161..c163 | g_dirLight0Dir / Color / SpecColor | c177..c179 |
| c164..c169 | g_dirLight1..3Dir / SpecColor | c180..c185 |
| c172..c175 | g_SHLight0..3 | c188..c191 |
| c176..c191 | g_PointLight: 4 lights x (position, colour, -, -) | - |
| c192..c211 | g_SpotLight: 5 lights x (direction, position, colour, -); c211 the fifth's (attA, attB, coneX, coneY) | - |
| c216..c231 | g_World, g_WorldView, g_WorldViewProj, g_WorldIT | c216..c227 |
| c232..c247 | g_View, g_Projection, g_InvView, g_ViewProj | same |
| c248 / c249 | g_pointLightGroup0AttenuationX / Y (lights 0..3 in x..w) | literals |
| c250..c253 | g_spotLightGroup0AttenuationX / Y (distance), Z / W (cone) | literals |

With the lights written at c176.. the pixel shaders read zeros at c161..c175
(no key light, no SH) and every lit surface was black. The screen effects'
passes also moved their constants into the material block (bright_sat's
m_ColorTransform0..2 c0..c2, was c128..c130; ComposeBloom c0 / c1; Cutoff c0;
DepthOfFieldNear c0 / c4) [C data].

Uniform blocks now: GdXG = c64..c255, GdXVM / GdXPM = c0..c63 of each stage
(c63 the port's alpha reference and bool mask, unused by gh4), GdXO = the
74 per-object registers of `xenos::objectSlot` (c160..c211, c216..c231,
c248..c253). A literal range over c248..c253 yields to the uniform when the
shader's constant table names the register.

## What the microcode does with the lights [C: D_3ChkBox_Pass0, the venues' main template]

- Vertex shader: per point light i, d = position - P, attenuation
  saturate((c248[i] - |d|^2) x c249[i]), N.L with the normalised d; per spot
  light, the same with c250 / c251 and a cone saturate((dot(-L, direction) -
  c252[i]) x c253[i]); colour x N.L x terms summed into one interpolator.
- Pixel shader: texture x material colour x (SH(n) + N.L x key colour) x
  vertex colour + the vertex lights' sum. No shader reads both
  g_ambientLightColor and the SH [C data].

## Lights (D17, D50, D51) [C: 824D7220, 8240F560, 823C9FD8]

Candidates: the lights of the object's groups (`lightgroup`, then
`lightgroup2`, `lightgroup3`, ...; `defaultgroup` lights everywhere, `band`
for `alt_band`) whose box holds the object, and the dynamic ones, at most
32, ranked by luminance (0.299, 0.587, 0.114) x intensity x group intensity
x master intensity (1) x attenuation, x 50000 for `shadow` lights. By rank:
directional lights take the four slots (key c161..c163, then c164..c169),
all but the first also into the SH; a `vertexlight` point light takes a
g_PointLight slot (four), a `vertexlight` spot light a g_SpotLight slot
(five; cone from hotspot / field, full angles); other point lights are
directional ones from the light (SH x attenuation, a slot while one is
free), other spot lights SH only (x clamp((P - pos) . dir, 0, 1)); `noSH`
keeps a light out of the SH. Ambient lights: c160 and SH0. The SH weights
4/17, 8/17 are GH3's [I: SUB_824d7180 not read]. Light flags (8240F560):
dynamic 0x4, shadow 0x8, vertexlight 0x10, noSH 0x20.

## Screen effects (D18, D40..D42)

gh4's TOD profiles (`scripts\engine\tod_profiles.qb`) list, in order:
bloom2, bright_sat, dof3, godray (off), vignette (off), noise2, uberscreenfx
[C data]; the Metal_* profiles add inkblotch, turn vignette / godray on.
None of these draws on its own: the uberscreenfx entry composes them
(D40).

- **The type switch** 824B8460 [C]: each instance of bg_viewport's list
  takes a slot by its type key and, when on, a flag: bloom2 1, godray 1,
  dof3 2, noise2 4, bright_sat 8, vignette 0x10, inkblotch 0x20 (a later
  instance of a type replaces an earlier one).
- **The uber pass** 8251EC80 [C]: the material sys_uberscreenfx (global.pak's
  global_gfx.scn, template `_Uber_ScreenFX`) with the template
  `_Uber_ScreenFX` + `_bloom1 _dof1 _noise1 _color1 _vignette1 _inkblot1`
  for the flags set (64 templates in the library); each part's constants
  set by its instance.
- **What the pixel shader does** [C: the library's microcode, read through
  xenos_glsl; constants named by its table]: viewZ from the depth
  (g_invProjMat c152); a = saturate(max((c9.x - z) / c9.y, (z - c9.z) /
  c9.w)) x g_DOF_Strength c10.x; colour = lerp(frame, dof blur, a) + bloom
  (g_BloomEffectSampler); colour = g_ColorTransform0..2 (c4..c6) x (rgb,
  frame alpha); n = fract(sys_noise + 270669.031 v + 668.3186 u), colour =
  lerp(colour, n x g_Color.rgb, g_Color.w); colour = lerp(colour,
  g_VignetteColor c8, the vignette mask); colour += inkblot.rgb x
  g_ScratchColor c11; colour = lerp(colour, g_BlotchColor c12, inkblot.a x
  c12.w); alpha = the frame's. Vertex: noise UV = (uv + g_Random.xy) x
  g_UV.xy + g_Random.zw, ink UV = g_InkBlotch_Mat (c13, c14, c16) x uv.
- **bloom2's buffer** [C: `_Bloom_SubtractModulate(Stencil)`: out = r c0 +
  g c1 + b c2 + c3; the stencil variant's alpha the converted stencil]:
  the stencil-marked materials (materialfilter) at a quarter of the size
  through colormodulate x (saturation(rgb) - subtract) [I: the matrix's
  values, colormodulate's fourth value as a scale], blurred over
  thickness / 4 quarter texels [I]; equation (eq0 everywhere) not applied.
- **dof3's buffer**: the colour at a quarter, GH3's 7x7 blur at 3 texels
  [I]; g_DOF0 = (nearblurend, nearblurend - nearblurbegin, farblurbegin,
  farblurend - farblurbegin) [I: the shader's form; the CPU's values not
  read]. Strength 0 (every profile; the cameras' DOF_* set it): no dof1.
- **noise2**: g_UV = uv (8, 6), g_Color = (color.rgb, intensity), g_Random
  new every frame [I]. sys_noise / sys_inkblot: global_gfx.tex [C data].
- **vignette**: sys_vignettemask names no texture on the disc [C data]:
  the mask is made from alpha x saturate((r - inner) / (outer - inner)) [I].
- **inkblotch**: placement moved every intervala..intervalb s, rotated
  rotationa..rotationb degrees, tiled xtiling / ytiling [I].
- **bright_sat**: as GH3 (hue, saturation, contrast, brightness, the mix)
  [C data], now inside the uber pass: after the bloom is added (the order
  changed: GH3's chain added the bloom last). ScreenFlash's FlashBS stays a
  pass of its own after the uber [I].
- **godray**: on in Metal_HotBlue only; not drawn [open].

Which profile (D41): the songs' camera notes 81..127 run
CameraCuts_SetNextScreenFx effect = X (change next_screenfx);
CameraCuts_StartCallback's ApplyDeferredCameraEffects runs ApplyScreenFx
effect = next_screenfx at the cut: TOD_Proxim_Apply_<X> replaces the whole
list with Metal_<...>_tod_manager's (none / default: Default_TOD_Manager),
then the shot's dof updates Depth_of_Field__simplified_ [C scripts
guitar_cameras.qb, tod_proxims.qb]. screen_fx.cpp effectManager / readShot;
the camera keeps next_screenfx (venue_camera.cpp). E.g. One: Default, then
Metal_NoisyDefault, Metal_BW_Grainy (black and white), Metal_BW_Grungy.

The camera notes 101..108 (D111) [C scripts misc_fx.qb, bg_screenfx.qb,
guitar_cameras.qb; I: the blend]: CameraCuts_NoteMapping spawns, with ScrID
CameraCuts_ScriptsToKillOnCut, CameraBlur_FastZoom_InandOut (101),
SlowZoom_InandOut (102), FocusIn (103), FocusOut (104), CameraFX_BlackFadeIn
(107), BlackFadeOut (108); 106 sets cameracuts_pendingwhiteflash, which
ApplyDeferredCameraEffects turns into CameraFX_WhiteFlash one game frame
after the next cut. BG_fx_set_blur puts dof3 on with every near / far
distance 0 and strength = appendStrength (the whole frame blurred);
BG_fx_set_brightness_BlackFade sets the TOD's Brightness___Saturation
brightness; _WhiteFlash brightness 100, contrast 0, hue 1, saturation
0.5348, the identity mix. Each step is ScreenFX_UpdateFXInstanceParams
with a `time`, taken as a linear blend from the values then; the scripts
end at the next cut (ApplyScreenFx then sets the profile again).
Timelines (s): 101: 1 over 0.1; at 0.2 0 over 0.3; at 0.4 0.5 over 0.1; at
0.5 back over 0.4. 102: 1 over 0.5; at 0.4 0 over 0.4; at 0.9 1 over 0.3;
at 1.2 back over 0.4. 103: 1 at once; at 0.1 back over 0.5. 104: 1 over
0.6. 107: brightness 0 over the note's length. 108: 0, a frame later back
over 0.75. Flash: 0.06 s, back over 0.06. In the songs: Sad But True and
Whiplash use 101 / 102, 13 songs 103 / 106, Master of Puppets and The
Unforgiven 107 / 108 [C data]. screen_fx.cpp cameraNoteFx; the shot keeps
the script (venue_camera.cpp). Checked: Sad But True 5.3 s (blur), Master
of Puppets 217.3 s (fade to black), Creeping Death 30.8 s (white frames
30.85..30.93 s, back by 30.97 s). A note at its shot's own cut is killed
with it (the original's deferred kill runs a game frame later: a frame of
its effect not reproduced). Not yet: godray, the practice / training
modes' skip of ApplyDeferredCameraEffects.

## Light volumes (D110)

gh4's housings use lightvolumetype volume2dsoftedge (most), volume2d,
volume3d (5..6 a venue), flareonly and laser (z_donnington) [C data];
GH3's ConeImpostor / volume (drawVolumes / drawRaytraced) appear on no gh4
zone, so until now no beam was drawn. The 2D volumes
(VenueView::drawVolumes2D):

- Material sys_lightvolume2d (global_gfx.scn): template _LightVolume2D,
  blend 1 (SRC_ALPHA + ONE), no depth write, textures s0 0x94191135 (the
  falloff), s1 0x49263059 (the pattern), s2 0xa500318e (the smoke)
  [C data]. The variant's name adds _smoke0 (smokeeffect false),
  _softedge1 (volume2dsoftedge), _pattern0 (no volume2dpatterntexture) in
  the library's order [C data: the template names; I: which field picks
  which part, 824496D0 does not decompile].
- The shaders [C: microcode and constant tables]: vertex in world space
  (g_ViewProj only) with a projective TEXCOORD0 (u q, v, q); falloff at
  (u, v), pattern at (u, v) x g_VolumeUVScale c5 + g_VolumeUVOffset c4,
  smoke from the world position x g_UVScale (VS c3) + g_UVOffset (VS c2)
  on the yz / zx / xy planes weighted by g_TexturePlainWeight c2; colour =
  falloff x g_ConeColor c1 x pattern x smoke, x saturate(3.33 x (scene
  view z - z)) for softedge1 (g_DepthBufferSamp s4, g_invProjMat c152);
  alpha x (1 - g_Burn c3).
- [I] (the CPU side, 8244E0F0, is VMX128 and not decoded): a camera-facing
  trapezoid from the lens (2 startradius) to range (2 endradius) along the
  housing's -z, q = the half-width; faded as the sight line turns onto the
  beam; g_ConeColor = volumecolor x density (alpha density) as GH3's
  impostor; g_TexturePlainWeight 1 each; g_UVScale 0.25 a metre, drift 0.02
  a second; g_Burn 0; pattern uv scale / offset the snapshots'
  lightvolumepatternuvscale / offset ((1, 1) / (0, 0) on the disc).
  volume3d (_LightVolume3D's ray-marched cone) is drawn as a 2D soft volume
  [I: approximation]; laser (_LightVolumeLaserBeam) and the volume shadow
  maps (_LightVolume2DShadowMap / ShadowedMap) not drawn.
- Development aids: GD10_VOL2D_LOG=1 (the drawn volumes), GD10_NO_VOLUMES2D=1,
  GD10_VOL2D_PLANE=<weight>; --shadertest with GD10_SHADERTEST_DUMP=<template
  substring> (constant tables, fetches, GLSL) and GD10_SHADERTEST_SYSMAT=1
  (global.pak's sys_* materials: template, blend, textures, constants).

## Particles (D19)

gh4's quick materials add `lighting` (smoke, glows, dust) and `softedge` /
`softedgescale` [C data]. Lit particles take the light set's SH0 (with the
ambient) and 4/17 of the key light at the system, clamped to 1 [I];
softedge fades a particle over softedgescale metres in front of the scene
depth [I: the fade]. `max` holds an emitter to max / mean life particles a
second [I]. Before: z_Forum's StageGlows (20 m add quads, colour 0x6E6E6E,
10 alive) drew unlit at full white: the grey haze over every shot.

## Display gamma and colour (gd05's lessons)

- D25 (gd05): the 360's output goes through the gamma ramp composed with
  the sRGB decode and the BT.709 re-encode; gd10 keeps gd04's
  displayGammaRamp (GH3's identity ramp, 823BB830) on every backend [I for
  gh4: its ramp not read].
  gh4's ramp (824558E8) is the identity too: i x 256 x (0.6 x 0.5 + 0.7)
  [C]; its 1.8 / 1.3 values are stored, not applied.
- D29 (gd05): material colour once, highlight from the first light: the
  original microcode runs as written, so both hold by construction.

## Loading without a stutter (docs/guidelines/usual-bugs.md)

`GD10_HITCHLOG=<ms>` logs each frame over `<ms>` with its screen and the
scopes that took its time (`render/hitch.h`), the longest at exit. Every
loader takes its uploads and compiles a piece at a time through
`hitch::slice()` (pieces while the frame is under 8 ms, one piece a frame
of all of them):

- VenueView: `prepare` (a worker: zone, ad and housing textures decoded,
  housing models read), then `initStep` once a frame: textures, programs,
  sectors, the vertex and index buffers in 4 MB pieces. VenueStage calls
  prepare in `start` (the preload's worker); particles likewise.
- The menu scene reads its zone (`venue::load`) and prepares its view on
  workers; nothing of a zone draws until its view and screen effects are in.
- TextureCache::poll uploads a decoded pak in pieces; SetPakManCurrent
  reads a zone pak's QB files on a worker while the script waits.
- Programs: shared by every `gh3shader::Programs` of a device (compiled
  once a run). Local OpenGL begins them in the driver's threads
  (KHR_parallel_shader_compile) and `settle` finishes them a piece a frame;
  gd-engine-core's Vulkan / OpenGL 3 compile in `settle`, one a piece.
  Drivers keep their own disk caches across runs; a cold cache costs one
  program per frame (NVIDIA: up to ~100 ms on core OpenGL 3, which gd10
  cannot make asynchronous; Vulkan pipelines are made at first draw by
  the core).

The walks check it: every frame from a song's request to 5 s into it within
`GD10_HITCH_BUDGET` ms of work (the present not counted; default 50).

## Development aids

GD10_GH4_SHADERS=0, GD10_XENOS_DUMP=1 (the GLSL of each program),
GD10_VENUE_MESHLOG=1 (template, blend, flags per mesh), GD10_VENUE_SKIP=<template>,..,
GD10_PARTICLES_LOG=1 (with size, colour, blend, lit), GD10_PARTICLES_SKIP=<texture>,..,
GD10_SCREENFX=<effect> (every shot takes that camera effect, e.g. InkBlotch_Red),
GD10_SCREENFX_TIME=1 (the chain's CPU time), GD10_NO_SCREENFX / _PARTICLES / _PROJECTORS / _RAYTRACE.
- GD10_LIGHTS_LOG=<ms> (every light's live values and each group's light
  count, once, at the first venue frame past <ms>); GD10_XENOS_DUMP=1 also
  logs each shader's constant table.

## Cameras (D43..D47) [C where marked]

The camera arrays and CameraCuts scripts are gd04's port (docs/venues.md in
gd-game-04), with gh4's additions:
- Camera lock targets (create_all_camera_lock_targets): ghosts with the
  GH4_Camera skeleton that play camera anims; an entry `lockto = <target>
  locktobone = bone_camera` sits on that bone (render/mocap_camera.cpp).
  Band_PlayClip's `cameras` / `drummercameras`, start_drummer_cameras, the
  members' loop cameras (<loop>_c01 / _c02) and the transitions'
  SpecialCamera_PlayAnim drive them (D43, D44). A target playing nothing at
  a cut is not picked [I].
- CrowdOff on an entry hides the crowd for the shot (D45);
  distfromlookat keeps the framing distance (D46); SpecialStepMove (D47).
- screenoffset: x right, y down, half-screens; FOV horizontal (16:9) [C data].
- GD10_CAMLOG=1 logs each shot (array, entry, name); =2 also each aimed
  shot's authored framing, camera and bone.

## Venue brightness (GD10-040, open)

z_forum's arena wide shot (venuetest, Enter Sandman) reads mean luminance
0.013 (0.043 without the screen effects) against 0.28..0.35 in the disc's
venue_forum.bik preview (meadowlands 0.215, tushino 0.188). No single
missing term found: D_3ChkBox_Pass0 (222 of z_forum's 394 meshes) is
diffuse x material colour x lightmap x (SH + key N.L) x vertex colour +
vertex lights [C microcode], and the data are dark (arena vertex colours
0.07..0.24, the stage lightmap mean 0.185, the Periph SH0 about 0.5);
Metal_BlueTint's bright_sat takes another ~3x. Ruled out [C]: ambient lights
add colour x intensity to c160 / SH0 unscaled (824D78E4); the light record
(824D7330, 8240F8F0) has no extra scale; light boxes (8240F560 reads bv_pos /
bv_size / bv_trans / bv_rot, `global`; only Periph_Ambient01 has one on
z_forum, 233 m); SUB_824d7180 handles directional / point / spot only (its
k0 / k1 at 0x82C7ADC0 / ADC4 have no immediate writer: 4/17, 8/17 stay [I]);
no exp_adjust in the vertex fetches or textures; no gamma sign bits in the
textures; the identity gamma ramp (D52). Leads [I]: the 3D pass's render
target / resolve (82431888 maps EDRAM formats to resolve textures, among
them A16B16G16R16F 0x1A22AB60: an FP16 surface exists; which pass uses it
not tied; no sRGB A8R8G8B8 constant found), a CPU-set constant scaling the
lightmap or vertex colour (2x "overbright"), the frame read as gamma
(frame^(1/2.2) gives 0.144 / 0.353). Measurements and debug knobs:
agents/scratch/m10_dark_notes.md (git-ignored).

### Measured 2026-11-12 (Enter Sandman, venuetest's 8 shots a venue)

Mean luminance (`magick -alpha off -colorspace Gray`) of the 8 shots
before / after the 2D light volumes, and the disc's venue_<x>.bik preview
(2 frames a second, all frames, luma; the Bink streams are full range):
z_forum 0.098 / 0.114 (movie 0.275), z_meadowlands 0.157 / 0.166 (0.186),
z_tushino 0.086 / 0.086 (0.148), z_justice 0.101 / 0.102 (0.353), z_mop
0.045 / 0.045 (0.270), z_donnington 0.051 / 0.051 (0.163), z_stone 0.090 /
0.091 (0.229), z_icecave 0.077 / 0.119 (0.282), z_studio2 0.133 / 0.138
(0.263), z_soundcheck2 0.208 / 0.208 (0.312). The movies are empty-venue
fly-throughs under beams, smoke and pyro, so the gap is a guide, not a
target. One more lead [I]: Metal_BlueTint's bright_sat (brightness 0.9705,
contrast 1.1064 about 0.5) maps x to 1.073 x - 0.051, which clips
everything under 0.048 to black: the arena wide shots (0.04 before it)
vanish; on a frame in the movies' range (Sad But True's profile: 0.26 ..
0.32) the same pass is mild. A scene pass that ends in a gamma-encoded
target would put those shots at ~0.2 before the pass (the lead above).

## The crowd (GD10-042)

- Every venue but z_icecave has <zone>_crowd_models; no difficulty or
  quality setting changes the count (guitar_crowd_data.qb, create_crowd_models:
  disable_crowd only) [C scripts]. The 3D peds render into the atlas; the
  cards draw Billboard_FastLit.
- Billboard_FastLit [C: microcode]: colour = atlas x m_psPass0MaterialColor
  x vertex colour x (g_ambientLightColor c160 + the g_PointLight /
  g_SpotLight vertex lights); no SH, no directional light. The cards'
  vertex colours are baked shading: on z_forum's card meshes about half
  the vertices are 0 (black) and the rest 0x20..0xBF [C data], so the back
  of the crowd is black whatever the lights, and the front reads at
  vertex colour x the crowd group's ambient (Crowd_Ambient01 0.55; its
  directional light 0.9 lights the peds in the atlas, not the cards).
  The port follows the shader as written; the cards show where the data
  makes them bright (AUDIENCE shots, the front rows) [checked: z_forum
  35.5 s audience shot].

## Open

- Why venues read darker than the preview movies (GD10-040): see "Venue
  brightness".
- The values the screen effects' instances put in their constants (VMX128
  code): the shaders are read, the constants [I].
- The crowd cards read dark by their data (see "The crowd"); the peds'
  own light (the atlas) is the crowd group's.
- The 2D light volumes' CPU constants and geometry (8244E0F0), volume3d's
  own shader, the laser beams.
- Camera FOV pulses (CameraCutsEffect_FOVPulse: ccam_pulsefinalfov's curve
  units), the hand cam's amplitudes (ccam_sethandcamparams), shadow casters
  per shot (cameracuts_shadowcasters) not applied.
- The highway / gems / HUD do not use the library's templates (D15).
