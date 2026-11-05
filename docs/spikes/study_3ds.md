# Nintendo 3DS (homebrew)

Status: study, 2026-10-28. It merges the first study on this topic
(written earlier the same day).

## Verdict

**Not viable for gdul or gd02. For gd01, only a heavily reduced "gd01-lite"
homebrew on the New 3DS is possible, and it would take 3 to 6 person-months
with a renderer written from scratch. Not recommended.** If the owner wants
a handheld, the PS Vita (`study_ps_vita.md`) gives more for less work. The
3DS has no OpenGL-class GPU, and SDL3's 3DS port renders in software only.

## The hardware against our games

| | Old 3DS / 2DS | New 3DS / New 2DS XL | gd01 (GH1, PS2) | gd02 (GH2, X360) |
|---|---|---|---|---|
| CPU | 2x ARM11 at 268 MHz | 4x ARM11 at 804 MHz, L2 cache | the PS2 EE ran at 294 MHz, plus VU0/VU1 and the IOP | 3x PPC at 3.2 GHz |
| RAM for apps | about 64 MB (of 128 MB FCRAM) | about 124 to 178 MB (of 256 MB) | the PS2 had 32 MB, but our engine is not frugal | the X360 had 512 MB |
| GPU | PICA200: fixed-function "TEV" combiners and programmable vertex/geometry shaders. **No fragment shaders, no OpenGL.** | same, plus 6 MB VRAM | GL 3.3 shaders, post-FX | GLES3, 587 programs, 10-bit HDR buffer, bloom, DOF |
| Screen | 400x240 (top, stereo 3D) + 320x240 touch | same | | |
| Storage | SD card, FAT32: **4 GB per file** | same | 2.2 GB ISO, fits | **7.3 GB ISO, does not fit** |

- **gd02 is out**: the ISO size, the memory and the shading model all rule it out.
- **gd01 is the only candidate, on the New 3DS only.** The renderer would have
  to be **rewritten for citro3d**: TEV combiner setups instead of GLSL,
  textures converted to the PICA tiled formats (ETC1/RGBA4/RGB565) at load
  time or offline, no post-FX, and a simplified venue. GH1's multi-pass
  materials (`gd-game-01/docs/material_stages.md`) map reasonably well onto
  TEV stages. The VU1 lighting (`lighting.md`) maps onto a PICA vertex
  shader, since the hardware also has per-vertex/fragment lighting LUTs.
- Audio: GH1's streams decoded on an 804 MHz ARM11 plus NDSP mixing works.
  **MPEG-2 FMV is not realistic: drop it.**
- **No launcher model**: a 3DSX/CIA is one static ELF, with no `dlopen`.
  **SDL3 has an official 3DS port, but it is software rendering only**, with
  cooperative single-core threading. That is fine for the window, input and
  audio, but not for drawing.
- **Input**: no guitars (no USB host, and Bluetooth is not open to
  homebrew). Play uses buttons or touch, which is not "match the original".

## How we would build and launch it

- **Toolchain**: devkitPro **devkitARM + libctru + citro3d/citro2d**,
  installed with devkitPro's `dkp-pacman -S 3ds-dev`. It is C++17-capable
  GCC (newlib). The CMake toolchain file is `$DEVKITPRO/cmake/3DS.cmake`.
  Releases build in the `devkitpro/devkitarm` Docker image (one release job).
- **Shaders**: PICA vertex shaders are written in assembly (`.v.pica`) and
  assembled with `picasso` at build time. Fragment work is TEV
  configuration in C.
- **Outputs**:
  - `.3dsx` (with an `.smdh` icon, built by `3dsxtool`/`smdhtool`), launched
    from the **Homebrew Launcher**. On Luma3DS consoles, open it from the
    Rosalina menu (L+Down+Select) → *Miscellaneous* → *Switch the hb. title
    to the current app*, or from the hbmenu CIA forwarder.
  - `.cia` (built with `makerom` + `bannertool`), installed to the HOME Menu
    with **FBI**. A CIA gets more memory modes and a HOME icon.
- **Developing**: `3dslink app.3dsx` sends a build over Wi-Fi to hbmenu
  (press Y). `romfs:/` carries our own assets. The ISO lives at
  `sdmc:/gd01/` (FAT32 is fine, the file is 2.2 GB).
- **Emulator**: Azahar (the successor to Citra and Lime3DS) for fast
  iteration. Measure performance on real New 3DS hardware.

## Effort

About 3 to 6 person-months for a playable but cut-down gd01: a new citro3d
renderer, an asset conversion pipeline (tiled/ETC1 textures, a vertex
budget), a memory diet, NDSP audio, and the input. It shares only the chart,
timing, scoring, script and data parsers with the main engine. After that it
is a permanent second renderer to keep up to date.

## Distribution, legal, audience

- Homebrew only: Luma3DS custom firmware (installed through the
  3ds.hacks.guide methods) plus the Homebrew Launcher. The eShop closed in
  March 2023, and Nintendo has no program for new 3DS titles.
- Publish a `.3dsx`/`.cia` on GitHub Releases, plus a **Universal-DB** entry
  (installs through Universal-Updater).
- Legal: no Nintendo SDK is used, and users bring their own disc. That is
  fine, but the audience is tiny: a hacked New 3DS, a GH1 PS2 disc, and
  willingness to play on buttons.

## If it is ever tried

1. Spike (2 weeks): citro3d chart and highway only (no venue), from one
   extracted GH1 song, on a New 3DS. Measure fps and memory.
2. Gate: a steady 60 fps highway and less than 64 MB in use. Otherwise stop.
3. Keep it in its own `gd-game-01-3ds/` folder, outside the launcher.

## Sources

- 3DS hardware and memory: https://www.3dbrew.org/wiki/Hardware , https://www.3dbrew.org/wiki/Memory_layout
- PICA200 GPU: https://www.3dbrew.org/wiki/GPU
- devkitPro getting started / citro3d: https://devkitpro.org/wiki/Getting_Started , https://github.com/devkitPro/citro3d
- SDL3 3DS port (software rendering only): https://wiki.libsdl.org/SDL3/README-n3ds
- Homebrew libraries list (old GL wrappers, unmaintained): https://www.3dbrew.org/wiki/Homebrew_Libraries_and_Tools
- Luma3DS / Homebrew Launcher guide: https://3ds.hacks.guide/
- Universal-Updater: https://github.com/Universal-Team/Universal-Updater
- Azahar emulator: https://azahar-emu.org/
- eShop closure: https://en-americas-support.nintendo.com/app/answers/detail/a_id/63227

## Backlog candidates

- **(GD01, Enhancement, low priority) 3DS feasibility spike**: a citro3d
  highway on the New 3DS with fps and memory gates. 2 weeks. Do it only if
  the owner explicitly wants a 3DS build after the Vita.
- No other items. A full port (3 to 6 months) should not enter the backlog
  unless the spike passes.
