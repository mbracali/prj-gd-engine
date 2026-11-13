# Study: Guitar Hero Encore: Rocks the 80s on Xbox 360 (fan port)

Study, 2026-11-13 (backlog GDALL-052). Read-only: nothing was built, run or imported. The repo was cloned
without its audio, video and model files (scripts and docs only). Every section ends with an empty
**Owner's take:** line for you.

## Executive summary

| Project | Repo | Licence | What it is | Platforms | We take | For | Value (1-10) |
|---|---|---|---|---|---|---|---|
| GH80s for Xbox 360 | hmxmilohax/guitar-hero-encore-xbox | **None** (all rights reserved) | GH80s' characters, venues, songs and menus moved onto GH2's Xbox 360 game: GH80s data rebuilt for the 360 and GH2's scripts edited, run by GH2's own 360 executable (patched) | Modded Xbox 360, Xenia | Proof that GH80s content runs on GH2's 360 engine with script changes only; the list of what had to change | gd03, gd02, GDALL-042 | 5 |

**The key finding.** GH Rocks the 80s (our gd-game-03, from the PS2 disc) is close enough to GH2 360 (our gd-game-02)
that its whole content runs on GH2's 360 engine with only data conversion and script edits: no new engine code.
That matches what we saw building gd03 from gd02, and it supports two ideas: GH80s songs inside gd02 (GDALL-042, the
"Encore" wave already planned) and an optional "gd02 look" for gd03's own venues. The repo itself ships Harmonix data
(converted milos, moggs, a patched XEX), so nothing can be copied.

Owner's take:

## Licensing

| Item | Licence | Copy? | Notes |
|---|---|---|---|
| guitar-hero-encore-xbox | No licence file | **No** | Contains converted GH80s / GH2 milos, menu audio loops (`.mogg`), Harmonix scripts and a patched `default.xex` |
| Build tools (ArkHelper, Superfreq, dtab, dtacheck) | MIT / BSD (upstream) | Ideas only | We read these formats ourselves |

Same MiloHax group as GH2DX: their rules reject agent-made contributions and AI recompilations. We take facts only
and never contact their repos from an agent.

Owner's take:

## Links

| What | Link |
|---|---|
| Repo | https://github.com/hmxmilohax/guitar-hero-encore-xbox |
| Release | v1.0.1, 2024-06-05 ("GH80s-Xbox-Release"); last commit 2026-09-12 |
| Group | https://milohax.org/ |

## What it is

- **Who / status.** Two MiloHax members; created 2024-04, released v1.0.1 2024-06, small fixes until 2026-09
  (character rework, venue headers, "no more sad Judy" face animation fix, save location, HD venues, gym parity).
- **How it works.** The player brings a raw GH2 360 folder; a Python build packs the GH80s content (characters,
  venues, songs, UI) converted to Xbox formats into a new ARK and copies a patched GH2 `default.xex`. The README asks
  for "as vanilla as possible": only GH80s characters and venues selectable, the 80s songs untouched, the strum
  limit kept, no DLC (later commit: "allow dlc, bass in qp").
- **Content.** GH80s' career as GH2's engine reads it: six tiers of five songs (battle, small1, small2, theatre, fest,
  arena), required songs per difficulty (Easy 3 / 4, Medium 4 / 5, Hard and Expert 5 / 5), GH80s cash awards;
  locale files for English, French, German, Spanish and Italian; menu loops of seven 80s songs.

Owner's take:

## Features (as listed by the project)

- No "Deluxe" features; as close to the PS2 original as GH2's 360 engine allows.
- All GH80s characters, venues, songs and UI on the Xbox 360; only 80s characters and venues selectable.
- Strum limit kept; DLC off at first, then allowed; bass in quickplay.
- Five languages.

## What we can use

### (a) Knowledge for our ports

| Fact (re-check on our discs) | Seen in | Use for us |
|---|---|---|
| GH80s content runs on GH2 360's engine with data conversion and script edits only (characters, venues, HUD, UI, campaign, songs) | the whole repo | Supports the "Encore" wave of GDALL-042 (80s songs inside gd02) and sharing more of gd02 in gd03; differences live in data and scripts, not engine code |
| What had to change, by file: `campaign_80s.dta` / `songs_80s.dta`, `modes.dta`, `store.dta`, `guitars.dta`, `credits.dta`, char / world / hud / ui object lists for PS2 and 360, five locales, HUD milos (`hud_*`, `you_rock`), `strum_limit`, `pause_quit` / `pause_restart` | repo tree | A checklist when gd02 loads gd03's songs or venues (GDALL-042): the same seams |
| PS2 characters' face animations broke on the 360 engine until fixed ("no more sad Judy") | commit log | A known risk when gd02 draws GH80s characters: check visemes / face clips (gd02 `facefx.md`) |
| PS2 venues needed new headers and an "HD" pass on the 360 | commit log | Venue data from gd03's disc needs the same header handling in gd02 |
| GH80s career: six tiers of five songs, Easy 3 / 4, Medium 4 / 5, Hard / Expert 5 / 5 required | `campaign_80s.dta` | Already in gd03 from its disc; a cross-check only |

Owner's take:

### (b) Ideas for our Enhanced menus and launcher

- **GH80s songs in GH2** (GDALL-042, the Encore wave): their port shows the content fits; ours reads it from the
  player's own GH80s disc through the launcher, in memory.
- **gd03 with gd02's renderer look** as an Enhanced option (their "HD venues"): GH80s venues lit and post-processed
  like GH2 360. Needs GDALL-006-style shader work; medium value.
- **Language choice** for gd03 (GDALL-045): the PAL GH80s locales (English, French, German, Spanish, Italian) can be
  read from a PAL disc if the player has one; our own PT-BR strings stay in our files.

Owner's take:

### (c) Things to avoid

- Their converted milos, audio, the patched XEX: game data, never copied or linked.
- Their way (rebuild an ARK, patch the executable) is the opposite of ours: we read the original files in place.

Owner's take:

## Bugs of ours this explains

- None directly. The face-animation fix in their log is a warning for GDALL-042 (GH80s characters inside gd02).

Owner's take:

## Suggested backlog items

| ID | Game | Class | Description | Effort (agent time) | Value |
|---|---|---|---|---|---|
| NEW-11 | gd02 | Enhancement | GDALL-042 Encore wave checklist from this study: campaign / songs / locales / HUD seams, venue headers, face clips of GH80s characters | 1 h (doc) | Medium |
| NEW-12 | gd03 | Enhancement | Optional gd02 look for gd03 (GH2 360 lighting and post-processing over GH80s venues) | 2-3 days | Medium |
| NEW-13 | gd03 | Enhancement | Language from a PAL GH80s disc when present (GDALL-045) | 3-4 h | Low |

Owner's take:
