# gd-game-24: working context (paths, tools, Ghidra)

Rock Band Unplugged (PSP, USA, ULUS-10418) -> `gd24` (not started: plan in
`PROGRESS.md`, waiting for the owner). Read this first, then `PROGRESS.md`,
`DECISIONS.md`, `OPEN_QUESTIONS.md`, `../docs/FORMATS.md` and the backlog's
gd-game-24 section.

## Paths (relative to the repository root)

| What | Where |
|---|---|
| Disc image | `original_isos/PSP - Rock Band Unplugged (USA).iso` (git-ignored) |
| Rip | `gd-game-24/extracted/` (`7z x -ogd-game-24/extracted "<iso>"`; 444 files, 1.2 GB) |
| Decrypted executable | `gd-game-24/ghidra/EBOOT.prx` (ELF, MIPS Allegrex LE) |
| Import stubs / NIDs | `gd-game-24/ghidra/stubs.tsv`, `gd-game-24/ghidra/nids.txt` |
| Ghidra project | `gd-game-24/ghidra/rbu.gpr` (program `EBOOT.prx`) |
| Ghidra export | `gd-game-24/ghidra/analysis/` (`functions.tsv`: 28,942 functions, `strings.tsv`, `refs.tsv`, `decomp/`) |

`extracted/` and `ghidra/` are git-ignored: never commit them, never paste
decompiler output into tracked files (`docs/legal.md`).

## Build (Linux dev build, -j2; check `free -g` first)

```sh
cmake -S gd-game-24/engine -B gd-game-24/engine/build/dev -G Ninja -DCMAKE_BUILD_TYPE=Release -DGD24_MODULE=ON
ninja -C gd-game-24/engine/build/dev -j2 gd24 gd24_module   # gd24 + gd-game-24.so
```

gd-engine-core comes in through `add_subdirectory` (the disc reader: .iso
and .chd); its OpenAL sub-build runs its own jobs, so the first build uses
more than two cores for about a minute.

## Tests (quick; one at a time)

```sh
ISO="$PWD/original_isos/PSP - Rock Band Unplugged (USA).iso"
gd-game-24/engine/build/dev/gd24 --formattest "$ISO"   # "formattest: all 184 files parse"
gd-game-24/engine/build/dev/gd24 --formattest <a .chd of the disc>   # same output
```

## Decrypting EBOOT.BIN (done once, local only)

pspdecrypt (https://github.com/John-K/pspdecrypt, GPL-3.0, libkirk) built
outside the repository (`make CC=gcc CXX=g++ -j2`; needs zlib and OpenSSL
headers), then:

```sh
pspdecrypt -o gd-game-24/ghidra/EBOOT.prx gd-game-24/extracted/PSP_GAME/SYSDIR/EBOOT.BIN
# "Decryption successful for tag C0CB167C with type 1"
```

The tool is not part of the project and is never shipped: the engine will
not need the EBOOT at run time (it reads the data files only).

## NID names

`nids.txt` = `{NID, name}` pairs grepped from PPSSPP's `Core/HLE/*.cpp`
function tables (sparse clone of https://github.com/hrydgard/ppsspp,
`Core/HLE` only):

```sh
grep -h -oE '\{0[xX][0-9A-Fa-f]{8}, *[^"]*"[A-Za-z0-9_]+"' Core/HLE/*.cpp \
  | sed -E 's/\{0[xX]([0-9A-Fa-f]{8}).*"([^"]+)"/\1 \2/' \
  | awk '{print toupper($1), $2}' | sort -u > gd-game-24/ghidra/nids.txt
python3 -I gd-game-24/tools/prx_imports.py gd-game-24/ghidra/EBOOT.prx \
  gd-game-24/ghidra/nids.txt gd-game-24/ghidra/stubs.tsv   # "named 312 unknown 12"
```

## Ghidra (headless; absolute project path; heap at most 6 GB)

Needs the ghidra-allegrex extension (kotcrab, release v21.4, asset
`ghidra_12.1.4_PUBLIC_20261025_ghidra-allegrex.zip`) unzipped into
`~/.local/opt/ghidra_12.1.4_PUBLIC/Ghidra/Extensions/`: the PSP ELF loader
and the Allegrex language (VFPU included).

```sh
export JAVA_HOME=~/.local/opt/jdk-21.0.12.1+1 GHIDRA_HEADLESS_MAXMEM=6G
G=~/.local/opt/ghidra_12.1.4_PUBLIC/support/analyzeHeadless
P=$PWD/gd-game-24/ghidra T=$PWD/gd-game-24/tools/ghidra_scripts
# import + analysis + stub names (about 4 min)
$G $P rbu -import $P/EBOOT.prx -overwrite -scriptPath $T -preScript NameStubs.java $P/stubs.tsv
# find the functions the first pass missed (2,599 -> 28,942; .text covered 54% -> 81%) and export (about 15 min)
$G $P rbu -process EBOOT.prx -noanalysis -scriptPath $T -postScript FindPspFunctions.java \
   -postScript ExportAnalysisPsp.java $P/analysis
# one function's assembly
$G $P rbu -process EBOOT.prx -readOnly -noanalysis -scriptPath $T -postScript DumpAsm.java fn:0x088f4c90
```

FindPspFunctions starts a function at the first word of every gap, so some
long functions may be split in two: check a function's start before
trusting its decompiled C.

## The executable

- ELF `ET_EXEC`, one PT_LOAD at 0x08804000 (image to 0x08C4CF44 with the
  BSS), entry 0x08804114, gp 0x08B62060, module `"rockband"` [C].
- Harmonix sources `../src/rock/beatmatch/*`, `../src/rock/utl/*`,
  `../src/rock/m2game/*` named in asserts (strings 0x08B4xxxx) [C].
- Key functions so far: FAT open by sector FUN_08a23f5c; song paths
  FUN_089f191c / FUN_089f1b30 / FUN_089f2648; ATRAC player FUN_089b65b4
  (sets up), FUN_089b5948 (decode loop). The `.ATM` song audio is
  encrypted and out of scope (DECISIONS D6): do not work on it.
