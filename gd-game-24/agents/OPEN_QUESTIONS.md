# gd-game-24: open questions

For the owner unless marked "agent".

1. **Plan approval.** Milestones and estimates in `PROGRESS.md`; the first
   boot (GD24-002) = one song's chart with its 5 stems on a 4-lane highway,
   autoplay, no menus. OK to start?
2. **Highway look for the first boot.** The original's track is a 3D object
   in the heap images (not mapped yet). For GD24-002: a labelled debug
   highway (like gd10's first boot) and the real one in M5, or wait for the
   real one?
3. **Controls.** On the PSP the four lanes are L / Up / Triangle / R by
   default [I] and other buttons switch tracks [to confirm from the code].
   Map them to the gamepad's shoulders + D-pad + face buttons by default,
   with the GAMEPADS screen as in every game?
4. **Launcher.** The launcher recognises Xbox 360 and PS2 discs; a PSP UMD
   ISO needs a new `iso_signatures` format (`psp_disc_id: ULUS-10418` from
   `UMD_DATA.BIN` / `PARAM.SFO`). Add it with the first boot, or later?
5. **DLC.** The disc holds one encrypted store song (`CANARY.EDAT`, "Just a
   Girl") and the code reads `ms0:/PSP/GAME/.../MUSIC/*/BEATMATCH.str.EDAT`.
   Out of scope (needs licences), agreed?
6. (agent) Exact chunk load addresses of each STR kind, from the stream
   loader (FUN_08a2596c area) instead of the pointer-density estimate.
7. (agent) The ATM cipher (FUN_0888ec50 / FUN_0888fa1c) and the stem order.
