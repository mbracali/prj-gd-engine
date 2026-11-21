# Kickoff — Guitar Hero: Metallica (gd-game-10)

> **Starting or resuming this reimplementation? Read this first.** The binding
> rules are in [`docs/guidelines/`](../docs/guidelines/) (`general_guidelines.md`,
> `universal-*.md`, `engine-backend-guidelines.md`) and the single backlog
> [`docs/tasks/gd-backlog.md`](../docs/tasks/gd-backlog.md). When this file
> conflicts with the guidelines, the guidelines win.

## ⚠️ Recurrent bugs — guard against these from day one

Full list with how-to-catch: [`docs/guidelines/usual-bugs.md`](../docs/guidelines/usual-bugs.md).
These have bitten us on more than one game — **they must never ship again**:

1. **Launcher ships without this module.** Every build must carry `gd-game-10`'s
   module; the build ends with a `verify: … complete` line per platform. Never
   ship a package without it.
2. **Launcher can't load ISOs / CHDs** (Windows wide paths, Android content URIs).
   Open discs through `RandomFile::open` / SDL, never a bare `fopen` of a UTF-8
   string on Windows. Test **Load ISO and a CHD** on every platform.
3. **Band, characters, menus or venue too dark / near pitch-black.** The single
   worst recurring class (Aerosmith was near pitch-black). **Never "fix" darkness
   by raising brightness/gamma/exposure** — the Xbox 360 gamma ramp is validated
   against a real console capture. Find the real cause: the character shader's
   lighting registers must be driven by the venue's group lights, and the band
   must **cast** its shadow without **receiving** its own. Compare face/menu luma
   **objectively** against an original capture (Xenia is authorised per game),
   never by eye.
4. **Physics on hair, cloth, accessories.** Colliders on, no penetration into the
   body, gravity/response matching the original. No clothes clipping skin.
5. **Menus & fonts.** With the mouse enabled, **every** option and legend in
   **every** menu (main, pause, song-select, options, modal prompts) must be
   clickable. Menus framed inside the 16:9 safe area. Use the game's **own**
   designed font — never a generic fallback.
6. **Black frames between screens** and **micro-stutter on load** — see usual-bugs.md.

**Golden rules:** match the original exactly; use objective expected-vs-measured
checks (data-derived), never eyeballing; work only in this module's lane; leave
versions, PRs and merges to the owner.

## This game

| | |
|---|---|
| Name | **Guitar Hero: Metallica** |
| Folder / program | `gd-game-10/` → `gd10`, env prefix `GD10_` |
| Quick test | `tools/quicktest.sh gd10` (selftest + short walks) |
| Local build | `gd-engine-launcher/scripts/build.sh linux --only 10` (one game, fast) |

Detailed plan & disc findings: [`docs/spikes/GD-GAME-10_KICKOFF.md`](../docs/spikes/GD-GAME-10_KICKOFF.md).
