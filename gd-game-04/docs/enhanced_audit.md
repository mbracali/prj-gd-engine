# Enhanced options, shadows and performance audit (2026-11-13)

The owner's bug list 2026-11-13 (`docs/tasks/BUGLIST_20261113.md`, items
06, 07 and 09; backlog GD04-118, GD04-119, GD04-121): "the Enhanced options
seem not to apply (HDR, resolution, frame lock)", "the band's self-shadows
are grey lines", "gameplay under 60 FPS on Windows (RTX 4060 Mobile, 1080p
borderless, 60 FPS lock); menus fine". Measured on Linux (the same build
as Windows, MinGW aside); the Windows numbers are the owner's to confirm
with `GD04_PERF=1` in `gd04.log`.

## 1. Every Enhanced option (GD04-118)

Each option's value is read in one place: `applyEnhancedGlobals` (app.cpp:
the picture chain, the venue extras, the rules' switches), `App::applyEnhanced`
(window, input) or where it is used (the frame cap, the overlay, the song
setup). Both run at once on every change, so every option takes effect from
the next frame unless its help says otherwise.

`--enhancedtest` (new) draws z_artdeco at 12 s (a wide shot of the band on
the stage) with the defaults, then with each picture option at another
value, applied through `applyEnhancedGlobals` as the game does, and checks
that the frame changes by more than drawing the defaults twice does (0.000:
the frames are identical), or the scene's size for the resolution rows.

| Option | Works? | Evidence | Fix |
|---|---|---|---|
| Graphics Backend / Audio Backend | Yes, at the next start (the help says so; Restart Now does it) | walk `enhanced_backends` (+ `_restarted`) | — |
| Screen Mode | Yes, at once | `SDL_SetWindowFullscreen(Mode)`; now logged ("screen mode: ...") for the owner's log | log line added |
| Resolution (`internal_res`) | Yes, at once, **for the venue and the band only** (the highway, HUD and menus are always drawn at the window's resolution). At 1080p borderless, "Window" and "1080p" are the same picture | `--enhancedtest`: scene 720 -> 1080 / 1440 rows | — (worth telling the owner: it is not the window's resolution; Screen Mode sets that) |
| Sharper Text & Sprites | From the next start (the help says so: the textures load at boot) | `TextureCache::sharp` | — |
| Anti-Aliasing FXAA / SSAA / TAA | Yes | `--enhancedtest`: passes fxaa / downsample (scene 1440 rows) / taa, 1.2-3.1 % of pixels change | — |
| AI Upscaling FSR 1, Upscaling Quality | Yes | scene 480 rows (Quality 1.5x) / 360 rows (Performance 2x), passes easu rcas | — |
| Max FPS | **No** before: the device was given 60 once at start (`setFrameCap(60)`); the option only added a wait in the loop, so 120 / 144 / 240 / Unlimited never went above the display's 60 Hz vsync, a 60 cap on a faster display stayed 60, and the loop's wait sat on top of the vsync | Real window (Linux, 60 Hz display), slowride in z_artdeco, fps measured (GD04_STATS): old build 59.5 / **15 -> 1.1** / **1.1 -> 0.5** for 60 / 120 / Unlimited; new build 60.0 / 119.9 / 109 | The song's cap goes to the device (`setFrameCap`: vsync where the display's rate is a multiple, its own clock otherwise; 0 = Unlimited, vsync off); the menus keep 60; the loop's own wait only headless. The collapse at 120 / Unlimited was the z_artdeco script loop below (more steps a frame, each costing more) |
| Show FPS Counter | Yes | `App::overlay` | — |
| HDR | **No** before: the row was greyed ("not in gd04 yet") and the value never read, although the chain had the HDR passes | `--enhancedtest`: Emulated changes 85 % of the pixels, passes lum x4, adapt, composite | Emulated now runs the chain's auto exposure and ACES tone map. **True HDR stays unavailable** (no HDR window surface in gd04 yet: gd03's `render/hdr_output` would have to be ported); its choice is shown greyed; a saved "true" plays as Emulated |
| Bokeh | Yes | None: 23 % of pixels change; Stronger: 0.8 % (only the out-of-focus parts) | — |
| Enable Particles FX | None: yes (from the next song, as its help says). **2x: no** before (only on / off was read) | `--enhancedtest` (z_hell, a 2-minute song sampled every 250 ms): 133 677 particles with Original, 0 with None, 266 585 with 2x | `Particles::density`: 2x doubles each emitter's burst and rate |
| Bloom None / Original / 2x / 3x | Yes | None 2.1 %, 3x 0.35 % (pyramid passes) | — |
| Cinematic Color, Color Grading, Film Grain, Chromatic Aberration, Ambient Occlusion, Sharpening | Yes | 64 %, 51 % (sepia), 23 %, 23 %, 36 %, 11 % of pixels; passes composite / finish / ao | — |
| Blur on Star Power | Yes (only while Star Power is on) | passes starblur, 0.9 % | — |
| All Band Mates Cast Shadows | **No** before: nothing drawn visibly | 0.000 before; 11.6 % of pixels after | The extras (gd02's EnhancedLights) were projected with Direct3D's depth range (0..1) on OpenGL: everything fell behind the floor. Converted to OpenGL's in `VenueStage::draw`'s lightsView. Also fixes More Lights' floor pools and the Tesla Star Power bolts' depth |
| More Lights on Venues | Partly before (beams only; the floor pools were hidden, same cause) | 33 % of pixels after (21 % before) | same fix |
| Dust in Light Beams | **Barely** before (motes nearly all behind the venue's depth) | 0.04 % of pixels (motes of about 1 cm; a few hundred pixels) | same fix; the motes are small by design (gd01's) |
| Disable Shaky Camera | Yes | camera rotation off vs on: mean 0.0058 a frame over 2 s | — |
| Remove All Effects | Yes | 51 % of pixels | — |
| Mouse, Touch, Gamepads | Yes | walks `mouse`, `mouseall`, `touch`, `gamepads`, `keyboard` | — |
| Shake for Whammy | Android only (accelerometer) | not testable on Linux | — |
| Audio / Video Calibration, offsets | Yes | walk `mouse` (set / reset), song setup reads them | — |
| Easy Chords | Yes | `--selftest` (easy chords on every chart) | — |
| No Fail, Autoplay, Forgiving HOPOs | Yes | walks, `--selftest` | — |
| Developer Debug Menu | Yes | walk `dev_debug` | — |
| Enable Cut Content | Removed by the owner (off only) | walk `cutcontent` | — |

## 2. The band's self-shadows (GD04-119)

**Cause.** GH3 biases each shadow caster's depth by GetShadowMapBias
(8291E644): single_sided 64 / 1280 and double_sided 1200 / 1600 x 2^-24
for cascades 0 / 1 [C]. gd04 applied the single-sided 64 to every caster.
The band's meshes draw into the shadow map cull none (both faces: a
double-sided caster), so they need the double-sided values [I: GH3's
choice by the caster's cull state]. With 64 x 2^-24 on cascade 0, most of
a member's lit surface changes depth by more than the bias within one
texel of the 1024 x 1024 map, and the receiver's four taps half a texel
around the pixel find the surface itself in front: stripes of shadow
("acne"), drawn grey (the shadow is 0.35 + 0.65 x lit).

**Fix.** `VenueStage::shadowPass` adds the bias to the casters' depth, as
GH3 does (z + b w), double-sided for the band; the receivers compare
unbiased.

**Check.** `BandView::AcneProbe` (`--venuetest`, and `GD04_SHADOW_LOG=1`
in a song): over the band's triangles in cascade 0, the share of their
texels whose depth changes by more than the caster's bias across one
texel; `--venuetest` fails above 10 %.

| Venue | Acne-prone before (64) | After (1200) |
|---|---|---|
| z_artdeco | 79.2 % | 0.5 % |
| z_budokan | 81.8 % | 1.1 % |
| z_dive | 79.8 % | 0.5 % |
| z_hell | 79.4 % | 1.1 % |
| z_party | 70.1 % | 0.8 % |
| z_prison | 85.5 % | 0.6 % |
| z_video | 71.8 % | 0.9 % |
| z_wikker | 80.7 % | 0.6 % |

On Linux the stripes were hard to see in our captures (the venues' shadow
light is mostly behind the band, so the striped side faces away from the
camera); the measure above is independent of the camera. Unverified on
the owner's Windows machine.

## 3. Performance (GD04-121)

`GD04_PERF=1` had no output (its logger was gone); now it logs every 2 s
the frames a second, the frame's CPU time and the time in each part of a
song's frame, and the device's draws, uploads, present and GPU wait.
`GD04_PERF=2` also waits for the GPU at each part's ends (each part's GPU
cost; slower overall). Script loops that run out of the VM's instruction
budget are logged once with `GD04_UILOG=1`.

**The top cost: a script loop that never waited.** z_artdeco's dragon and
two knights (TRG_Geo_Dragon / Knight1 / Knight2's node scripts:
`begin / if LightShow_GetParams / switch <performance> ... repeat`) call
LightShow_GetParams, which gd04 did not have: the call returned true with no
`<performance>`, no case matched, and the loop ran the VM's whole budget
(100 000 instructions) every 60 Hz step: 7-7.5 ms a frame on this machine.
Worse, it grows with the frame time (more steps a frame), so a frame cap
above 60 collapsed to 1-15 fps. LightShow_GetParams now returns the
performance (poor / medium / good from the rock meter's bands, as the light
show's). No other venue has such a loop (`--venuetest` with GD04_UILOG).

Slow Ride in z_artdeco, Expert, autoplay, 1920 x 1080, Max FPS Unlimited,
headless (Linux, Ryzen laptop CPU, NVIDIA RTX 3060 Laptop):

| | Before | After |
|---|---|---|
| fps (GD04_STATS, 2 runs each, alternated) | 79 / 112 | 140 / 163 |
| CPU ms a frame: tick (rules + venue scripts) | 5.3-7.5 | 0.00-0.03 |
| venue (all 3D drawing, CPU) | 3.6-4.0 | 3.6 |
| of it: shadow cascades (CPU skinning of the band) | 1.3-1.6 | 1.3 |
| frame (CPU, to the present) | 9.5-13.7 | 5.8 |
| real window, 60 Hz display, Max FPS 60 / 120 / Unlimited | 59.5 / 15 -> 1.1 / ~1 | 60.0 / 119.9 / 109 |

GPU cost of each part (GD04_PERF=2, ms a frame): venue meshes 2.45, shadow
2.29, peds 1.19, crowd 0.83, band 0.64, screen effects 0.57, highway + HUD
0.51, lights 0.29. Per frame: ~520 draws, ~1.2 MB of uniforms and ~6 MB of
vertex uploads (the band's CPU skinning, every frame).

Not changed (worth doing later, not clearly wasteful): the band's CPU
skinning and its 6 MB upload a frame (GPU skinning would remove both);
the two shadow cascades redraw the band (GH3 does the same).

## Unverified on Windows

- The frame rate on the owner's machine (expected: z_artdeco no longer
  below 60; other venues: send `GD04_PERF=1` logs).
- The stripes' disappearance (measured by the probe, not seen on Windows).
- Max FPS with the owner's display (the TV at 60 Hz: 60 = vsync; 120+ runs
  unsynced, paced by the clock).
