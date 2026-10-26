# GH2's highway and HUD: how the game drives them

How Guitar Hero II places and animates its track (`track/gen/track_new.milo_xbox`)
and its single-player HUD (`hud_sp`), as gd02 reproduces them in
`engine/src/render/track_view.*` and `hud_view.*`. Addresses are in
`default.xex` (`analysis/`); **[C]** confirmed in code, **[I]** inferred.
The scenes themselves are listed in `milo_and_textures.md`.

## Track

- **Scrolling** [C]: a gem's y = ms × 0.06 × track_speed (`0x82699D50`). At
  init, y_per_second = 60 × track_speed (`0x82363328` from `0x826967B8`);
  track_speed is the difficulty's value (`0x826932D0`), × 1.5 with the
  hyper-speed cheat. TrackDir::Poll (`0x823634D0`) sets `anim.grp` to frame
  now_y (the surface scrolls); gd02 draws everything relative to the now bar,
  which gives the same picture.
- **Gem lifetime** [C]: added while y < now + 120, freed at end_y < now − 15
  (`0x826989A8`); widget dropped at y + base_length < now − 10 (`0x823662B0`);
  x = the TrackDir slot (`0x82363A88`). A hit gem loses its widget at once
  (`0x8269E2C8`); a passed gem switches to the miss widget (`0x822E2FD0` →
  `0x82695AA0`).
- **Widget choice** (`0x826912E0`) [C]: normal and bonus gems use the gem or
  HOPO list (index 5 bonus, 6 miss, else the slot); star gems use the star or
  star-HOPO list (5 when missed). HOPO look = chart HOPO flag and index > 0
  (`0x82699348`). Star = inside a phrase with the missed latch clear
  (`0x82697620`). `set_bonus_gems` (`0x82697D30`) turns live, unmissed gems
  into bonus gems when Star Power starts.
- **Sustain tails** [C]: template `0x826A0100` (30 segments of
  (120 + 15) × 1.01 / 30), mesh `0x8269C2A0`, rebuild `0x8269EAD0`. Clip 2.0;
  while held, now + 1.5 − start; frozen on release (`0x82697710`). Whammy
  history entry = bar × −3.0, moving 2 units per frame. Materials and glow:
  `0x8269E7F0`, `0x8269E2C8`, `0x8269E340` (`tail_white` / `tail_purple` are
  never used).
- **Smashers** (`0x8269D110`) [C]: the pressed material follows the held fret;
  `gem_smasher_hit.tnm` on a hit (`0x8269CF00`), `gem_smasher.tnm` on a pop or
  miss (`0x82697958`); effects run at 30 fps.
- **Effects** [C]: smash views `0x8269B058` / `0x8269C968`; burn state frames
  10 / 20 / 0 (`0x826A0868`); beat lines `0x82699F40`, `0x8235C890`; side
  rails `0x8269A2F8`; lightning only during Star Power (`0x82695AF8`);
  blow_streak (surface flash + shake when a ×2+ streak ends) `0x82696560`.

## HUD

- HudPanel poll `0x8268E670` [C]: the streak shown is 10 above 29, else
  streak % 10 with exact multiples of ten shown as 10.
- UI camera (UIManager init `0x8236C4C0`) [C]: fov 34.516°, at (0, −768, 0),
  near 250, far 1000. The HUD milo's own `Cam.cam` is unused [I: merge order].
- Load (`0x82690308`) hides `youwin.mesh` and the masks [C].
- Score display `0x8212AE68` / `0x8212B630` [C]: right-aligned, no leading
  zeros, 6 digits.
- Streak display `0x8212EBE0` / `0x8212EEE8` [C]: LED tier = (Star Power ?
  mult / 2 : mult), − 1 when the row is full, clamped 0..4, 4 → 3 outside Star
  Power; colours from `score_colors.mnm` (`0x8212EB58`).
- Star meter `0x82131708` [C]: shown value eases at 0.5/s, view frame = value
  × 100, "ready" at ≥ 0.5 or while in use.
- Crowd (rock) meter `0x82139010` [C]: eases at 0.9/s, needle frame = value ×
  100, warning under 0.15.
- Messages (`STAR POWER READY!`, `SONG FAILED!`) `0x826908D0` [C]: 3 s,
  `rokk.font`, animated by `message0.tnm`.

## Simplified or inferred in gd02

- Particles are not drawn yet (smash sparks, `tail_bonus.part`, star-meter
  sparks); only the flame-light meshes of the smash and burn views show.
- Not done: the intro gem sparkle, `peak_on` (rock meter peak flames), the
  track-fail explode, the far-end fade (`track_fade.tex` is a runtime render
  target).
- Whammy bend scaled by a further 0.2 [I]; miss pop on the held frets [I];
  side-rail animation indices [I]; sphere tex_gen skips the camera half-angle
  step [I]; MatAnim / TransAnim SetFrame semantics follow GH1 [I].
- Two-player tracks (`0x82334628`, `0x822B37D8`) not read yet.
- Mesh winding: GH2's visible faces are counter-clockwise about the normal,
  so `cull` maps to culling GL's counter-clockwise screen faces (the Y flip
  between D3D and GL screen space).
- The texture transform is built from tex_xfm per tex_gen as Mat setup
  `0x8230A358` does [C].
