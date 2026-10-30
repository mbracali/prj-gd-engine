# Guitar Dream Engine - Game 3
id = `gd-game-03`

**Generals about Guitar Dream Engine, approach on re implementations**:
- Don't contain any original code.
- Don't contain any original asset.
- Don't distribute any original asset.
- If you want to play, buy the original media.
(Support the industry)


**This folder contains the re implementation of**:
- **Game**: Guitar Hero Encore: Rocks the 80s
- **Version**: Playstation 2
- **Launched**: Jul/2007
- **Musics**: --
- **Venues**: --
- **Chars**: --


**In-game instrument support**:
- [X] Guitar
- [X] Bass (partial)
- [ ] Drums
- [ ] Vocals
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
- [ ] Band play
    - [ ] Carrer
    - [ ] Quick play
- [X] Unlocks and/or in-game shops
- [ ] Character creation
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

**Universal menus** ([universal_mechanisms.md](../universal_mechanisms.md),
as gd-game-01 and gd-game-02): the GAMEPADS screen (the yellow fret on the
main menu, or Enhanced Options > Gamepads) with four player boxes (the game
has two players), each player's instrument (5 Fret Guitar, Gamepad, Generic
MIDI) and device, and Customize inputs (two buttons per control, Require
Strum, the controller type, Save / Discard, Export / Import); Enhanced Options
with MODES presets (Steam Deck, Original Feel, Clean Image, Max Out (Full HD /
4K), Rocking on a Budget, Good Old Days), CONTROLS, PERFORMANCE & GRAPHICS
(with Widescreen (16:9): the game is 4:3 by default, as on the PS2) and
CHEATS & ENHANCERS, a scrollbar and Back / Export / Import; both open at once.
The help bar's access buttons (SELECT, BACK, START, UP / DOWN, all
clickable); every logo, movie and the credits skip with green, red, Enter,
Start, a click or a tap. The visual effects of the other games are listed
but not in this version yet (greyed); every download starts with the
original look (Original Feel). The default controls are in
`engine/README.md`.

**Menu animations**: as on the PS2, each screen change moves the menus'
shared camera out of the old screen and into the new one (half a second
each; going back, the other way), and the focused button grows to its
focused size over a tenth of a second.
