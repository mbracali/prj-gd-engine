# Guitar Dream Engine - Game 5
id = `gd-game-05`

**Generals about Guitar Dream Engine, approach on re implementations**:
- Don't contain any original code.
- Don't contain any original asset.
- Don't distribute any original asset.
- If you want to play, buy the original media.
(Support the industry)


**This folder contains the re implementation of**:
- **Game**: Rock Band
- **Version**: Xbox 360 (USA, title ID 45410829)
- **Launched**: Nov/2007 (Harmonix / MTV Games / Electronic Arts)
- **Musics**: 58 on the disc (45 in the main set list plus 13 bonus songs; `songs/gen/songs.dtb`), plus the tutorial, Drum Trainer and vocal lesson tracks
- **Venues**: four kinds of stage (small club, big club, theater, arena), 41 venue sets (`world/`)
- **Chars**: the character creator (male and female, 13 outfit slots each) and 74 premade band members (`config/gen/characters.dtb`)


**In-game instrument support**:
- [X] Guitar
- [X] Bass
- [X] Drums
- [X] Vocals
- [ ] Keys
- [ ] Real guitar/bass (MIDI)
- [ ] Real drums (MIDI)

**In-game features**:
- [X] Single player
    - [X] Carrer
    - [X] Quick play
- [X] Two players
    - [X] Carrer
    - [X] Quick play
- [X] Band play
    - [X] Carrer
    - [X] Quick play
- [X] Unlocks and/or in-game shops (the Rock Shop)
- [X] Character creation
- [ ] Music creation


**Enhanced features by Guitar Dream Engine**:
- Higher resolutions.
- Higher framerates.
- AI upscalers.
- Image enhancers.
- Rich image filtering.
- Easy cheat codes.
- Cut content restored.
- Extended gamepad support.
- Mods support


**Status**: wave 01 in progress (see `agents/PROGRESS.md`). The plan is the
kickoff, [docs/spikes/GD-GAME-05_KICKOFF.md](../docs/spikes/GD-GAME-05_KICKOFF.md).
The engine starts from Guitar Hero II's (gd-game-02): Rock Band runs on the
same Harmonix engine family, one generation later.

**Running**: you need your own Rock Band (Xbox 360) disc image (`.iso`); no
game data is included. `gd05 <disc.iso>`; the tests and the developer aids
are in `agents/CONTEXT.md`.

**Folder layout**: as gd-game-02 (`engine/`, `docs/`, `tools/`,
`build_commands/`, `local_build.sh`, `agents/`; the git-ignored `extracted/`,
`ghidra/` and `local_builds/`).
