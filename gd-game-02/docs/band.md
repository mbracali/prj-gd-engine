# GH2's band: characters, clips, drivers, the pose and the band on stage

Milestone M9, first phase: the band's core. Every character class on the
disc decoded; the venue's band slots filled with the characters GH2 puts
there; their clips played by GH2's CharDriver chain on the song clock; the
servo, IK, twists, hair and look-ats posing the bones; the MidiParsers
driving the hands, the fret position and the band's text events; the
guitarist's native side (modes, one-shots, Star Power, game over); the drum
kit answering the drummer's hits; the camera director aiming at the real
band. Walks and interacts (M9.6), faces (M9.8, FaceFX), the crowd's clips
(M9.9) and customisation / endgame photos (M9.10) are the next phases.
Addresses are in `default.xex` (`analysis/`); **[C]** confirmed in code or
data, **[I]** inferred.

Code: `char/char_objects.*` (decoders), `char/clip.*` (the channel buffer
and clip sampling), `char/char_driver.*` (CharClipSet and CharDriver at run
time), `char/skeleton.*` (transforms), `char/rig.*` (IK, twists, hair,
look-ats, weights, poll order), `char/character.*` (a character: load,
outfits, servo, LOD), `char/midi_parser.*` (MidiParser), `game/band.*`
(the band on stage, GuitarCharacter), `render/band_view.*` (drawing, the
`--view char/<c>` viewer), `tests/chartest*.cpp`, `tests/miditest.*`
(`--chartest`).

## Classes

Each record starts with the class's own revision; "hdr" is the Hmx::Object
header (0x82315350), "Wt" CharWeightable (0x8216A190: u32 rev, f32 weight,
rev > 1: ObjPtr weight_owner). ObjPtrs are names. `--chartest` decodes every
record of every class on the disc to its end and asserts the counts.

| Class | Ver | Load | Count | Fields |
|---|---:|---|---:|---|
| Character (own) | 10 | Pre 0x82153CF8, Post 0x82157678 | 107 | RndDir v9 pre/post, LODs {f32 screen_size, Group}, shadow Group, u8 self_shadow, sphere_base, CharacterTest v6 (editor) |
| Character (venue slot) | 10 | same | 85 | RndDir post only: placement, proxy `../../../char/<c>/og/<c>.milo`, environ |
| BandCharacter | 1 | Pre 0x82126048 | 19 + 16 | then Character's |
| CharClipSet (own) | 16 | Pre 0x82167468, Post 0x82168E08 | 82 | ObjectDir v17, hdr (type), blend_width, play flags, default flags, per clip {name, flags, u32 [I: its size in memory]}, move_self, recenter (2 lists + u8), type, type version |
| CharClipSamples | 11 | 0x82162C68 | 1994 | CharClip v5 (0x821A0BF0) + three CharBonesSamples (0x821A3030 / 0x821A1500) |
| CharBone | 3 | 0x8215FDF0 | 5797 | a set's skeleton entry "bone_x.trans": Trans, u8 position, u8 scale, u32 rotation type, u32 delta type (9 none), f32 weight |
| CharClipGroup | 1 | 0x821775A8 | 330 | clip names, u32 (0 / 0xcdcdcdcd) |
| CharClipFilter | – | 0x82163F20 | 82 | hdr only |
| CharDriver | 5 | 0x8216A728 / 0x8216BE80 / 0x8216C800 | 58 | hdr, Wt, bones (the servo), clips FilePath, u8 realign (+0x54), u8 (+0x55: no ScaleDown), starved clip |
| CharDriverMidi | 3 | 0x82184AB0 / 0x82185590 / 0x82185600 | 38 | CharDriver + inactive clip |
| CharIKHand | 5 | 0x82174470 | 38 | hdr, Wt, hand, finger, targets, u8 orient, stretch, (recompute lengths), move_elbow |
| CharIKMidi | 4 | 0x82184950 | 19 | the fret bone |
| CharHair | 2 | 0x8217FAF0 | 92 | 6 params, strands {root, angle, points, 2 x 3x3}, simulate |
| CharLookAt | 3 | 0x8216EBB0 | 36 | hdr, Wt, source, pivot, dest, 8 f32 (smoothing s, yaw / pitch limits deg, an angle fade), allow_roll |
| CharEyes | 4 | 0x82172708 | 18 | look-ats, view_direction, interests (none) |
| CharWeightSetter | 3 | 0x8218DFC0 | 38 | hdr, Wt, driver, flags (kGuitarLeftIK / RightIK), invert |
| CharUpperTwist / CharForeTwist | 1 | 0x82182C00 / 0x82182018 | 88 / 79 | arm and twist bones (fore: f32 offset deg) |
| CharServoBone / CharWalk | 1 | 0x82188870 / 0x82189F80 | 57 / 19 | hdr only |
| CharPosConstraint / CharIKRod | 2 | 0x82179178 / 0x821806F8 | 4 / 4 | inactive on the disc (no targets / dest) |
| OutfitLoader | 2 | 0x8215B528 / 0x82157CC8 | 20 | hdr (type guitar / drummer), base path, categories |
| FaceFxLipSyncServo | 5 | 0x8218E378 / 0x8218FAF0 / 0x82192470 | 22 | hdr, Wt, .fac, viseme set, eye entries (M9.8) |

### Clips [C]

- CharClip v5: start, end, frames per second (beat clips: frames are
  beats; realtime clips 30 fps), flags (+0x24), play flags (+0x28: blend
  0xf, loop 0xf0, time 0xf600), blend width (+0x30), range (+0x34: a random
  start phase, below), relative (1 only on the 23 `visemes` clips),
  transitions {dest clip, (src frame, dest frame)...}, enter / exit event
  scripts, events {frame, script}.
- Three sets of channels "bone_x.<type>" (pos 0, scale 1, quat 2, rotx..z
  3-5, drotx..z 6-8; type from the suffix 0x8215D520): full (sampled over
  the clip), one (one static sample), third (deltas: no channels on the
  disc). Samples: pos / scale 3 f32, quats s16 / 32767, rotations s16 x
  1/1638.4 rad. On the disc: no scale or roty channels; rotx only on the
  drummer's fingers. A fourth set, bone_facing_delta.pos / .rotz, is
  derived at load from bone_facing (0x82162AE8).
- The bone a channel moves: "bone_x.trans", else "bone_x.mesh" (0x82192DD8).

## Characters on stage (game change_chars 0x822B7670) [C]

- The venue's `<v>_chars` milo holds the slots: BandCharacter guitarist0 /
  guitarist1, Character singer / bassist / drummer (a keyboardist takes the
  singer slot), crowd archetypes (type crowd, not slots).
- guitarist<p> <- player p's character, `char/<c>/og/<c>.milo` (og -> ng).
  Quick play: the song's `(quickplay (character_outfit X) (guitar G))`
  (set_quickplay 0x822B86D0; every song has one) until the character
  screens choose (M9.10).
- The band <- the song's `(band ...)` (11 songs; 5 with female_singer:
  crazyonyou, tattooedloveboys, ftk, lesstalkmorerokk, yeswecan), else
  gh2.dta `(default_band metal_singer metal_bass metal_drummer)`: "sing" or
  "key" -> the singer slot, "drum" -> drummer, "bass" -> bassist. Two
  players: the drummer only. Empty slots are deleted (no singer in gemini,
  soybomb; no bassist in jordan).
- Outfits (OutfitLoader, merged by 0x82158F30): the guitar
  (change_guitar 0x822B6250: guitars.dta `(guitar (skins (skin (outfit O)
  (mat M))))` -> `char/ng/guitars/O.milo`; the skin's mat goes on
  `guitar.mesh`, 0x822B2148; quick play takes the guitar's first skin [I:
  0x822C6270]) and the drummer's kit (change_drums: `dw_<venue>_drums`).
  Merge rule per outfit object: new names are added; a "bone_*" duplicate
  keeps the character's object (a Group among them gets the outfit's
  members); another duplicate whose Trans parent is a "bone_*" takes the
  outfit's data (kMerge: the guitar's neck spots); the outfit's LOD and
  shadow groups join the character's by index.
- Placement: the slot's transform, then BAND_COMMON / guitarist `enter`
  teleports to `{waypoint_find kStart*}` (Character::Handle 0x82152108:
  teleport 0x82151D08 = the waypoint's world transform, scale removed).

## Clips at run time (char/char_driver.*) [C]

- **Selection.** `play <int>`: the first clip of the set's LRU list (+0x8c)
  with all the bits, moved to the back (0x82165038). A name: that clip, or a
  CharClipGroup's next clip (0x82165460): round robin, index first
  incremented (0x82175AB8 [I: the start index 0]). A list: any element
  (0x8219C8B0). `play_if_safe flags pf safe length`: the first LRU clip
  with `flags` that is not (safe-flagged and at least `length` long)
  (0x8216C3A8 / 0x8216A498). Default play flags kPlayLast (4) (0x8216B468).
- **A new node (0x821A4BA8).** Flags: the clip's own (+0x28), the call's
  non-zero blend / loop / time fields over them. Older nodes that have not
  started are dropped (0x821A4AF0). The clip's enter event runs ($dude = the
  character). NoBlend deletes the chain. Transition (0x8219E598) from the
  older clip's table: kPlayFirst the first edge to the clip with src >= the
  older frame (0x8219E4A8), kPlayLast the last (0x8219E520); otherwise (or
  kPlayNow) src = the older frame (kPlayLast: not before its end less half
  the new blend width), dest = the new start at the same phase of the new
  clip's beat align. delay = src - older frame, frame = dest, weight 0. No
  older node: frame = start, weight 1, and with a range > 0 a random phase:
  start + mod(frame + random(0, range) - start, (end - start) / 2). Dirty:
  weight 1e-6. Explicit starts (CharDriverMidi) give frame and delay.
- **Advance (0x821A4E30), oldest first.** dt = the older node's frames this
  poll, else realtime ? seconds : beats x beat_scale (TaskMgr 0x82311988
  beat, 0x82311998 delta beats, 0x82311970 delta seconds; seconds are not
  scaled). The delay counts down; then frame += rate x (beats, or fps x
  seconds; UserTime: no advance) plus the beat-align correction (0x821A4A50:
  mod(beat - frame, align) into (-align/2, align/2]; not for kPlayLoop or
  rate != 1), events with old < frame < new fire in order, wrapping
  (FUN_821A4B40), weight += advance / blend width (1 at once when 0). At
  weight 1 the older nodes are freed (their exit events run). kPlayLoop
  wraps past the end. Chain weight: w' = 0.5 - 0.5 cos(pi w) (FUN_8226E050
  is sin), the new over the older.
- **Poll (0x8216AF70).** Realign (drummer): when the song beat x scale
  passes a whole beat by other than one, the chain's align bits are cleared
  and the newest started clip replayed with GraphLoop | Dirty. One node
  left: the starved message to the character; GraphLoop replays the clip
  with kPlayLast (it loops through its own graph); NodeLoop replays the last
  selection with 0x44. Then advance; then the newest clip's channels times
  (1 - chain weight x driver weight) (ScaleDown, unless +0x55) and each node
  adds its eased share (0x821A4988), newest first.
- **set_beat_scale (0x8216AE90):** the first started node and the older keep
  their speed (rate x old / new, align cleared) and the clip is replayed
  with GraphLoop | Dirty.
- **Queries:** flag weight (0x8216A588: eased weights of the chain's clips
  with any of the flags), first playing flags (0x8216A6E0: the newest node
  with weight != 0).

## The pose (char/character.*, char/rig.*) [C unless noted]

- **The channel buffer (CharBones).** A driver's clip set lists its
  skeleton (its CharBone entries) and, when its clips carry bone_facing,
  bone_facing.pos / .rotz and (move_self) bone_facing_delta (0x821674D0).
  ScaleAdd (0x82162570): the static set's sample, the full set's floor and
  ceil samples at t (weights (1 - f) w and f w, 0x821A1DB0), the root delta
  (bone_facing at t minus at t - dframe / length, the position turned into
  the earlier facing, 0x82162378). Quaternions add sign-aligned to the
  accumulator, |w| on xyz and w on w (0x8215DF28); each channel keeps an
  accumulated weight too.
- **The servo (CharServoBone Poll 0x82189408).** PoseMeshes (0x821A51E0):
  pos sets the local position, quat (normalised) replaces the rotation
  (0x8226A190), rotx / y / z replace it with a single-axis rotation
  (0x8217B1C0 / 0x8214C240 / 0x821A50E8), scale rescales the rows last.
  Root motion: with bone_facing_delta channels ("move self", 0x821895C8)
  the character itself moves by the deltas in its own frame (0x821886E8);
  otherwise bone_facing goes under the pelvis (0x82188640). The deltas are
  cleared after (FUN_821885D0).
- **Poll order** (Character Poll 0x82156B78, sort 0x821563C8): each
  pollable after the ones that change what it reads (each class's
  dependencies, vtable slot 4) [I: the hierarchy rule]. alterna1: main.drv,
  right.weight, right_hand.drv, left.weight, left_hand.drv, bone.servo, the
  look-ats, hair, fret.ik, CharEyes, right_hand.ik, upper / fore twists,
  left_hand.ik, ...
- **Weights:** a CharWeightSetter's weight = the main driver's flag weight
  of kGuitarLeftIK / kGuitarRightIK (or 1 - it, invert) (0x8218CFB0); the
  hand drivers and hand IK take theirs from it (weight_owner).
- **Hand IK (0x82173308):** the target's world position (several: weighted
  by 1 / offset); the hand moves toward it by the IK weight; the elbow bend
  from the law of cosines (cosine clamped to [-0.985, 0.88]); orient blends
  the hand's rotation to the target's.
- **Fret IK (CharIKMidi, Handle 0x821838E8, Poll 0x82183410):**
  `midi_parser spot_neck_fretNN start end` blends bone_fret from where it is
  (relative to the spot's parent, so it rides the guitar) to the spot by
  0.5 - 0.5 cos(pi p) until the note.
- **Twists:** upper arm (0x82182738) spreads the arm's twist over its two
  twist bones; fore arm (0x82181980) a third of (offset + the hand's twist).
- **Eyes (0x82170BA8 / 0x82170130):** not the camera: points ahead of the
  head, picked again after 8 s [C]; **look-at** (0x8216E4E8) smoothing and
  yaw / pitch limits. **Hair (0x8217B898):** one step per poll [C].
- **LOD (DrawShowing 0x82153090):** from min_lod (`set_min_lod`, the camera
  shots' force_char_lod), the first LOD i whose screen_size x (1.09 below
  the current LOD, 0.91 from it on) the screen size reaches; FUN_82151DE0
  shows its group and hides the old one. Screen size (0x8214FC60): the
  sphere radius over its depth, scaled by the camera [I: the camera
  fields]. The shadow group is not drawn in the colour pass [I: GH2 draws
  it into the shadow buffer; gd02 has no shadow pass yet].

## MidiParser (char/midi_parser.*) [C unless noted]

Made from `config/midi_parsers.dta` (ctor 0x8233F9F0, init 0x8233D130,
params 0x8233CF08). track_name: the first of its values that names a track
(0x8233EA68). Types: midi (notes; key = pitch or (lo hi)), gem (the track's
gems; key = fret 1..5 for a single gem, (frets...) a chord holding all of
them, `()` any, `default` any single gem: 0x8233D5C8), text (text events as
symbols through the mapping), echo (text events as messages: `[play]` ->
`play`, 0x8233EE88). `[map X]` switches the mapping (parsers with mappings,
0x8233F440). Per event (0x8233EFA0): the mapping's params over the
parser's (a mapping's idle_event overrides); the first matching entry's
value with $mp.length / prev_start / prev_end (0x8233D7E8); without eval a
list value picks one at random (0x8233EB88); start + start_offset, end
(zero_length: start) + end_offset; the previous end moved to start - gap
(gap clamped to min_gap..max_gap); idle events in gaps of idle_length or
more; the length clamped to min_length..max_length. inverted TRUE: each
event runs from the previous event's end to its own start (0x8233F728),
so hands arrive on the note. Poll (0x8233E5B8): enter_events first, then
at most one due event per poll: the value itself as a message (echo:
`{sink play}`) or `{sink midi_parser <value> <start> <end>}` (beats).
Text events no script answers fall through: BAND DRUMS `[allbreat]`,
`[all_beat]`, `[allplay]`, `[double]`, `[doubletime]`, `[doulbe_time]`,
`[halftime]`; BAND SINGER `[dile]`; BAND BASS `[wail_on]` / `[wail_off]`.

- CharDriverMidi (0x82184B28): the clip starts at frame -(end - start) with
  that blend width, reaching full weight and frame 0 on the note; a looping
  clip already newest is left alone.
- Wiring (guitarist enter): player<p>_strum (bass_strum for a bass player)
  -> right_hand.drv, player<p>_fret -> left_hand.drv, player<p>_fret_pos ->
  fret.ik, player<p>_parser -> the guitarist; band members: their
  `(parser ...)`.

## The guitarist (GuitarCharacter; game/band.cpp) [C]

- Enter (0x822D3B18): mode idle, bad group "bad", hands on, run-time clip
  flags ORed into groups (0x82175C58): interact_* 0x30000; star_power,
  sync_jump / wag / head_bang 0x20000; win / lose / win_finals 0x40000;
  star_power 0x80000.
- filter_clips (game 0x822B6650, on the set's enter): every group shuffled
  (0x82176238: swap(i, random(i, n))); `<venue>_exclude`
  (`_multi_exclude`) dropped; the other anim tempo dropped
  (delete_domain_any song_tempo 3 TRUE); one player: win (the campaign's
  last song) or win_finals, sync_head_bang, sync_wag dropped; two players:
  multi_exclude, the clips the other guitarist's `shared` group has, walk_*,
  win_finals, interact_* dropped; then trims (0x8219C368: clips marked keep,
  play flags 0x10000, count first, the last others get the mark, the rest
  go): intro 1, lose 1, normal 5, extreme 5, win 1, win_idle 1, lose_idle 1,
  star_power 3. Band sets: delete_domain_any song_tempo 3 TRUE.
- Messages (Handle 0x822D2FE8): play -> normal, idle -> idle, wail_on ->
  extreme, wail_off -> normal if extreme, gtr_solo_on / off -> solo /
  normal (0x822D24A0 / 0x822D26A8: not while walking; a blending 0x20000
  clip -> the group with kPlayLast, a newest 0x20000 clip -> wait, else
  kPlayNow); band_jump / sync_wag / sync_head_bang -> a Dirty one-shot of
  sync_jump / ... (0x822D2780: not while walking or in Star Power; wag and
  head bang only with two players); set_game_over (0x822D22E0);
  lose_teleport (0x822D28E8: a singer within 72 -> the nearest walk spot);
  playing_starpower (0x80000 weight > 0 and 0x4000 weight <= 0),
  playing_far_starpower, actually_walking (M9.6).
- Poll (0x822D2B90): an unstarted interact due within this poll is dropped
  (excitement < 2); when the chain is settled the current group (the bad
  group while the rock meter is under 0.2, 0x822C6DC0) plays with kPlayLast
  (0x204 for idle after a realtime clip); interacts and walks are M9.6.
- Star Power (the game -> 0x822E3840 -> 0x822D25A8): unless a 0x20000 clip
  blends, a task plays the next star_power clip with kPlayFirst at once.
- Game over (0x822D22E0 / 0x822D2038 / 0x822D1F80): the mode becomes win /
  win_finals / lose; a win starts 0.75 s later, a loss at once, NoBlend;
  then win_idle / lose_idle (kPlayLast | RealTime) when two players play
  (or an encore follows a win, 0x822CA6E0).

## The band members (char_objects.dta, run by the interpreter) [C data]

BAND_COMMON / the drummer's type answer the echo parsers' play / idle /
tempo / double_time / half_time / allbeat / nobeat, the world's downbeat
(20 % re-pick), game_won_msg / game_lost, band_jump. The drummer: realign
TRUE, starts kBandIdle. The drum kit: the drummer's clip events
`{handle ($dude hit_hihat)}` (x194) / `hit_snare` (x58) and the world's
kick_drum / crash_symbal (BAND DRUMS 36 / 37, the drummer a world sink)
reach the drummer; what no script answers fires the EventTriggers of its
directory, the merged kit's kick / snare / hihat / crash.trig (EventTriggers
register with their directory's MsgSource, 0x822578E8) [I: the routing].

## Camera director hooks (game/camera_director.cpp) [C]

cam_check_shot (0x822B2240): guitarist0's 0x80000 weight > 0 needs
starpower_ok, its 0x4000 weight far_starpower_ok; bad_waypoints against the
waypoint nearest the character with kWalkSpot | kSoloWalkSpot (mask 0xc0,
squared distance, no radius; Waypoint::Nearest 0x82195548), looked up once
per beat (the M8 192-unit radius was wrong). Targets and parents {entity,
subpart}: a band member and its bone. `world set_min_lod` sets the members'
min LOD. guitarist0's playing_starpower / actually_walking come from the
band.

## Tests and aids

- `--chartest <disc>`: every Char* record decoded with the counts above;
  clip quats unit; golden poses against the reference parser; every
  character (37), guitar (59) and kit (8) loads with every bone resolved;
  the MidiParser golden (hangar18) and unknown texts; the band golden:
  hangar18 in arena through the world, director, lights and the band,
  autoplayed with Star Power deployed: (beat, member, clip) log hash, drum
  kit triggers, no non-finite transforms.
- `--venuetest`: the director golden now runs with the band.
- `--view char/<c>` (`GD02_VIEW_CLIP`, `GD02_VIEW_FRAME`, `GD02_VIEW_YAW`...):
  one character posed. In game: `GD02_CHAR=<outfit>`, `GD02_GUITAR=<guitar>`,
  `GD02_NO_BAND=1`, `GD02_BANDLOG=1`, `GD02_FORCE_SHOT=<CamShot>`.

## Open

- Walks, interacts, the walk schedule (M9.6); faces (M9.8); the crowd's
  clips (M9.9); character / guitar choice and photos (M9.10).
- The shadow pass (the shadow group); hair collision types 1-4; the eyes'
  retarget threshold; the face-off "doing badly" rule (score lead);
  CharDriverMidi's inactive clip (+0x74 / +0x78 meaning).
- CharClipGroup's starting index; the hand drivers' order among equal
  dependencies.
