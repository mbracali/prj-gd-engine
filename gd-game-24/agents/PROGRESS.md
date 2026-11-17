# gd-game-24 progress log

Rock Band Unplugged (PSP) -> `gd24`. Newest entries at the bottom.

## GD24-001 discovery (2026-11-17, about 1.5 h of agent time)

Done: rip (444 files), EBOOT decrypted, Ghidra project with Allegrex,
named imports and an export (28,942 functions), the formats in
`../docs/FORMATS.md`. Not done: any engine code (owner approves the plan
first).

What the game is: Harmonix's Rock Band beatmatch code ported to the PSP by
Backbone (sources `src/rock/beatmatch`), Frequency / Amplitude play: one
player, four instrument tracks, four lanes each, switching tracks. 41 songs +
14 tutorial tracks, 24 tour venues, 5 languages.

What makes it different from our other games: almost all data are **heap
images** (raw RAM with absolute pointers, objects identified by vtable) and
the song audio is **encrypted** 5-stem ATRAC3plus. Both need the EBOOT's
code first; everything else (PNG, AT3 previews, PMF, TEXT, SBlk ADPCM) is
standard.

### Plan (owner to approve; agent time, from git history of gd10 / gd02)

| Milestone | Content | Estimate |
|---|---|---|
| M0 set-up | Folder from gd-game-02's layout (engine/, build_commands/, CMake, `gd24`, FFmpeg with atrac3p + h264 + mpegps), disc reader (ISO 9660 + UMD paths) | 1-2 h |
| M1 data layer | STR container + heap-image reader (chunk bases from the loader, vtable -> class table); preview AT3 -> FFmpeg atrac3plus; TEXT; SBlk/ADPCM (gd01's decoder); PMF through FFmpeg; `--formattest` | 4-6 h |
| M2 first boot (GD24-002) | One song: the four tracks' gems for one difficulty, tempo map, sections from BEATMATCH; the 5 stems in sync; 4-lane highway (debug look unless Q2 says otherwise), autoplay and real input, one track at a time | 4-6 h |
| M3 gameplay rules | Track switching, phrase capture and the auto-play "deploy" of captured phrases, overdrive (energy), multiplier, scoring, fail, stem muting, results; from BeatMatcher / PhraseDB / TrackWatcher | 6-10 h |
| M4 menus | UI heap images (UIList / UIElement / UIAnim / UITextBox), fonts, quickplay flow, settings, GAMEPADS / Enhanced menu per the guidelines | 10-15 h |
| M5 venues and band | GE meshes / swizzled paletted textures / materials / cameras from the venue images, the 3D highway, characters + ANIMS clips | 15-25 h |
| M6 career and the rest | World tour / world map, band creation (ROCKERMAKER), presets, saves, jukebox, tutorials (BNK voice-overs), movies, launcher module + packages | 10-15 h |

Total about 50-80 h of agent time; the first boot (M0-M2) about 9-14 h.

### Reuse

- gd-engine-core: platform, inputs, audio, render, image backends.
- gd-game-05 (Rock Band 1): the same Harmonix beatmatch classes (GameGem
  record, SongData, track watchers, scoring constants) as a reference for
  the layouts and rules; its chart / audio / highway code as a model.
- gd-game-04 / gd-game-10: the FFmpeg build (add `atrac3p`, `h264`,
  `mpegps`/PSMF) and the video player.
- gd-game-01: PS-ADPCM decoder for the SBlk banks; gd-game-02: folder
  layout, launcher module and packaging.

### Risks

- Heap images: every needed class layout must be read from the EBOOT
  (vtable map); cost grows with each feature (UI and venues the most).
- The song audio: the `.ATM` stems are encrypted and out of scope (D6);
  the previews play.
- The function export splits some functions (FindPspFunctions); the
  decompiled C needs a check against the assembly in tricky places.
- PSP rendering data (GE display lists, swizzled 4/8-bit textures, VFPU
  math) for M5.

## GD24-002 start (2026-11-17): owner's answers in

Owner approved the plan (OPEN_QUESTIONS 1-5, DECISIONS D5): the first boot
shows the **original** highway (no debug highway), the launcher recognises
PSP discs (.iso, .chd) with it, PSP mapping + GAMEPADS + guitars.

Re-estimate of the first boot with the original highway: the highway is an
object graph in a heap image (meshes, materials, swizzled PSP textures,
the gem models), so M2 also needs the heap-image reader for those classes
and a small GE-format mesh / texture renderer: +6-10 h. M0-M2 now about
15-25 h of agent time.

Song audio: the `.ATM` files are encrypted. Owner 11-17: no protection is
ever broken, so the full 5-track audio is out of scope and the key work was
dropped (D6); songs play their unencrypted preview AT3 from the disc.
