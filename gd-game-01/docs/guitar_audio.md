# Guitar audio: miss mute and the face-off mix (UnmuteBeatMatchAudio)

[C] = read in the decomp, [I] = inferred.

## Objects

- `beatmatch.dta (audio (unmute (multiplayer_scale 0.33) (multiplayer_attenuation 1.5) (multiplayer_common_attenuation 1.5)))`
  selects `UnmuteBeatMatchAudio` (`FUN_00257270`; `slipstream` is the cheat alternative) [C].
- One audio object **per PlayerMatcher**, so one per player (PlayerMatcher ctor in `00100000.c:5329` passes the
  player index and the player count) [C]. Its `+0x54` member (`FUN_002662b0` ctor, `FUN_002665c0` setup) is that
  player's **own instance of the song stream** (`synth` vtable `+0x124`) with the song's `pans` / `vols` / `cores` [C].
- `UnmuteBeatMatchAudio_v22` (init from the song): for each `slip_tracks` entry (GH1: one, `(2 3)`), each channel
  gets an entry `{owner, channel, vol, f3 = 1, f4 = 1}` (`FUN_002654c8`) and a SlipTrack (`+0xe4`) [C].

## Miss mute = the slip voice (sink callbacks) [C]

Sink slot offsets are `8n + 0xc` in vtable `0x2fd648`.

| watcher event | sink | Unmute | effect |
|---|---|---|---|
| hit (`FUN_00264120`) | 0x24 | `_v3` | SlipTo 0 (heard, in sync), hit state `+0xf4 = 1`, `+0xf8 = gem` |
| own gem's time reached (`FUN_00263db0` → `FUN_002645d0`) | 0x14 | `_v1` | if hit state: SlipTo 0 |
| miss / overstrum (`FUN_002641e0`) | 0x2c | `_v4` | StopSlip (silent), hit state 0 |
| own gem passed, live (`FUN_002642e8`) | 0x34 | `_v5` | StopSlip, hit state 0 |
| opponent's gem passed (`FUN_00264660`; at the gem's own time, no slop) | 0x3c | `_v6` | StopSlip, **hit state kept** |
| gem passed before the start point (`FUN_002643a0`) | 0x44 | `_v7` → `_v3` | as a hit |
| sustain let go: a fret released or a higher one pressed (`FUN_00263578` / `FUN_002634f0` → `FUN_00264018`) | 0x9c | `_v18` | if gem == `+0xf8`, the gem has a sustain (flag bit 9 clear) and time is left: StopSlip, hit state 0 |

StopSlip frees the IOP slip voice (`FUN_0000321c`): the guitar copy is silent until the next SlipTo, i.e. the
next hit (or, after an opponent's section, the next own gem's time while still in hit state).

## Channel volume (`FUN_002660c0`) [C]

`vol * (+0xdc ? f3 : 1) * (+0xe0 ? common_attenuation : 1) * f4 * (+0x10)`

- `f3` (`_v28`, from `FUN_00257dd0`): 1.0 for the active slip track, else `levels[count]`; GH1 has no `levels`, so 1.0.
- `f4` (`_v29` / `_v40`): the BeatMatchAudio volume `+8` (player_matcher `set_volume`).
- `+0xe0` (`_v4` of vtable `0x2fd610`, a gem-list listener, `FUN_00258b20` slot 0x2c): active players == 0 or 3.
  It only sets the flag: the volume is **recomputed at the next set_volume**, so the common attenuation is latched then.
- `common_attenuation` = `+0x54+0x74` = 10^(-1.5/20).

## Face-off [C]

- `FUN_002665c0` (player count != 1): `scale = multiplayer_scale`, `atten = 10^(-multiplayer_attenuation/20)`,
  `offset = scale - 1` (player 1) / `1 - scale` (player 2).
- `FUN_00267d08` (each guitar channel): `pan = pan * scale + offset` (player 1 left, player 2 right),
  `vol *= atten`.
- `FUN_00267b08` (player 2's stream only): every non-guitar channel at volume 0. Player 2's stream is a second copy
  of the guitar channels.
- Each player's misses, passes and whammy (`_v31`) act on their own copy only.

## Port

- `Mixer::setGuitarCopies(count, scale, atten)`: one copy (single player) or two (face-off); each copy has its own
  gain (StopSlip / SlipTo), volume, whammy slip speed, lag and resync. The guitar channels are summed once per copy
  with the copy's pan transform.
- `Player`: `hitState_` / `hitStateGem_` / `reached_` and the table above (`slipTo()` / `stopSlip()`, `slipTos()`).
- `App::pollPlayer`: a change in `slipTos()` resyncs that player's copy; each frame sets each copy's audible and
  whammy speed. `App::setupGuitarCopies` reads beatmatch.dta at song start. `hooks.guitarVolume` latches the common
  attenuation (`guitarCommon_`: single player from the first gem, face-off from `activeMask_`).
- [I] The mute fades over ~8 ms (the PS2 frees the voice outright); kept to avoid clicks.
- Autoplay presses the next gem's frets 1 ms before it and holds sustains to their end, so it never trips `_v18`.
