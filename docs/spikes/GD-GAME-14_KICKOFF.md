# Kickoff: gd-game-14, Band Hero (Xbox 360), wave 01

> **You are starting Band Hero as a delta of Guitar Hero 5.** Read
> `GD-GAME-12_KICKOFF.md` (GH5) first, then this file. Everything there
> applies here, with `gd12` → `gd14` and `GD12_` → `GD14_`, unless this file
> says otherwise: ground rules, the clean-room rule, the mistakes list
> (rules 1–21), the pattern, the gates, "Reaching 100 %". `GD-GAME-05_KICKOFF.md`
> stays the long reference. When this file conflicts with a tool default, this
> file wins; when it conflicts with `docs/guidelines/`, the guidelines win.

## 1. The target

| | |
|---|---|
| Game | **Band Hero** (Neversoft / Activision, November 2009), Xbox 360, USA / Europe |
| Folder / program | `gd-game-14/` → `gd14`, env prefix `GD14_`, Android `io.github.mbracali.gd14` |
| Disc | `original_isos/Band Hero (USA, Europe) (En,Fr,De,Es,It).iso` (7.8 GB, XDVDFS, game partition at `0xFD90000`) |
| Executable | `default.xex` (13.9 MB, PowerPC Xenon, XEX2), **title ID `4156085C`**, internal name `gh5_1_burn_Final.exe`, CRC32 `57900866` [C, read 11-10] |
| Branch | the one the owner names. Never create or switch branches yourself |
| Starts from | **gd-game-12** (GH5), once it has reached the gate the owner sets (question 2) |
| Goal | a full decompilation-based reimplementation, 100% faithful, playable from the Universal Launcher on Linux, Steam Deck, Windows and Android |

### Disc first look (2026-11-10, read-only)

Read with `gd-game-05/tools/xdvdfs.py list` and a scratch reader in
`/tmp/claude-1000/kick12/` (nothing extracted into the repo).

| What | Band Hero | GH5, for comparison |
|---|---|---|
| Partition | `0xFD90000`, 1,417 entries, 5.47 GB | 1,542 entries, 5.64 GB |
| Root | `default.xex`, `data/`, `AvatarAssetPack`, `nxeart`, `$SystemUpdate/` | same |
| Archives | 917 `.pak.xen` (773 in `data/compressed/PAK`), 5 `.pab.xen`, `compress.toc(.lst).xen` | 970 / 793 |
| Compression | the same **`CHNK`** chunked container [I, one file probed] | same |
| Songs | 125 `*_song.pak.xen`: **65 set-list songs**, 32 tutorial charts, 28 debug / sync charts, no `_MM` variants | 86 songs, 9 `_MM` |
| Set list overlap | **0** songs shared with GH5's disc (same tutorial and debug charts) | |
| Song audio | 396 `.fsb.xen` in `data/music`: 99 songs × `_1`, `_2`, `_3`, `_preview` | 118 × 4 |
| Venues | 11 zones (`Z_AwardShow`, `Z_Cabo`, `Z_CentralPark`, `Z_Cube`, `Z_EasterIsland`, `Z_Gorge`, `Z_Mall`, `Z_Montreux`, `Z_Paris`, `Z_Space`, `Z_Tokyo`) plus `Z_Visualizer`, `z_studio`, `z_viewer`, `z_in_game`, `global*` | 14 zones |
| Characters | `cas_pieces.pab.xen` 598 MB; 178 `L_*` anim paks, with Band Hero's own "moment" clips (`L_DRUM_Moment_*`, `L_GUIT_Belong*`) | 532 MB, 165 |
| Other | 58 `musicstudio*` paks, `MaterialLibrary.bin.xen`, `engine_params.qb.xen` | same set |
| Video | 43 Bink files (13 venue loading movies) | 39 (15) |

**Disc vs the community notes.** The title ID (`4156085C`) and the internal
name match the build described in `docs/spikes/colabs01.md`. Their notes give
no CRC for Band Hero and were made on a "USA" disc, ours is "USA, Europe":
**verify at M0** that their addresses land on the same instructions in our XEX
(sample 20 of them; log the result in `docs/FINDINGS.md`) before using any.

### What is different from GH5 (the delta)

The study measured the two executables as about **99.3 % the same
functions** and 96.4 % of Band Hero's ~9,000 scripts present in GH5 (most
byte-identical); 326 scripts are Band Hero-only. From that, gd-game-12 covers
an estimated **70–80 %** of this port. The work is the difference:
- **Content:** its own set list (65 pop songs, none shared with GH5), venues,
  characters (premade and the celebrity guests, from the data), loading
  movies, career gigs and challenges, unlocks.
- **Scripts:** diff the QB scripts against GH5's by checksum; port the
  326 new ones through the same QScript VM; log every new C-function.
- **Struct offsets:** the study saw the renderer block shifted by 32–48
  bytes from about +12,600 and the player input object by +8 from +128.
  Treat these as a watch-list: confirm every shared struct in our XEX.
- **Renderer:** one extra screen-FX fragment (projection) and one GH5 pass
  absent (visualizer in pass 1) [I, to confirm]; the same per-effect flags.
- **Presentation / look:** menus, fonts, colours, the HUD skin and the
  gamma / colour path (gd05 rule 15) are re-measured, never assumed GH5's.
- **Same as GH5, confirm only:** the 49.875 MHz timer quirk, the two-frame
  scene draw, frame-counting logic above 60 FPS, ultrawide rules, the DLC
  manifest layout, the frame-alignment and drummer scripts.

The clean-room rule of GD-GAME-12 section 1 applies unchanged: addresses and
ids are Ghidra leads only, no code copied, no names of other projects or
maintainers in our code or the game's docs.

### Band Hero features to reproduce (verify from the data)

Full band, 1–4 local players (guitar, bass, drums, vocals), Party Play and
drop-in, every difficulty; Career, Quickplay+, competitive modes, Practice,
tutorials, the music studio as scoped for GH5; the creator and the store;
venues, crowd, cameras, lighting. Online, leaderboards, achievements, song
export / import and DLC: stubbed, logged, "not in the port".

## 2. How you work

As GD-GAME-12 section 2. **Time estimate, measured:** gd-game-03 (a copied
engine, the closest case) reached its final report in about 4 h; Rock Band's
wave 01 (a copied engine, wider) took about 4.3 h.

| Plan for Band Hero (from a gd-game-12 at its first playable or later) | Estimate |
|---|---|
| First playable: boot, main menu with our universal menus, one Quickplay song on guitar in a venue | 1.5–2.5 h |
| M0..M9, everything gd-game-12 already does | 5–7 h |
| Parity waves | follow gd-game-12's: a fix there is usually a fix here |

## 3. Ground rules: the differences from GD-GAME-12

- **Never modify gd-game-12** (or any other game) from this task. Copy its
  engine, tools and scaffolding into `gd-game-14/`, renamed, unless the owner
  chooses a shared engine (question 1). A bug found in the shared code is
  fixed here and reported in gd-game-12's backlog section as a row, not
  patched there.
- **Versions:** never bump the launcher or any game version unless the owner
  asks; gd-game-14 stays at `0.0.0` until he does.
- Outside `gd-game-14/`, only: your backlog section, `build_module_14` in
  `gd-engine-launcher/scripts/build.sh`, `games.yaml` if needed.
  `gd-engine-core/` is not changed.
- Everything else as GD-GAME-12 section 3 (commits with `-s` and no AI lines,
  `repo_guard.py`, no game data, no emulator, processes, subagents).

## 4. Mistakes to avoid

GD-GAME-12 section 4 (rules 1–21). Two more for a delta port:
22. **A copied rule is [I] until re-read in Band Hero's XEX** (L2). "99.3 %
    the same" means about 0.7 % is not: find which by address, don't guess.
23. **Content tables, not code.** A list copied from the parent is GH5's,
    not Band Hero's: song lists, venues, characters and unlocks come from
    Band Hero's own QB.

## 5. The pattern

As GD-GAME-12 section 5, with `gd14`, `GD14_`,
`SDL_GetPrefPath("gd-engine","gd14")`, `GD14_MODULE`, `build_module_14`, and
`iso_signatures`: `format: xdvdfs`, `xex_title_id: "4156085C"`. Add a
**`--deltatest`** that diffs Band Hero's QB scripts and C-function uses
against the copy's tables and lists every script the engine has not run.

## 6. Milestones

**M0: set-up (≈30 min).** Copy gd-game-12 (engine, tools, Ghidra scripts,
builds) renamed; `agents/` files; README from the data; rip; Ghidra import and
export; **title ID, CRC and the 20-address sample check** (section 1); diff
the function lists against gd-game-12's Ghidra export (same instructions,
nearby addresses) and write the map to `ghidra/analysis/` (git-ignored) and a
summary to `docs/FINDINGS.md`. Backlog: replace GD14-001 with milestone rows.

**M1: data (≈30 min).** gd-game-12's tools over the whole disc: every pak,
QB, FSB, texture, scene and Bink parses. Gate as GD-GAME-12 M1.

**M2: songs and rules (≈45 min).** The 65 songs from QB; `--selftest` all
charts fully hit; every rule re-confirmed at Band Hero's addresses.

**M3: scripts, UI → first playable (≈1 h).** The 326 new scripts, the
`--deltatest` at 0 unknown; Band Hero's menus; `--walktest quickplay` from
boot to results; `bh_fuzz` passes.

**M4–M9 (≈3–4 h).** As GD-GAME-12, each limited to Band Hero's differences:
HUD skin, the 11 venues and their cameras, characters and moments, career and
modes, port extras, launcher module and packages (`verify_bundle` lists gd14
on every platform).

**M10: close the gaps.** Audit, ledger, final report.

## 7. Gates and 8. Reaching 100 %

As GD-GAME-12 sections 7 and 8. The ledger (`gd-game-14/docs/parity.md`)
keeps GH5's areas and weights so the two games can be compared row by row.

## 9. Questions for the owner

1. **Engine sharing:** copy gd-game-12 into gd-game-14 (each game owns its
   code; the shared family engine later, by the rule of two), or one shared
   GH5-family engine folder used by both from the start?
2. **When to start:** as soon as gd-game-12 has its first playable, or after
   it reaches a ledger level you choose (for example 60 %)?
3. **Branch:** which branch does Band Hero's wave 01 run on?
4. **First version:** may M0 set gd-game-14 to 0.0.1, or stay at 0.0.0 until
   you ask?
5. **Scope of wave 01:** stop at the first playable, or run on to M9?
6. **Warriors of Rock (gd-game-17):** should it later start from gd-game-12
   or from gd-game-14 (the newer of the two builds)?

## Owner's answers
