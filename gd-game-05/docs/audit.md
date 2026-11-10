# gd-game-05 audit (wave 01)

What the port runs of Rock Band's own data, what it stubs and what is
missing, as the tests count them. Facts from the disc are [C data], from
the executable [C RB 0x...], the port's guesses [I].

## Scripts (`gd05 "$ISO" --scripttest`)

- `config/band.dta` boots: 212 files, 186 `#include`, 25 `#merge`, 1720
  `#define`; every one of the 291 `.dtb` files loads on its own, 0 directives
  left over. 22 includes name files that are not on the disc, all in tool
  and test configs the game does not load.
- RB's `band.dta` has `(ui (init ...))` and no `(system (init ...))`.
- Run of the boot tree: 0 script errors, 0 tasks pending.

## Menus (`--walktest quickplay`, `--walktest keys`)

Both pass: intro movie, title, main menu, Solo, Quickplay, the song list,
the difficulty screen (RB's player slots), the loading screens, the song in
its venue, `endgame_waiting_screen`, `qp_endgame_screen`. `keys` does it
with Enter and the arrows through SDL's events.

STUBs the walks accept (`engine/tests/ui_stubs.txt`, each with its reason):
the characters' `available_prefab` (M6), the solo score and the profiles'
save data (M7, DECISIONS D4), practice's vocal mode (M7), the crowd
characters' enter and clip groups (M5), Xbox Live services.

## Components the scripts name (`--uitest`)

FAILED in wave 01: 42 names a panel's script uses are not in that panel's
milo. Most are shared macros naming another panel's button (`solo.btn` in
`main_solomode_panel`, `multi.btn` in the band menus), labels of endgame
panels the port has not walked (`rank0.lbl`, `score.lbl`, `stars.lbl`), and
two the PS3 milo has (`community_ps3.milo`: `checkinvites.btn`,
`managefriends.btn`). Each needs a look: a name the original also misses is
harmless (its `find` returns nothing), one the port fails to find (a proxy
or an inline directory not searched) is a bug. Open (GD05-005).

GH2's career checks (`campaign`) are not run for RB; RB's tours are M7.

## Natives

The host objects RB's scripts call are in `engine/src/ui/rb_natives.cpp`
(gamemode, session, platform_mgr, profile_mgr, memcardmgr, band_ui_sink,
tour, char_cache, content_mgr, session_mgr, song_mgr, song_provider, the
stand-in characters), `ui/natives_game.cpp` (game, gamecfg, player
configs, players, beatmatch) and `ui/rb_multifocus.cpp` (player slots).
Messages answered with a fixed value carry an `[I]` comment saying so.
