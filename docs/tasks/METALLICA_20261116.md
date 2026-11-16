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

- GD10-064 (world agent): the crowd's dances live in perm_anims_in_game.pak, never read; now read (D112). Peds' farthest bone 0.000 -> 0.130 m per 100 ms. Owner: any song; the crowd cards should dance (GD10_CROWD_ANIMS_LEGACY=1: the old still crowd).
- GD10-065 (world agent): Band_PlayClip clips play on their startnodes (97 Metallica clips on drummer_start) (D113). z_mop, six Metallica songs: > 1 m past the front 3395..6964 samples -> 0..51; pairs < 0.5 m 48..341 -> 0..6. Owner: Battery or Master of Puppets in a Metallica venue (GD10_BAND_STARTNODES_LEGACY=1: the old walk).
- GD10-066 (world agent): the upside-down band was in the smoke: soft particles faded by the mirrored depth row (D114); local, opengl3, vulkan. Owner: a smoky Metallica song (Battery, z_mop), close shots of Lars / James (GD10_SOFT_DEPTH_LEGACY=1: the old look).
- GD10-062 (UI agent): the help bar's button glyphs sat 10.9 px over their pills (a \b glyph not centred on its text's line as 0x82454C78 does); now 0.8 px on every menu the menus / qp_pause walks reach, checked by the walks (D115). Owner: main menu, Settings, set list, pause: the green / red / yellow / blue buttons sit in the middle of their pills (GD10_HELPER_LEGACY=1: the old look).
- GD10-063 (UI agent): gh4's own HUD (setup_band_hud's hud_1g + AttachHudWidget) replaces GH3's (most of its images not on the disc) for one player: rock meter on the left, multiplier and streak lights by the highway, score / stars / streak counter on the right (D116). quickplay walk check:hud: 112 of 112 elements, 35 of 35 drawn, score / needle / nixie as the player's. Owner: any one-player song (GD10_HUD_LEGACY=1: the old HUD). Not yet: two-player HUDs, gh4's fire-style streak / Star Power messages.
