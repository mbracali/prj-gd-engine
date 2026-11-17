# gd-game-24: decisions

Each entry: what, why, how to undo.

## D1. Discovery only until the owner approves the plan (2026-11-17)

What: GD24-001 produced the rip, the decrypted EBOOT, the Ghidra export,
`docs/FORMATS.md` and the plan in `PROGRESS.md`; no engine code yet.
Why: the dev wave rule ("the owner approves the plan first").
Undo: n/a.

## D2. EBOOT decrypted locally with pspdecrypt (2026-11-17)

What: the owner approved decrypting the EBOOT "like we did with the other
games". pspdecrypt was built outside the repository; the decrypted ELF is in
the git-ignored `ghidra/`. The engine will never need it.
Why: the behaviour comes from the executable. Undo: delete `ghidra/EBOOT.prx`.

## D3. Ghidra with kotcrab's ghidra-allegrex extension (2026-11-17)

What: release v21.4 (built for Ghidra 12.1.4) installed into the local
Ghidra's `Extensions/`; NID names from PPSSPP's tables
(`tools/prx_imports.py` + `NameStubs.java`); extra functions from
`FindPspFunctions.java`.
Why: the stock MIPS language lacks the Allegrex / VFPU instructions.
Undo: remove `Ghidra/Extensions/ghidra-allegrex`.

## D4. Proposed (owner to confirm): read the heap images by class layout

What: no relocation; a `HeapImage` reader maps each chunk at its original
address and reads objects through per-class layouts identified by vtable
address, written from the EBOOT, starting with the classes the first boot
needs (song data, gem lists, tempo map).
Why: the STR files are raw RAM with absolute pointers (FORMATS.md). The
alternative, running the PSP code (an emulator / recompiler), is against the
project's clean-room approach.
Undo: the owner prefers another route (OPEN_QUESTIONS Q1).

## D5. Owner's answers 11-17: the first boot uses the original highway

What: GD24-002 = one song's chart and its 5 stems on the **original**
highway decoded from the disc and the EBOOT (no debug highway); PSP disc
recognition (.iso and .chd) in the launcher with the first boot, the game
not ready_to_play; controls = the PSP mapping from the code + GAMEPADS
screen + guitar controllers (frets = lanes); DLC out of scope.
Why: owner (OPEN_QUESTIONS 1-5). Undo: the owner decides otherwise.

## D6. Superseded: implement the player's .ATM song audio (2026-11-17)

What: the owner now requests a clean-room implementation that accepts a
song's `.ATM` file from the player's disc and returns its five decrypted
ATRAC3plus streams. The old restriction against searching for the key is
superseded. Keep previews as fallback until the implementation is complete.
Why: owner 11-17. Undo: owner changes direction.

## D7. Gameplay rules: the code's structure, the numbers inferred (GD24-003, 2026-11-17)

What: `src/game/song_play.*` follows the code's rules (FORMATS.md "Gameplay
rules": multiplier int + 1 per capture up to 5, doubled in Overdrive with 5
-> 11, gem / capture points x multiplier, misses inside the phrase reset it
after the capture timer, release after 8 bars / 11 at x2+, crowd = mean
health - failed tracks, Easy / Medium protection) [C]. The numbers come from
a tuning block the code reads at run time and that was not found, so they
are [I]: gem 25 and capture 500 points (DTA gem_value / max_bar_value),
health 0.6 at the start, -0.04..-0.07 per missed phrase (DTA), +0.02 per hit,
+0.06 per capture, an unplayed phrase on another track costs the same as a
miss (once per phrase), red under 0.3, blinking under 0.15, crowd penalty
0.15 per failed track, capture timer 1.5 s, an Overdrive run +1/4 meter, a
full meter 32 beats.
Why: the owner wants the original's rules; the structure is known, the
values are not. Undo: replace `Rules` (song_play.h) when the tuning block's
source is found.

## D8. Phrases are 2-bar windows; the song fails at an empty crowd meter [I] (2026-11-17)

What: a phrase = a track's gems inside bars [2k, 2k + 2) of the song's own
time signatures (the DTA's `(phrases (finder (bar 2)))`); the indicator
takes the first phrase that has not started when the player gets there,
moves to the next after a capture or a miss; a wrong press during the
phrase fails it ("Go easy on the buttons!"); captured / failed tracks can't
be played (their gems play on their own / are lost); switching never skips
tracks. The song fails when the crowd meter reaches 0. Overdrive rescues
failed tracks (health back to 0.6).
Why: the PhraseFinder / BarFinder code was not followed to the end; the
tips describe the behaviour. Undo: when the finder's rule is decoded.

## D9. Our own HUD, Phrase Indicator frame and results screen for now (2026-11-17)

What: the song's bottom bar shows score, multiplier, phrase streak, Energy,
crowd and the four healths; the active track's phrase is framed in orange
(beat-bar strips); tracks are tinted (captured dim, danger red / blinking,
failed dark red); the song ends with its preview (the full audio is not
played) and shows a results panel with the original's numbers (score,
longest streak in phrases, each instrument's phrases captured / attempted,
notes, best multiplier, Overdrive). The original's HUD / results UI (UI_*.STR)
is GD24-004.
Why: the original's UI is not decoded yet. Undo: GD24-004.
