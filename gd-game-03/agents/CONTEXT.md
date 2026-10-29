# gd03: context for agents

How to build, test and run gd-game-03 (Guitar Hero Encore: Rocks the 80s,
PS2). Read the kickoff (`GD-GAME-03_KICKOFF.md` at the repository root),
`PROGRESS.md`, `DECISIONS.md` and `OPEN_QUESTIONS.md` first.

## Layout
- `engine/`: the C++ engine, a renamed copy of gd-game-02's (DECISIONS.md D2)
  with the PS2 layers from gd-game-01. Comments citing `0x82xxxxxx` are GH2
  Xbox 360 addresses (gd-game-02's evidence); `0x00xxxxxx` are SLUS_215.86.
- `tools/`: Python decoders (`ark.py`, `dtb.py`, `milo.py`, `tex.py`, `vgs.py`,
  `midi_chart.py`) and `ghidra_scripts/`.
- `extracted/` (git-ignored): `iso/` (7z of the disc), `ark/` (ark.py
  extract; `../` path parts become `_up_/`), `dta/` (every .dtb as text).
- `ghidra/` (git-ignored): project `gh80s`, program `SLUS_215.86`, exports in
  `ghidra/analysis/` (functions.tsv, strings.tsv, refs.tsv, decomp/*.c).

## Disc
`ISO="$HOME/Development/prj-gd-engine/original_isos/Guitar Hero Encore - Rocks the 80s (USA) (PS2).iso"`
(1.9 GB ISO 9660, volume id `GH2`, boot `SLUS_215.86`).

## Reverse-engineering tools (which did what)
- **REA MCP** (`mcp__rea__open_binary`): opens `SLUS_215.86` (identifies an
  ELF, MIPS, SHA-256 03cbbf4b...408f) but every analysis call fails: Ghidra
  provider "architecture_unsupported" (MIPS variants need a separate
  profile), Hopper / IDA / Kuna not configured or no MIPS lane. Tried once
  on 2026-10-28; nothing else used REA.
- **Ghidra 12.1.4 headless** (r5900 extension, language
  `r5900:LE:32:default`) does all the analysis:
  ```sh
  export JAVA_HOME=~/.local/opt/jdk-21.0.12.1+1
  G=~/.local/opt/ghidra_12.1.4_PUBLIC/support/analyzeHeadless
  P=$HOME/Development/prj-gd-engine/gd-game-03/ghidra
  S=$HOME/Development/prj-gd-engine/gd-game-03/tools/ghidra_scripts
  # import + analysis (once, ~5 min)
  $G $P gh80s -import $HOME/Development/prj-gd-engine/gd-game-03/extracted/iso/SLUS_215.86
  # export (functions, strings, refs, pseudocode)
  $G $P gh80s -process SLUS_215.86 -noanalysis -readOnly -scriptPath $S -postScript ExportAnalysisElf.java $P/analysis
  # assembly at an address (rule 4: read call sites in assembly)
  $G $P gh80s -process SLUS_215.86 -noanalysis -readOnly -scriptPath $S -postScript DumpAsm.java 002d95b0:40
  # data words (float / int / string)
  $G $P gh80s -process SLUS_215.86 -noanalysis -readOnly -scriptPath $S -postScript ReadF.java 003e07a4:4
  ```
  The project path must be absolute with no `.` element.

## Build
From `gd-game-03/engine`:
- configure once: `cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release`
- build: `ninja -C build/linux` (0 warnings required)
- Check the binary is newer than your edits before testing (kickoff rule 2).

## Tests (all take the disc path first; a usage text means it did not run)
`build/linux/gd03 "$ISO" --scenetest` (more added per milestone; see PROGRESS.md
for each one's last verified output).

## Clocks
Inherited from gd02 (gd-game-02/docs/audio_and_charts.md): the song clock is
the mixer's (audio frames played), the track clock follows it; the sfx log and
screenshot timestamps are wall clock. Re-checked per milestone; offsets go in
`docs/audio_and_charts.md`.
