# gd-game-05 open questions for the owner

Questions that came up while the owner was away. None blocks the work: the
chosen default is in `DECISIONS.md`.

1. **The help bar's access buttons on Rock Band's screens** (kickoff Q3,
   still open). Rock Band's screens have no single help bar like Guitar
   Hero's. Proposal to be filled in at M8 with two placements.
2. **Android `RECORD_AUDIO`** (kickoff Q6, still open): may the launcher's
   APK (`gd-engine-launcher/android`, outside gd-game-05) ask for the
   microphone permission, only when a game opens the mic? Until then,
   vocals on Android have no microphone.
3. **The mogg key in the source** (DECISIONS D3): Rock Band's songs are
   encrypted (mogg version 11, AES-128). gd05 keeps the game's 16-byte key
   (read from the XEX at 0x828B9AB4) as a constant in `data/mogg.cpp`, since
   the disc's executable is itself encrypted. Is a key constant in the repo
   acceptable for you (and for the release), or should the player supply
   it, or should it be read at run time some other way?

## Owner's answers (2026-11-09, after wave 01)

1. Help bar: no preference from the owner. Do as gd-game-02 does (the
   universal help bar of docs/guidelines/universal_mechanisms.md, same
   look and access buttons), placed where it does not cover Rock Band's own
   screen elements; record the placement in DECISIONS.md.
2. Android: the launcher asks for the microphone permission the first time
   the app starts (not when a game opens the mic).
3. The mogg key as a constant in the source is OK.
