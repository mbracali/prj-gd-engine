# Similar behaviours: patterns every game shares

Behaviour that is the same in every gd-game, gathered from gd-game-01 (the
reference) and gd-game-02. A new game starts with all of it; a game that
breaks one of these has a Bugfix item in [gd-backlog.md](../gd-backlog.md).
Where the original game itself behaves differently, the original wins and the
game's docs say so. The screens of the port itself (Enhanced menu, GAMEPADS,
mouse, touch) are in [universal_launcher.md](universal_launcher.md).

## In a song

- **Nothing counts before the fretboard is live.** Controllers are live only
  once the song's highway is up (gd-game-01: from song time −25 ms, as the
  original's GamePanel; frets already held are synced, not pressed). A
  player can never miss, fail or hear a miss sound before the fretboard
  appears (gd-game-01 B86).
- **The song start follows the original's order**: the venue loads, then the
  highway appears (with the original's intro, e.g. the fret buttons popping
  in with their sound), then the music starts (gd-game-02 wave B #1).
- **Back pauses** the song (Start does too); the pause menu takes the mouse
  again. Pausing pauses the song, never the audio device: the pause menu and
  the end screens keep their button sounds (gd-game-01 B41 / B44, lesson L8).
- **Frame-rate independence.** Cameras, camera filters and shake, animations,
  particles and blinking run on the original's fixed step (60 Hz on both
  games so far) at any frame rate: 60, 120, 144, 240 or uncapped must look
  the same and pick the same shots. Random draws for one system never come
  from another system's generator (gd-game-02 bug-fix 4; checked by
  `--venuetest` at 60 vs 240 fps).
- **Frame pacing.** Limit frames in flight, keep separate, eased audio and
  draw clocks, never read back from the GPU synchronously (lesson L7). The
  song clock is held back by the audio device's latency.

## Menus and text

- **Enter is Start** and **Enter finishes text entry** (band names, high
  score names); Esc / Back cancels.
- Names typed in a text entry grow **left to right** from the entry's anchor,
  as in the original (gd-game-01 B64; gd-game-02 wave B #2).
- **Esc three times** in a row on the main menu quits. Logos, splashes and
  videos skip with Enter / Esc / Start (and Green / X).
- **Held directions repeat** with the original's hold and repeat times; key
  repeat resets on screen transitions (gd-game-01 B80).
- **Menu sounds never go silent.** Every volume or mute a screen changes is
  restored by the same object that changed it (lesson L8; gd-game-01 B70,
  gd-game-02 wave B #7).
- Loading and save screens hold at least 2 s, as the original's memory card
  screens (gd-game-01 B22, M3).
- The help bar hides when something is selected and comes back on the next
  screen; its items are clickable.
- PS2 games keep their menus 4:3, centred and pillarboxed; gameplay may
  extend wider only as far as it does not break. Xbox 360 games are 16:9.

## The port's extras (the same in every game)

- **The Glitches**: a band at the bottom of Choose / Load Band in every save,
  not saved, with everything unlocked and bought (gd-game-01 X24,
  gd-game-02 bug-fix 3).
- **Cheats in the Enhanced menu**: the port's cheats (Enable Cut Content, No
  Fail Mode, Autoplay, Forgiving HOPOs) and then the original's own cheats,
  with the original's names, each saying how it is entered in the original.
  The original's button codes keep working.
- Enhanced features are **opt-in, labelled and default to the original**
  (lesson L20); every optional effect degrades to "off" instead of blacking
  out the game (lesson L5).
- Per-platform defaults live in one table (gd-game-01's `profileValues`);
  Steam Deck settings apply as a preset (lesson L9).

## Saves, settings and logs

- Saves are versioned JSON in the save folder (`career.json`), written
  atomically with a `.bak`; every setter that changes a saved value marks the
  save dirty (lesson L12). Settings: `enhanced.json`; controllers:
  `controllers.json`.
- **Log everything that is not handled**: every unhandled script message,
  stub or unknown class (lesson L1). Each game writes its log (e.g.
  `gd02.log`) and, on a crash, the crash address to the save folder; the
  launcher logs every action in `gdul.log`.
- Dev aids from day one: scripted keys, timed screenshots
  (`GDNN_SCREENSHOT`), screen jumps (`GDNN_SCREEN`), headless `--selftest`
  over every chart, `--walktest` walks (lesson L6).

## Versions and files

- `game-info.yaml`'s `guitar_dream_version` equals the engine's version; the
  game's `CHANGELOG.md` has a section per version. Bump both at PR time, and
  the launcher's `launcher-info.yaml` when the launcher changed.
- Every game folder has the same layout as gd-game-02 (`README.md`,
  `agent-instructions.md`, `CHANGELOG.md`, `local_build.sh`,
  `build_commands/`, `docs/`, `engine/`, `tools/`, `agents/`), see
  [docs/adding-a-game.md](../docs/adding-a-game.md).
- Never commit game data, rips, decompiler output or Ghidra files
  (`python3 .github/scripts/repo_guard.py`).

The lessons L1–L21 are in
[gd-game-02/docs/reuse_and_lessons.md](../gd-game-02/docs/reuse_and_lessons.md) §3.
