# gd01: what this project is, what users get, how to organise the code

## 1. What are we doing?

We are building a **reimplementation** of Guitar Hero (PS2, 2005), informed by **reverse engineering**.

| Term | What it means | Is it what we do? |
|---|---|---|
| **Reverse engineering** | Studying a program you don't have the source for (its machine code, file formats, behaviour) to learn how it works. | **Yes, as the research half.** Ghidra exports of the PS2 executable, file-format decoders in `tools/`, the notes in `FINDINGS.md` and `docs/`. |
| **Re-engineering** | Taking an existing system and rebuilding or restructuring it, usually keeping its behaviour. The umbrella term for "reverse engineer, then rebuild". | Broadly yes: the whole project is re-engineering. |
| **Decompilation / recompilation** | Turning the machine code back into equivalent C (by hand or tool) and compiling *that*, so the new program *is* the old code (e.g. the Mario 64 and Zelda decomps, or static recompilers). | **No.** We read the decompiled code to understand it, but we do not ship it. |
| **Emulation** | Running the original executable on a simulated PS2 (PCSX2). | **No.** |
| **Reimplementation** | Writing new code from scratch that behaves like the original and reads the original's data (like OpenMW for Morrowind, OpenRCT2, ScummVM). | **Yes, the engineering half.** The `engine/` (gd01, C++/SDL3/OpenGL) is new code. It loads the game data from the user's own disc. |

So: **we reverse engineer the original to learn its exact rules, then reimplement them in new, portable code that runs the original game data**.
Where the game's logic is data (the DTA scripts for menus, sounds, the arena, the HUD wiring), we go further and run
those scripts as they are, through our own interpreter, which keeps that behaviour 1:1 by construction.

### How close can this get?

- **Rules, timing and logic: identical.** Hit windows, scoring, Star Power, the rock meter, unlocks, cash,
  the animation driver and so on are read from the code and ported exactly; the self-test plays every chart.
- **Data: identical.** Every model, texture, animation, chart, sound, script and string comes from the user's disc.
- **Visuals: very close, not bit-exact.** The PS2 drew with its own hardware (the GS and VU1 microcode). We
  reproduce the same maths in OpenGL (lighting formula, fog, blend modes), but pixel-level details differ:
  texture filtering, colour precision, interlacing, the exact reverb and blur hardware. Effects whose parameters
  are not in the binary (the SPU2 reverb presets) can only be tuned by ear or eye against the real console.
- **Things the PS2 decides in hardware or timing** (frame pacing, audio latency) behave like the original
  but are not cycle-identical.
- **What's needed to close the last gaps:** side-by-side captures from a real PS2 (or a trusted emulator) for
  visual and audio comparison.

In short: functionally the same game; visually and audibly as close as careful comparison allows, with the
option to go beyond the original later (the "extended features").

## 2. What the end product looks like

### For players

One small download per platform. **No game data is included**: the player points it at their own copy.

```
gd01-windows-x64/            gd01-linux-x86_64/             gd01-macos/
  gd01.exe                     gd01                           gd01.app/
  SDL3.dll                      README.md                         Contents/MacOS/gd01
  README.md                     (gd01.AppImage, later)           Contents/Frameworks/libSDL3*.dylib
                                                                  Contents/Resources/
                                                                README.md
```

How they use it:

- **Windows:** unzip, double-click `gd01.exe`, drop the `.iso` (or extracted folder) on the window once.
- **Linux:** unpack, run `./gd01 /path/to/disc.iso` (or drop the ISO on the window); later an AppImage or Flatpak.
- **macOS:** open `gd01.app` (right-click → Open the first time, as it is unsigned), drop the ISO on the window.

The game remembers the disc path. Saves and settings live in the platform's standard user folder:

| Platform | Folder |
|---|---|
| Windows | `%APPDATA%\gd01\gd01\` |
| Linux | `~/.local/share/gd01/gd01/` |
| macOS | `~/Library/Application Support/gd01/gd01/` |

Inside: `config.txt` (disc path, later settings) and `career.json` (profiles, careers, high scores).

### For developers (this repository today)

```
engine/          the gd01 reimplementation (C++20, SDL3, OpenGL 3.3, CMake)
  src/           app, audio, data (ARK/ISO/DTB/MIDI/VGS), game, render, scene, script, ui
  build.sh       Linux + Windows (mingw) builds into dist/
tools/           Python decoders for every file format (validated byte-exact)
analysis/        Ghidra headless exports of SLUS_212.24 (decompilation, strings, functions)
docs/            reverse-engineering specs, one per system
FINDINGS.md      the summary of what is known
backlog.md       done / open tables
extracted*/      decoded game files (local only, from the user's disc: never published)
original_*/      the disc image and executable (local only: never published)
```

## 3. Suggested git structures

Whatever the structure, **never commit game data** (the ISO, the executable, `extracted*/`, `analysis/` outputs that
contain decompiled code), and keep the licence of the reimplementation clear.

### Option A: one repository, clean split (simplest)

```
gd01/
  engine/      the game (released)
  tools/       format decoders (released)
  docs/        specs (released)
  research/    scripts to regenerate analysis/ from a user's own executable (released)
  .gitignore   analysis/, extracted*/, original_*/
```
- One place for issues, one CI pipeline (GitHub Actions builds Windows/Linux/macOS and attaches the binaries to a
  GitHub Release on each tag).
- Best while the project is small and the same people work on research and engine.

### Option B: two repositories (what you suggested)

1. **`gd01`** (the product): `engine/`, its docs, CI, releases. Players and contributors to the game only need this.
2. **`gd01-research`** (the tooling): `tools/`, `docs/` RE specs, Ghidra scripts, notebooks, `FINDINGS.md`.
   Possibly private, since it is closer to the original binary.

- The engine can reference the specs by link; the research repo can pin an engine version for tests.
- Cleanest legal separation and a smaller, friendlier product repo; costs some duplication of docs.

### Option C: three repositories (if the project grows)

1. **`gd01`**: the engine only.
2. **`gd01-formats`**: a reusable library + CLI for the Milo/GH1 formats (ARK, DTB, RND, VGS, BNK…), usable by
   modders and other projects, published on its own (PyPI / crates).
3. **`gd01-research`**: Ghidra/REA scripts and notes, private.

- Fits the "extended features" era (modding, new songs, texture packs), where the format tools become a product.

**Recommendation:** start with **A** now (fast iteration, one CI), move to **B** before the first public
release, and spin out **C**'s format library only when modding work starts.

### Branches and releases (any option)

- `main` always builds and passes `--selftest`; work happens on short feature branches with pull requests.
- Tags `v0.x` produce GitHub Releases with the three platform builds; a `CHANGELOG.md` per release.
- CI: build all platforms on every PR, run `--selftest` on Linux, upload artifacts.
