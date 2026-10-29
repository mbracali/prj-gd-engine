# gd-game-04: working context (build, test, run, tools, clocks)

Read this first in a new session, then `PROGRESS.md`, `DECISIONS.md`,
`OPEN_QUESTIONS.md` and the backlog's gd-game-04 section.

## Paths (relative to the repository root)

| What | Where |
|---|---|
| Disc image | `original_isos/Guitar Hero III - Legends of Rock (USA) (Rev 1) (X360).iso` (git-ignored) |
| Rip | `gd-game-04/extracted/` (`tools/xdvdfs.py extract <iso> gd-game-04/extracted`; 660 files, 3.6 GB) |
| Name table | `gd-game-04/extracted/names.tsv` (`tools/qbkey.py --table gd-game-04/extracted/names.tsv`) |
| Ghidra project | `gd-game-04/ghidra/gh3.gpr` (program `default.xex`) |
| Ghidra export | `gd-game-04/ghidra/analysis/` (`functions.tsv`, `strings.tsv`, `refs.tsv`, `decomp/*.c`) |

Both `extracted/` and `ghidra/` are git-ignored: never commit them, never paste
decompiler output into tracked files.

## Build

```sh
# development build (no FFmpeg), fastest
cmake -S gd-game-04/engine -B gd-game-04/engine/build/dev -G Ninja -DCMAKE_BUILD_TYPE=Release -DGD04_VIDEO=OFF
ninja -C gd-game-04/engine/build/dev
# release-shaped builds (packages in gd-game-04/local_builds/)
gd-game-04/local_build.sh linux|steamdeck|windows|android|all [--force]
```

Check the binary is newer than your edits (`ls -la .../gd04`) before any test
(kickoff rule 4.2).

## Tests (each takes the disc image or the rip folder as its argument)

```sh
ISO="$PWD/original_isos/Guitar Hero III - Legends of Rock (USA) (Rev 1) (X360).iso"
B=gd-game-04/engine/build/dev/gd04
$B --formattest "$ISO"     # ends with "formattest: all files parse"
$B --shadertest            # "all N shader programs compiled"; GD04_GLES=1 for OpenGL ES
SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy GD04_QUIT_AFTER=1500 \
  GD04_SCREENSHOT=/tmp/x.bmp GD04_SCREENSHOT_AT=1000 timeout 30 $B "$ISO"   # headless smoke run
python3 -I gd-game-04/tools/pak.py check gd-game-04/extracted    # "paks: 400, files: 24726, with problems: 0"
python3 -I gd-game-04/tools/qb.py check gd-game-04/extracted     # "qb files: 2731, ... with problems: 0"
```

A test that prints the usage text has not run.

## Reverse-engineering tools: which did what

| Tool | Result |
|---|---|
| REA (`mcp__rea__open_binary`) on `default.xex` | Rejected: "unsupported binary format" (no XEX2 / PowerPC support). |
| REA on the Ghidra project (`gh3.gpr`) | Rejected: "truncated or unsupported binary header". |
| REA `raw-binary` mode | Offers 6502 / x86 / x86_64 / ARM / ARM64 / m68k only: no PowerPC. |
| Ghidra 12.1.4 headless + XEXLoaderWV | Imports and analyses the XEX (`PowerPC:BE:64:A2ALT-32addr`); `FixXexFunctions.java` then `ExportAnalysisXex.java`: 32,998 functions, all decompiled, 8,705 strings, 85,859 data refs. |
| Ghidra scripts `DumpAsm.java`, `ReadF.java` | Assembly ranges / whole functions (`fn:<addr>`) and data reads (floats, ints, strings): `tools/ghidra_scripts/`. |
| Python (`tools/`) | Our own decoders, validated over the whole disc (L21). |

So: Ghidra for everything in the XEX; REA stays the first choice for any
non-PowerPC artefact (none so far). Headless Ghidra invocation (the project
path must be absolute, no `.` element):

```sh
export JAVA_HOME=~/.local/opt/jdk-21.0.12.1+1
G=~/.local/opt/ghidra_12.1.4_PUBLIC/support/analyzeHeadless
P=$PWD/gd-game-04/ghidra
$G $P gh3 -process default.xex -readOnly -noanalysis -scriptPath $PWD/gd-game-04/tools/ghidra_scripts \
   -postScript DumpAsm.java fn:0x82300000 0x82300000:40
$G $P gh3 -process default.xex -readOnly -noanalysis -scriptPath $PWD/gd-game-04/tools/ghidra_scripts \
   -postScript ReadF.java 0x82001234:f:4 0x82005678:str
```

## The executable

- `default.xex`: XEX2, title ID **415607F7**, media ID 43048167, version 4,
  base address 0x82000000, entry 0x82136AC8 [C: XEX optional headers].

## Clocks (kickoff rule 5)

Only one so far: the wall clock (`SDL_GetTicksNS`), used by `GD04_KEYS`,
`GD04_SCREENSHOT_AT` and `GD04_QUIT_AFTER` (ms since the main loop
started). The song clock, the audio clock and the sfx log arrive with M2;
each one's offset is documented here and in `docs/` when it does.

Note (z-order research, 2026-10-29): screen-element z_priority rules were read
with Ghidra headless (`DumpAsm.java`, `ReadF.java`) plus the existing
`ghidra/analysis/decomp` export; REA was not used (it cannot open the XEX).
Notes in `agents/scratch/zorder_spec.md`.
