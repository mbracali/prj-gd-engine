# Guitar Hero: Metallica bug fixes 2026-11-16 (owner, branch `feature/20261116-metallica-bugfixes`)

Saved here so it survives context loss.

## Owner's list

| Row | Item |
|---|---|
| GD10-062 | Menus still misaligned; the button legend (green, yellow... buttons) overlaps and sits out of place |
| GD10-063 | During the song the HUD widgets are missing |
| GD10-064 | The crowd does not move during gameplay |
| GD10-065 | The band moves well, but members walk into each other, and in some set pieces (especially Metallica) walk past the venue |
| GD10-066 | A screen effect mirrors the band upside down (an effect on the screen, not the band) |

## Rules

- One build-heavy agent at -j4; a second may build in its own dir at -j2 after checking `free -g`. One game binary each at a time.
- Quick checks only (short runs, screenshots the lead and agents look at); every behaviour change has a `GD10_*_LEGACY` switch and an objective check that fails before and passes after.
- No version bumps, no PR unless asked.

## Plan

1. Agent UI (devv, -j4): GD10-062, GD10-063.
2. Agent world (own dir, -j2): GD10-064, GD10-065, GD10-066.
3. Lead: packages, launcher, the owner's test list.

## Progress

