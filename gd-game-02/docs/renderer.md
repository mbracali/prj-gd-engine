# gd02 renderer: what GH2 does, and how gd02 does it

gd02's renderer (`engine/src/render/`) is written new from Guitar Hero II's
own rendering. OpenGL 3.3 core / GLES 3.0 first, behind a device interface
(`device.h`) so a Vulkan back end can be added later. Addresses are in
`default.xex` (see `analysis/`); **[C]** confirmed in code, **[I]** inferred.
The game's shader sources ship on the disc as HLSL (`system/run/shaders/*.fx`);
they are game data and stay out of the repo. gd02's GLSL is our own,
computing the same results.

## Shader permutations [C]

Compiled shaders on the disc are named `<effect>_<hex key>_vs|ps4314`
(format `%s/gen/%s_%x_%s%d.%s_%s`). The key's bits switch features of the
"standard" shader (define builder `0x8268d898`), lowest bit first:

| Bit(s) | Switch | Bit(s) | Switch |
|---|---|---|---|
| 0 | PER_PIXEL_LIGHTING | 12–13 | NUM_WEIGHTS |
| 1 | SPECULAR_MAP | 14–16 | NUM_LIGHTS |
| 2 | SPECULAR | 17 | DIR_LIGHTS |
| 3 | ENVIRON_MAP | 18 | FOG |
| 4 | DIFFUSE_MAP | 19 | SHADOW_BUFFER |
| 5 | NORMAL_MAP | 21 | COST_ESTIMATE |
| 6 | NORMALIZE | 22 | HDR |
| 7 | GLOW_MAP | 23 | EXTRUDE |
| 8 | PRELIT | 25 | BILLBOARD |
| 9 | CUSTOM_DIFFUSE | 26–27 | FADE_OUT |
| 10–11 | TEX_GEN | 28–29 | PROJ_LIGHTS |
| | | 30 | USE_ENVIRON |

Postprocess shaders reuse the low bits for their own switches (COLORXFM,
NOISE, DOF, BLOOM, BLENDPREVIOUS, COPYPREVIOUS, NUM_TAPS at 14–17, LUMMAP at
20). gd02 builds the same keys (`standard_shader.*`), so `standard_<hex>`
in gd02 names the same variant as the disc's file. `--shadertest` compiles
all 546 programs gd02 builds.

## How a Mat picks its key — `0x8268d280` (from shader selection `0x82308098`) [C]

Mat fields (offsets from SyncProperty `0x821ed930`): intensify 0x28, blend
0x2c, color 0x30, alpha 0x3c, use_environ 0x40, z_mode 0x44, stencil_mode
0x48, tex_gen 0x4c, tex_wrap 0x50, tex_xfm 0x54, diffuse_tex 0x94, prelit
0xa0, alpha_cut 0xa1, alpha_write 0xa2, next_pass 0xa4, cull 0xb0,
per_pixel_lit 0xb1, normalize 0xb2, emissive_multiplier 0xb4, specular_rgb
0xb8, specular_power 0xc4, normal_map 0xc8, emissive_map 0xd4, specular_map
0xe0, custom_diffuse_map 0xec, environ_map 0xf8.

- DIFFUSE_MAP when there is a diffuse texture; PRELIT when prelit (or for
  particles); USE_ENVIRON when use_environ in a lit pass.
- Lit passes only: normal map, custom diffuse and specular map need
  per_pixel_lit; SPECULAR from a specular map or a non-zero specular_rgb;
  GLOW from the emissive map; ENVIRON_MAP from environ_map; NORMALIZE needs
  the normal map; tex_gen 2/3/5 → TEX_GEN 1/2/3 (normal / world position /
  reflection).
- NUM_WEIGHTS = bones − 1. SHADOW_BUFFER needs per-pixel lighting and the
  global shadow switch. Lights count only for use_environ materials (Environ
  +0x78 count, +0x7c projected count, +0x80 directional flag).
- FOG needs Environ+0x4c and a blend of src, src_alpha, multiply or 7+.
- FADE_OUT needs Environ+0x68 and fade_end ≠ fade_start: src / src_alpha →
  mode 1 (alpha, fade colour 0); add / src_alpha_add / subtract → mode 2
  (fade colour 0); multiply → mode 2 with fade colour 1.
- HDR when drawing into the HDR buffer and the blend is not multiply.

## Render state (Mat setup around `0x8230a5d0`) [C]

- Blend: dest = zero/one; src = opaque (src_alpha while the scene fades
  out); add = one/one; src_alpha = src-alpha / inverse-src-alpha;
  src_alpha_add = src-alpha / one; subtract = one/one reverse-subtract;
  multiply = zero/src-colour, or dest-colour/src-colour (2×) with intensify.
- z_mode: 0 off; 1 LESS + write; 2 LEQUAL, no write; 3 ALWAYS + write;
  4 LEQUAL + write.
- cull culls clockwise faces (D3D's screen winding).
- tex_wrap applies to sampler slots 0–3; the environment map (slot 4) and
  custom diffuse (slot 11) are clamped.
- Constants: multipliers = (emissive_multiplier, intensify + 1); default
  ambient (c1) white; vertex and pixel shaders have separate constant banks.

## Lighting model (from `lighting.fx` / `standard_*.fx`, in our words)

- Ambient = base × ambient: base is the vertex colour (prelit) or the
  material colour; ambient is the Environ's (use_environ) or white.
- Directional lights add N·L plus Phong (R·V)^power, the highlight only from
  the first 4 lights. Point lights scale by 1 − clamp(distance / range).
- The light sum is tinted by the material colour; per-vertex lighting tints
  a second time (kept as in the game).
- An environment map replaces the light specular: a cube-map lookup with mip
  bias = specular power, × specular colour.
- Projected (spot) lights: directional lights masked by a projected texture
  × (1 − the shadow texture's alpha), always per pixel.
- Output = emissive × multiplier + lit × diffuse × intensify + specular, then
  fog, then fade; HDR divides by 4.
- BC5 normal maps: x from the second channel, y from the first,
  z ≈ clamp(1.35 − |x| − |y|).

## gd02's implementation

- `device.h`: handles for textures (RGBA8, BC1, BC3, BC5; 2D and cube),
  buffers, programs, render targets; blend / depth / cull states; std140
  uniform blocks at fixed bindings; frames and `readPixels`.
- `gl_device.cpp` / `gl_api.*`: the GL back end (functions loaded through
  SDL; forward-compatible core context for macOS). S3TC / RGTC when
  available, else CPU decode (`texture_codec.*`, `GD02_DECODE_TEXTURES=1`
  forces it). Uniform ring buffer. Pacing (gd01 lesson L7): fences keep ≤ 2
  frames in flight; vsync when the display matches the cap, else software
  pacing; `GD02_FPS` sets the cap (60).
- `standard_shader.*`: the GLSL standard shader and its permutation cache;
  a failing variant falls back to simpler ones (never a black screen;
  `GD02_SHADER_FAIL=<bits>` tests it).
- `material.*`: `Material` (Mat v28), `LightEnv` (Environ + Lights),
  `standardKey()` / `materialPipeline()` (the rules above), `MaterialRenderer`
  (Mesh v28 48-byte vertices, tangents generated, up to 4 bones).
- `camera.h`: Cam (yFov in radians, near/far); GH2's axes (x right, y
  forward, z up) map to GL eye space as (x, y, z) → (x, z, −y).
- `canvas.*`: 2D batcher at 1280×720 logical, letterboxed.
- `--shadertest`, `--view <milo>` (scene viewer, `GD02_VIEW_CAM`).

## Open / inferred

- Tangents are generated at load [I]; CubeTex is not decoded yet (no
  environment maps on screen yet).
- alpha_cut threshold 0.5 [I]; blend 7 as premultiplied alpha [I]; light
  types 0 point / 1 directional, shining along +y [I].
- Shadow-map, particles, postprocess (bloom, DOF) passes: not done yet.
