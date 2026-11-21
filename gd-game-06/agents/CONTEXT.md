# gd-game-06: working context (paths, tools, the game)

Read this first in a new session, then `PROGRESS.md`, `DECISIONS.md`,
`OPEN_QUESTIONS.md`, `docs/FORMATS.md` and the backlog's gd-game-06 section.
Guitar Hero: Aerosmith is Guitar Hero III's engine (the XEX names its build
`GH3_xp1`); `gd-game-04` (GH3) is the model for everything, `gd-game-10`
(Metallica, gh4) the example of a game started from a copy of gd04's engine.

## Paths (relative to the repository root)

| What | Where |
| --- | --- |
| Disc image | `original_isos/X360 - Guitar Hero - Aerosmith (USA, Europe).iso` (git-ignored) |
| Rip | `gd-game-06/extracted/` (`python3 -I gd-game-04/tools/xdvdfs.py extract <iso> gd-game-06/extracted`; 806 files, 4.67 GB, under a minute) |
| Name table | `gd-game-06/extracted/names.tsv` (41,234 names; built with a copy of `gd-game-04/tools/qbkey.py --table`, which reads `../extracted` next to the script) |
| Ghidra project | `gd-game-06/ghidra/gha.gpr` (program `default.xex`) |
| Ghidra export | `gd-game-06/ghidra/analysis/` (`functions.tsv` 33,178, `strings.tsv` 8,282, `refs.tsv` 85,940, `cfuncs.tsv` 1,401 entries, `decomp/` all 33,178 decompiled) |

`extracted/`, `ghidra/` and `agents/scratch/` are git-ignored: never commit
them, never paste decompiler output into tracked files (docs/legal.md).

## Tools

No engine or tools of our own yet (GD06-001 is discovery only). gd-game-04's
Python decoders read Aerosmith's rip as they are; run them on
`gd-game-06/extracted` (they are read-only):

```sh
T=gd-game-04/tools; E=gd-game-06/extracted
python3 -I $T/pak.py check $E        # paks: 620, files: 39372, with problems: 0
python3 -I $T/qb.py check $E         # qb files: 2545, sections: 46883, scripts: 17912, with problems: 0
python3 -I $T/qbscript.py check $E   # scripts: 17912, with problems: 0
python3 -I $T/scn.py check $E        # scn 159, mdl 11, skin 432, hkc 20, with problems: 0
python3 -I $T/img.py check $E --layout-only   # files: 1385, textures: 8202, with problems: 0
python3 -I $T/havok.py check $E      # hkc 20, rag 29, ..., with problems: 0
python3 -I $T/matlib.py check $E/DATA/FXFILES/MaterialLibrary.bin.xen   # shaders: 4804, with problems: 0
python3 -I $T/anim.py check $E       # .ska: 30,333 problems (FORMATS.md "Animations")
```

Dumps that need names (`qb.py dump`, `qbscript.py dis`) resolve keys through
the table next to the script, so for Aerosmith run copies of the tools placed
in a folder whose `../extracted` is gd-game-06's rip (a scratch copy, or
gd-game-06/tools once M1 copies them). Never run `fsb.py` on song banks
(song audio is an owner item).

## Ghidra (headless, heap at most 6 GB; the project path must be absolute)

```sh
export JAVA_HOME=~/.local/opt/jdk-21.0.12.1+1 GHIDRA_HEADLESS_MAXMEM=6G
G=~/.local/opt/ghidra_12.1.4_PUBLIC/support/analyzeHeadless
P=$PWD/gd-game-06/ghidra; SP=$PWD/gd-game-06/tools/ghidra_scripts
$G $P gha -import $PWD/gd-game-06/extracted/default.xex                       # 4 min
$G $P gha -process default.xex -noanalysis -scriptPath $SP -postScript FixXexFunctions.java   # 2 min
$G $P gha -process default.xex -noanalysis -readOnly -scriptPath $SP -postScript DumpCFuncs.java $P/analysis
$G $P gha -process default.xex -noanalysis -readOnly -scriptPath $SP -postScript ExportAnalysisXex.java $P/analysis  # 1 min
$G $P gha -process default.xex -readOnly -noanalysis -scriptPath $SP -postScript DumpAsm.java fn:0x82426528
```

REA cannot open the XEX (no XEX2 / PowerPC), as for GH3 (gd-game-04 D6).
GH3's [C] addresses do not carry over: re-find each function in Aerosmith's
export (same size and callees, a string it references, or its C-function
name in `cfuncs.tsv`). Example: GH3's .ska rotation-key decoder 0x824213B8 is
Aerosmith's 0x82426528 (same body).

## The executable

`default.xex`: XEX2, title ID **41560819**, media ID 37A41C2D, version 1,
base 0x82000000, entry 0x8213A0B0 [C: XEX optional headers]. Language
`PowerPC:BE:64:A2ALT-32addr` (XEXLoaderWV).

## The game (from the data)

- **Songs.** `gh3_songlist` has 43 real songs (31 career, 11 bonus, the
  credits' Kings and Queens) plus test / tutorial / placeholder entries; 60 song paks and 60 audio banks on the disc (with the 13
  tutorials and 4 sync / test songs). Career: 31 songs in 6 tiers; Bonus:
  11 (`GH3_Bonus_Songs`, incl. Joe Perry's guitar battle track).
- **Career** (`GH3_Career_Songs`, `GH3_Career_Progression`, scripts
  `guitar_progression.qb`): 6 tiers, each a venue, an opening act's two
  songs first (`num_intro_songs_to_progress = 2`,
  `Progression_TierAerosmithUnlock`), then Aerosmith's set, then an
  Aerosmith encore; a movie before tier 1 (`AO_short_1`) and after each tier.
  Tier 6 adds the boss battle against Joe Perry (`bossjoe`, `Boss_Joe_Props`).
  Opening acts by song artist: Cheap Trick and Mott the Hoople; Joan Jett and
  the Blackhearts and The Kinks; The Clash and New York Dolls; The Cult and
  Run DMC; The Black Crowes and Lenny Kravitz feat. Slash; Ted Nugent and
  Stone Temple Pilots. Songs to progress: easy / medium 3, hard / expert 4.
  No co-op career (GH3's `coop_career_menu_flow.qb` is gone; a
  `coop_quickplay_menu_flow.qb` is new).
- **Venues** (zones): Nipmuc High (`z_Nipmuc`), Max's Kansas City
  (`z_MaxsKC`), The Orpheum Theatre of Boston (`z_Fenway`), Moscow
  (`z_nine_lives`), Half-time show (`z_JPPlay`), Hall of Fame (`z_hof`), plus
  credits, soundcheck, viewer and a test zone. None of GH3's venues remain.
- **Characters** (`Musician_Profiles`): GH3's playable cast (Axel, Casey,
  Izzy, Judy, Johnny, Lars, Midori, Xavier, new colour sets 5-7) plus Joe
  Perry (several outfits), DMC; band members Brad Whitford, Tom Hamilton,
  Joey Kramer (`Aero_Drummer`, one rig per venue), Steven Tyler
  (`Aero_Singer`, per-venue rigs and per-song face anims); GH3's bosses
  (Slash, Tom Morello, Lou) and the God of Rock are gone from the paks
  (their boss props stay in the scripts).
- **Modes**: `p1_career`, `p1_quickplay`, `p2_faceoff`, `p2_pro_faceoff`,
  `p2_battle`, `p2_coop` (quickplay), online (`net_guitar_battle.qb`, out of
  scope as in GH3).
