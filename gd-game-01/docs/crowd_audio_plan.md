# Crowd audio: original vs gd01, gap list

This audit covers every crowd and ambient sound during gameplay. It builds on `docs/audio_sfx.md` §4 ("Crowd audio is streamed") and adds the CrowdAudio internals.

Confidence tags: **[C]** confirmed from the decompilation or the data, **[I]** inferred.

Our code: `engine/src/audio/crowd_audio.{h,cpp}`, `engine/src/audio/mixer.cpp` (stream slot `kCrowd`), `engine/src/ui/ui_system.cpp:512-528` (`CrowdAudioObj`), and `engine/src/app.cpp` (wiring, at lines 248, 286-300, 439, 694-725).

---

## 0. Summary table

| # | item | status | priority |
|---|---|---|---|
| G1 | Crowd VGS files are 22050 Hz, but the mixer does not resample them, so they play at 2x speed, one octave up | **BUG** | P0 |
| G2 | The fx-volume option never reaches the crowd stream (`setFxVolume` has no caller) | **missing** | P0 |
| G3 | FadePanel `fade_synth` (SPU master volume) does not fade the crowd stream | missing | P1 |
| G4 | Final-song win: the `wingame` stream (WIN_FINAL enter_event) is never started | missing | P1 |
| G5 | The Boot→Okay transition plays the "rising" sfx; the original plays "falling" | wrong | P2 |
| G6 | `set_excitement` with the same file keeps the stream; the original always restarts it | differs | P3 |
| G7 | `crowd_audio set_volume / set_enabled / enabled` and `sound.dta (crowd_audio TRUE)` are not handled | missing | P3 (cheats only) |
| G8 | Global `mixer` object (`set_volume`, `set_mute`) is not implemented | missing | P3 (debug only) |
| G9 | Multiplayer: excitement comes from player 0 only, claps when any player is in SP | N/A (no MP) | later |
| OK | Excitement streams, intro, turn_off, pause, reaction one-shots, claps, SP deploy cheer, win/lose | implemented (scripts + engine) | verify by ear |

---

## 1. CrowdAudio object (original) [C]

- **Creation:** the ctor is `FUN_00111ef8`, called from ArenaPanel enter (`ArenaPanel_v13` 0x10c758) and stored at ArenaPanel+0x80. The object is deleted on ArenaPanel exit (`ArenaPanel_v14` 0x10d558), which also destroys the stream. The object exists only while the arena panel is up. GAME_PANELS includes the arena, so `post_show` still has it.
- **Fields:**
  - +8 `levelVol`.
  - +0xc `globalVol`. It is set to `Mixer.vol × !Mixer.mute` (singleton `FUN_00106ba0` "mixer", default 1.0, unmuted). CrowdAudio registers as a Mixer listener (`FUN_00106c90`).
  - +0x10 `enabled`, read from `config/sound.dta (crowd_audio TRUE)`.
  - +0x14 the stream.
  - +0x18 `started`.
  - +0x1c `excitement` (default 2).
  - +0x20 dir `"venues/%s/streams"` (with the venue name).
  - +0x38 the intro entry and +0x3c the `levels` array, from venues.dta `(sound (crowd …))`.
- **Messages** (`CrowdAudio_v1` 0x112570):

  | message | function | behaviour |
  |---|---|---|
  | `play_intro` | `FUN_00112338(intro)` | |
  | `play_stream vol file` | `FUN_001123a0` | |
  | `set_excitement n` | `FUN_001122f0` | looks up `levels[n]` and plays it; stores n at +0x1c |
  | `turn_off` | `FUN_001124b0` | Stop and delete |
  | `set_volume v` | `FUN_00112518` | sets +0xc and reapplies |
  | `set_enabled b` | `FUN_00112538` | false → turn_off; true → replays `levels[+0x1c]` |
  | `set_paused b` | `FUN_00112280` | |
  | `enabled` | | returns +0x10 |

- **Play** (`FUN_001123a0`):
  - It does nothing if `!enabled`.
  - Otherwise it **destroys the current stream immediately**: Stop, then delete (`FUN_00112130`). There is no crossfade.
  - It sets `levelVol`, then creates the stream `"%s/%s"` (dir/file) through Synth vtable +0x124.
  - It calls `SetJump(-1.2e-7, 0)` (StreamEE slot 23). This makes the stream loop from its end back to 0 [C call, I meaning].
  - It sets `started = 0`.
  - It always restarts, even when the file is the same.
- **Poll** (`FUN_00112198`): ArenaPanel_v10 calls it every frame, but **only when ArenaPanel+0x90 (= !tutorial running) is set**.
  - Its condition is that the stream is not playing (StreamEE_v5: state==4) and is ready (StreamEE_v1: state 3..5).
  - On the first ready frame it sets ch0 pan -1 and ch1 pan +1, applies the volume, and sets `started`.
  - It then calls `Play()`.
  - So a new stream starts a few frames after the request, once it has loaded, and a paused stream resumes as soon as it is un-paused.
- **Volume** (`FUN_00112470`): `levelVol × globalVol × Options.fx_volume` (Options+0x18).
  - It goes to every channel through `FUN_00213738` → StreamEE_v13 → `FUN_0021bab8`, which computes `vol*32767` and `pan*127`. The volume is **linear** [C].
  - It is only applied at stream start, on unpause, and on `set_volume`. `set_fx_volume` (`FUN_00107698`) does **not** push to a live stream [C].
- **Pause** (`FUN_00112280`):
  - b=TRUE → stream Stop (StreamEE_v4: state 4→5, keeps the position).
  - b=FALSE → reapply the volume; the next Poll calls Play, which resumes.
  - It is reached through `ArenaPanel_v7` (= GamePanel SetPaused). This covers the pause menu **and `game set_paused TRUE` in `game_outro` and in post_show enter**, so the crowd goes silent at the outro [C].
- **Tutorial:** the stream is never polled there, so the crowd is silent [C]. The venue bank is also absent, so all crowd_* sfx fail too.

## 2. Streams and levels [C]

The streams are `venues/<v>/streams/crowd_vN_{0intro,1danger,2poor,3norm,4good}.vgs`. All are stereo. The rates come from the VGS headers, read from the ARK:

| venue | files | rate | loop lengths (intro / danger / poor / norm / good) |
|---|---|---|---|
| basement | crowd_v1 | **22050** | 28.1 / 13.3 / 16.8 / 2.7 / 11.8 s |
| small_club, small_club_multi | crowd_v2 | **22050** | 21.3 / 12.9 / 15.6 / 8.7 / 11.1 s |
| big_club | crowd_v3 | **22050** | |
| theatre | crowd_v4 | intro 22050, levels **44100** | |
| fest | crowd_v5 | **22050** | |
| arena | crowd_v6 + `wingame` | 22050; wingame **44100** (19.4 s) | |

Volumes per level (venues.dta):
- Most venues: intro 0.4, Boot 0.3, Bad 0.3, Okay 0.2, Great 0.3, Peak 0.4.
- theatre: 0.3 / 0.3 / 0.25 / 0.35 / 0.45.
- fest: 0.25 / 0.25 / 0.25 / 0.35 / 0.4.
- arena: 0.25 ×4, Peak 0.3.

**Peak is never reached.** `FUN_0010d370` returns only 0..3:
- `rating < bootLevel(+0x94)` → 0 (Boot)
- `< 0.33` → 1 (Bad)
- `< 0.66` → 2 (Okay)
- otherwise → 3 (Great)

The rating is **player 0's crowd rating only** (Player+0x6c). `FUN_0016a3c8` runs every frame. On a change it sets `arena.old_excitement` and `arena.excitement` and sends `excitement` to the arena.

## 3. Script timeline (arena/gen/arena_game.dta, venue.dta) [C]

| event | original | ours |
|---|---|---|
| `intro_start` (song −10 s) | `play_game_sfx crowd_begin`; `crowd_audio play_intro` (:64-65) | OK: `app.cpp` startSong sends game `intro_start`, and `game.dta:101` forwards it to the arena |
| `music_start` | `crowd_upto_norm`, then `game delay_task 480` → `set_excitement $arena.excitement` (:74-79; CROWD_AUDIO_DELAY_TICKS, config/sfx_macros.dta:5) | OK (`app.cpp:705`, music_start at measure 2). The 480 runs on the game-panel TaskMgr in ms [I] |
| `excitement` | +480 → `set_excitement` (:139-145) | OK (`app.cpp:714-715`) |
| transitions | venue.dta:143-183: Boot → `excitement_boot`; Bad → rising if last==Boot, else falling; Okay → if last≠Okay: rising if last==**Bad**, else falling; Great → `excitement_great`. Then arena_game.dta:147-176 stop/play the `crowd_upto_*` / `crowd_dnto_*` pairs | `app.cpp:716-724`. Okay uses `level > old` → **G5** |
| SP deploy (`start_using`) | `sp_deployed[_multi]`; `stop_sfx crowd_upto_good`; `play_game_sfx crowd_upto_good` (config/player.dta:51-53). This is the "cheer on deploy" | OK through the script (`app.cpp:745`) |
| SP phrase captured | only `sp_awarded` (+200 ms); **no crowd sound** (player.dta:36) | OK |
| note hit / miss | no crowd sound (beatmatch.dta) | OK |
| **claps** | `beat` handler (:83-128): if not tutorial and **any** player `in_star_mode` → `arena delay_task 440` (CLAP_DELAY_TICKS, ticks → lands just before the next beat) → `play_game_sequence claps`. `claps` is a venue-bank random_group_seq of clap1..6. num_seqs is 2 in arena, theatre and fest; basement lists clap1 twice. Claps happen **only during Star Power**, on every beat; no section- or excitement-based claps exist [C: no other `claps` reference] | OK: app.cpp:702 `beat`, the `ArenaObj` tick delay, `PlayerStatsObj in_star_mode`, the `tutorial` stub returns 0, and `random_group_seq num_seqs` (synth.cpp:257) |
| `game_over` | stop `sp_deployed*` | OK |
| `game_won` | `track_exp` + `play_game_sfx crowd_win` (:191-194). The crowd loop continues | OK |
| `game_lost` | `crowd_lose` (core-1 reverb). The song slows 1→0 over 2 s; the crowd stream is not slowed | OK (`setSongSpeed` only affects the song) |
| WIN_FINAL clip enter_event | `crowd_audio play_stream 1.0 wingame` (config/macros.dta:84-88). Then a white fade at 20.5 s with `fade_synth FALSE` | **G4** |
| `game_outro` | `synth stop_all_sfx`; `game set_paused TRUE` → crowd paused | OK (`hooks.setPaused` → `crowd_->setPaused`) |
| post_show / lose / multi_compete enter | `crowd_audio turn_off` (endgame.dta:10, lose.dta:11, multiplayer.dta:611); `postshow_loop` +300 ms; win screens `play_sfx crowd_win` (endgame.dta:384/499, not fx-scaled) | OK through the scripts |
| pause menu | GamePanel SetPaused → sfx pause + crowd Stop/resume | OK (`app.cpp:286-293`) |
| leave game | ArenaPanel exit deletes CrowdAudio | OK (`app.cpp:299` turnOff) |

Things that **do not exist** in GH1 [C]:
- No crowd singing, chants or encore audio. There are no such strings in the ELF or scripts, and the only crowd assets are the 5 loops + wingame + the 14 bank samples.
- `metagame meta_crowdlp` (a looping sample) is referenced by nothing.
- The menu ambience is `meta_people` (already in `app.cpp:667`).
- The `stereo` option only re-pans the players' song tracks (`FUN_001076b0` → `FUN_00108880`), not the crowd [I].

---

## 4. Gaps and implementation steps

### G1 — the crowd streams play at double speed [C] (P0)
**Original:** the SPU stream voices play each file at its own rate (22050 or 44100).

**Ours:**
- `Mixer::fillStream` (`mixer.cpp:170-210`) decodes frames 1:1 at the device rate. The device rate is `rate_`, set from the song (`mixer.cpp:34`, 44100).
- The comment there ("every shipped stream is 44.1 kHz") is wrong for the crowd.
- Result: crowd loops play an octave high, at 2x speed, in every venue except the theatre levels and wingame.

**Fix (`mixer.cpp`):**
1. Add `double frac = 0; double ratio = 1; std::vector<float> hist;` to `Stream`. In `playStream`, set `s.ratio = stream->sampleRate() / double(rate_)`. Also recompute it if `rate_` changes in `play()`.
2. In `fillStream`, when `ratio != 1`:
   - Decode `n = ceil(frac + frames*ratio) + 1` source frames into the scratch, keeping the previous last sample as `x[-1]`.
   - Handle the loop wrap inside that decode loop (the current seek-to-start logic, unchanged).
   - Output `y[i] = lerp(x[floor(p)], x[floor(p)+1], p - floor(p))` with `p = frac + i*ratio`.
   - Then carry `frac` and the last source sample over to the next call.
   - Linear interpolation is enough. For SPU authenticity, the 4-tap Gaussian from `spu.cpp` can be used instead [I].
3. `done` / the end-of-stream test should count output frames.
4. This also fixes any other 22050 Hz VGS played through `playStream` (the tutorial VO streams are 22050 too).

### G2 — the fx-volume option is ignored [C] (P0)
**Original:** volume = `levelVol × globalVol(1) × Options.fx_volume`, read **when the stream starts or is un-paused** (`FUN_00112470`).

**Ours:** `CrowdAudio::setFxVolume` exists (`crowd_audio.h:27`), but nothing calls it (grep), so `fxVolume_` stays 1.

**Fix:**
- Give `CrowdAudio` a `std::function<float()> fxVolume` (or call `setFxVolume`) and refresh it:
  - in `CrowdAudio::play()` (`crowd_audio.cpp:47`);
  - in `setPaused(false)` (also call `mixer_.setStreamVolume(kCrowd, curLevelVol_ * fx)`, so store `curLevelVol_` in `play()`).
- Source: `campaign_->profile().options.fx` (the same value `OptionsObj get_fx_volume` returns, `career_objects.cpp:191`). Wire it in `app.cpp` next to `crowd_ = std::make_unique<CrowdAudio>` (line 248), with a fallback of 1 when there is no campaign.
- Do not push live changes from `set_fx_volume`; the original does not either.

### G3 — FadePanel `fade_synth` does not fade the crowd [C] (P1)
**Original:** `fade_synth` sets the SPU MVOL (`1 - fade`). The crowd stream is SPU voices, so it fades with everything else. WIN_FINAL's white fade uses `fade_synth FALSE` so that `wingame` survives.

**Ours:**
- `hooks.masterVolume` → `Spu::setMaster` (`app.cpp:312`) only scales SPU voices (`spu.cpp:280`).
- The `kCrowd` stream ignores it.

**Fix:**
- Add `float Spu::master() const`.
- In `Mixer::fillStream`, multiply by `spu_.master()` for the crowd slot.
- Apply it to `kMusic` and the song as well if the other audits agree. On PS2 MVOL scales all streams [C per audio_sfx.md §3], but change this together with the song and menu music owners.

### G4 — the final-win `wingame` stream is missing [C] (P1)
**Original:** the WIN_FINAL clip's `enter_event` (config/macros.dta:73-105) runs these steps:
1. `char_sys start_final_win_fx`.
2. `{if {exists crowd_audio} {crowd_audio play_stream 1.0 wingame}}`. This hard-cuts the excitement loop. wingame is 44.1 kHz, 19.4 s, looping, at vol `1.0 × fx`. The file exists only in `venues/arena/streams`.
3. At clip frame 20.5, `fade start_fade 2000 (color 1 1 1) (fade_synth FALSE)`.

**Ours:**
- `venue_renderer.cpp:417` plays `kGuitarWinFinal`, but clip enter_events are not executed (no `enter_event` in `engine/src`).
- `docs/camera_endgame.md:286` lists the same gap.

**Fix:**
- Minimal: in `app.cpp` game-over (around line 1016), when `finalWin`, run `crowd_->playStream(1.0f, "wingame")` when the guitarist's WIN_FINAL clip first blends in.
  - For an approximation, that is the moment `venue_->gameOver(won, true)` starts it.
  - Ideally add a hook from `Character` when the clip starts.
- Better: implement clip `enter_event` / `events` evaluation in the character system (docs/character_animation.md:282). The script line then just works through `CrowdAudioObj`, and `exists crowd_audio` must return true in game.

### G5 — the Okay transition picks the wrong direction [C] (P2)
**Original** (venue.dta:161-174): `if last != Okay: if last == Bad → okay_rising else okay_falling`. So **Boot→Okay plays `okay_falling`**: it stops crowd_upto_good and plays crowd_dnto_norm.

**Ours** (`app.cpp:720`): `level > old ? rising : falling` gives rising for Boot→Okay.

**Fix:** `case Okay: transition = old == VenueScript::Bad ? "excitement_okay_rising" : "excitement_okay_falling";`. Also set `arena.last_excitement` (the venue.dta name; the engine binary writes `arena.old_excitement`) before sending, for script parity. The other cases already match: Bad rising only from Boot, Great always.

### G6 — same-file `set_excitement` [C] (P3)
**Original:** every `set_excitement` / `play_*` destroys the stream and restarts the file from 0, with a short load gap, even when the file is the same.

**Ours** (`crowd_audio.cpp:68-76`): the same file while playing → only the volume changes.

**Fix:** remove the shortcut and always call `play(vol, file)`. It is rarely audible: Great and Peak share a file, but Peak never occurs.

### G7 — CrowdAudio's other messages [C] (P3)
**Ours:** `CrowdAudioObj` (`ui_system.cpp:519-524`) lacks `set_volume`, `set_enabled`, and `enabled`. `CrowdAudio` does not read `config/sound.dta (crowd_audio TRUE)`.

**Fix:**
- Add `bool enabled_ = true; int excitement_ = 2; float globalVol_ = 1;` to `CrowdAudio`.
  - `play()` returns early if `!enabled_`.
  - `setExcitement` stores `excitement_`.
  - `setEnabled(b)`: false → `turnOff()`; true → `setExcitement(excitement_)`.
  - `setVolume(v)`: `globalVol_ = v`, then reapply.
  - Volume = `level × globalVol_ × fx`.
- Handle the 3 messages in `CrowdAudioObj`. `enabled` returns an int.
- These are used only by the cheats `toggle_crowd_audio` (cheats_funcs.dta:359-371) and `set_paused` (:632).

### G8 — the `mixer` object [C] (P3, debug)
**Original** (`Mixer_v1` 0x107080):
- `set_volume v` / `set_mute b` / `mute`.
- Changes are broadcast to listeners (CrowdAudio `set_volume v*!mute`) and to the synth global volume (`FUN_00212e48`).
- `set_mute TRUE` also calls `stop_all_sfx`.

These are used only by camedit.dta:43 and the cheats. **Fix:** optional; a `mixer` ScriptObject that forwards to `CrowdAudio::setVolume` and the synth global volume.

### G9 — Multiplayer (later, when MP gameplay exists)
- [C] Excitement, and therefore the crowd loop and the reaction sfx, come from **player 0's** crowd rating only (`FUN_0010d370` index 0). Whether P0's rating moves in face-off is [I].
- [C] Claps play if **either** player is in star mode (arena_game.dta:107-114). Each player's deploy restarts `crowd_upto_good` (not panned), and `sp_deployed_multi` is panned ±1 (`play_game_sfx_player`).
- [C] small_club_multi uses the crowd_v2 files and the volumes of small_club.
- [C] `multi_compete` enter does `crowd_audio turn_off` + `postshow_loop`.
- The lights are forced to Great in MP (venue.dta downbeat); that is visual only.

---

## 5. Verification checklist (after the fixes)
1. Basement: `crowd_v1_3norm` should be a 2.70 s loop at normal pitch, not 1.35 s.
2. Fx slider at 0 → the crowd loop and the reaction sfx are silent. The win-screen `crowd_win` stays audible because it uses plain play_sfx.
3. Pause → the crowd freezes and resumes at the same position. Game outro → the crowd goes silent before post_show.
4. Fail from Boot, then recover to Okay → `crowd_dnto_norm`.
5. Star Power → claps on every beat (two at once in arena, theatre and fest), plus a `crowd_upto_good` cheer at deploy.
6. Career final song win → the wingame loop replaces the crowd and survives the white fade.
