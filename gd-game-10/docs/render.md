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

## Screen effects (D18)

gh4's TOD profiles (`scripts\engine\tod_profiles.qb`) list, in order:
bloom2, bright_sat, dof3, godray (off), vignette (off), noise2, uberscreenfx
[C data]. The parsers are in the XEX (bloom2 825074F8, dof3 82503388, the
type switch 824B8460) [C: the keys] but their passes are VMX128 code not
read. gd10 draws:

- **bright_sat**: as GH3 (hue, saturation, contrast, brightness, the mix;
  gh4 writes the mix as red_mix_r.. floats) [C data]; Default_TOD_Manager's
  saturation 0.6 makes the venue less saturated, as the data says.
- **bloom2**: on GH3's simple_bloom passes: the stencil-marked materials
  (materialfilter 1) or the colour over `subtract`, blurred at a quarter
  (7x7 H + V), added x colormodulate [I]; saturation, thickness and
  equation are not applied.
- **dof3**: the far blur only, farblurbegin..farblurend in metres x
  strength (_2D_DepthOfFieldNear), 3 quarter texels [I]; the near blur is
  not drawn.
- Not drawn: noise2 (film grain, intensity 0.1), uberscreenfx, godray and
  vignette (off in every profile) [open].

Order: dof, bright_sat, bloom (GH3's chain); the gh4 list order (bloom2
first) is not followed yet [open].

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
GD10_NO_SCREENFX / _PARTICLES / _PROJECTORS / _RAYTRACE.

## Open

- The XEX's light set (which light takes which slot), bloom2 / dof3 / noise2 /
  uberscreenfx passes: VMX128 code; needs a VMX128 decoding table of our own.
- Meadowlands at 90 s (Enter Sandman): a ring of 17 scripted spark glows
  (PH_RadialGlow_01, 0.2 -> 4 m, add, opaque) near the camera still veils
  the shot.
- The arena floor in front of z_Forum's stage draws black (crowd area).
- The highway / gems / HUD do not use the library's templates (D15).
