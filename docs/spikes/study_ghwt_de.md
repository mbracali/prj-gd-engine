# Study: Guitar Hero World Tour: Definitive Edition (for gd-game-07, gd-game-10, gd-game-15)

Study, 2026-11-13 (backlog GDALL-052). Read-only: the repositories were cloned shallowly, read and deleted;
nothing was built, run or imported. From the 2 GB updater repository only the plain-text files (a readme, a
controller table, ID lists) were opened; no binary, asset or game file was. Every section ends with an empty
**Owner's take:** line for you.

## Executive summary

| Project | Who | Licence | What it is | Platforms | Status | We take | Make it ours (1-10) |
|---|---|---|---|---|---|---|---|
| GHWT:DE | "GHWT:DE Team" / Fretworks | **None stated**; closed binaries (`GHWT_Definitive.exe`, `GHWTDE.dll`) | A large mod of **GH World Tour PC**: a patched exe + a DLL that hooks the engine, dynamic mod loading, many assets ported from other GH games | Windows (needs the user's GHWT PC install) | Very active: 1.4.3.5 (2026-04); v1.0 was 2021-12-25 | Knowledge from its **wiki** (formats, QB, song arrays, camera / light-show notes, INI options), feature ideas | n/a (no code to take) |
| ghwt-de-volatile (gitgud) | Fretworks | None | Not source: the **updater's payload** (179 zips: the patched exe, DLLs, and assets ripped from GH3 / GHA / GH5 / BH / WoR / Metallica / Van Halen / DJ Hero / Tony Hawk) | - | Mirrors each release | **Nothing** (game assets of many titles) | - |
| Guitar Hero SDK (gitgud, linked from the wiki) | Fretworks (Zedek) | **ISC** (in `package.json`, no LICENSE file) | Node.js modding kit: PAK / QB / IMG / TEX / FSB compile and decompile, a Clone Hero to GHWT song importer, **QB key name lists for ~20 Neversoft games** | Any (Node) | Active (2026-02) | Format notes; the key lists as a **local** lookup only (game-derived names, never committed) | 6 if ever adapted (ISC notice) |

**The key finding.** GHWT:DE is the most complete living knowledge base on the **World Tour engine family**
(World Tour, Metallica, Smash Hits, Van Halen, then GH5 / Band Hero), and its wiki is open. The code is closed,
the payload is ripped game content, and there is no licence, so **nothing enters the repo**; what we gain is
facts to re-find on our own discs (formats, song arrays, note encodings, engine limits) and a long, proven list
of features World Tour-era players want (song speed 20-200 %, HOPO / tap chords, no DOF / bloom switches, controller
binding without X360CE, auto-launch into a song, raised limits). For gd-game-10 (Metallica, the WT engine) this is
directly useful now; for gd-game-07 / gd-game-15 later.

Owner's take:

## Licensing

| Source | Licence | Copy? | Use |
|---|---|---|---|
| GHWT:DE exe / DLL | None, closed | **No** | Never run or open |
| ghwt-de-volatile zips | None; content of Activision titles | **No** | Never extract assets |
| ghwt.de wiki | None stated | **No text copied** | Read for facts, written fresh in our words |
| Guitar Hero SDK code | ISC (permissive: keep the notice) | Possible, but we have our own pak / QB code in gd04 / gd10 | Read for format facts |
| SDK `SDKCode/Checksums/*_keys.txt` (QB key -> name, per game) | Names are derived from the games (debug files) | **Never committed** | A local, git-ignored lookup for Ghidra and logs, like `ghidra/` leads |

Owner's take:

## 1. What it is and how it works

GHWT:DE replaces World Tour PC's exe with a patched one (no-CD, raised limits) and injects `GHWTDE.dll`, which
hooks the engine (the wiki speaks of an SDL2 input layer since 1.4.2, a developer console, a script tracer). It
loads **mods dynamically** from `DATA/MODS/` (any sub-folder depth): songs, characters, instruments, highways,
menu music, song categories, each a folder with an INI and paks, registered by QB "API" scripts
(`API_Add_Song`, `API_Add_Character`, `API_Redirect_SongFiles`, `DE_GetINIValue` ...). Since 1.3 it reads
**Clone Hero song folders natively** (no conversion). A `GHWTDE.ini` holds the settings ([Logger], [Graphics],
[Audio], [Debug], [Config], [AutoLaunch], [Band]). Online play goes through their own revived servers.

Owner's take:

## 2. Links

| What | Link |
|---|---|
| Fandom article | https://guitarhero.fandom.com/wiki/Guitar_Hero_World_Tour:_Definitive_Edition |
| Website | https://ghwt.de/ |
| Wiki (mod and SDK) | https://ghwt.de/wiki/ (sections `wtde/` and `sdk/`) |
| Updater payload repo | https://gitgud.io/fretworks/ghwt-de-volatile (commits = update log) |
| Guitar Hero SDK | https://gitgud.io/fretworks/guitar-hero-sdk |
| Fan wiki | https://ghwtde.fandom.com/ |
| Mods | https://www.nexusmods.com/guitarheroworldtour/mods/ |
| Discord | https://discord.gg/ghwtde |

Owner's take:

## 3. Knowledge for our World Tour-engine ports (re-find on our discs)

| Fact (from the wiki / SDK docs) | Where it helps |
|---|---|
| PAK entry header: extension key, offset (relative to the entry), size, asset context (the pak's name, matters for zone nodes), full-name key, short-name key, parent, flags; split `.pak` + `.pab` | gd10 (done: GD10-003); a cross-check for gd07 / gd15 |
| Files a pak may hold: `.ske .ska .fam .shd .rag .mqb .skin .mdl .mcol .img .pimg .tex .scn .col .hkc .sqb .qb .pfx .nqb .rnb .nav .fnt .fnc .gap .dbg .wav .trkobj` | A checklist of loaders still missing in gd10 (GD10-003: scenes, anims, `.wav`, skins "not yet") |
| Song arrays in `<song>.mid.qb`: `_timesig` [ms, num, den], `_fretbars` (ms), and **(time, packed note) pairs** for `_anim_notes`, `_cameras_notes`, `_lightshow_notes`, `_drums_notes`; the packed note holds note, length and velocity in one 32-bit value | gd10 cameras (GD10-039), light show (GD10-040), drummer anims (GD10-044) |
| Camera-cut, crowd, light-show and trigger note numbers documented one by one (`qb_cameracutnotes`, `qb_crowdnotes`, `qb_lightshownotes`, `qb_triggernotes`) | Expected-vs-measured checks for gd10's camera notes 101..108 and its light groups |
| QB arithmetic: int op int -> int; any float -> float; pair / vector op pair / vector component-wise; scalar x vector scales; `->` between vectors | Already in gd04's VM (`vm.cpp`: dot product); a ready test table for gd10 |
| The note limit is **4,096** notes per song in WT PC (`FixNoteLimit`); an aspect-ratio check caps default video modes (`FixAspectRatioLimit`) | Our loaders must not cap note counts (custom / foreign songs, GDALL-042) |
| Debug knobs that exist in the engine: button cheats, heap scaler, checksum lookup, script tracing | gd10 debug: button cheats enable the engine's on-screen debug buttons (WoR gates its debug menu on a similar `enable_button_cheats` flag, see `study_wor_plus.md`); the checksum lookup reads the game's own **`dbg.pak`** to name QB keys: our discs carry it too, so our ports can name keys from the user's own disc |
| Controller table (`GHWTDEControllerDB.ini`): per USB VID:PID the button and axis numbers of real guitars (e.g. the 360 X-plorer `1430:4800`, the PS3 Harmonix guitar `12ba:0000`), axis thresholds for the strum bar on a hat, tilt on an inverted axis | Facts for our `controllers.json` defaults (GAMEPADS menu): re-measure with our own devices before trusting |
| The SDK's per-game QB key name lists (GH3 38k, Metallica 92k, Van Halen 87k, WT 99k / 360 97k, GH5 98k, BH 98k, WoR 107k names) | Name unknown checksums in our logs and Ghidra (gd04 GD04-016's 678 missing natives, gd10's 71, gd17 later); kept **outside** the repo |

Owner's take:

## 4. Feature ideas for Enhanced menus and the launcher

| Idea | Maps to |
|---|---|
| Practice / song speed 20-200 % | Every game (see study_fastgh3 NEW-4) |
| HOPO chords and tap chords, open notes, slider (tap) notes from Clone Hero charts | Custom / foreign songs only (GDALL-042) |
| Native Clone Hero song folders (`song.ini`, `notes.chart` / `notes.mid`, stems) | GDALL-042: one `gd::core::songs` reader for all games |
| Disable DOF, disable bloom, AA choice, borderless window, VSync off, FPS limiter | Our Enhanced graphics rows (gd10 has its uber pass with DOF / bloom: per-effect switches) |
| **AutoLaunch**: start straight into a song with venue, instrument, difficulty, bot and HUD on / off | Launcher CLI (GDUL-006 `--play`): add `--song --venue --part --diff --bot` |
| Bind any controller in an in-game menu (no X360CE), Wii / PS3 / 360 guitars recognised by VID:PID | Our GAMEPADS menu; a shared guitar table in gd-engine-core |
| Preferred band members and their highways; a "change band members" setting; guest / any character on any part | gd10 / gd07 band select; "no gender lock" idea from WoR+ |
| Gem themes from other games (GH3 / WoR gems) and highways | Cross-game asset choice when the user owns both discs (like GDALL-041) |
| Microphone device choice, vocal latency adjustment | gd05 / gd10 vocals |
| Mod / content list screen ("Installed mods") with errors shown | Launcher: a "songs found" page for GDALL-042 |
| Havok FPS setting (physics rate separate from render rate) | gd10 ragdolls (GD10-044) at high FPS |

Owner's take:

## 5. Things to avoid

- Downloading, extracting or opening any zip of ghwt-de-volatile: they hold the patched exe and assets ripped
  from many games (including games the owner owns: still not from our discs, so never usable).
- Their online servers, Discord rich presence, NexusMods content.
- Committing the SDK's key lists: they are derived from game debug data; use them only in the private workbench.
- Their "port assets of game X into game Y" approach as a default: our rule is each game as it shipped; cross-game
  content only when the user owns both discs (GDALL-041 / GDALL-042 model).

Owner's take:

## 6. Does it explain any open bug?

- **GD10-040 (venues 1.1x..6x darker)**: no cause found there. Their releases retuned venue lighting venue by
  venue (Meadowlands first), so their look is not a reference. Worth checking our light-group decode against the
  documented light-show note numbers before tuning intensities.
- **GD10-039 (cameras)**: the documented camera-cut note list is a direct expected-vs-measured table for our camera
  notes.
- **GD10-038 (overlapping images after entering a menu)**: nothing found.

Owner's take:

## Suggested backlog items

| ID | Game | Class | Description | Effort (agent h) | Value |
|---|---|---|---|---|---|
| NEW-1 | gd10 | Original | Check our song-array decode (time / packed note pairs) against the documented camera, crowd, light-show and trigger note numbers; a selftest table of expected vs decoded notes for one song | 2 | High |
| NEW-2 | gd10 | Original | Pak file-type checklist: list which of the ~28 pak entry types gd10 reads, stubs or skips; feed GD10-003 | 1 | Medium |
| NEW-3 | gd04 / gd10 / gd17 | Original | Name QB keys from the disc's own `dbg.pak` (gd04 already reads it, `formats.md`; make sure gd10 / gd17 do too) in logs and audit lists; for Ghidra, a private git-ignored lookup for keys `dbg.pak` lacks; nothing committed | 1 | High |
| NEW-4 | launcher | Enhancement | CLI auto-launch into a song: `--song --part --diff --venue --bot --nohud` (GDUL-006 follow-up), also speeds up tests (GDALL-049) | 2-3 | Medium |
| NEW-5 | gd10 (then gd07 / gd15) | Enhancement | Per-effect Enhanced switches for DOF, bloom and the camera pulse | 1-2 | Medium |
| NEW-6 | gd-engine-core | Enhancement | Shared guitar / drum table by USB VID:PID (buttons, strum hat, tilt axis and sign), re-measured on the owner's devices, feeding every game's GAMEPADS menu | 3 | Medium |

Owner's take:
