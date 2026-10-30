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
Start, a click or a tap. The visual effects are gd-game-02's: HDR (None /
Emulated / True HDR), Bokeh, Particles, Bloom, Cinematic Color, Color
Grading (Sepia among the looks), Film Grain, Chromatic Aberration, Ambient
Occlusion, Sharpening, Blur on Star Power, Band Shadows, More Lights, Dust
in Light Beams, Disable Shaky Camera and Remove All Effects, with Sharper
Text & Sprites, FXAA / SSAA / TAA, FSR 1, touch controls, Audio / Video
Calibration, No Fail, Autoplay and Forgiving HOPOs; they work in 4:3 and
16:9. Every download starts with the original look (Original Feel: every
effect off, HDR None). The cheats include Encore's own Horse Head and a
burning Flaming Head (0.0.3). Still greyed: Enable Cut Content (this disc
has nothing unused to restore), FSR 3.1 / DLSS.
The default controls are in `engine/README.md`.

**Menu animations**: as on the PS2, each screen change moves the menus'
shared camera out of the old screen and into the new one (half a second
each; going back, the other way) while the screen fades through black, the
focused button grows to its focused size over a tenth of a second and its
letters jiggle, and the screens' looping animations (the character
select's highlight, the store's spinning video case) loop.

**0.0.3**: held sustain tails swell with the whammy bar as on the PS2 (up
to four times as wide, the bulges running down the tail); tutorials 2 and 3
complete (the hammer-on / pull-off trainer); career tour wins; practice
pitch correction; the crowd meter holds where the mode turns it off; mono
as the PS2 (every sound panned to the centre). Status per area:
[PARITY_GAPS.md](../PARITY_GAPS.md); walks: `engine/src/tests/walktest.h`
(`whammy_tail`, `flaming_head`, `tutorial2`, `tutorial3`, `career_win` ...).
