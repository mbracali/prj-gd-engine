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

## Lights (D17) [I where marked]

Candidates and ranking are GH3's (box, group, at most 32, luminance x
intensity x attenuation, shadow lights x 50000) [C for GH3; I for gh4: the
light set is VMX128 code not read]. The first directional light is the key
light, the next three the specular slots and the SH (4/17, 8/17 l); the
first four point lights go to g_PointLight, the first five spot lights to
g_SpotLight (cone from hotspot / field as full angles, half of each against
the cosine [I]); further point / spot lights into the SH as GH3's;
ambient lights into c160 and SH0 [I]. Vertex colours 0..255 (no 2x).

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

Not yet: the camera notes 101..108 (CameraBlur_* zoom / focus blurs,
CameraFX_WhiteFlash, CameraFX_BlackFadeIn / Out: BG_fx_set_blur /
_brightness on the instances over time), godray, the practice / training
modes' skip of ApplyDeferredCameraEffects.

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

## Open

- The XEX's light set (which light takes which slot), and the values the
  screen effects' instances put in their constants (VMX128 code; needs a
  VMX128 decoding table of our own): the shaders are read, the constants [I].
- Meadowlands at 90 s (Enter Sandman): a ring of 17 scripted spark glows
  (PH_RadialGlow_01, 0.2 -> 4 m, add, opaque) near the camera still veils
  the shot.
- The arena floor in front of z_Forum's stage draws black (crowd area).
- The highway / gems / HUD do not use the library's templates (D15).
