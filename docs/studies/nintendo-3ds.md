# Nintendo 3DS port

Status: study, 2026-10-28.

## Verdict

**Not viable as a port of gdul, gd02 or gd01. At most it could be a
separate, much reduced "gd01-lite" homebrew experiment, and even that is
months of work.** I don't recommend it.

## The hardware against our games

| | Old 3DS | New 3DS | gd01 needs (GH1, PS2) | gd02 needs (GH2, X360) |
|---|---|---|---|---|
| CPU | 2x ARM11 at 268 MHz | 4x ARM11 at 804 MHz | The PS2 EE ran at 294 MHz, plus a VU and IOP | 3x PPC at 3.2 GHz |
| RAM for apps | about 64 MB (of 128 MB FCRAM) | about 124 to 178 MB (of 256 MB) | 32 MB on the PS2, but our engine is not frugal | the X360 had 512 MB |
| GPU | PICA200: fixed-function fragment "TEV" combiners, programmable vertex and geometry shaders, **no fragment shaders, no OpenGL** | same | GL 3.3 shaders, post-FX, DLSS/Vulkan | GL 3.3/GLES3, GH2's standard shader, 10-bit HDR buffer, bloom, DOF |
| Screen | 400x240 (top, 3D) plus 320x240 | same | | |
| Disc | an `.iso` on a FAT32 SD card: **4 GB file limit** | same | 2.2 GB ISO, fits | **7.3 GB ISO, does not fit** (it would need splitting or extracting) |

- **gd02 is out.** The ISO size, the memory and the shading model all rule it out.
- **gd01 is the only candidate.** PS2-era assets are within reach of the New 3DS.
  But the renderer would have to be **rewritten for citro3d**: TEV combiners
  instead of GLSL, textures converted to PICA tiled formats at load time, no
  post-FX, the highway and venues simplified. Audio and video work would also be
  needed: decoding GH1's streams on an 804 MHz ARM11, and MPEG-2 FMV is not
  realistic (drop it). The Old 3DS should be treated as unsupported.
- **The launcher model does not apply.** There is no `dlopen` of game modules
  (3DSX/CIA homebrew is one static ELF), no SDL3 with OpenGL (SDL2 has a
  community 3DS port with software rendering only), and the stack is C++17 on
  devkitARM.
- **Input.** Guitars are effectively out. USB is not available, and Bluetooth is
  not open to homebrew. The game would be played on buttons or the touch screen,
  which is not "match the original".

## Effort

About 3 to 6 person-months for a playable but heavily cut-down gd01 on the New
3DS: a new renderer, an asset pipeline, a memory diet and an audio path. It would
share little code with the main engine (only the chart, timing and data parsers).
After that it is a permanent second renderer to keep up to date.

## Tooling, legal, distribution

- Toolchain: devkitPro **devkitARM + libctru + citro3d/citro2d** (free).
  Releases can be built in the `devkitpro/devkitarm` Docker image, which runs in
  Actions.
- Running it needs a console with custom firmware (Luma3DS) and the Homebrew
  Launcher. Nintendo has no developer program for new 3DS titles, and the eShop
  closed in March 2023. **This can only be distributed as homebrew** (a
  3DSX on GitHub, or Universal-Updater).
- Legal: the engine and homebrew SDK are fine (no Nintendo SDK). Players need a
  hacked console and their own disc dumps. This fits our "bring your own disc"
  rules, but makes the audience tiny.

## If it is ever tried

1. Build a spike: chart plus highway only (no venue), on the New 3DS, from an
   extracted GH1 song.
2. Measure FPS and memory before committing to anything more.
3. Keep it in its own `gd-game-01-3ds/` folder, outside the launcher.

## Sources

- 3DS hardware: https://www.3dbrew.org/wiki/Hardware , https://www.3dbrew.org/wiki/Memory_layout
- PICA200 / GPU: https://www.3dbrew.org/wiki/GPU
- devkitPro / libctru / citro3d: https://devkitpro.org/wiki/Getting_Started , https://github.com/devkitPro/citro3d
- eShop closure: https://en-americas-support.nintendo.com/app/answers/detail/a_id/63227
