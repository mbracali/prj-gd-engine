# Ghidra scripts for Guitar Hero World Tour (Xbox 360)

Headless Ghidra (12.1.x, JDK 21) scripts that turn your own copy of
`default.xex` into the offline study files under `gd-game-07/ghidra/analysis/`.
The Ghidra project (`gd-game-07/ghidra/`) and `ghidra/analysis/` hold original code
and are git-ignored: never commit them.

| Script | What it does |
| --- | --- |
| `FixXexFunctions.java` | Repairs the imported program and saves it: finds the register save/restore helpers (`__savegprlr_NN`, `__restgprlr_NN`, `__savefpr_NN`, `__restfpr_NN`, `__savevmx_NN`, `__restvmx_NN`), makes them returning and inlined, re-creates every non-leaf function from `.pdata` with its exact size, re-runs auto-analysis, then adds the `lis`/`addi` data references Ghidra misses and defines the strings they point at. |
| `DumpCFuncs.java` | Writes `cfuncs.tsv`: the QScript C-function tables (name, function, table entry). |
| `DumpAsm.java`, `ReadF.java`, `FindBytes.java`, `FindConst.java` | Assembly of an address range or a whole function (`fn:<addr>`); data reads (floats, ints, strings); byte-pattern and constant searches. |
| `ExportAnalysisXex.java` | Writes `functions.tsv`, `strings.tsv` (with referencing functions), `refs.tsv` (data references: Ghidra's plus a `lis`/lo linear scan) and `decomp/*.c` (pseudocode per 64 KiB). |

Placeholders: `<ghidra>` is the Ghidra install folder, `<jdk>` a JDK 21,
`<repo>` this repository, `<xex>` your extracted `default.xex`
(`python3 -I gd-game-10/tools/xdvdfs.py extract <iso> gd-game-07/extracted`).

## 1. Install the XEX loader

Ghidra has no Xbox 360 loader of its own. Get a XEXLoaderWV release
(https://github.com/zeroKilo/XEXLoaderWV, or a fork built for your Ghidra
version) and unzip it into `<ghidra>/Ghidra/Extensions/` (or use File >
Install Extensions in the GUI). If the zip was built for another 12.1.x
release, set `version=` in `XEXLoaderWV/extension.properties` to your Ghidra
version.

## 2. Import (once)

```sh
export JAVA_HOME=<jdk>
mkdir -p <repo>/gd-game-07/ghidra
<ghidra>/support/analyzeHeadless <repo>/gd-game-07/ghidra ghwt -import <xex>
```

The loader is picked automatically (the log says `Using Loader: XEX Loader by
Warranty Voider`, language `PowerPC:BE:64:A2ALT-32addr`). The first
analysis takes a couple of minutes.

## 3. Fix the functions (once per import)

```sh
<ghidra>/support/analyzeHeadless <repo>/gd-game-07/ghidra ghwt -process default.xex -noanalysis \
  -scriptPath <repo>/gd-game-07/tools/ghidra_scripts -postScript FixXexFunctions.java
```

Takes about 2-3 minutes and ends with `Save succeeded`. Don't pass `-readOnly`
here, or nothing is saved. Optional script arguments: `noinline` (keep the helpers as
calls), `noanalysis` (skip the auto-analysis pass).

## 4. Export

```sh
<ghidra>/support/analyzeHeadless <repo>/gd-game-07/ghidra ghwt -process default.xex -noanalysis -readOnly \
  -scriptPath <repo>/gd-game-07/tools/ghidra_scripts -postScript ExportAnalysisXex.java <repo>/gd-game-07/ghidra/analysis
```

The output directory argument is required. Files are overwritten. Decompiling
every function takes a while.

These scripts are gd-game-10's (Metallica), copied unchanged (only the `@category`). Keep the
headless heap at most 6 GB on a shared machine: `export GHIDRA_HEADLESS_MAXMEM=6G`.
On World Tour's XEX the four steps (import, fix, cfuncs, export) took 7 minutes.
