# Porting gd02's (GH2) shaders and post-effects to gd01 (GH1) as Enhanced options

Status: study, 2026-10-28.

## Verdict

**Worth doing selectively.** Four GH2 features fit GH1's PS2 scenes with no
missing data and are cheap: the **GH2 post chain "look"** (bloom + colour
transform + luminance curve), **GH2-style depth of field**, the **GPU-side
flare occlusion test** and **higher-resolution crowd cards**. Each takes
days, not weeks. **Per-pixel lighting** is a medium job with a modest gain.
**Normal, specular and environment maps, and projected spotlights with
shadow maps**, need data GH1 does not have (or that gd02 itself has not
built yet). Leave them for mods.

All of them must be **Enhanced options, default off**. The default stays
GH1's exact look ("match the original"). One extra preset, **"Look: GH2"**,
can switch the fitting ones on together.

For the recurring theme ("newer games' shaders in older games"), build a
small **shared effects library** (`gd-engine-fx`): our own GLSL plus
parameter maths, independent of the renderer, used by gd02 for its
faithful chain and by older games as Enhanced options. See the end.

## What gd01 already has (so we don't duplicate it)

gd01's Enhanced post chain (`render/post_fx.h`): dual-Kawase bloom (low,
medium, high), cinematic curve + grades, ground fog / Super Fog (volumetric
light shafts from the stage lights), SSAO, light streaks, Star Power motion
blur, TAA/FXAA/SSAA, FSR 1/3.1, DLSS 4, CAS, chromatic aberration, and grain.
It has GH1's own DOF (`enable_dof` per shot, `render_gems_dof.md` §4), GH1's
VU1 per-vertex lighting and fog (`lighting.md`), the spotlight with beam and
floor glow (`spotlight.md`), flares with GH1's **1-pixel depth readback**
(`renderer.cpp` `glReadPixels`), and crowd impostors rendered with the main
camera's rotation into **128x256** targets (`crowd_renderer.cpp`).

The key data fact: **GH1 has no overbright**. Textured surfaces clamp at 1.0
(`lighting.md`: "There is no 2× overbright"). Only additive passes (beams,
glows, flares, particles) stack above 1. GH2 draws everything into a /4 "HDR"
buffer, with emissive multipliers and intensify pushing values up to 4. So
GH2's thresholds cannot be used literally on GH1 frames (see item 1).

## Candidates, one by one

| # | GH2 feature (`gd-game-02/docs/renderer.md`, `venues.md`) | Fits GH1? | Missing data | Effort |
|---|---|---|---|---|
| 1 | **Bloom** (bright pass `smoothstep(threshold.w, 4, lum)` + per-channel `saturate((c − t)·1024)`, two 2× box downsamples, a 15-tap Gaussian at a quarter of the size, σ = bloom_scale, kernel 2.5, intensity 1) | Yes, as a "GH2 style" choice next to gd01's Kawase bloom. Needs gd01's RGBA16F scene so additive stacks keep values above 1. | GH1 has no RndPostProc. Use GH2's shipped defaults (thresholds 3.9/1, scale 3, intensity 1, kernel 2.5 are numbers, not assets) and **rescale the white threshold to GH1's 0..1(+additive) range** (about 0.8 to 1.0 instead of 1 to 4 on the /4 scale), tuned by eye. | 3 to 5 days |
| 2 | **Colour transform + luminance map** (postprocess.fx: RGB select rows + bias, `rgb × curve(lum/4)`, GH2's curve dipping to about 0.55 near luminance 1.5) | Yes. It is a pure screen-space grade. | The curve comes from `arena_luminancetest04.tex` on **GH2's disc**: we can't ship it. Either fit our own curve with the same shape (a few control points), or read it from the user's GH2 disc when gdul knows one (optional, and it makes a game depend on another game's disc: avoid). | 2 to 3 days |
| 3 | **Depth of field** (7-tap separable blur at a 640×320-equivalent buffer, ±3.5 texels × 0.666, depth lerp `clamp(|z·a + b|, 0, max_blur)` from focus, blur_depth, max_blur) | Yes. GH1 shots already say *when* (`enable_dof`) and *what* to focus on (the shot's target). GH1's own DOF is a cruder 3-px blend. | GH1 has no blur_depth/max_blur. Use GH2's most common keyframe values (blur_depth 0.5, max_blur 0.5) and GH2's focus formula `|camera − target| × (1 + multiplier)`. | 3 to 5 days |
| 4 | **Flare occlusion on the GPU** (5 depth samples into a 128×1 ping-pong texture read by the flare pass, no readback) | Yes. It is invisible to the player and removes the per-frame `glReadPixels` stall (worst on macOS/Android). | None | 2 to 3 days |
| 5 | **Crowd cards at GH2 quality** (256×512 32-bit card per archetype, card rectangle from projected feet/head/hands, `rotate` facing the camera) | Mostly. GH1's impostors already follow the camera. The gain is the resolution (128×256 → 256×512 or more at high render scales) and tighter rectangles. | None (GH1 crowd chars have the needed bones) | 2 to 4 days |
| 6 | **Per-pixel lighting** (PER_PIXEL_LIGHTING: N·L per fragment, point-light falloff `1 − clamp(d/range)`) | Partly. GH1 lights are up to 3 directionals or 1 point (`scene_renderer.cpp`). Per-pixel evaluation smooths low-poly gradients and point-light hot spots. | Nothing for diffuse. **Specular has no data** (no specular_rgb/power in GH1 mats). Inventing it is not faithful, so leave it off or make it a mod. | 1 to 1.5 weeks (scene + character shaders, with a check that "off" stays pixel-identical) |
| 7 | **Spotlight beams, GH2 style** (camera-facing sheets with side/end fades, lens disc, floor spot, fade when end-on; GH2's own shader has screen-space softness) | Partly. GH1 has its own beam meshes (`spotlight.md`, the `anim_beam*` rigs). What ports is **soft intersections** (depth-fade where the beam meets the floor or band) and the **end-on fade**. Not GH2's geometry. | None | 3 to 5 days |
| 8 | **Projected spotlights + shadow buffer** (PROJ_LIGHTS, SHADOW_BUFFER) | Weak fit. GH1 has one follow spot and a projected guitarist shadow already (`faces_shadows.md`). | No gobo textures in GH1, and **gd02 has not built its shadow-map pass yet** (`renderer.md` "Open"). Do it in gd02 first, then reuse it. | 2 to 3 weeks (after gd02) |
| 9 | **Normal / specular / environment (cube) maps** | No. | GH1's PS2 data has none of them. Generated maps (AI or heuristics) would be an asset-mod feature, not a shader port. | n/a (mods) |
| 10 | **Light presets with crossfades** (GH2's preset manager: keyframes, fade_in timed to land 4 beats ahead, adjective scoring) | Not a shader. GH1's venues drive light with their own EnvAnims and scripts. Replacing those would change the original's behaviour. | GH1 has no presets | Skip |
| 11 | **FOG / FADE_OUT / noise / trails** | gd01 already has ground fog, grain and Star Power blur. GH2's depth-volume fog and trails are unused by its own data. | n/a | Skip |

**Suggested "Look: GH2" preset** = items 1 + 2 + 3 + 5 (+ 7). It is cheap
because the passes are full screen and resolution-scaled, and it gives GH1
venues the GH2 bloom/grade/DOF feel.

## General approach: a shared effects library (`gd-engine-fx`)

The goal: the next game (gd03+) and older games get newer games' effects
without copying code between frozen trees.

- **Where**: a small library beside the launcher SDK (for example
  `gd-engine-fx/`), statically linked into each game module. It has our own
  code only. The disc's HLSL never enters the repo (as with gd02 today).
- **What it holds**, per effect:
  1. the **GLSL** (330 core / 300 es, later SPIR-V/MSL through
     `study_backends.md`'s pipeline);
  2. a **parameter struct + the maths** that turns game data into shader
     constants (GH2's DOF constants from near/far/focus, the Gaussian
     weights, the bloom threshold remap);
  3. a **pass description**: inputs (scene colour, depth, previous frame),
     outputs, relative sizes.
- **Thin backend interface** (about 8 calls: create program, create target,
  bind texture, set constants, draw a full-screen triangle, blit, query depth
  convention). gd02 implements it on `RenderDevice`. gd01 implements it on
  its raw GL (and later on the device layer). This avoids making gd01 adopt
  gd02's whole device first.
- **Per-game adapters** map each game's data onto the parameters (for
  example GH1 `enable_dof` + shot target → DOF params, and GH1 colour range →
  bloom threshold remap). Faithful use (gd02 with GH2's own PostProc values)
  and Enhanced use (gd01) share one implementation, so a fix lands in both.
- **Rules**: every borrowed effect is opt-in (Enhanced, default off). The
  Enhanced menu's planned "remove all effects" switch (wave B backlog)
  covers them. Selftest screenshots must stay identical with the effects
  off.
- **Move gd02's DOF and bloom into the library first** (they are already
  isolated in `post_fx.*`, 504 lines), then wire gd01 to them. This proves
  the interface with real code.

## Sources (in-repo; no external facts needed)

- gd-game-02/docs/renderer.md: permutation bits, lighting model, post-process
  (DoPost), RndPostProc defaults, DOF constants
- gd-game-02/docs/venues.md: light presets, spotlights and flares, crowd cards
- gd-game-01/docs/lighting.md (VU1 lighting, colour scale, fog),
  render_gems_dof.md §4 (GH1 DOF), spotlight.md, venue_fx.md (flares),
  crowd.md / crowd3d.md (impostors), faces_shadows.md (projected shadow)
- gd-game-01/engine/src/render/post_fx.h (current Enhanced chain)

## Backlog candidates

- **(GDALL, Enhancement) `gd-engine-fx` shared effects library**: backend
  interface, gd02's bloom + DOF moved in, gd02 still pixel-identical. 1 to
  1.5 weeks.
- **(GD01, Enhancement) GH2-style bloom** (bloom style: Kawase / GH2) with
  the threshold remap for GH1's range. 3 to 5 days.
- **(GD01, Enhancement) GH2 colour transform + luminance curve** (our own
  curve). 2 to 3 days.
- **(GD01, Enhancement) GH2-style depth of field** (smooth 7-tap,
  focus/blur_depth/max_blur from GH2's defaults) as a DOF option. 3 to 5 days.
- **(GD01, Enhancement) GPU flare occlusion test** (no `glReadPixels`
  stall). 2 to 3 days.
- **(GD01, Enhancement) Crowd card resolution** (128×256 → 256×512+, scaled
  with the render scale). 2 to 4 days.
- **(GD01, Enhancement) Soft spotlight beams** (depth-faded intersections,
  end-on fade). 3 to 5 days.
- **(GD01, Enhancement) Per-pixel lighting** (diffuse only). 1 to 1.5 weeks.
- **(GD01, Enhancement) "Look: GH2" preset** in Enhanced Settings (switches
  the above on together). 1 day after the items.
- **(GD02 then GD01, Original re-implementation then Enhancement) Shadow-map
  pass**: gd02 first (GH2's SHADOW_BUFFER), then offer it to gd01. 2 to 3
  weeks + 1 week.
