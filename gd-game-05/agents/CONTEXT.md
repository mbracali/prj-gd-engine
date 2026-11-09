# gd05: context for agents

How to build, test and run gd-game-05 (Rock Band, Xbox 360). Read the
kickoff (`docs/spikes/GD-GAME-05_KICKOFF.md`), `PROGRESS.md`, `DECISIONS.md`
and `OPEN_QUESTIONS.md` first.

## Layout
- `engine/`: the C++ engine, a renamed copy of gd-game-02's (DECISIONS.md D1).
  Addresses in comments inherited from gd02 are GH2's; RB's are written
  `RB 0x82...` in code and `[C] 0x82...` in `docs/`.
- `tools/`: Python decoders (`xdvdfs.py`, `ark.py`, `dtb.py`, `milo.py`,
  `tex.py`, `mogg.py`, `midi_chart.py`) and `ghidra_scripts/` (gd-game-04's).
- `extracted/` (git-ignored): `disc/` (xdvdfs.py extract of the game
  partition), `ark/` (ark.py extract, 4,588 files), `dta/` (dtb.py dump, every
  `.dtb` as text, 291 files).
- `ghidra/` (git-ignored): project `rb1`, program `default.xex`, exports in
  `ghidra/analysis/` (functions.tsv, strings.tsv, refs.tsv, decomp/*.c).

## Disc
`ISO="$HOME/Development/prj-gd-engine/original_isos/Rock Band (USA).iso"`
(7.8 GB, XGD2, title ID 45410829). `default.xex`: XEX2, image base
0x82000000, entry 0x824F9000 [C: XEX headers, Ghidra import log].

## Reverse-engineering tools
- **Ghidra 12.1.4 headless** + XEXLoaderWV (language `PowerPC:BE:64:A2ALT-32addr`):
  ```sh
  export JAVA_HOME=~/.local/opt/jdk-21.0.12.1+1
  G=~/.local/opt/ghidra_12.1.4_PUBLIC/support/analyzeHeadless
  P=$HOME/Development/prj-gd-engine/gd-game-05/ghidra
  S=$HOME/Development/prj-gd-engine/gd-game-05/tools/ghidra_scripts
  # done once (2026-11-09): import, FixXexFunctions (41,175 functions), export
  $G $P rb1 -process default.xex -noanalysis -readOnly -scriptPath $S -postScript ExportAnalysisXex.java $P/analysis
  # assembly of a function / a range (rule 4: read call sites in assembly)
  $G $P rb1 -process default.xex -noanalysis -readOnly -scriptPath $S -postScript DumpAsm.java fn:0x82300000 0x82300000:40
  # data words
  $G $P rb1 -process default.xex -noanalysis -readOnly -scriptPath $S -postScript ReadF.java 0x82001234:f:4 0x82005678:str
  ```
  The project path must be absolute with no `.` element.
- REA is not used: on gd-game-04 it could not open a XEX nor a PowerPC raw
  image (gd-game-04/agents/CONTEXT.md).

## Build
From `gd-game-05/engine`:
- configure once (FFmpeg with Bink, see `build_common.sh`):
  `bash -c '. ./build_common.sh; build_ffmpeg build/ffmpeg/linux Linux --disable-x86asm'`, then
  `cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DGD05_VIDEO=ON -DGD05_FFMPEG_DIR="$PWD/build/ffmpeg/linux/ffmpeg-prefix"`
- build: `ninja -C build/linux` (0 warnings required)
- Windows (MinGW): `./build_windows.sh`; all platforms: `../local_build.sh all`.
- Check the binary is newer than your edits before testing (kickoff rule 4.2).

## Tests (all take the disc path first; a usage text means it did not run)
Always with the owner's devices ignored, the dummy audio driver and a
scratch data folder:
```sh
cd gd-game-05
export XDG_DATA_HOME=$PWD/agents/scratch/xdg SDL_AUDIO_DRIVER=dummy SDL_VIDEO_DRIVER=offscreen
export SDL_GAMECONTROLLER_IGNORE_DEVICES=0x1bad/0x0002 SDL_JOYSTICK_IGNORE_DEVICES=0x1bad/0x0002
timeout 120 engine/build/linux/gd05 "$ISO" --scenetest
```
Each milestone adds its tests here; `PROGRESS.md` has each one's last
verified output. Python checks:
`python3 -I tools/milo.py scan extracted/ark`, `python3 -I tools/dtb.py dump extracted/ark extracted/dta`.

## Clocks
Inherited from gd02 (`gd-game-02/docs/audio_and_charts.md`): the song clock
is the mixer's (audio frames played, held back by the device latency), the
track clock follows it; the sfx log and screenshot timestamps are wall
clock. Rock Band adds the vocal pitch clock (the mic capture) and the
input / video calibration: documented in `docs/audio_and_charts.md` as they
land.
