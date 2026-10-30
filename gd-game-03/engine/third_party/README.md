# Third-party code

The disc's audio (VGS and sound-effect samples, SPU2 PS-ADPCM) and movies
(PSS: MPEG-2 video) are decoded by the project's own decoders, copied from
gd-game-01. Besides SDL3 (zlib-licensed, fetched by the build scripts), the only
third-party code is:

| Folder | What | Version | Licence |
|---|---|---|---|
| `rtmidi/` | RtMidi (MIDI input: ALSA, WinMM, CoreMIDI), unchanged, copied from gd-game-02; built by its own `CMakeLists.txt`; the Generic MIDI input of the Gamepad menu (`src/input/midi_in.cpp`); not built on Android (MIDI shows as unavailable) | 6.0.0 | MIT (`rtmidi/LICENSE`) |
