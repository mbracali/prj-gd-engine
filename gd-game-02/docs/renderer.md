# gd02 renderer: what GH2 does, and how gd02 does it

gd02's renderer (`engine/src/render/`) is written new from Guitar Hero II's
own rendering. OpenGL 3.3 core / GLES 3.0 first, behind a device interface
(`device.h`) so a Vulkan back end can be added later. Addresses are in
`default.xex` (see `ghidra/analysis/`); **[C]** confirmed in code, **[I]** inferred.
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

Postprocess shaders reuse the low bits for their own switches (COLORXFM 1,
NOISE 2, DOF 3, BLOOM 4, BLENDPREVIOUS 5, COPYPREVIOUS 6, NUM_TAPS at 14–17,
FOG 18, LUMMAP 20; define builder `0x8268d898`). gd02 builds the same keys
(`standard_shader.*`, `post_fx.*`), so `standard_<hex>` / `postprocess_<hex>`
in gd02 name the same variant as the disc's file. `--shadertest` compiles
all 585 programs gd02 builds (the 2D program, 545 standard variants, the
video program and 38 post-process programs).

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
- cull culls clockwise faces in D3D's screen space; in GL (Y flipped) that
  is counter-clockwise (see `highway_and_hud.md`).
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

## Post-process (DX DoPost `0x82303e68`) [C]

`0x823055e8` ends each frame: resolve the HDR buffer's colour (cleared with
the clear colour / 4) and depth to textures, then DoPost, then present.
Everything (world, track, HUD) is drawn into one 2:10:10:10 buffer with
colours / 4 (the HDR key bit). DoPost, with the selected RndPostProc
(`0x821ea108`: the shot's, else the Rnd's "[default postproc]"):

1. DOF, when the camera set it (rnd +0x1dc): the buffer unpacked (× 4) and
   box-filtered (downsample_collapse, 4 taps ± ½ texel, alpha 0) to a
   640×320 buffer, then blur.fx twice (`0x82302308`): 7 taps (`0x827469f4`)
   weighted 1/7 at {−3.5, −2.5, −1.5, 0, 1.5, 2.5, 3.5} × 0.666
   (`0x827469f0`) texels, across then down.
2. Bloom, when bloom_intensity > 0: bloom.fx at full size into an 8-bit
   buffer (point sampling; white = smoothstep(threshold.w, 4, luminance),
   rgb = saturate((c − threshold.rgb) × 1024) − white), two 2× box
   downsamples, then a 15-tap Gaussian twice (`0x82303a60`), down then
   across at a quarter of the size: weight(i) = kernel × exp(−i²/2σ²) /
   √(2πσ²), σ = bloom_scale, i = 0, ±1..±7 texels.
3. postprocess.fx full screen: unpack; for pixels deeper than the UI plane
   (0.1, set every frame): DOF lerp to the low-resolution blur by
   clamp(|z·a + b|, 0, max_blur), bloom added (rgb + white) × intensity,
   noise, colour transform (rows red/green/blue select + bias), the
   luminance map (rgb × curve(luminance / 4), the curve divided by the
   luminance above 1). Program by shader type `0x823073f8`: DOF + BLOOM
   from the passes run, NOISE when noise_intensity ≠ 0, LUMMAP with a
   luminance texture, COLORXFM whenever a PostProc is selected,
   BLENDPREVIOUS when threshold < 1 and duration > 0, COPYPREVIOUS +0xfd.

RndPostProc v9 (Load `0x821eb3d0`, SyncProperty `0x821eb598`, constructor
`0x821eafb8`): bloom thresholds r/g/b/white +0x28 (default 4 4 4 4), kernel
multiplier +0x38 (1), bloom scale +0x3c (1), intensity +0x40 (0, so no
bloom), luminance_map +0x44, hue/saturation/lightness/contrast/brightness
+0x50..+0x60 (editor inputs to the transform), red/green/blue select
+0x64/+0x74/+0x84 and biases +0x94..+0x9c (identity), flicker intensity
+0xa4 / secs_range +0xac (`0x821ea128`), noise_base_scale +0xc8 (32, 24),
+0xd0 (1.3591, the second noise layer's scale), noise_intensity +0xd4,
noise_map +0xd8, threshold +0xe4 (1), duration +0xe8, emulate_fps +0x100.
All 28 shipped PostProcs: thresholds 3.9 3.9 3.9 1, scale 3, intensity 1,
kernel 2.5, luminance map arena_luminancetest04.tex (512×8: a curve dipping
to ~0.55 around luminance 1.5; none in the _mp milos), no noise, no
trails, no flicker. `rnd reset_postproc` (`0x821e5088`) selects the default.

Depth of field (Rnd::SetDOF, DX `0x82302728`), from the CamShot keyframes
(`0x82227440`): focus = |camera − focal target| × (1 + focus_blur_multiplier),
blur_depth and max_blur lerped between keyframes; on when max_blur > 0;
blur_depth ≥ 0.001; zf = depth(focus), zb = depth(focus × (1 − blur_depth))
with depth(d) = (far − far·near/d) / (far − near) (0 nearer than near); the
constants are (1/(zf − zb), −zf/(zf − zb), 0, max_blur). Every CamShot has
use_depth_of_field and a focal target (guitarist0); most keyframes have
blur_depth 0.5, max_blur 0.5.

gd02 (`render/post_fx.*`, `Renderer::post()`): the world goes into a
RGB10A2 target (RGBA8 fallback) with a depth texture; DOF buffers at half
size (blur offsets kept in GH2's 640×320 uv units), the bright pass straight
to half size (the 2×2 average of the full-size results, the same numbers),
the Gaussian at a quarter; the final pass into the picture. The track and
HUD are drawn afterwards over it [I: same picture, except that GH2's bright
gems also feed the world's bloom]. Not built: FOG (depth-volume fog),
BLENDPREVIOUS / COPYPREVIOUS and the flicker (unused by the data). The DOF /
bloom buffers' 8-bit format is [I] (`0x821d5e30` with the screen's bpp).
`GD02_POST=0` turns it off; `GD02_POST_DEBUG=bloom|dof|lum|doflow` shows the
bloom buffer, the DOF blend, the luminance factor (× ½) or the DOF buffer;
`--view` takes `GD02_VIEW_POST=<milo>[:<PostProc>]` and
`GD02_VIEW_DOF=<focus>,<blur_depth>,<max_blur>`.

## Performance (GD02_PERF) and macOS

`GD02_PERF=1` logs, every 2 s, the average frame split into: **update**
(input, the song, world, band and menu scripts: what is left of the frame),
**draw** (from `Canvas::begin` to the present: building and issuing the
draws on the CPU, with draws, uniform / vertex / texture KB and GPU objects
created per frame), **present** (the internal frame's blit, the frame cap's
sleep, shown as *pacing*, and `SDL_GL_SwapWindow`) and **gpu wait** (the fence
on the frame before last: high means the GPU is the bottleneck), plus the
window's and the drawn frame's pixel sizes and the display scale. On this
Linux machine (RTX 3060, 1280x720, yyz in a venue): 60 fps, update 0.3 ms,
draw 2.7 ms for ~625 draws and ~270 KB of uniforms, gpu wait 0.01 ms.

Apple's OpenGL runs on Metal. `glBufferSubData` into a buffer the GPU may
still read makes that driver wait or copy, and the device streamed every
draw's uniforms that way (two writes a draw into one 12 MB buffer). On macOS
those writes (and the vertices the canvas appends) now go through
`glMapBufferRange(... WRITE | INVALIDATE_RANGE | UNSYNCHRONIZED)`, safe because
the uniform regions rotate behind the two-frame fence and appended vertices
were never drawn from. `GD02_GL_STREAM=map | subdata` picks either path
anywhere; both draw identical frames here (subdata is faster on NVIDIA).

To measure on the Mac (M4 Pro, ~20 fps reported):
1. `GD02_PERF=1 GD02_LOG=perf.txt ./gd02.app/Contents/MacOS/gd02 <iso>` in a song, and in the menus.
2. The same with `GD02_GL_STREAM=subdata` (the old path): if *draw* falls a lot
   with `map`, uniform streaming was the cause.
3. A high *gpu wait*: fill rate; try Enhanced Options' internal resolution
   (the Retina window is drawn at its pixel size, e.g. 3456x2234, with the
   whole post-process chain) and `GD02_POST=0`.
4. A high *present* with little *pacing*: the swap waits for the display or
   the compositor (vsync / ProMotion); compare a windowed and a full-screen run.

## Open / inferred

- Tangents are generated at load [I]. Environment maps: a Mat's
  environ_map names a CubeTex (v1, six inline faces, docs/venues.md),
  uploaded as a cube texture (SceneCache::cubeTexture) in Direct3D's face
  order, which OpenGL samples the same way.
- alpha_cut threshold 0.5 [I]; blend 7 as premultiplied alpha [I]; light
  types 0 point / 1 directional, shining along +y [I].
- Shadow-map pass: not done yet. Post-process: the noise map is our own
  white noise (GH2's rnd +0x29c is not on the disc) [I]; the shader type →
  effect mapping (4 downsample, 5 downsample_collapse, 8 blur, 9 bloom, 10
  postprocess) is [I] from the call sites.
