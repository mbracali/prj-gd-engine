# gd-game-03 open questions for the owner

Nothing blocks on these; each has a default written in DECISIONS.md or the docs.

## Q1. Aspect ratio
The PS2 original draws 4:3 (with a widescreen option in its video settings
[I]); the port renders 16:9 like gd02. Default kept: 16:9. Should gd03
pillarbox to 4:3 like gd01?

## Q2. Track far-end fade
The original lays the venue strip back over the track's far end
(track_fade.trans); the port ends the track with a hard edge until a frame
copy exists in the renderer. Priority?

## Q3. Goldens
The director, band, crowd and MIDI goldens are this disc's first baselines
(the port's own output), not checked against the original. Is a capture
from an emulator (PCSX2) wanted to verify them?

## Q4. Mono
The audio settings' mono folds left and right at half each after the mix
(the console's exact fold is not traced).
