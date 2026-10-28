# GH2's venues: loading, classes, drawing and the world at run time

Milestone M8, first phase: the eight venues behind the song, loaded as
`world_panel` loads them, every venue class decoded, drawn statically from
their first camera shot and lighting preset, with their world clock,
animations and EventTriggers running. The camera director, the lighting
preset manager, spotlight beams, the crowd and the multiplayer specifics are
the second phase. Addresses are in `default.xex` (`ghidra/analysis/`); **[C]**
confirmed in code or data, **[I]** inferred.

Code: `scene/world_objects.*` (decoders), `game/venue.*` (loading),
`game/world.*` (the world at run time), `game/world_driver.*` (what the song
tells it), `render/venue_view.*` (drawing, `--view <venue>`),
`tests/venuetest.*` (`--venuetest`).

## Venues and their milos

Career order (`config/campaign.dta (order ...)`) [C data]: battle, small1,
small2, big, theatre, fest, arena, stone.

`ui/game.dta` world_panel [C data] loads `../world/<v>/<v>.milo` for one
player, `<v>_mp.milo` when `{game multiplayer_vs}` (face-off, pro face-off)
and `<v>_coop.milo` for the other two-player modes; `finish_load` names the
directory `world`. The WorldDir names its sub-directories in its ObjectDir
data and each names the next, so a venue is a chain [C data]:

    world/<v>/gen/<v>[_mp|_coop].milo_xbox   WorldDir: CamShots, PostProc, camera path TransAnims
    -> ../og/<v>_lighting.milo   (ng/gen/)   Spotlights, LightPresets, beams, EventTriggers
    -> ../../<v>_chars.milo      (gen/)      band slots (Character), WorldCrowd, Waypoints, band lights
    -> ../og/<v>_geom.milo       (ng/gen/)   the stage, its effects and EventTriggers
    -> ../../<v>_bank.milo       (gen/)      sound effects
    (stone's geom also -> ../eng/starmessage.milo, one per language)

"og/" folders resolve to "ng/" (`0x821c9480`) [C]. RndDir objects with a
proxy file load their own milo [C data]: big's two flash pots
(`big_geom_flashpot_3.milo`), small1's four bottle throws
(`small1_geom_bottle_throw_proxy.milo`, type `bottle_throw`) and
battle / small1's `big/ng/drummer_explode_proxy.milo`. The chain's
directories find each other's objects (ObjectDir::FindObject searches its
sub-directories [I: later Harmonix code]): `--venuetest` resolves every
reference of every object of the 24 variants (none is missing). Unused
[C data: nothing names them]: the six `*_geom_mp.milo`,
`battle/ng/gen/vault_cola.milo`, `stone/ng/gen/language.milo`.

Song -> venue: `songs.dta (quickplay (venue x))` [C data], else the career
tier that lists the song (stone has no quickplay songs), else battle;
`GD02_VENUE=<v>` overrides (development aid). With the menus, game.dta's
`set_venue` / `get_venue` decide and world_panel's milo name is used.

## Classes (Load = the class's Hmx::Object sub-vtable slot 9)

Every object of every class on the disc decodes exactly to its end marker
(`--scenetest` asserts the counts).

| Class | Ver | Load | Count | Notes |
|---|---:|---|---:|---|
| CamShot | 27 | 0x8222de20 | 440 | keyframes (0x8222a5b8): duration, blend, blend_ease, fov, xfm, screen_offset, blur_depth, max_blur, focus_blur_multiplier, targets, focal_target, parent, use_parent_rotation, shake amp / freq / max angle, zoom_fov; then looping, path_ease, near, far, use_dof, filter, clamp_height, path, fade_time, category, selection_weight, crowd3d pairs, u32 dropped, hide_list, crowd, glow_spot |
| LightPreset | 9 | 0x82243660 | 174 | keyframes of spots / environs / lights, then the three name lists, looping, category, adjectives, trigger, fade_in, min / max excitement, manual, locked |
| Spotlight | 25 | 0x82238098 | 341 | Draw, Trans, beam (cone, sizes, borders, material, offsets, brighten, xsection, expand, cutouts), can, target, colour, intensity, floor spot, flare, lens, additional objects, flags |
| WorldDir | 12 | 0x8224e5c0 / 0x8224de60 | 24 | PreLoad: u32 0, u32 1.0f, HUD milo, then PanelDir; PostLoad: PanelDir's, default camera xfm, sub-directory hides and bitmaps |
| WorldCrowd | 6 | 0x8224a090 | 15 | Draw, placement mesh, num, rotate, characters (milo, height, density, radius) and per character its placements, f32, show_3d_only, then the object block (last) |
| EventTrigger | 10 | 0x8225d708 | 344 | trigger_events, anims (anim, blend, wait, delay, filter: enable, rate, start, end, scale, type, period), sounds, shows, hide delays, enable / disable / wait_for events, next_random, proxy calls |
| PostProc | 9 | 0x821eb3d0 | 28 | (render/post_fx.h) |
| Flare | 4 | 0x8220ddb8 | 23 | Trans, Draw, mat, size, range, steps |
| LightAnim | 2 | 0x82205cc0 | 9 | light, colour keys, keys owner |
| EnvAnim | 4 | 0x8221fbe0 | 34 | environ, ambient keys, owner, fog colour keys, fog range keys |
| CamAnim | 2 | 0x82206b78 | 2 | no keys on the disc (not played) |
| Set | 0 | 0x8221b188 | 50 | a list of objects |
| CubeTex | 1 | 0x822092e0 / 0x82209618 | 60 | face size, bpp, six paths; inline flag and six bitmaps |
| Waypoint | 3 | 0x82197a38 | 68 | Draw, Trans, flags (kStart* bits), connections, radius, 2 f32 |

Character / BandCharacter (the band, M9) and the sound bank classes stay
undecoded.

## Drawing (render/venue_view.h)

- Draw list [I: later Harmonix code's RndDir::SyncObjects]: the chain's
  drawables (meshes, groups, particle systems, proxy directories) less those
  a Group lists, stably sorted by draw_order. Groups draw their members in
  order with their Environ selected; a directory's own environ is the
  default. Proxy directories draw their milo at the proxy's transform.
- Meshes through GH2's materials (MatAnim state, next passes, skinned meshes
  from their bones, MeshAnim vertices); particles (render/particles.h) on
  the world clock; environment maps from CubeTex.
- Environs and lights as EnvAnim / LightAnim and the lighting preset leave
  them. LightPreset apply 0x82239BB8 [C]: a spotlight only with its
  animate_from_preset (+0x254), an Environ only with its animate_from_preset
  (+0x74: ambient, fog colour and range lerped, fog off pushes the range out,
  0x82238920), a Light's colour and range with animate_color_from_preset
  (+0xcc), its transform with animate_position_from_preset (+0xcd,
  0x822387E8).
- Before the first shot (and in `--view`): the first INTRO shot's first
  keyframe and the INTRO preset's first keyframe, framed as the director
  frames it (CameraDirector::evaluate). In a song the
  camera director ("Camera director") plays the shots and the preset
  manager ("Lighting presets") the presets. The band slots draw their
  members where the slots sit in the draw list (`docs/band.md`); without a
  band (`--view <venue>`) a slot stands at its start waypoint (guitarist0
  flag 1, guitarist1 2, singer 4, keyboard 8, bassist 16, drummer 32,
  face-off guitarist0 512; macros.dta [C data]).
- Spotlights, their beams, floor spots, cans, lenses and flares, and the
  RndFlares, after the draw list ("Spotlights and flares").
- Order of a frame: the venue (into the post-process's HDR buffer), the
  post-process, then the highway and HUD.

Not drawn yet: the band.
The crowd: "Crowd" below. Venues are therefore darker than GH2's (theatre most).

## The world at run time (game/world.h)

- Clock: song seconds (kTaskSeconds) and beats (kTaskBeats).
- Animations [I: the AnimTask rules of later Harmonix code]: rate 0
  (k30_fps) 30 frames a second, 1 (k480_fpb) 480 a beat, 3 (k1_fpb) one a
  beat [C data: the beat-driven anims use 1]. An AnimFilter maps its frame
  through scale (or (end - start) / (period x frames per unit)) and offset
  into start..end, clamped, looped or bounced; loop and shuttle filters run
  until replaced. A new animation of an object replaces its old one; blends
  are not done.
- EventTriggers (registration 0x822578e8, fire 0x82259588 -> 0x82257d00
  [C addresses; behaviour I]): enable / disable events switch a trigger
  (on at first); with wait_for_events it waits for one of them; firing plays
  the anims (with their own filter when it is enabled), shows objects, hides
  others after their delays, calls proxies (the message goes to the proxy
  directory's script type, e.g. bottle_throw start, and its triggers), plays
  its sounds (the venue bank's Sfx, audio/synth.h) and fires next_random.
- The `world` script object: the venue's WorldDir type (world_objects.dta:
  WORLD_OBJECT_BASE + world/<v>/<v>.dta), with the menus on the UI's
  interpreter (the type from the boot tree's `(objects ...)`), else on its
  own (world/world_objects.dta). Messages the type does not answer go to the
  chain's RndDir types (lighting / chars / geom: set_multiplayer...), then
  the natives: the WORLDEVENTS and every trigger event fire triggers; then
  the handlers modules add (World::addNatives: the camera director, the
  lighting presets, the crowd; World::addObjectNatives for venue objects:
  a CamShot's `get duration`, a PostProc's `select`); what is left answers
  0 as a STUB (L1). Venue objects get their class's script type by their
  header type (CamShot `band`: camshot.dta) [C data]. Venue
  objects are script objects by name (`sparks.filt animate`, `[env1]
  animate (dest 10) (period 0.01)`, `band_shadow1.mesh set_showing`), the
  band slots are the band's characters (`docs/band.md`; without a band
  they answer 0 as STUBs); `{waypoint_find flags}` finds a waypoint by its
  kStart* bits; `{dir exists x}` is false for a band slot left empty and
  `remove_sink` drops a sink.
- Events from the song (game/world_driver.h): beat / downbeat; EVENTS
  [verse] / [chorus] / [solo] and one_bar_to four beats early,
  [lighting (x)] -> lighting_change four beats early too (the EVENTS
  look-ahead of 0x822CFD90, so that the presets' delayed switch ends its
  fade on the event; GH2's typos are read: `[lighting(blackout)]`, stray
  spaces), the crowd texts, band_jump, sync_*,
  music_start; config/midi_parsers.dta [C data]: BAND DRUMS 36 kick_drum, 37
  crash_symbal, BAND BASS 36 bass_hit, TRIGGERS 52 venue_effect, 48 / 49 / 50
  do_lighting_next / prev / first_keyframe four beats early; hit_p0 /
  hit_p1_fret1..5 per fret of a hit gem; build_streak; star_power_ready /
  start / stop; excitement (0 Boot under the warning level, 1 under 0.33, 2
  under 0.66, else 3; 0x822CAAB0) when it changes; peak_on / peak_off at 4x
  over 0.66; intro_end at song time 0 [I]. hit_snare / hit_hihat come from
  the drummer's clips (char_objects.dta) [C data] and wait for the band.
  With the menus, intro_start_msg / game_won_msg / game_lost reach the world
  as a game sink (`{game add_sink $this}` in WORLD_OBJECT_BASE enter).
  Any world adds itself, also one loaded outside a game (the Enhanced
  Options backdrop). GH2's AddSink (0x823262C8) takes a reference on the
  sink (Hmx::Object AddRef 0x82317480; RemoveSink 0x823257C0 releases it
  with 0x82316378) [C], so a deleted sink leaves its sources [I: the
  deletion's replace]. In gd02, `SinkHolder` (script/script.h) does this for
  GamePanel, WorldDir and MidiParser. Before bug-fix 5 the backdrop's freed
  world stayed in `game`'s sinks, and the next song's intro_start_msg
  crashed the game (the owner's "Strutter on Medium" career crash).

## Camera director (game/camera_director.h)

Which shot runs when is the venue's script [C data]: WORLD_OBJECT_BASE
(world_objects_worldbase.dta) and camshot.dta (the CamShot `band` type).
intro_start_msg picks INTRO / INTRO_FAST / INTRO_ENCORE with 6 bars left;
each downbeat counts a bar down and, unless player 0 is in Star Power,
check_camera_shot cuts when none is left (a new length: random_int over
the venue's camera_durations for the excitement, upper bound excluded);
each beat the current shot's check_shot can force a cut; one_bar_to cuts a
bar before [verse] / [chorus] / [solo] (solo shots in solos);
pick_regular_camera_shot filters (facing opposite the last shot's, after
far / behind only null / near / closeup, solo never, low_excitement_ok at
excitement <= 1, walk_ok while walking, starpower_ok in Star Power,
special FALSE) over NORMAL_CAMSHOT_CATEGORIES; band_jump, sync_wag and
sync_head_bang cut for 4 bars when excited enough; one LIGHTER shot a
song; game_won_msg picks WIN (WIN_ENCORE, WIN_GAME) after 1.75 s and
game_lost LOSE. Without the menus the director provides `game` and
`gamecfg` (pick_new_shot needs `{exists game}`).

The natives [C addresses]:
- pick_shot cats [filter] (0x82268DE0 -> 0x82268CA8 -> 0x82268648): per
  category (0x822682F8; per shot 0x82267748) the shots not used yet that
  pass the filter (0x822660D8: each (prop value) or (prop (values)) against
  the shot's property, its header props then camshot.dta's defaults) and
  `shot_ok` (camshot.dta -> cam_shot_ok); none left in a category: its
  used shots pass again and are unmarked (a no-repeat bag); one weighted by
  selection_weight over all categories (0x822662C0) is marked used and
  started unless it already runs; none: "No acceptable camera shot".
- cam_shot_ok (0x822B2538): WIN, WIN_ENCORE, WIN_GAME, LOSE, LIGHTER,
  INTRO, INTRO_ENCORE, INTRO_FAST always; `special` refuses; else
  cam_check_shot (0x822B2240): guitarist0 in its Star Power clip (flag
  0x80000) needs starpower_ok, its far one (0x4000) far_starpower_ok, and
  the waypoint nearest it among those flagged kWalkSpot | kSoloWalkSpot
  (Waypoint::Nearest 0x82195548, mask 0xc0, squared distance, no radius;
  looked up once per beat) must not be in bad_waypoints. The band answers
  (`docs/band.md`); without one, player 0's Star Power stands in.
- force_shot (0x82266A10), current_shot, find_shot (0x82268418),
  cycle_shot (0x82266958, by name), set_pick_shot / toggle_pick_shot,
  play_camera_sequence (0x82265D58), toggle_free_cam (no free camera:
  picking pauses); `set_min_lod` (the band's LOD) and `reset_postproc`
  quietly. guitarist0's `playing_starpower` / `in_star_mode` answer player
  0's Star Power, `actually_walking` 0, `lose_teleport` nothing.
- Start (StartAnim 0x82226120): the old shot's EndAnim (0x82226338:
  stop_shot, its hide_list shown again), the hide_list hidden, the
  shake / filter state reset, start_shot (camshot.dta: PostProc select or
  rnd reset_postproc, set_min_lod, resend_excitement, the crowd's
  crowd_update and rotate); the PostProc is also taken natively. After
  the first frame shot_started (world post_switch_cam); at the shot's end
  (frame >= its length) shot_over once (camshot.dta: next_shot).
  `CameraDirector::onShotStarted` tells the crowd.

Playback (SetFrame 0x8222a068), 30 frames a second from the start [C]:
- Keyframes (0x82226448): keyframe i starts at the sum of the earlier
  (duration + blend), holds its duration, then blends over its own blend
  to i + 1 (the last holds; looping wraps); t eased by the outgoing
  keyframe's blend_ease, a sigmoid from 0 to 1 (0x8226F988 / 0x8226F6E8:
  [-k, k] mapped through an odd function [I: tanh]).
- A keyframe's camera (0x82226988) [C]: its xfm; with a path, the path
  TransAnim evaluated at end_frame x u (u = frame / length eased by
  path_ease, 0: linear; RndTransAnim 0x821B6B00 into the shot's matrix:
  position keys, trans_spline's Hermite 0x8226E560 / 0x8226E368, rotation
  keys, scale keys scaling the rows; no keys: origin / identity) with the
  FIRST keyframe's xfm multiplied in under it (0x8226A630 on keyframes[0]
  +0x18: the other keyframes' xfms are not used); the parent (a band slot:
  its waypoint, a bone its height above it [I]) added as a translation, or
  multiplied in with use_parent_rotation; clamp_height (> 0, exactly one
  target) keeps it above that target + clamp_height. The targets' mean
  (0x82226800) and the parent's transform are low-passed per keyframe
  (+0x8c / +0xac) by filter x min(d, 1) once a 60 Hz frame, d the screen
  distance (0..1 across the picture) between where the current camera
  shows the target (0x821D7030) and the keyframe's screen offset.
- Framing (0x82227440) [C]: when the two keyframes aim at different
  targets (0x822266F0), each keyframe's camera looks at its own filtered
  target; then position lerp, rotation slerp (0x8226A980). When they aim at
  the same targets the blended camera is turned to each keyframe's target
  and those two slerped. The look-at (0x82224BD8 + 0x8214F7F0): forward =
  target - position, the up hint the camera's own up row (the keyframe's:
  shots roll as authored), right = forward x up, up = right x forward: the
  target lands at the centre. The screen offset (-1..1, y up) is then a
  move of the camera in its own plane, only with the same targets: by
  (-so.x d tan(fov / 2) aspect, 0, -so.y d tan(fov / 2)) in camera space
  (0x8214F950; the projection's +0xf4 = cot(fov / 2) h / w and +0x118 =
  -cot(fov / 2), 0x821D75F8), d the distance to the target(s) and so the
  offset, both lerped, fov without zoom_fov. Field of view lerped, then
  zoom_fov lerped and added; near / far the shot's; depth of field (from
  the moved camera) when use_depth_of_field: focus = |camera - focal
  target| x (1 + focus_blur_multiplier), blur_depth / max_blur lerped; only
  the outgoing keyframe with a focal target: max_blur x (1 - t), only the
  incoming one: x t; none: DOF off (Rnd vtable +0x70 / +0x74).
- Shake (0x82224D18) [C]: per 1/60 s an impulse with probability noisefreq
  (direction a random angle: x = sin(a + pi / 2), y a third of x, z =
  sin(a), 0x8226E050 being GH2's table sine; size random x noiseamp;
  angles within +-shake_maxangle degrees about x and z; past noiseamp the
  offset is taken back to 2 amp - |v|), followed by springs (0.02 x
  distance, velocity x 0.9). The offset is added in the camera's own axes
  (0x8214F950); the angles turn it about its right and up [I: 0x8216DB48's
  order].
- Bug-fix GD02-092 (10-28): before, the look-at was solved with world up
  and the screen offset as a turn, which flipped the camera over whenever
  the target sat high above it (stone's intro: the picture upside down,
  the crowd "hanging from the sky"); the path was multiplied under each
  keyframe's own xfm, so arena's intro (Intro.tnm, its second keyframe's
  xfm a world position) blended to twice the path's offset, about 300
  units under the arena floor. `--venuetest` now frames every shot of
  every variant through its length (tests/venuetest_camera.cpp).
- Frame rate: GH2 runs the filter (one lerp a frame, 0x82122100) and the
  shake once a 60 Hz frame [C: 0x82226988 / 0x82224D18 per SetFrame]. The
  port takes the filter as (1 - filter)^(dt x 60) and runs the shake in
  whole 1/60 s steps (the remainder carried), so 30, 60, 144 Hz or an
  uncapped frame rate move the camera alike; the shake draws from its own
  RNG stream, so how many steps it ran never changes the director's picks
  (both draw from GH2's one RandomGen there, seeded by the clock) [I].
  Before (bug-fix 4) both ran once a frame: 2.4x too fast at 144 Hz, 4x at
  the 250 fps limiter, and the shots differed with the frame rate.

## Lighting presets (game/light_presets.h)

The preset manager (WorldDir + 0x14c; Handle 0x82265010, Poll 0x82263070)
[C addresses]:
- set_lighting cats [adjectives [delayed]] (0x82264EB8): a symbol or a
  non-empty list replaces the categories, an empty list keeps them
  (0x82263868); a list replaces the adjectives, anything else (0) keeps
  them (0x82263988); delayed defaults to TRUE. set_excitement (0x82264F70)
  switches at once when the best preset changed. Both, and the keyframe
  steps, are ignored while ignore_lighting_events (+0x85) is set.
- Candidates (0x82264D38 / 0x82264770): per category in order, its
  presets with min_excitement <= ex <= max_excitement whose adjectives meet
  the requested ones (or either list is empty). Score (0x82261738): the
  category's index i of n gives n - i, each requested adjective j of m the
  preset has (m - j) x n, each preset adjective not requested -1; the
  top-scored are shuffled (0x82261D38) and the first wins.
- Switch (0x822629F8 -> 0x822627E8): delayed so that the new preset's
  fade_in ends 4 beats from now (now + 4 - fade_in / 480 beats; at once when
  that is <= 0; a delayed request restarts even the current preset);
  immediate only when it is another preset. During the fade the previous
  preset is applied over the current one at 1 - t (Poll). Starting a
  preset fires its `trigger` EventTrigger (small2's neon presets) [I].
- `chase` (85 chart events, no preset has it): no preset with adjectives
  meets it, so the categories' presets without adjectives win (the
  section's plain look) [C: the candidate rule].
- Preset SetFrame (0x82242560): automatic presets (0x822399A8) hold
  keyframe i for its duration from the sum of the earlier (duration +
  fade_out), then fade to the next over its fade_out (looping: the last
  fades to the first; else the last holds). Manual presets step on the
  queued commands (lighting_next / prev / first_keyframe, 0x8223FC58...,
  queued at now + 4 beats: the TRIGGERS note's time): a step starts at its
  time - the current keyframe's fade_out / 480 beats and fades until the
  note; next / prev wrap when looping, else clamp; steps queued before the
  preset started are dropped. Rate 1: 480 frames a beat; rate 0: 30 a second.
- TRIGGERS 48 / 49 / 50 -> do_lighting_*_keyframe four beats early; at
  excitement >= Great every note steps, else every other one
  (ignored_last_light_change) [C data: WORLD_OBJECT_BASE].
- Apply (0x82239BB8): a spotlight with animate_from_preset (+0x254):
  intensity and colour lerped, rotation slerped, target and flare taken
  when the weight reaches 1 (0x82232698); an Environ with
  animate_from_preset: ambient, fog colour and range lerped, fog off pushes
  the range out (0x82238920); a Light's colour / range and transform by its
  flags (0x822387E8). The state is World::spots() and the pose's environ /
  light state. Strobes are two looping one-frame keyframes at 30 fps [C
  data].

## Spotlights and flares (render/spotlight_view.h)

Spotlight::DrawShowing (0x822328E8) queues a spotlight whose intensity x
colour is not zero into the SpotlightDrawer (0x822312C8); the DX drawer
(ctor 0x8225F778) draws the queue (0x82260B40: beams 0x822601D0, then the
other parts) after the world [C addresses]. No SpotlightDrawer object is on
the disc: its defaults (0x8222F7B0: total 1, ambient 0 / 0.2, lit 0.2 /
0.2, white, 0.025, fog and lit ranges 0..1000). GH2 draws the beams with
its own shader (screen-space softness, constants 0x51 / 0x5a / 0x5b);
gd02 draws them through the material shader [I]:
- the light can: the light_can mesh at the spotlight (light_can_offset
  along the beam), lit by its directory's Environ;
- the beam (beam.material; all 341 beams are sheets, is_cone 0): a strip
  facing the camera from `offset` to `offset + length` along the light's
  -z (the presets' rotations point -z down on average [C data]),
  top_radius at the light, bottom_radius at the end; the sides fade over
  top_side_border .. bottom_side_border of the half width and the end over
  bottom_border of the length; angle_offset tilts it (degrees); colour x
  intensity x brighten, added; it fades when seen end-on [I];
- a target (the preset's or the object's) turns the light's -z onto it (a
  band slot: 35 above its waypoint) [I];
- the floor spot (spot_material): a square of half size spot_scale where
  the beam meets z = spot_height [I];
- the lens (lens_material): a disc of lens_size across the beam at
  lens_offset, as bright as the beam faces the camera [I];
- flares (the spotlight's when its preset entry says flare and
  flare_enabled; RndFlare objects; RndFlare draw 0x8220D6A8 [C]): a
  screen-space square size x the picture's width wide, lerped from size.x
  to size.y as it faces the camera, faded by distance over range (full at
  range.x, gone at range.y; equal values: no fade); tested flares (a
  spotlight's stored visibility byte 0, RndFlares always) are visible when
  the scene's depth at them is behind them (5 samples; GH2: a point test
  0x821E6F48), and step 1 / steps towards visible or hidden each frame. The
  test runs on the GPU from the HDR buffer's depth into a 128 x 1
  ping-pong texture the flare pass reads (no readback); without the post
  process flares are not tested.
- Spotlights, RndFlares and Waypoints have their Trans (gfx::transOf).

## Crowd (game/crowd.h, render/crowd_view.h)

A WorldCrowd (in `<v>_chars`) has archetypes and, per archetype, its
placements [C data: 15 crowds, 106 to 1959 placements a venue]. An
archetype (CharData, 0x44 bytes: +0x10 character, +0x14 height, +0x18
density, +0x1c radius, +0x20 the card RndMultiMesh, +0x2c the shown 3D
placements, +0x38 all of the shot's 3D ones) names a Character of the
`<v>_chars` directory, the proxy `crowd_male01` etc. (proxy file
`../../../char/crowd/og/crowd_male01.milo` -> `char/crowd/ng/gen/
crowd_male01.milo_xbox`, environ `crowd.env`) [C data]: a Character of type
crowd (skinned meshes, bones, the spot_head / spot_*-hand / spot_*-forearm
spots, hand variants, the inline `lighter_flame` RndDir, main.drv on
`char/crowd/anims/gen/{female,male1,male2}_main`, groups idle ok bad great
lighter_slow lighter_fast). There is ONE Character per archetype: every
placement of it shows the same pose, and crowds naming the same archetype
(fest, stone) share it [C: 0x82247310 draws one character per CharData].

- set_fullness a b (0x82247C08) [C]: per archetype the cards move between
  the shown list and a spare list until int(n_cards x a) show (truncated);
  the 3D list keeps the first int(n_3d x b) of the shot's 3D placements.
  The first argument is the cards' fraction (+0x64), the second the 3D one
  (+0x60).
- Per shot (CamShot StartAnim 0x82226120 -> 0x82247FD0) [C]: the shot's
  `start_shot` script runs first, then the crowd named by the shot's
  `crowd` field gets its crowd3d pairs (archetype index, card index): the
  old 3D placements go back to the cards, the named cards move to the 3D
  list (out of range: reported and skipped), the list is sorted and the
  fullness applied again. CamShot rev < 12 had a `hide_crowd` flag the
  loader turns into the default crowd (0x8222DE20) [C]; the flag left in
  the shots' properties is not used.
- camshot.dta start_shot [C data]: `{[crowd] crowd_update}` and `{[crowd]
  set rotate [crowd_face_camera]}`; a CamShot's `[crowd]` answers its
  WorldCrowd (game/crowd.cpp).
- rotate (0x82247310) [C]: x = normalize(z x camera forward), y = z x x
  (the camera's forward flattened, reversed), z up; otherwise the
  placement's rotation (all placements of a crowd share the placement
  mesh's rotation [C data]). A placement's z is the character's middle:
  the character stands at z - height / 2 [C: 0x82247310; data 47.5 = 95 / 2,
  and fest's 75-high archetype sits 10 lower].
- show_3d_only (+0x5c): no cards, and no card rendering (crowd_hide).
- crowd.dta (WorldCrowd type gh2) [C data]: `enter` asks the world for
  intro_start_msg, crowd_update, excitement, crowd_hide, crowd_lighters_*
  and crowd_*_tempo (`{$this dir} add_sinks ...`; the world forwards a
  message to its sinks once its own type has left it unhandled, World::
  addSink); intro_start_msg: set_fullness 0.5 0.5 and 80 % ok / 20 % great
  (an encore: Great); crowd_update (with `game`): show_3d_only FALSE,
  set_fullness Boot 0.1, Bad 0.25, Okay 0.5, Great / Peak 1 (widescreen
  the same), then the mix (Boot bad; Bad .8 bad .2 ok; Okay .8 ok .2 great;
  Great / Peak great, or the lighter group when lighters are on).
- iterate_frac (0x822452E0) [C]: the archetypes (those that loaded)
  shuffled, the fractions scaled to their count, an accumulator from -0.5;
  each archetype runs its fraction's script with $this = the archetype and
  `main.drv` its CharDriver (play_group, set beat_scale; kept per
  archetype for M9: crowd_half / double / normal_tempo set beat_scale 0.5 /
  2 / 1).
- The clips [C data / C 0x822452E0]: crowd.dta's crowd_update (each
  shot's start_shot) runs `animate` for [excitement_level] (the game's
  excitement, Game +0x338, via the world's `excitement`): Boot all bad; Bad
  .8 bad .2 ok; Okay .8 ok .2 great; Great / Peak great, or the lighter
  group (lighter_slow / lighter_fast) while [lighter] is on; iterate_frac
  gives each archetype `{main.drv play_group <g> $crowd.play_flags}`
  (NoBlend | GraphLoop; RealTime too at intro_start_msg, which plays .8 ok
  .2 great), so the clip changes on the cut; the group's next clip loops
  through the transition graph. The archetypes poll with the world (song
  beat x beat_scale; crowd_*_tempo set beat_scale .5 / 2 / 1).
- Hands [C data]: the clips' events `{$dude set_hand clap|devil|fist|
  lighter}` run char_objects.dta's Character type crowd `set_hand` (enter:
  clap): the hand meshes shown / hidden, `lighter_flame start / stop`.
  lighter_flame is a RndDir of type `world_fx` (rnd_objects.dta) [C data]:
  enter / exit `{$this stop}`; start `{$this animate (range 0 kHugeFloat)}
  {$this set_showing TRUE}`; stop `{$this stop_animation} {$this
  set_showing FALSE}` (and frees its particles: it has none). So the flame
  shows only between a start and a stop, and its filter (a texture-scale
  flicker looping on its MatAnim) runs from frame 0 at each start, 30
  frames a second [I: the clock].
- Random numbers: the shuffles, the drivers' clip picks and the scripts
  while an archetype is $this draw from the crowd's own RandomGen
  (Crowd::random, seed 0x43524F57), like the band's.
- Drawing (WorldCrowd draw 0x82247310) [C], per archetype: unless
  show_3d_only, the character stands at the origin turned like the crowd
  (rotate: x = normalize(z x camera forward), y = z x x, z = the placement
  mesh's z; else the placement mesh's rotation) and is rendered into its
  card texture through a card camera with the main camera's rotation, at
  d = max(h / 2 + near, |camera - (mesh + (0, 0, h / 2))|) back along the
  forward axis from (0, 0, h / 2), y fov 2 atan(h / 2 / d), the main
  camera's near / far; the texture is 256 x 512 32-bit per archetype with
  gfx_mode 1 (set at start-up, 0x82270F48; 0x822496A0), one shared 128 x 256
  16-bit one with gfx_mode 0. The card's rectangle comes from the projected
  feet (the origin), spot_head, both spot_*-hand and (when there) both
  spot_*-forearm: u from min(x, .25) - .05 to max(x, .75) + .05, v from
  min(y, .4) - .05 to max(y, .6) + .05, clamped to 0..1; the quad (four
  vertices, faces (0 1 2) (1 3 2), 0x822490F8) spans x = u h / 2 - h / 4,
  z = h / 2 - v h in the card mesh's frame. The card mesh has the
  FastBillboardXYZ constraint (8) on the card camera (0x822490F8 ->
  SetTransConstraint 0x821AFAA0), so the RndMultiMesh draws it with
  multimesh.fx's BILLBOARD variant (DxMultiMesh draw 0x8230F138: effect
  0x13 when the mesh's constraint is 8; `multimesh_2400011` on the disc has
  bit 25): each shown card placement keeps only its position and the quad
  is turned like the camera (x its right, y its forward, z its up) [C; C
  data multimesh.fx]. Cards always face the camera, never edge-on, and tilt
  with its pitch. The card Mat (0x82249368, offsets from Mat SyncProperty
  0x821ED930) [C]: use_environ (+0x40) and prelit (+0xa0) off, blend 1 =
  src (+0x2c), alpha_cut (+0xa1), tex_wrap 0 = clamp (+0x50); cull keeps
  the Mat default TRUE (Mat ctor 0x821ECC78, +0xb0), harmless as the
  billboard shows its front. Then the same character is drawn at each shown
  3D placement (turned as above, at z - h / 2).
- The card's alpha [C]: the card camera's Select (0x82308DC8) clears a
  rendered (type 2) texture's colour to 0x00000000 (and depth / stencil);
  the 360's Mat setup (0x8230A358) never reads alpha_write (+0xa2), so every
  draw writes alpha (D3D's default colour mask): the character's pixels
  carry their shader alpha, the rest stays 0. alpha_cut only switches the
  alpha test on (0x823BDA38); the device set it up once as ALPHAREF 0,
  GREATER (0x82303768) for every Mat, so a card pixel shows when its alpha
  is above 0 (the filtered edges included, darkened by the black clear).
  gd02's alpha_cut threshold is 0 for every material (render/material.cpp).
- The port (render/crowd_view.h) renders the card mid-draw as GH2 does and
  binds the world's target and viewport again (RenderDevice::
  currentTarget / currentViewport); one card texture per character. It
  clears the card to 0 and writes alpha in the card pass (forceAlphaWrite)
  as the 360 does. `GD02_CROWD=0` leaves the crowd out.
- "Crowd hanging from the ceiling" (arena intro, reported with the M8
  renderer; stone's intro the same): the camera, not the crowd. The old
  look-at turned the camera upside down when the target was high above it
  and the old path put arena's intro under the floor (Camera director,
  bug-fix GD02-092); seen from below the floor the crowd hangs from the
  ground. Cards face the camera from any view (the billboard above).
- Without GH2's menus and without the director's `game` (the dev modes),
  intro_start_msg / excitement / a shot apply the same fullness natively.

For the camera director: `world.crowd().startShot(shot)` after the shot's
`start_shot` (CameraDirector::onShotStarted, wired in app.cpp);
`startShot(shot, true)` also runs the script part for a host that does not
send start_shot (the viewer).

### Crowd: what still differs or is not confirmed

Checked against the code only: there is no capture of the original yet
(backlog Q1), so nothing was compared frame by frame.

- gfx_mode (FUN_82270c08): the port always runs mode 1 (a 256 x 512 32-bit
  card per archetype); mode 0 (one shared 128 x 256 16-bit card) is not
  reproduced. Which mode the retail 360 runs is set at start-up
  (0x82270F48) [I: 1].
- The card texture's sampling: linear, no mipmaps [I]; GH2's render target
  is resolved from EDRAM (format and filtering not traced).
- The crowd's random numbers come from its own generator (GH2 has one
  global one), so the archetypes' shuffles and clip picks differ from the
  original's sequence while following the same rules.
- The level loops are taken to loop, and their pans (1, 3, 34) to be
  plain stereo [I]; the excitement_loop delay and the flame's animation run
  on the world's clock [I].
- crowd_volume, crowd_audio_delay and crowd_reactions are config/sound.dta's
  values as constants, not read from the file.
- `crowd_audio play_stream 0.0 wingame` (stone's ending) is a logged stub.

## Crowd audio (audio/crowd_audio.h)

GH2's `crowd_audio` (CrowdAudio ctor 0x822E0928, Handle 0x822DFD98) [C
unless noted]:

- The streams: the venue type's `(sound (bank b) (crowd (intro dB file)
  (levels (kExcitementBoot dB file) ...) (encore_outro dB file (loop_ms a
  b)) (encore_intro dB file)))` [C data], files `world/<v>/streams/<file>
  .mogg` ("world/%s/streams" 0x8204958c): plain Vorbis moggs, stereo,
  22050 Hz (theatre 44100); Peak reuses Great's file. Played on their own
  SDL stream (SDL mixes it with the song's), decoded once on a worker
  thread. One stream at a time: a new one deletes the old (0x822DF860, no
  cross-fade). The entries' `(pan n)` (1, 3, 34) set the two channels' pans
  to -n / +n (0x822DF860) [I: clamped to full left / right, plain stereo].
- Volume (0x822DF2F8): the entry's dB + set_volume's dB (+0x2c) +
  crowd_volume (config/sound.dta: -2 dB, +0x30) + the options' sound
  effects volume (0x822DF140: 0 dB in the attract mode), set when a stream
  starts and on set_volume.
- Enabled (+0x38): the mode's crowd_audio at construction (modes.dta: FALSE
  in practice and tutorial; the port reads it at the world's enter);
  set_enabled (0x822DF9D8): off deletes the stream, on runs the excitement
  rule below with the last level.
- intro_start_msg ($fast $encore) (0x822DFCA8): the crowd_begin reaction
  (+0x64, encore or not), then encore_intro when $encore and not $fast,
  else intro.
- music_start: started (+0x3a), the last level (+0x40) = Okay, then the
  excitement rule with the game's excitement (Game +0x338). Every chart has
  [music_start] [C data: 74 / 74].
- excitement (0x822DF590), only once started and while enabled: the
  reaction for last -> level (also for the same level), the last level =
  level, and an `excitement_loop <level>` message crowd_audio_delay later
  (config/sound.dta: 1 s, +0x34); each such message plays its level's
  loop (0x822DFD40), so two changes within a second play both loops in
  turn. Game excitement (0x822CAAB0, set by 0x822CBAC8 when it changes,
  polled 0x822CDEB8): the rock meter (a competitive mode's: the better of
  the two players'; otherwise the performer's, co-op's band) under the
  warning level (Game +0x340) -> Boot, < 0.33 Bad, < 0.66 Okay, else
  Great; Peak (4) is never set by the game.
- encore_outro (0x822E1688): the encore_outro stream looping between its
  loop_ms.
- turn_off (0x822DF0E8; ui lose.dta, endgame.dta, multiplayer.dta): only
  the stream is deleted. game_lost plays crowd_lose and leaves the stream
  (the music slows and stops, the crowd plays on until the lose screen's
  turn_off). set_paused follows the game's pause.
- Reaction sounds (0x822DF350, when crowd_reactions, config/sound.dta TRUE,
  +0x39): +0x64 crowd_begin, +0x68 crowd_win at game_won_msg, +0x6c
  crowd_lose at game_lost and per excitement +0x70: index (level + 14) x 2
  + (previous <= level), `crowd_<dnto|upto>_<danger poor norm good peak>`
  (0x822DFAB0); a new one stops the last. They are the bank's Sfx and play
  (docs/audio_and_charts.md "Sound effects"); names a bank lacks are
  silent.
- [I] The level loops loop (their preloaded streams, +0x9c, 0x822DF860's
  second path); the excitement_loop delay runs on the world's clock.

### The sound banks

`world/<v>/gen/<v>_bank.milo_xbox`: Sfx, SfxSeq, RandomGroupSeq,
SerialGroupSeq, WaitSeq, FxSendReverb and the SynthSamples (XMA2). Their
layouts, how they play and gd02's Synth are in docs/audio_and_charts.md,
section 4 "Sound effects". The venue cues play: claps, crowd reactions,
crowd_win / crowd_lose, encore_yes / no, blew_bignote, vroom; the UI's
play_sfx / play_meta_sfx of a venue cue (crowd_win, encore_yes) finds it in
the loaded venue.

## Two players, encores and the venues' extras

- _mp / _coop worlds [C data]: the world's enter sends set_multiplayer /
  set_singleplayer and set_player1/2_guitar / bass to the chain's types
  (world_objects.dta RndDir lighting / chars / geom): (single|multi)player
  _hide_objects hidden or shown (big's Curtain.grp in face-off), the amp
  groups (amps_left/right_guitar / bass.grp in small1, small2, big,
  theatre) shown and their triggers enabled / disabled. Face-off facing
  (faceoff_active_players) is the director's filter.
- Encores [C data]: intro_start_msg $encore -> play_encore_fx (or
  skip_encore_fx when $fast), the venues' triggers: arena's city / grim /
  squid, battle's encore video screen, big's curtain, fest's crypt
  explosion and zombie, small1's four bottle throws (proxy start), stone's
  UFO, theatre's rock ball; encore_outro (1.75 s after game_won_msg) ->
  big's curtain, fest's star field, and the encore_outro crowd loop.
- The drummer explosion (battle, small1 one-player worlds only [C data]):
  drummer_explode.trig calls the drummer's hide_drummer (M9) and the
  `big/ng/drummer_explode_proxy.milo` proxy.
- small1 bottle_throw (RndDir type, small1.dta) [C data]: start -> after a
  random 0-10 s show and throw (frames 0-63 over 1.5-2.1 s), again every
  6-10 s, each proxy with its own `throw_task`: a proxy directory's script
  object has a name scope (ScriptObject::nameScope), where its tasks' names
  live and its lookups look first [I: GH2 names objects in their own
  directory].
- small2 neon_controller (RndDir type) [C data]: the lighting presets'
  triggers (neon_all_on.trig...) call its all_on / sweepup / random...:
  a beat thread_task over its nine EnvAnims (`[env1] animate (dest 10)
  (period 0.01)`).
- arena sparks [C data]: chorus x peak_on -> sparks.filt and sparks_on.filt,
  4 s later sparks_bounce.filt; verse / solo / peak_off -> sparks_off.
- stone's star message: `../eng/starmessage.milo` per language (eng fre deu
  ita esl), campaign_won -> star_message_GO.trig.
- fest's `song_movie fest_1` and `reduced_crowd_shots` are not used on the
  360 [C: neither symbol is in default.xex's image; fest_1 is
  `world/shared/fest_1.ipu`, a PS2 IPU movie listed in arkorder.dta and not
  in the ark].

## Tests and aids

- `--venuetest`: 8 venues x sp / mp / coop: the chain and proxies load, no
  undecoded objects of a decoded class, no unexpected raw class, every
  reference resolves, the scripted mini-song runs with no unknown names,
  unhandled messages or script errors; the unused milos are the expected
  ones.
  Per variant also the crowd (tests/venuetest_crowd.cpp): archetypes load,
  intro / excitement fullness and shown counts, the iterate_frac mixes,
  tempo, a shot's crowd3d and rotate, crowd_hide; the crowd audio streams
  decode and play in order (intro, the level after 1 s, encore intro /
  outro, loop_ms); the multiplayer hides and amps; play_encore_fx,
  drummer_explode (one-player battle / small1), arena's sparks, small2's
  neon EnvAnims, small1's four bottles.
- `--chartest` crowd (tests/chartest_crowd.cpp): arena's crowd for two
  minutes at 120 beats a minute, the excitement changing every 8 s with a
  crowd_update after each, lighters from 64 s, half tempo from 96 s: every
  archetype loads and enters (clap hands), at least four groups play, the
  clips' events switch the hands, the lighters show, beat_scale 0.5; the
  (beat, archetype, clip / hand) log plus each archetype's card rectangle
  every 4 s for a fixed camera: line count and FNV hash
  (`GD02_CROWD_LOG=<file>` writes it).
- `--view <venue>` or `--view world/<v>/gen/<v>[_mp|_coop].milo_xbox`:
  shots (Left / Right, Up / Down keyframes), presets (P), events (1-5, K, B,
  E, G, O, X); `GD02_VIEW_CAM`, `GD02_VIEW_SHOT`, `GD02_VIEW_CATEGORY`,
  `GD02_VIEW_PRESET`, `GD02_VIEW_TIME`, `GD02_SCREENSHOT`;
  `GD02_VIEW_EXCITEMENT=<0..3>`, `GD02_VIEW_ENCORE=1` (encore intro,
  play_encore_fx; 2 also encore_outro); each shot starts the crowd's shot;
  the second line counts the crowd shown / drawn and its draws.
- `GD02_WORLDLOG=1` logs the triggers fired, the anims that end, the
  shots and the presets.
- `--venuetest` also plays hangar18 in arena with the autoplay bot through
  the world, the director and the presets with fixed seeds
  (tests/director_golden.h): the (beat, shot) / (beat, preset) log's line
  count and hash must match; `GD02_GOLDEN_LOG=<file>` writes it.
- `--venuetest` camera (tests/venuetest_camera.cpp): every CamShot of
  every variant framed (CameraDirector::evaluate: no filter, no shake, the
  band slots at their waypoints) every 5 frames and at its end: a finite,
  orthonormal camera, not upside down (up z >= -0.5), not under the venue
  (more than 10 below the lowest waypoint and below every up-facing static
  triangle over its spot), and with the same targets in both keyframes the
  targets' mean at the lerped screen offset (within 0.02).
- `--venuetest` camera rate (tests/director_golden.h): in each venue its
  first song, the band and the director stepped at 60 and at 240 fps for
  60 s (`GD02_CAMRATE_SECONDS`): the same shots and positions within 5
  units every 1/12 s (`GD02_CAMRATE_LOG=1` prints both).
- `GD02_FORCE_SHOT=<CamShot>`, `GD02_FACE_CAM=<member>[:<distance>[:<degrees>[:<bone>[:<height>]]]]`
  (e.g. `bassist:40:180:bone_pelvis.mesh:10`, from behind) frame a shot or a
  band member; `GD02_WORLDLOG=1` lists each shot's planes and hide list.
- `GD02_SPOTS=<letters>` draws only some spotlight parts (c cans, f floor
  spots, b beams, l lenses, F flares); `GD02_SPOTS_LOG=1` logs the lit
  spotlights and flares once a second.

## Open

- CamShot: clamp_height, fade_time; focal_target /
  parent order (from field offsets) [I].
- EventTrigger anim entry: the u8 / f32 order of wait / delay [I]; delays
  are in the anim's units [I].
- Animatable's first u32 (a float: 160.0 on CamShots).
- RndDir::SetFrame on proxies (all its anims) [I].
- TRIGGERS-track lighting texts; where Peak comes from natively.
- Director: the ease function (tanh assumed), the shake angles' axes
  (0x8216DB48's order); the order of downbeat and one_bar_to in one frame
  (two cuts in the same beat happen); TransAnim repeat_trans / follow_path
  on camera paths (none on the disc).
- Spotlights: GH2's beam shader (screen-space softness, fog), the floor
  spot's plane and shape, the lens orientation, damping_constant,
  target_shadow, additional_objects, xsection / cutouts (unused on the
  disc).
- script_task keeps the values of every $variable its body names at
  creation, and a value the task sets stays set [I: later Harmonix code's
  ScriptTask]; GH2's own rule unread.
- Crowd: the card texture's alpha (above); lighter_flame's show / hide;
  `$cheat_crowd_heads` (eyeball / monkey heads) is never set; the CamShot
  crowd3d second value is a card index [I: 0x82247FD0 walks the card list].
- Sound banks: no reverb sends or ADSR (docs/audio_and_charts.md section
  4); crowd_audio play_stream (stone's wingame.mogg after the campaign) is a
  STUB.
