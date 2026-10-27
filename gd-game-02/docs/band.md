# GH2's band: characters, clips, drivers, the pose and the band on stage

Milestone M9: the band. Every character class on the disc decoded; the
venue's band slots filled with the characters GH2 puts there; their clips
played by GH2's CharDriver chain on the song clock; the servo, IK, twists,
hair and look-ats posing the bones; the MidiParsers driving the hands, the
fret position and the band's text events; the guitarist's native side
(modes, one-shots, Star Power, game over); the drum kit answering the
drummer's hits; the camera director aiming at the real band; the guitarist's
walks and interacts (M9.6, "Walks"); the faces (M9.8, "Faces", FaceFX). The
crowd's characters, clips and cards (M9.9) are in `docs/venues.md` "Crowd";
the character / guitar choice and the endgame photos (M9.10) are in
"Characters, guitars and photos".
Addresses are in `default.xex` (`analysis/`); **[C]** confirmed in code or
data, **[I]** inferred.

Code: `char/char_objects.*` (decoders), `char/clip.*` (the channel buffer
and clip sampling), `char/char_driver.*` (CharClipSet and CharDriver at run
time), `char/skeleton.*` (transforms), `char/rig.*` (IK, twists, hair,
look-ats, weights, poll order), `char/character.*` (a character: load,
outfits, servo, LOD), `char/midi_parser.*` (MidiParser), `game/band.*`
(the band on stage, GuitarCharacter, the walk schedule and timeline),
`char/char_walk.*` (RootMotion, CharWalk), `render/band_view.*` (drawing, the
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
  The character, guitar and skin are the player config's (see "Characters,
  guitars and photos"): quick play sets the song's `(quickplay
  (character_outfit X) (guitar G))` (set_quickplay 0x822B86D0; every song
  has one), the career the band's choice.
- The band <- the song's `(band ...)` (11 songs; 5 with female_singer:
  crazyonyou, tattooedloveboys, ftk, lesstalkmorerokk, yeswecan), else
  gh2.dta `(default_band metal_singer metal_bass metal_drummer)`: "sing" or
  "key" -> the singer slot, "drum" -> drummer, "bass" -> bassist. Two
  players: the drummer only. Empty slots are deleted (no singer in gemini,
  soybomb; no bassist in jordan).
- Outfits (OutfitLoader, merged by 0x82158F30): the guitar
  (change_guitar 0x822B6250: guitars.dta `(guitar (skins (skin (outfit O)
  (mat M))))` -> `char/ng/guitars/O.milo`; the skin's mat goes on
  `guitar.mesh`, 0x822B2148; skin `default` is the guitar's first unlocked
  one, 0x822BDAA0 / 0x822C6270) and the drummer's kit (change_drums:
  `dw_<venue>_drums`).
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
  Root motion, only with bone_facing_delta channels (+0xbc; 0x821884D8
  looks the root channels up only then, so a set with bone_facing but no
  delta poses no root at all): "move self" (+0xc8, on at the servo's enter
  0x821895C8) moves the character by the deltas in its own frame
  (0x821886E8: position along its rows, then R x Rz(delta)); off, bone_facing
  goes under the pelvis (0x82188640). A switch (+0xc9, FUN_82188620) moves
  the facing between the pelvis and the root so the pose stays put (off:
  root = inv(pelvis') x pelvis x delta(root); on: root = inv(pelvis) x
  facing(pelvis) x root). The deltas are cleared after (FUN_821885D0).
- **Regulate (0x82188DC8)** while moving self: the regulate waypoint
  (+0xd8: set by every teleport to a waypoint, 0x82151D08, and by a walk's
  end) pulls the character so that the playing clip's root motion up to its
  pending transition (the second-oldest node's delay; none: no pull) ends
  inside the waypoint (0x82194A90: a circle of `radius` in x y, or with a y
  radius a box along the waypoint's axes) and within its angle tolerance
  (0x82194C58), a share of dBeats / max(2, delay x 2/3) each poll.
  regulate_override clip frame waypoint bone (0x82189A40 -> 0x82189928;
  the guitarist's start_interact: {bone.servo regulate_override $clip
  $frame stage_interact.way bone_L-ankle.mesh}) does the same toward
  `frame` of that clip with the bone's offset from the root [I: posed alone
  at that frame], so the ankle lands on the waypoint.
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
- Poll (0x822D2B90): the timeline polls; the walk timer; while the CharWalk
  walks (state 1) nothing else; an unstarted interact due within this poll
  is dropped below Okay; when the chain is settled the current group (the
  bad group while the rock meter is under 0.2, 0x822C6DC0) plays with
  kPlayLast (0x204 for idle after a realtime clip), after a roll for an
  interact ("Walks").
- Mode switches (0x822D24A0) only with the CharWalk idle (state 0);
  one-shots (0x822D2780) not while it walks (state 1).
- Game over start (0x822D1F80): the walk reset; a winner takes his regulate
  waypoint's rotation; no regulating.
- Star Power (the game -> 0x822E3840 -> 0x822D25A8): unless a 0x20000 clip
  blends, a Star Power entry on the timeline ("Walks": priority 1, mask 1,
  so refused while a walk runs) plays the next star_power clip with
  kPlayFirst (0x822E49D8) and ends once it is no longer the first playing
  (0x822E4A30).
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

## Random numbers [C]

GH2 has one RandomGen (0x82783D08), seeded with 666 at start-up
(0x826E1738): a 249-word lagged XOR generator (seed 0x8226DB90: two LCG
steps x * 0x41C64E6D + 0x3039 per word; next 0x8226DBE0); RandomInt lo +
r % (hi - lo) (0x8226DCB8), RandomFloat the low 16 bits / 65536 (0x8226DD10 /
0x8226DDA0). The script's random_int / random_float (0x8232AB20 /
0x8232AB80) and all native code draw from it. gd02 has the same generator
(`script/random_gen.h`) and gives the band its own instance
(Band::Config::seed): the band's natives, the parsers' random picks, the
expression task and every script handler or task running with a band
object as $this (ScriptObject::random) draw from it, so the world's, the
crowd's or the director's numbers no longer move the band's picks (that is
what changed the band golden after the sound effects / FaceFX work: the
expression task, the crowd's shuffles and the venue scripts drew from the
interpreter's generator the band scripts used). The order of draws cannot
match the console's anyway (it depends on frame timing).

## Faces (M9.8; docs/facefx.md) [C unless noted]

Each character with a `lip.servo` (FaceFxLipSyncServo: the 19 guitarists,
the singers) gets its viseme clip set (the servo's `visemes` and `neutral`
clips, its own channel buffer listed like a driver's set, 0x821A8368), the
eye entries' bones and the FaceFX runtime (`facefx::LipSync`): the servo's
.fac, the type's visemes list, the entries' nodes. change_song: the gh2
type plays `_blinktrack.voc`; the singer `<midi_file folder>/<base>.voc`
(game set_lipsync_song 0x822B6A18; the instrumentals have none: no pose).
The servo polls at its place in the rig order [I: after its entries' look-
ats; its PollDeps were not read], skipped while the character is hidden:
the entries' angles from their local rotation (eyeAngle), LipSync::poll
(song and UI seconds), then 0x821A7978: ScaleDown `visemes` to 0, ScaleAdd
each pose node's frame with its weight, the residual onto every quaternion's
w (sign-matched), the `neutral` clip at its start as an additive layer
(0x82161F98 -> 0x8215E6A0: positions and scales +, quaternions neutral x q,
angles +), and PoseMeshes from that buffer (0x821A51E0; it does not use the
servo's CharWeightable weight, which is 1 on every disc servo). The gh2
type's expression task (ExpressionScript: pick_expression at enter /
intro, do_pick_expression every 1.5-2.5 s from the bad or good list by the
game's excitement, override_expression OwFace / resume_random_expression
on ow_face_on / off) runs whether the character shows or not. All of it
runs on kTaskSeconds = the song's seconds in a game (0x822CDEB8) [C]; with
the menus gd02 used to leave the menus' seconds there, which pushed the
singer's lip sync off the music and stopped it after a few minutes in the
menus (fixed 2026-10-27, docs/facefx.md).

## Walks and interacts (M9.6; game/band.cpp, char/char_walk.cpp) [C unless noted]

**The timeline (0x82197CB8 / 0x82198188 / 0x82197FB0).** Each guitarist
keeps entries {start, end (beats), priority, mask, started} by start. Add:
refused when its priority is below 0, or an earlier entry overlapping its
start or a later one starting before its end shares a mask bit and has at
least its priority (that later one's Conflict runs first); else an earlier
overlapping entry sharing a mask bit is replaced (the new one's Conflict
with it) and later overlapping entries (both masks non-zero) are deleted
[C: GH2 steps over the entry after each deletion]. Poll at the song beat,
in order until one has not started: Start (its priority becomes 1e30, so a
running entry is never replaced), Poll, and when its end has passed End and
removal. Entries: walk (0x822E4898, mask -1), hold (0x82046E18, no-ops),
Star Power (0x822E4AF8, mask 1), interact (0x822E4C80, mask -1).

**The schedule (Enter, 0x822D16B8).** Walking needs a kWalkSpot, one player
and a CharWalk. walk_delays (FALSE FALSE (35 55) (20 40) FALSE) per
excitement are rolled (RandomFloat(lo, hi) s, FALSE never) and the last
roll set to -15 s. Tempo scale 2 when the player's parser has half_tempo.
Walk lengths: the CharWalk's longest walk_turn + walk_stop clip plus
max_walk_wait (6), and the slowest walk_walk clip's beats per unit of root
motion (0x8218A8F0), times the longest connection path between walk spots
(0x82195618) for a random walk, from a walk spot to a kSoloWalkSpot for the
solo lead, to the end spot (kWinWalkSpot; kWinGameWalkSpot for the
campaign's last song, kEncoreWalkSpot before an encore) for the end. From
the player parser's events: each solo_on adds (at its solo_off, ending 4
beats after it) a walk to a kSoloWalkSpot starting the lead before the
solo (priority 2; a solo before beat 8 teleports there at once), or
without a solo spot a hold from solo_on (mask all but bit 0: walks wait,
Star Power does not). Then a walk to the end spot arriving at the song's
end (priority 4) [I: FUN_82278D30 as the song's end], and a hold from the
end on (priority 4, mask -1).

**Poll (0x822D2B90).** With the CharWalk idle and walking allowed: when
walk_delays[excitement] has passed since the last roll and the mode is
normal, a walk to a kWalkSpot (0x822D11D0: now until now + the walk length,
priority 1). The roll repeats at each walk entry's end (0x822E4AE8). While
settled, before the next group clip: with probability
interact_probs[excitement] (0 0 0.5 0.8 0.8), the nearest waypoint with any
flag (Nearest mask -1) whose `clip_group` (the interact type's, default
interact_back) has not been used and no 0x20000 clip blending: an interact
entry, from now until the current clip's end plus the group's next clip
(realtime clips through the tempo map, 0x822D1C88), refused when a solo_on
/ solo_off falls before its end (0x822D1518 [I: from the parser's events
after now]); its Start plays the clip with kPlayFirst and uses the group up
(+0x210). The excitement is the game's (Game +0x338 = 0x822CAAB0: 0 Boot,
1 Bad, 2 Okay, 3 Great; the world's excitement messages carry the same),
answering plan question 6.

**A walk's start (0x822E4E78).** From the walk spot nearest the character,
the first waypoint of the global list with the walk's flags that a depth-
first search through the connections reaches without crossing another
spot (0x82196940 / 0x82196430; it then moves to the list's end, as
{waypoint_last w} does after a start teleport). Skipped for an end walk
already at such a spot. The first waypoint of the path, in the target's
frame: |2x| > |y| strafes (kWalkRight / Left; a quarter of the time
kWalkForward instead), turn clips of that side, any stop; else kWalkForward
/ kWalkBackward with that stop (turn filter backward only); kWalkExtreme
at Great, else kWalkNormal. Conflicts (0x822E4910): an end walk replacing or
outliving a walk takes its start (from a walk spot when it was random); a
solo walk takes a random walk's.

**CharWalk (0x8218CA90 and below).** Points: the character, the path. The
plan: the walk_walk clip with all the walk flags (round robin, 0x82176550),
half its blend width the plan's offset; the turn (0x8218BFC8 / 0x8218A238):
of the walk_turn clips with the turn flags and a transition from the
current clip (kPlayFirst), with a last edge into the walk clip, those that
end nearer the first point than they start, the one whose walking heading
(the walk clip's first beat) points best at it; the walk clip looped
(kPlayLast into itself) with a mark per beat {entry, frame, root distance}
until the path's length; the stop (0x8218C1F8): each walk_stop clip with
the stop flags integrated back from the target at the target's facing from
its end to floor(end - 2.2 - halfBlend) + halfBlend (stepping further back
while the integrated position is within 6 units of the origin: a quirk of
0x82189E28, the test is on the world position), starting on the last leg,
on the same foot as a mark (frame parity), the mark nearest the distance
needed (two marks apart), scored 57.3 x |heading error| + 5 x max(0, the
distance error beyond the target's radius - 3); the last point becomes the
stop's start, the target moves by the clamped error along the leg, the plan
is cut after that mark. Poll (0x8218B1A0): each plan clip queued once the
dominant node is the newest, with its frame and the previous clip's
transition frame (NoBlend when nothing plays); after the last, state 2
until the stop clip is no longer the first playing, then reset and the
servo regulates to the target. Steering (0x8218ABC8, only on the walk
entries): no regulating; a beat ahead along the plan (0x8218A618) against
the goal (the segment's end; on the last leg where the character must be
now to end on the target, integrated back, 0x8218A768) kept within
path_radius (12) of the leg; the character turns by -asin(the cross product
of the two directions over their lengths, at least 5) x dBeats; at each
mark passed the speed is corrected by (the path left - the plan's distance
left) / max(2, marks left), along the motion. actually_walking (0x8218A568):
the dominant clip is the plan's current turn or walk clip. Reset (vtable
+4) drops the plan [C: its check for a queued plan clip searches the
cleared plan, so the clip stays queued].

**lose_teleport (0x822D28E8)**: the singer within 72 units in x y [I: its
world sphere's centre] -> teleport to the nearest kWalkSpot.

**Waypoints.** GH2's global list (0x82782750) is the world's
(World::waypoints(), [I: the venue's load order]); Find (0x821954F8) the
first with any of the flags, Nearest (0x82195548) by squared distance, the
script natives waypoint_find (any flag), waypoint_nearest object flags and
waypoint_last (0x821973C0). Waypoint fields: +0xdc radius, +0xe0 y radius,
+0xe4 angle tolerance [C uses; the record's two trailing floats].

## Camera director hooks (game/camera_director.cpp) [C]

cam_check_shot (0x822B2240): guitarist0's 0x80000 weight > 0 needs
starpower_ok, its 0x4000 weight far_starpower_ok; bad_waypoints against the
waypoint nearest the character with kWalkSpot | kSoloWalkSpot (mask 0xc0,
squared distance, no radius; Waypoint::Nearest 0x82195548 over the global
list), looked up once per beat (the M8 192-unit radius was wrong). The
shots' walk_ok filter comes from {guitarist0 actually_walking}
(world_objects_worldbase.dta), the band's CharWalk. Targets and parents {entity,
subpart}: a band member and its bone. `world set_min_lod` sets the members'
min LOD. guitarist0's playing_starpower / actually_walking come from the
band.

## Characters, guitars and photos (M9.10; ui/char_select.*) [C unless noted]

Data: `config/gh2.dta (characters (punk (punk1) (punk2)) (alterna ...) ...
(classic (classic)) (funk1 (funk1)) (grim (grim)))` (11 characters, their
outfits; goth and rock list outfit 2 first), `guitars.dta` (guitar, `(type
guitar | bass | grim | cheat)`, skins with outfit milo and mat),
`store.dta` (the guitars, skins, characters and outfits sold).

- **PlayerConfig** (handler 0x822BDDC0): character +0x38, outfit +0x3c,
  guitar +0x40, skin +0x44, track type +0x30 (kTrackGuitar 2 after
  0x822BD6E8). `set_character <outfit> [reset]` (0x822BD8B8) finds the
  outfit's character (0x822BD5E0); `set_outfit_index i` takes the
  character's outfit i (0x822BD9A0, clamped); `get_outfit_index`;
  `set_guitar <g> <skin>` / `force_guitar` (0x822BDAA0; skin `default` =
  the first unlocked skin); `set_guitar_index i` (the i-th available
  guitar, skin default); `get_guitar` / `get_guitar_skin` first validate
  (0x822BD490: a guitar the player lacks becomes the first available one,
  a skin the guitar lacks its first); `get_guitar_desc` / `get_guitar_skin_desc`
  = `<sym>_desc`; `get_num_skins` (available), `get_total_num_skins` (all
  of the guitar's, 0x822BF410). `game` passes these to player 0.
- **Available guitars** (0x822C43B0): the `grim` character has the scythe
  only; else the unlocked guitars of the player's part (`guitar`, or `bass`
  for kTrackBass), filtered by type (0x822C1F08), career from the profile,
  quick play from any difficulty (0x82285FF0). Skins (0x822C6000): the
  guitar's unlocked skins. Unlocked (Campaign::IsUnlocked 0x82285CC0): the
  all-access cheat unlocks all; otherwise GH2 reads its unlock table
  (profile +0x38, per difficulty, not traced); gd02: an item `store.dta`
  sells is unlocked once bought (`career.json` items), every other one from
  the start [I]. So a new band has Les Paul, SG and Flying V with one skin
  each, and outfit 1 of the first eight characters.
- **Defaults**: `main.dta reset_player_settings`: player 0 `punk1`, player 1
  `rockabill1`, guitar index 0; quick play: the song's quickplay look.
- **Career save** [C]: each profile keeps per difficulty a 0x24-byte record
  (profile +0x20) whose four symbols are saved by 0x822A8D00 / read by
  0x822A8D68 in Campaign::Save v17: character, outfit, guitar, skin.
  `campaign set_character_info` (0x82284E88) copies player 0's validated
  config in (marks the save dirty when it changed); `get_character_info`
  (0x82284E00) sets player 0's outfit and guitar / skin from it;
  `new_campaign` (0x822A8A88) is true while the record's character is
  empty. gd02 keeps it as `career.json` profile `characters[difficulty]`
  (`character`, `outfit`, `guitar`, `skin`), with `last_difficulty`
  (profile +0x2c, campaign last_difficulty / update_difficulty).
- **Screens** (career.dta, GH2's own scripts): sel_difficulty ->
  `sel_character_new_screen` (new campaign) -> `sel_guitar_new_screen` ->
  tour_start; career_screen's hero / guitar buttons -> the `_edit_` screens.
  - `character_provider` (0x822C1530): gh2.dta's characters in order;
    `get_symbol`, `get_index`, `num_outfits` (unlocked ones, 0x822BF568),
    `get_outfit c i` (0x822BF660), `is_active` [I: unlocked].
  - **CharsysPanel** (`char_single`, 0x8228E8A8; slots of 0x30 bytes):
    `show_char n <outfit>` loads `char/<outfit>/og/<outfit>_ui.milo` (a
    Character of type `ui`, no guitar) through a cache (0x8228E0A0 /
    0x8228E428 preload the list's neighbours; gd02 loads on demand);
    `char_event n <msg>` is sent once loaded (0x8228D348): char_objects.dta
    `(Character (types (ui (animate ...) (select ...) (store ...))))`
    plays `ui_enter` then `ui_loop`; `get_char`, `is_char_loaded`,
    `are_chars_loaded`, `set_placer` / `set_door` / `set_env`,
    `set_exclusions` (cache, ignored). Poll (0x8228F250): the placer holds
    the character; the env is the slot's (else the character's
    `character.env`; a locked one in store mode gets the panel's); the door
    (`cs_door.mesh`) is set to Euler (pi/2, 0, z) with z from the
    character's `bone_door` local rotation (0x82269700): the ui_enter clip
    kicks the door open. Drawn with the placer's panel camera, keeping the
    room's depth [I]; the menus' clip clock runs at 120 beats a minute [I].
  - `{reset_hair $char}`, `{{$new main.drv} transfer {$old main.drv}}`
    (the outfit switch keeps the clip and frame [I: by clip name]).
  - **GuitarSelectPanel** (0x82293B10): per player (`(quickplay ...)`
    entries) guitar, skin, skin-select and done flags, from the player's
    validated config on enter (0x822934B8). Buttons (0x82293830): X sends
    `guitar_selected p`; up / down step the guitars (or skins) with
    wrap-around (0x822936D8; a new guitar keeps the skin's index, else its
    first) and send `update_display`. `get_selected_guitar` / `_skin`,
    `get_num_guitars`, `get_num_skins p g`, `is_skin_select`,
    `set_skin_select`, `is_select_done`, `set_select_done`.
  - **GuitarDisplayPanel** (0x822922B8): `show_guitar p g skin proxy filter`
    loads the skin's outfit `char/og/guitars/<outfit>.milo` into the
    UIProxy (`guitar.pxy`, type `guitar`: `main_trans guitar.mesh`); poll
    (0x82291FF8) puts the skin's mat on `guitar.mesh` (0x822B2148), sets the
    env and turns the filter (`guitar_single.filt`) at 30 frames a second,
    wrapping at 240. gd02 draws the outfit's first LOD with `guitar.mesh` on
    the proxy [I: the main trans' own local dropped].
  - GHScreen passes a message it has no handler for to its focus panel's
    script too (`{$this choose_skins}`) [I].
- **The band** (GamePanel enter): each player's validated outfit, guitar
  and skin go to the venue band (`UiHost::GameSetup`, app.cpp startBand);
  without the menus the song's quickplay look. `GD02_CHAR`, `GD02_GUITAR`,
  `GD02_SKIN` override player 0.
- **Endgame photo** [C]: GH2 does not render the band into it. When the
  mode has a `win_preload_panel` (quickplay, career: `endgame_panel`),
  GamePanel loads that panel with the song (0x822CAFF8) and, once it is
  loaded (IsLoaded 0x822CC848 -> 0x822CC730), per player of the game
  (gamecfg 0x822B3880) picks `ui/image/og/photo_<outfit><RandomInt(0, 4)>
  _keep.bmp` (string 0x82046078; 76 pictures, e.g. photo_rock13 = rock1,
  picture 3, photo_classic0); FinishLoad (0x822CC9B0) puts picture i on the
  panel's `winner_photo<i>.tex` (RndTex::SetBitmap 0x821D60F8). The
  newspaper's `winner_photo0.mesh` shows it. gd02 draws the pick from the
  interpreter's generator [I: GH2's global RandomGen, call order differs].

## Tests and aids

- `--chartest <disc>`: every Char* record decoded with the counts above;
  clip quats unit; golden poses against the reference parser; every
  character (37), guitar (59) and kit (8) loads with every bone resolved;
  the MidiParser golden (hangar18) and unknown texts; the band golden:
  hangar18 in arena through the world, director, lights and the band,
  autoplayed with Star Power deployed: (beat, member, clip) log hash, drum
  kit triggers, no non-finite transforms; faces (every lip servo's viseme
  set, channels, entries and weight; the singer's visemes, the guitarist's
  blinks and expressions, nothing posed while hidden); walks (at least 4,
  each arriving, at least 600 walking frames, an interact).
- `--venuetest`: the director golden now runs with the band.
- `--walktest career`: a new band picks Judy Nails' second outfit and the
  SG's white skin (the walk buys both first) through GH2's screens;
  `career.json` and player 0's config must hold `alterna alterna2 sg
  sg_white`. `--walktest quickplay` checks the endgame photo (player 0's
  outfit, picture 0..3, on the disc).
- `--view char/<c>` (`GD02_VIEW_CLIP`, `GD02_VIEW_FRAME`, `GD02_VIEW_YAW`...):
  one character posed (`char/<o>/ng/gen/<o>_ui.milo_xbox` with
  `GD02_VIEW_CLIP=ui_loop`: the menu character). In game: `GD02_CHAR=<outfit>`, `GD02_GUITAR=<guitar>`, `GD02_SKIN=<skin>`,
  `GD02_NO_BAND=1`, `GD02_BANDLOG=1` (clips, walks, arrivals, interacts),
  `GD02_FORCE_SHOT=<CamShot>`, `GD02_TRACK=0` (no track, to see the band),
  `GD02_FACE_CAM=<member>[:<distance>]` (a close-up of that member's head:
  `singer`, `guitarist0`, ...).

## Open

- M9.10: GH2's unlock table (profile +0x38) and its per-difficulty unlocks;
  the CharsysPanel preload order and memory budget; how the door / room
  occlude the menu character on the 360 (depth kept here); the menu
  characters' clock; CharDriver transfer; multiplayer character / guitar
  screens (MultiCharSelPanel, M10); the store (M10).
- Walks: the global waypoint list's order (load order assumed); CharWalk's
  and the lip servo's PollDeps; regulate_override's bone offset (posed
  alone); arrivals end 10-40 units from the target before the servo
  regulates (the stop starts 2.2 beats before its end, see the 6-unit
  quirk); the song end used by the schedule; multiplayer walks (none: one
  player only) untested.
- The shadow pass (the shadow group); hair collision types 1-4; the eyes'
  retarget threshold; the face-off "doing badly" rule (score lead);
  CharDriverMidi's inactive clip (+0x74 / +0x78 meaning).
- CharClipGroup's starting index; the hand drivers' order among equal
  dependencies.
