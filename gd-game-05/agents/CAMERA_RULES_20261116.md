# RB1 camera rules vs the port (GD05-107, research 2026-11-16)

Research only (no builds). Sources: RB's XEX in ghidra/ (addresses below),
the disc's DTA (`extracted/dta`: world/gen/worldbase.dta, config/gen/midi_parsers.dta,
system/run/band/gen/band_objects.dta, world/gen/camera_cats.dta), the venue milos
(BandCamShot records read with a scratch decoder that follows
`readBandCamShot`) and the 62 songs' VENUE tracks.
[C] = confirmed from XEX or data, [I] = inferred.

## How RB picks and holds a shot (summary)

- Cuts happen only on: VENUE note 60 (`world_camera_parser`, start_offset -0.125 beat, so 1/8 beat
  early) -> `pick_new_shot`; `[do_directed_cut <cat>]` text (world_focus_parser) -> `start_directed_cut`;
  a shot's end (`shot_over`); a player failing / being saved (BFTB_*); lose / win / outro [C data].
- `pick_new_shot` runs only when `beat > $camera.next_cam_beat` and picking is not disabled [C data
  worldbase.dta]. Category NORMAL<b d g v> from the held focus notes 61..64; 70..73 set no_behind /
  force_far / force_closeup / no_closeup; none held -> random_elem of 8 categories; filters on
  distance, facing, one_time_played [C data].
- A shot's end: band_objects.dta `shot_over`: next_shot set -> `force_shot next_shot`; else
  `$world camshot_end`: next_cam_beat = -10000, then if the ending shot was directed (or the intro)
  `pick_new_shot` **at once**, else next_cam_beat = now + 2 beats (the last frame holds and the
  next note 60 within 2 beats is skipped) [C data]. shot_over is sent once when frame >= the shot's
  end frame (+0x168) and it is not looping (CamShot SetFrame RB 0x82292968) [C].
- pick_shot (RB 0x8229F6A8 -> 0x8229F620 -> 0x8229F0D0): per category the shots that are pickable
  (+0x16c set), unused (+0x16e clear), pass the property filter (0x8229CB58) and shot_ok
  (0x8228E250), weighted by selection_weight (+0x44); none unused -> the used ones again; nothing
  at all -> "No acceptable camera shot" notify and **returns 0: no cut, the current shot stays** [C].
  find_shot (0x8229EF48) draws the same way but does not start it [C].
- min_time / max_time of NORMAL shots are not read by code (no string ref for max_time; min_time
  only in directed-cut / validation scripts) [C strings.tsv + data]. Durations come from keyframes.

## Differences (priority order)

| # | Prio | Area | Port (file:line) | RB (address / data) | Fix suggestion |
|---|---|---|---|---|---|
| 1 | P1 | Shot end: no next_shot, no camshot_end | camera_director.cpp:935-944 sends `shot_over` to the shot's script object, but BandCamShot has no script type in the port (world.cpp:373 looks up types in world/world_objects.dta only; crowd.cpp:447 says so) -> nothing happens; the last frame holds until the next note 60 | band_objects.dta BandCamShot `shot_over`; worldbase.dta `camshot_end` [C data]. 680 directed cuts in 62 songs (11 / song), median directed shot 100 frames; note-60 gaps median 4.5, p90 12.5, p99 28 beats [C data] -> after every directed cut RB re-picks at once, the port can hold a directed shot's last frame (often a whip / crowd angle) for bars: the owner's "held too long / not looking at the band" | In CameraDirector, on a BandCamShot's end: next_shot -> forceShot(next); else callback App::rbCamshotEnd: if the shot was directed (directed_cut flag set by the intro, start_directed_cut, BFTB) run pickNormal() now with rbNextCamMs_ cleared, else rbNextCamMs_ = now + 2 beats. Switch GD05_SHOTEND_LEGACY. Check: GD05_SHOTLOG time from each directed shot's end frame to the next cut = 0 (+1 frame) |
| 2 | P1 | Per-venue disabled shots (camshot_overrides) | scene/objects.cpp:806 reads WorldDir `camshots` into w.camshots; never used (venue.cpp:107-121 applies mat / hide overrides only) | WorldDir load RB 0x8228B6C8 (rev > 0xf): camshot_overrides list (0x822883A8) of CamShots; their +0x16c flag ends 0, and the picker (0x8229E190) only takes shots with +0x16c set; SetFrame (0x82292968) also skips such shots' frame logic [C; I: default +0x16c = 1]. Data: arena_07 lists 19 shots, big_club_04 21, big_club_11 34, theater_03 16 ... (0 for some venues) [C data] | Exclude the variant WorldDir's `camshots` from rbShots_ when the venue loads (app.cpp:3586 loop). This is Harmonix's own per-venue list of shared-kind shots that clip that venue's set: the likely cause of "inside the crowd / behind objects" in specific stages. Check: per venue log listed vs excluded counts |
| 3 | P1 | Directed cut start: zero_time ignored | app.cpp:3887 reads `s->flag("zero_time")` (a header prop); zero_time is a BandCamShot member (scene `zeroTime`, world_objects.cpp:185), so 0: the shot starts at the event | midi_parsers.dta world_focus_parser `text`: start = event - zero_time (frames converted by the shot), then `start_directed_cut`: next_cam_beat = start + (min_time + zero_time) / 30 s = event + min_time / 30 [C data]. 266 of 346 directed shots have zero_time > 0, up to 425 frames (14 s) [C data] | Schedule directed cuts zero_time / 30 s early (scan the VENUE events ahead of t), hold until event + min_time / 30. Check: shot frame at the event time = zero_time (+-1) |
| 4 | P2 | Pick failure fallbacks | app.cpp:3870-3871: no shot -> same category without the filter, then NORMAL1111 with the filter (always cuts) | 0x8229F0D0: nothing acceptable -> no cut, current shot kept [C] | Drop both fallbacks (keep under legacy switch); log the failed category. Categories NORMAL0000 / 0101 / 0111 / 1110 exist in camera_cats.dta but often have no shots |
| 5 | P2 | Solo focus swap | app.cpp:3843-3856: categories straight from the notes | worldbase.dta pick_new_shot: `gamemode is_solo` and exactly 2 focus notes -> the active player's instrument forced in (replaces bass, else guitar, else a random one of the other two) [C data] | Add for qp_solo / tour_solo (Quickplay solo is where the owner saw it) |
| 6 | P2 | Directed cut filters | app.cpp:3885 rbShot(cat) without a filter | get_shot: find_shot cat with (valid_male / valid_female by the member's gender) and (valid_banger / dramatic / rocker / spazz by its attitude), the member chosen by "bass" / "drum" / "guitar" / "vocal" in the category name [C data]; ~70 directed shots set some valid_* to 0 [C data] | Pass the filter from the character's gender / attitude (rb_meta / band data) |
| 7 | P2 | Directed cuts while a player is failed | app.cpp:3881-3890 checks rbLoseShot_ only | start_directed_cut needs num_players_failed == 0 and !disable_picking; player_failed / player_saved pick BFTB_<i>_FAIL / REVIVE and hold min_time + zero_time [C data] | Band / co-op: wire player_failed / saved (BFTB categories, 8 shots each kind) and the gate |
| 8 | P2 | next_shot chains outside the intro | only App::rbIntroShots follows next_shot (playSequence) | shot_over -> force_shot next_shot for any shot [C data]: stage dive coop_dir-v_sd -> sd01, sd02 -> sd03; coop_dir_vcs01 -> 02 | Covered by #1 |
| 9 | P2 | one_time never applies | app.cpp:3860 compares `flag("one_time") == "TRUE"`; the milos store bools as int 1 | start_shot sets one_time_played; pick filter (one_time_played FALSE); 29 one_time shots in the 4 kind milos [C data] | Accept "1" / "TRUE"; reset at song start (worldbase.dta enter) |
| 10 | P2 | First frame after a teleport [I] | app.cpp:2712-2730: band_->update runs before rbShots -> forceShot -> rbShotStart teleports; director_->update snaps the keyframe target (camera_director.cpp:756) on bones posed before the teleport / new clip group, then the shot's filter (often 0.001..0.5) barely moves it | RB order not traced [I] | Measure: log the target point on the shot's frame 0 vs frame 2; if they differ by > 1 unit, pose the band again (or skip the snap one frame) after a BandCamShot with teleports / anim starts |
| 11 | P3 | Cut timing 1/8 beat early | note 60 at its own time (app.cpp:3875-3878); focus notes held from their own time | world_camera_parser and world_focus_parser start_offset -0.125 beat [C data] | Pick at note time - 1/8 beat (tempo map) |
| 12 | P3 | Drums focus rule | app.cpp:3845 drops drums when >= 2 others | WORLD_OBJECT_BASE_CHECK_FOCUS: only when exactly 2 others [C data] | `== 2` |
| 13 | P3 | Facing after a null-facing shot | app.cpp:3861 allows any facing after null; nothing current: anything | filter_shot: prev null -> (left right); prev left -> (right null); prev right -> (left null); prev behind -> any; no current shot = near / right [C data] | Match the table |
| 14 | P3 | Win / outro | app.cpp:3898 one WIN shot | game_win_camera (co-op): construct_active_shot WIN with WIN_PRE / WIN_POST, each player's WIN shot keyframe 0 set to 60 / 50 / 40 / 30 frames for 1..4 players, blend 0; then next_outro_camera loops gamemode outro_camera shots [C data] | After the gameplay fixes |
| 15 | P3 | Special-shot start script | not run (no BandCamShot script type) except crowd parts (crowd.cpp:436) and PostProc (camera_director.cpp:606) | start_shot: lighting_override (preset / category), event_start / event_end triggers, play_anim, failing.pp at excitement bad [C data]; used by intro / win / lose / BFTB shots only, not NORMAL [C data] | Load band_objects.dta's BandCamShot type for the world (would also give #1, #9 for free) or port the parts |

## Q2: keyframe space and targets

- Keyframes are world space [C]: the keyframe's xfm translation is the property `world_offset`
  (keyframe +0x48, CamShot SyncProperty RB 0x82293A70; screen_offset +0x58), moved only by a
  parent target. The band targets' teleport `to` transforms live in the same record and space
  (coop_d_c07: camera (45.7, -21.0, 59.7), drummer teleported to (6.42, -35.39, 48.94)) [C data].
  No hidden stage / venue origin: big_club coop_d_c.shot's z 33 is a low angle from the pit
  (43 units in front of the drummer, 16 under the deck), rising to z 71.1 over 300 frames [C data].
- Target point [C 0x8228EAB0]: the target Trans is the entity itself, or the subpart found in it
  when a subpart is set; so a subpart-less character target (coop_d_c07: `player_drum0`, '')
  aims at the character's own Trans (its root), as the port does [I: origin, not a bounding
  centre]. coop_d_c07 is disabled in no venue's camshot_overrides (coop_d_c05 is, in big_club_04 /
  08 / 12) [C data]: if it looks "inside the kit", check that the kit pieces follow the drummer's
  teleport, and #10.

## Q3: crowd / objects

- Per-venue disabled shots (#2) are the original's answer to shared shots that clip a venue's set.
- The port already does: hide / show lists (0x8228F608 / 0x8228F6C0), the shot's crowd only
  (0x82286E50), crowd3d cards, crowd_density / face_camera / crowd_anim (crowd.cpp:436).
- Not checked [I]: whether RB's WorldCrowd skips cards near the camera (near plane 10 on every
  shot sampled).

## Q4: fan studies

- docs/spikes/study_rb2dx.md: shake / hair tuned for 24 Hz (DX scales shake x0.25 above 25 fps);
  no shot-picking change. study_rb3dx.md: intro clock = minus the intro shot's duration (matches
  D98); "cycle camera" and "skip venue intro" options only. study_milo_script_library.md: nothing on
  cameras. None of them explains #1..#3; those come from the disc's own scripts.
