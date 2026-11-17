# gd-game-24: open questions

For the owner unless marked "agent". Answered ones keep the answer.

1. **Plan approval.** Answered 11-17: approved, start M0-M2 now (GD24-002).
2. **Highway look for the first boot.** Answered 11-17: **wait for the real
   one**. No debug highway: the first boot shows the original's own
   highway, decoded from the disc and the executable (look, lanes, gems, how
   it is drawn). Decoding it moves into GD24-002 (D5).
3. **Launcher.** Answered 11-17: recognise PSP discs (ULUS-10418) with the
   first boot, as `.iso` and `.chd` (gd-engine-core's libchdr reader,
   GDUL-030; PSP CHDs from chdman createdvd / createcd). The game stays
   `ready_to_play: false` until it plays. Never ship a launcher without a
   module, or one that cannot load ISOs (Android, Windows).
4. **Controls.** Answered 11-17: the PSP buttons mapped on the gamepad (the
   real mapping from the code), the GAMEPADS screen like every game, and
   guitar controllers too (frets play the lanes).
5. **DLC.** Answered 11-17: out of scope.
6. (agent) Exact chunk load addresses of each STR kind, from the stream
   loader instead of the pointer-density estimate.
7. **Song audio.** Answered 11-17: the `.ATM` 5-track audio is encrypted;
   no protection is broken, so it is out of scope. Songs play their
   unencrypted `<SONG>_PREVIEW.AT3` from the player's disc for now.
8. (agent) The highway: which heap image holds it (COMMON / GLOBAL_BINARY
   [I]), its classes, textures and how the gems are drawn.
