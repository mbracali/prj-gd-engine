# Kickoff: gd-game-15, Guitar Hero: Van Halen (Xbox 360), wave 01

> **You are starting a delta reimplementation.** Van Halen runs on the same
> Neversoft "gh4" engine as Guitar Hero: Metallica, so gd-game-15 is built
> **as a delta on gd-game-10**. Read this file, then
> `docs/spikes/GD-GAME-10_KICKOFF.md` in full: its rules, mistakes list,
> pattern, tests and gates all apply here with gd10→gd15 and GD10_→GD15_.
> This file only says what differs. When it conflicts with a tool default,
> this file wins; when it conflicts with `docs/guidelines/`, the guidelines
> win.

## 0. When to start

**Not before gd-game-10 has passed its gates** (M0..M9, `ready_to_play`,
and its `docs/gh4-vs-gh3.md` written). Van Halen copies a working Metallica
engine; starting earlier copies its bugs twice. If gd-game-10 is not there
yet, stop and write that in `gd-game-15/agents/PROGRESS.md`.

## 1. The target

| | |
|---|---|
| Game | **Guitar Hero: Van Halen** (Underground Development / Activision, December 2009), Xbox 360, USA / Europe |
| Folder / program | `gd-game-15/` → `gd15`, env prefix `GD15_`, Android `io.github.mbracali.gd15` |
| Disc | `original_isos/Guitar Hero - Van Halen (USA, Europe) (En,Fr,De,Es,It).iso` (7.8 GB image, XDVDFS / XGD2) |
| Executable | `default.xex` (10.5 MB, PowerPC Xenon, XEX2), **title ID `4156083D`** [C, read 11-10] |
| Branch | the owner's branch for this run, named in the run prompt. Never create or switch branches |
| Goal | a full decompilation-based reimplementation, 100% faithful, playable from the Universal Launcher on Linux, Steam Deck, Windows and Android |
| Expected sharing | **≈60–70 % with gd-game-10** (same engine, same code shape; the community study in `docs/spikes/colabs01.md` section B found a function-level map between the two executables) |

### What the disc holds (first look, 2026-11-10)

Read-only with `gd-game-05/tools/xdvdfs.py list` and `gd-game-04/tools/pak.py
list` (two files copied to a scratch folder outside the repo).

| Item | Found | Against Metallica |
|---|---|---|
| Title ID / media ID | `4156083D` / `4B8BD7DD` | `41560830` |
| Game partition | `0xFD90000` (XGD2), 1,468 entries, 4.8 GB of files | same layout, 1,493 entries, 7.2 GB |
| `.pak.xen` / `.pab.xen` | 1,070 / 3 (941 in `DATA/COMPRESSED/PAK`, 81 in `DATA/COMPRESSED/SONGS`, 27 in `DATA/PAK/CAS`, 19 zones) | 1,060 / 3 |
| `.fsb.xen` | 322 (321 in `DATA/MUSIC`: `_1`, `_2`, `_3`, `_preview` per song; `streamall.fsb.xen`, 170 MB) | 342 |
| `.bik.xen` | 22 (logos, venue movies, loading and instructions; no story movies) | 37 |
| `.img.xen`, other | 13; the same `compress.toc`, `pak_sizes.txt`, `MaterialLibrary.bin`, `engine_params.qb` | same set |
| Songs | 54 `_preview` streams; 81 song paks including tutorials and debug / sync-test charts. Set list from `songlist.qb` | 59 / 101 |
| Venues (zones) | drum_kit, frankenstrat, la_block_party, london, paris, rome, s_stage, starwood, soundcheck, soundcheck2, studio, studio2 (+ frontend, in_game, viewers) | all different except soundcheck / studio |
| Characters | `DATA/PAK/CAS/` (27 paks, same names as Metallica's) | same structure |
| QB | `qb.pak.xen` lists 1,036 entries with every name resolved (Metallica 955); the pab data layout is gd-game-10's M1 finding | same engine |

## 2. What differs from the gd-game-10 kickoff

- **Start from a copy of gd-game-10** (engine, tools, tests, walks, build
  scaffolding, `parity.py`), renamed gd10→gd15 and GD10_→GD15_, as gd-game-10
  started from gd-game-04. **Never modify gd-game-10** (or any other game).
  Shared gh4 code moves to a common place only by the owner's decision
  (question 4).
- **Ghidra:** import Van Halen's XEX into `gd-game-15/ghidra/`, then transfer
  gd-game-10's confirmed names with a function matcher (instruction-level
  index). colabs01 section B notes a data shift between the two builds
  (`.data` about +0x1FE80, `.rdata` about +0x1FDD0): a **lead** that speeds
  up the transfer, never a substitute for confirming each function on our
  disc. Every gd10 [C] rule is **[I] here** until re-confirmed at its Van
  Halen address.
- **Clean room, as gd-game-10:** no code copied, no names of other projects
  or their maintainers in our code, `gd-game-15/docs/` or commits; facts
  re-found on our disc, in our own words.
- **The original's bugs:** check that Van Halen has the same clock drift,
  claps ignoring calibration and frame-counted timers; reuse gd10's toggles
  with Van Halen's own addresses, same defaults as the owner chose for gd10.
- **Docs:** `gd-game-15/docs/vh-vs-metallica.md` is the main document: every
  difference in scripts, data and rules (career, set list, venues,
  characters, store, menus, HUD, movies). The other docs follow gd10's set.
- **Full band:** drums and vocals were left untested by the community study
  for this game as well; test them early (M5 can run in parallel with M4's
  polish). Reference gd-game-05 and gd-game-10 for the mic and drums.
- **Versions: never bump** the launcher or any game version unless the
  owner asks (`general_guidelines.md`); the skeleton stays `0.0.0`.
- **Launcher:** `build_module_15`, `GD15_MODULE`, `iso_signatures` with
  `format: xdvdfs`, `xex_title_id: "4156083D"`.

## 3. Milestones (gd-game-10's, shortened by the copy)

Measured base: gd-game-10's own times (write them here from its
`PROGRESS.md` when it finishes). Planning figures:

| Milestone | What | Estimate |
|---|---|---|
| M0 | copy gd-game-10, rename, rip, Ghidra import, matcher transfer of names, backlog rows (replace GD15-001) | 1 h |
| M1 | data layer: confirm every gd10 format on VH's disc (`--paktest`, QB, FSB, Bink) | 0.5 h |
| M2 | songs and guitar core: VH's `songlist.qb`, rules re-confirmed at VH addresses | 0.75 h |
| M3 | scripts and menus: VH's own front end through the copied interpreter, audit | 1 h |
| M4 | **proof of concept**: Quickplay, one song on guitar in a VH venue, results, back | 0.5 h |
| M5 | full band: drums, vocals, four players | 1 h |
| M6 | VH's venues, cameras, lighting, characters | 1.5 h |
| M7 | modes, store, saves, The Glitches | 1 h |
| M8–M9 | port extras, launcher module, packages | 1 h |
| M10 | gaps against `docs/parity.md` | remaining time |

**Total M0..M9 about 7–8 h** (against gd10's 11–14 h), if gd-game-10's
engine is generic as its kickoff asks. Write your own estimate at the top of
`PROGRESS.md` and revise it at each gate. Gates, tests and the end-of-run
checks are exactly gd-game-10's section 7.

## 4. Extra mistakes to avoid on a delta game

1. **Don't trust the copy.** A rule that worked in Metallica is [I] in Van
   Halen until its address is confirmed; `parity.md` starts with every
   copied row at [I].
2. **Don't leave Metallica's content behind:** no Metallica song, venue,
   movie, string or menu layout reachable in gd15. Add an audit check that
   every content name the engine loads exists on VH's disc.
3. **Different defaults per game:** VH's menus, colours, HUD and career are
   its own; find each in VH's scripts, not in gd10's code.
4. **One copy per fix:** a bug found in gd15's copied gh4 code is also in
   gd10: write it in `OPEN_QUESTIONS.md` for the owner (don't touch gd10).

## 5. Questions for the owner

1. **Start:** confirm gd-game-15 waits for gd-game-10's gates, or may it start
   after gd-game-10's proof of concept (M4)?
2. **Version and branch:** may M0 set gd15 to `0.0.1`, and which branch does
   wave 01 run on?
3. **Proof-of-concept stop:** stop after M4 and wait for your go, as gd10, or
   run M0..M9 in one go?
4. **Shared gh4 code:** keep gd15 a full copy of gd10 (rule of two), or move
   the gh4 engine into one shared place for gd10 / gd15 / gd07 before or
   after this wave?
5. **World Tour content in Van Halen** (songs from gd-game-07's disc or
   packages): out of scope until gd-game-07 exists, or never?

## Owner's answers

