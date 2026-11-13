# Study: Milo Script Library (for our DTA interpreters: gd01, gd02, gd03, gd05; later RB2 / RB3)

Study, 2026-11-13 (backlog GDALL-052). Read-only: the repository was cloned shallowly, scanned with a small
counting script kept outside the repo, and deleted. Nothing from it is in the repo. Every section ends with an
empty **Owner's take:** line for you.

## Executive summary

| Project | Who | Licence | What it is | Size | Status | We take | Make it ours (1-10) |
|---|---|---|---|---|---|---|---|
| hmxmilohax/milo-script-library | MiloHax community | **None**; the content is Harmonix's game scripts | The **scripts of ~35 Harmonix titles**, many builds each (retail, title updates, demos, prototypes, every platform), as text DTA: decoded from the games' DTB files, or Harmonix's own raw DTA with comments where a disc shipped them (Rock Band PS2, an Amplitude demo) | 737 MB, ~30,000 files | Active (last commit 2026-10) | A **local reference and test corpus** (never committed): builtin coverage, handler names, other builds' scripts | n/a (data, not code) |

**The key finding.** This is game content: copyrighted Harmonix scripts, decoded from discs. It can never be
committed, shipped or quoted at length (`docs/legal.md`), exactly like the files on the user's disc. But read
**locally**, it is the best tool we have to measure our four DTA interpreters against the games' **whole** script
base, not only the paths our walks reach. A first scan (head symbol of every `{...}` call, compared with our
builtin tables) found:

- **gd01 (GH1) and gd02 (GH2 360)**: every builtin their final builds call is implemented; the remaining heads are
  objects and handlers (our UI / object layer, already audited by the walks).
- **gd03 (Encore)**: `search_replace` is missing, but only Milo editor tool scripts call it (not loaded in play).
- **gd05 (Rock Band 360, retail `base`)**: five builtins are missing: `with` (8 files: the shop, tour, characters,
  the band's servo bones), `sort` (6: world objects, the gem track, options), `search_replace` (`ui/game.dta`,
  song names with `_short`), and the bitwise assignments `|=`, `&=` with `'~'` (bitwise not), used by the
  characters' **arm flags** (`set_leftarm_true` / `_false` ... in `char/char_objects.dta`). The last group may
  explain part of **GD05-044 (the band members' hands)**: if those handlers run, our port reports UNKNOWN and the
  arm flags never change.
- **`mod` with a negative operand**: GH2's and Encore's `ui/career.dta` step the store with
  `{mod {+ itemIdx $dir} n}` (the line behind #100 in GH1, fixed in gd01 as GD01-319), and the tutorials do the
  same in GH1 / GH2 / Encore / RB1. gd02 / gd03 / gd05 still keep the sign: see `study_arson.md` NEW-0.

Owner's take:

## Licensing

| Part | Licence | Commit? | Use |
|---|---|---|---|
| Every file in the repo | **None**; Harmonix / MTV / Activision content | **Never** (not as fixtures, not as quotes, not as generated tables) | Local, private reference: counts, names, comparisons |
| Facts learned from it (a builtin exists, its arguments, a handler name) | Facts | Yes, in our words | In our docs, cited as "the game's scripts", confirmed on our disc |

The same rule as our disc data: the user's disc is the source of truth for what ships; this library only tells us
where to look. Never name the repo or its people in code or game docs (this study is the one place).

Owner's take:

## 1. What it holds (the parts that matter to us)

| Game (our folder) | Builds in the library | Files |
|---|---|---|
| Guitar Hero (gd-game-01) | `final`, `deluge` (a 2005-09 build) | 312 |
| Guitar Hero II (gd-game-02) | PS2 final / OPM demo / store demo / "deluxe", **360 final 061220**, 360 demos, **360 DLC** | 1,242 |
| Encore: Rocks the 80s (gd-game-03) | `final`, `deluge` | 365 |
| Rock Band (gd-game-05) | 360 `base`, **title updates tu2 / tu3 / tu5**, a demo; PS2 (raw DTA with comments), PS3, Wii | 1,714 |
| Rock Band 2 (gd-game-08), Beatles (gd-game-09), Lego (gd-game-13), Green Day (gd-game-16), RB3 (gd-game-18), RB4 (gd-game-19) | All platforms, several builds | 2,000-6,000 each |

The decoded files have no comments; Rock Band PS2's raw files keep Harmonix's own comments (about 200 files),
which explain intent ("why") where our Ghidra work only shows "what".

Owner's take:

## 2. Links

| What | Link |
|---|---|
| Repository | https://github.com/hmxmilohax/milo-script-library |
| Community (from its README) | https://discord.gg/WWmsQvHSC6 |
| A DTA language implementation from the same group | https://github.com/hmxmilohax/arson (see `study_arson.md`) |

Owner's take:

## 3. How it helps our interpreters (knowledge)

| Use | How (all local, nothing committed) | Helps |
|---|---|---|
| **Builtin coverage** | Count every call head per build; subtract our builtin tables and object classes; list the rest by count | gd05 now (the five above), RB2 / RB3 / TBRB before they start |
| **Handler names** | List each object class's handlers the scripts call (`{ui ...}`, `{game ...}`, `{beatmatch ...}`) and diff with what our classes answer | gd02 / gd05 audits (`docs/audit.md`): find handlers the walks never reach |
| **Parser check** | Decode our own disc's DTB with our parser, print it as text, diff with the same build's text here (GH2 360 final, RB 360 base) | An objective test of `parseDtb` / `parseDataArray` on every file, not only the ones that load |
| **Title updates** | RB1 tu5 adds `with` in more places and new UI; we do not own the update | Know what the retail disc lacks (our ports follow the disc) |
| **Prototypes** | GH1 / Encore `deluge`, GH2 PS2 OPM demo | Cut-content leads (gd01 / gd03 Enhanced "cut content", if wanted) |
| **Harmonix's comments** | RB PS2 raw DTA | The intent behind rules we decode in gd05 (scoring, overdrive, crowd) |
| **Next games** | RB2 (gd08), Beatles (gd09), Lego (gd13), Green Day (gd16), RB3 (gd18) | Size the interpreter work before a kickoff: new builtins, new classes |

Owner's take:

## 4. Feature ideas

- A **"script coverage" developer report** per game: builtins and handlers the disc's scripts call vs what the port
  answers, from the disc only (no library needed at run time). The library is how we validate that report once.
- GH2 360 DLC scripts show how the DLC song lists hook into the game: useful for GDALL-042 (foreign songs in
  another game's list), read on the owner's own DLC if he has any (GD02-066 is dropped, so no work now).

Owner's take:

## 5. Things to avoid

- Committing any file, excerpt, generated table or test fixture from it.
- Treating it as the truth: a decoded file can differ from our disc's build (region, update). Every finding is
  checked on our disc before code changes.
- Running anything from it (there is nothing to run; keep it that way).

Owner's take:

## Suggested backlog items

| ID | Game | Class | Description | Effort (agent h) | Value |
|---|---|---|---|---|---|
| NEW-1 | gd05 | Original | Add the missing builtins `with`, `sort`, `search_replace`, `\|=`, `&=`, `'~'` (semantics confirmed in RB's XEX), with scripttest cases | 2 | High |
| NEW-2 | gd05 | Bugfix | Check whether the characters' arm-flag handlers (`set_leftarm_true` ...) run in our port and failed on the missing bitwise builtins; link to GD05-044 | 1 | High |
| NEW-3 | gd02 / gd05 | Original | Parser check: our DTB decoder printed as text vs the same build's decoded text, every file, run locally (private corpus path in an env var, skipped when absent) | 2-3 | Medium |
| NEW-4 | gd01 / gd02 / gd03 / gd05 | Enhancement | A `--scriptcoverage` report from the disc: every call head and handler in the game's scripts vs what the port implements | 2-3 | Medium |
| NEW-5 | gd08 / gd18 (future) | Original | Before each RB kickoff: size the interpreter delta (new builtins and classes) from the library, written into the kickoff | 1 per game | Medium |

Owner's take:
