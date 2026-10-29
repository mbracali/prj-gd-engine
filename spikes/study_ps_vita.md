# PS Vita (homebrew)

Status: study, 2026-10-28.

## Verdict

**Possible for gd01 only, as homebrew. It is the most realistic of the
handheld targets, but it is still a separate port of about 2 to 3
person-months, not a build flag.** gd02 is out: the GH2 data and shaders are
too heavy for the hardware. gdul does not apply either. A Vita build is one
static `eboot.bin` with no game modules, so it would be "gd01 for Vita", a
standalone VPK.

Do it after the backend work in `study_backends.md`. gd01 calls OpenGL
directly in about 530 places, so the renderer needs a GLES2-level path
anyway, and a device layer would make that path much cheaper.

## Hardware and platform facts

| | PS Vita (PCH-1000/2000, PS TV) | What gd01 needs today |
|---|---|---|
| CPU | 4x ARM Cortex-A9 (3 usable by apps), up to 444 MHz | GH1 logic is light. FFmpeg's MPEG-2 FMV decoding and the audio mix are the heavy parts. |
| GPU | PowerVR SGX543MP4+ through Sony's GXM. No native OpenGL. | GL 3.3 core / GLES 3.0, GLSL 330/300 es, FBO post chain |
| RAM | 512 MB main + 128 MB CDRAM (video). Homebrew gets roughly 256 to 365 MB of main RAM depending on the heap and extended-memory settings [community figures, verify on hardware]. | PS2 data was 32 MB. Our engine keeps whole archives and decoded textures in memory, so a memory diet is needed. |
| Screen | 960x544 (PS TV: 720p/1080i out) | Native res is fine. Turn the post-FX off by default. |
| Storage | Memory card / SD2Vita (exFAT, so no 4 GB file limit) on `ux0:` | GH1 ISO is 2.2 GB and fits |
| Input | Buttons, sticks, front and rear touch. PS TV also takes DS3/DS4. | No guitars. Frets on the face buttons and shoulders, strum on the D-pad. |

## Graphics route

- **vitaGL** (Rinnegatamante): an OpenGL wrapper on sceGxm with a
  fixed-function pipeline and **runtime GLSL translation**. Most modern
  Vita ports use it (the `.so` loader ports, Doom 3, Half-Life ports). It
  needs `libshacccg.suprx` (Sony's shader compiler), which the user extracts
  on the console. This is a standard step in Vita homebrew guides.
- Its GLSL support is closer to **GLSL ES 1.00 / GL 2.x** than to GL 3.3
  core. UBOs and `textureSize` are experimental, and there are no geometry
  shaders. gd01's GLES3 shaders (Android) would need a GLES2-compatible
  variant: `attribute`/`varying`, no UBOs, no integer textures, and no
  MRT-heavy passes.
- **SDL**: SDL3 has an official Vita port (VitaSDK CMake toolchain). Its GLES
  path is `-DVIDEO_VITA_PVR=ON` (PVR_PSP2), or desktop GL through gl4es4vita.
  Most ports use SDL only for the window, input and audio, and vitaGL for
  GL. Our SDL3 code (input, audio, paths) carries over.
- **Shaders at run time**: vitaGL compiles GLSL on the device, so the first
  run of each shader stutters. vitaGL can cache the compiled shaders. gd01
  has few programs (it is not gd02's 587), so this is acceptable.

## What changes in gd01

1. **Static build** with the VitaSDK toolchain (arm-vita-eabi, newlib, C++17).
   There is no `dlopen` and no launcher. Use the gd01 standalone entry point.
2. **Renderer**: a "Vita" render profile with GLES2-level shaders, the
   highway, venue and crowd only, and no post-FX or AI upscalers (no Vulkan).
   Texture uploads stay RGBA8. The PS2 palettised textures could stay
   paletted (GXM supports P8) to save memory.
3. **Memory**: stream the venue archives instead of keeping them resident,
   free decoded data after upload, and cap the texture cache.
4. **Audio and video**: SDL audio works. MPEG-2 FMV (PSS) uses software
   FFmpeg on an A9: test it, and drop the FMV if it cannot hold 30 fps (the
   Vita's hardware decoder is AVC only).
5. **Paths**: `ux0:data/gd01/` for the ISO, saves and config. There is no
   drag and drop, so add an ISO picker or a fixed path.
6. **Input**: map the frets to the buttons, with an optional rear-touch strum.

## Effort and costs

- About 2 to 3 person-months for a playable gd01: renderer profile 4 to 6
  weeks, memory work 2 to 3 weeks, packaging and input 1 to 2 weeks. If the
  device layer from `study_backends.md` exists first, the renderer part
  roughly halves.
- Money: none. VitaSDK is free. Releases build in the `vitasdk/vitasdk`
  Docker image, which is one more release job (within the Actions budget,
  release-only).
- Testing needs a real hacked Vita. Vita3K (the emulator) runs vitaGL
  homebrew, which helps, but timing and memory must be checked on hardware.

## Blockers and requirements

- **The console must be hacked.** HENkaku (FW 3.60) or HENlo (3.65 to 3.74),
  made permanent with **Ensō** (3.60/3.65). Without Ensō it has to be
  re-applied after each reboot. Users also install `libshacccg.suprx`.
- **Distribution as homebrew only.** A VPK on GitHub Releases, listed on
  **VitaDB** (the community store, installable through its Vita app). There
  is no Sony program. The PS Store for Vita accepts no new titles.
- Legal: VitaSDK is not Sony's SDK, so this is fine. The user brings their
  own disc, which fits our rules. The audience is small but loyal (rhythm
  ports do well on VitaDB).
- Guitar play is not possible, and button play is not "match the original".
  It is presented as a handheld mode, like the Android touch mode.

## Steps (if pursued)

1. Spike (1 to 2 weeks): VitaSDK + SDL3 + vitaGL "hello highway". Load one
   extracted GH1 song chart and the highway in Vita3K, then on hardware.
   Measure fps and memory.
2. Gate: 60 fps for the highway alone, and at least 30 fps with a venue,
   within 300 MB.
3. Add the GLES2 render profile (shared with other weak-GPU targets) and the
   memory diet.
4. Package the VPK (LiveArea assets are our own, with no GH logos), add a
   release job, and submit to VitaDB.

## Sources

- SDL3 Vita port: https://wiki.libsdl.org/SDL3/README-vita
- vitaGL (GXM wrapper, runtime GLSL, libshacccg requirement): https://github.com/Rinnegatamante/vitaGL
- VitaSDK: https://vitasdk.org/ , Docker image https://hub.docker.com/r/vitasdk/vitasdk
- HENlo / firmware support and Ensō: https://vita.hacks.guide/using-henlo , https://consolemods.org/wiki/Vita:HENlo , https://consolemods.org/wiki/Vita:HENkaku
- VitaDB: https://www.rinnegatamante.eu/vitadb/
- Vita3K emulator: https://vita3k.org/

## Backlog candidates

- **(GD01, Enhancement) PS Vita spike**: VitaSDK + SDL3 + vitaGL highway-only
  build, with fps and memory measured on hardware. 1 to 2 weeks.
- **(GD01, Enhancement) GLES2 "low-end" render profile**: shaders without
  UBOs or MRT, post-FX off, RGBA8/P8 textures. It can be reused for the
  Vita, old Android devices and TVs. 3 to 4 weeks (about 2 with a device layer).
- **(GD01, Enhancement) Memory diet / streaming venue archives**: a cap for
  handhelds. 2 to 3 weeks.
- **(GD01, Enhancement) PS Vita release (VPK + VitaDB)**: packaging, input
  map, `ux0:` paths, a release job. 1 to 2 weeks after the above.
