# Guitar Hero III (Xbox 360): the band

Who stands on the stage during a song, what each member is made of, how GH3
animates them and how gd04 draws them (M6). **[C]** confirmed in the data,
the disassembled scripts or the XEX (address given), **[I]** inferred.
Code: `engine/src/data/band.*` (members and their files),
`render/band_anim.*` (the anim tree and its scripts on the song clock),
`render/band_view.*` (drawing), `render/band.*` (the band of a song),
hooked from `render/venue_stage.*`; `tests/chartest.*`.

Sources: `tools/qbscript.py` dumps of `qb.pak.xen` (`guitar_character.qb`,
`guitar_anim_tree.qb`, `guitar_animation_data*.qb`, `guitar_band_profile*.qb`,
`guitar_hand_events.qb`, `guitar_midi_anim_funcs.qb`), the song paks'
`<song>_performance`, `tools/anim.py` / `tools/scn.py` on the character paks,
and Ghidra headless (`DumpAsm.java`, the `analysis/decomp` export) for the C
functions named below. REA cannot open the XEX (agents/CONTEXT.md).

## 1. Who plays  [C script `create_band`, guitar_character.qb]

One player (`current_num_players = 1`):

| Member (object) | Profile | Notes |
|---|---|---|
| `Guitarist` | `player1_status.character_id` (`find_profile_by_id`: the `Musician_Profiles` entry whose name's key is the id; else Axel) | body `Guitarist_<name>_Outfit<o>_Style<s>` from `player1_status.outfit / style`; instrument `player1_status.instrument_id` (none with `Cheat_AirGuitar`); outfit 2 uses `skeleton2` |
| `Drummer` | `'drummer'` | |
| `Bassist` | the song's `bassist` (`gh3_songlist_props.<song>`), else `'bassist'` | `current_bass_model` overrides the instrument; a Morello / Slash bassist plays the guitar part |
| `vocalist` | the song's `singer`: `none` (no singer), `female`, `bret`, else male -> `singer`, `singer_female`, `singer_bret`; `_alt` (another microphone) in z_wikker, z_budokan, z_hell; `Cheat_BretMichaels` | |

`z_credits`: the guitarist alone. `$disable_band = 1`: nobody.
gd04 `band::chooseBand`.

Two players (`current_num_players = 2`) [C scripts `create_band`,
`create_guitarist_profile`]: `Guitarist` and `Bassist` both through
create_guitarist_profile (body `Guitarist_<name>_Outfit<o>_Style<s>`, the
player's instrument_id): Guitarist = player 1, Bassist = player 2 (co-op /
p2_career with player 1 not on guitar: swapped); the drummer; no vocalist. A
boss battle's player 2 is the boss (start_gem_scroller sets
player2_status.character_id = the boss props' character_profile, outfit 1,
style 1), so Morello / Slash / Lou stand at the Bassist's spot. Whether the
guitarist_player1/2 waypoints replace the usual start nodes in two-player
venues is not checked here (render/band*).

## 2. What a member is made of  [C data]

- `Musician_Profiles[]` (guitar_band_profile.qb): `name`, `Skeleton`
  (`skeleton2` for outfit 2), `musician_body {desc_id}`,
  `musician_instrument {desc_id}`, `Type`, `anim_set`, `finger_anims`,
  `fret_anims`, `strum_anims`, `stance`, `ik_params`, optional `scale`.
- `musician_body[]` / `musician_instrument[]` (guitar_band_profile_data.qb):
  `desc_id`, `mesh`, `pak` ('pak\models\...\x.pak' = `DATA/COMPRESSED/PAK/x.pak.xen`),
  `anims_pak`. 83 profile / body pairs (81 `musician_body` entries; all on
  the disc but `boneman_ped.pak` of the debug-only BoneMan_Ped), 113
  instruments, all on the disc.
- A body pak: the `.skin` and its `.tex`, a few `.ska`; the anims pak the
  rest of the member's clips; `perm_anims.pak` the shared hand, strum and
  fret clips and the rig defaults; the song pak the singer's lip-sync
  clips (`GH3_Singer_Male_SlowRide_1`, ...). Skeletons: `ZONES/global.pak`
  (`GH3_Guitarist_Axel`: 125 bones; drummer 100, singers 121).
- Every part is skinned to the member's skeleton: the guitar to
  `bone_guitar_body` (index 119), the microphone to the singer's stand bone,
  the drum set's cymbals to the drummer's extra bones (checked on all 193
  skins: every bone index < the skeleton's bone count).
- Materials of the character templates (0x852105c8, 0x6f3f03f3, ...): a
  normal map (class 1), the colour map (class 0), a class-3 map; `+0xd8` 3 on
  hair / lashes (blended), `+0xe8` top byte the alpha cut-off. The textures
  are the template's samplers in order, the constants its c0.. (section 6).

## 3. Placement  [C scripts `create_band_member`, `get_start_node_id`]

`<zone>_TRG_Waypoint_<guitarist|bassist|drummer|vocalist>_start`
(`GetWaypointPos`, `obj_setorientation dir = GetWaypointDir`), all four in
every venue [C data]. The model faces its local +z [C data: portraits from
+z show the faces; venue shots from the crowd show the band facing it]; the
member's transform is the waypoint's position and rotation (docs/venues.md
"Angles") times the profile's `scale` [I: the scale's use].

## 4. Animation

### Clips  [C data unless marked]

- Rotations are absolute; a compressed clip's translations are offsets
  from the skeleton's bind translation (bone lengths are 0 in every track,
  e.g. bone_neck 0.0 vs bind 0.311); a single-pose clip (flags 0x10000000)
  holds absolute values (the `rig/defaults/*_default` clips equal the bind
  pose).
- Flags 0x200: a difference clip (the hands' `_D` clips): finger bones with a
  zero translation and a rotation off the bind; gd04 composes it onto the
  pose below (pose x delta, translations added) [I: the order].
- Body clips (`GH3_Guit_Axel_A_Med_Idle1`) drive the root, spine, legs,
  head, the guitar bones and the IK targets, **not** the arms: a
  guitarist's arms come from IK (below). Break / intro clips (`handsoff`)
  animate the arms themselves.
- The bone mask (flags 0x80000) limits a clip to its bones.

### The tree  [C data `guitarist_static_tree`, `drummer_static_tree`, `vocalist_static_tree`]

```
guitarist: constraintbones( ragdoll( ik( partialswitch( face,
             applydifference( arm_branch(left_arm(LeftArm + LeftHand), right_arm(RightArm)), body ) ) ) ) )
drummer:   ... partialswitch( cymbals, applydifference( BodyTwist,
             partialswitch( arms(DrummerLeftArm + DrummerRightArm + feet), body ) ) )
vocalist:  ... partialswitch( face, body )
```

gd04 (`bandanim::Animator::evaluate`): body (cross-fades from the bind
pose), the face partial over it, then for guitarists the fret (LeftArm),
finger (LeftHand) and strum (RightArm) difference clips scaled by each arm
branch's strength; for the drummer the arm and cymbal partials and the body
twist difference scaled by the twist value. A `degenerateblend` node
cross-fades from what it played to the new clip over the blend time [I:
linear]; a `play` timer holds its last frame [I].

### IK  [C data `Hero_Ik_params`; I: the solver]

Two-bone chains bicep -> forearm -> palm onto `Bone_IK_Hand_Guitar_R`
(child of `BONE_GUITAR_STRUM_POS`) and `Bone_IK_Hand_Guitar_L` (child of
`BONE_GUITAR_FRET_POS`, which the fret clips slide along the neck). gd04
solves in the plane through the animated elbow, gives the palm the target's
orientation, and blends with the animated arm by the arm's strength
(`hero_toggle_arms`, 0.25 s). Leg chains, the drummer's and the singer's IK,
the hinge limits and `hero_ConstraintBones` (twist bones) are not done.

### Body loop  [C scripts `hero_play_adjusting_random_anims`, `handle_play_anim`, `handle_change_stance`]

- The body plays `anim_set.<stance>.<anim>.<skill>.<speed>` once, then
  chooses again (blend 0.2 s). `hero_play_tempo_anim_cfunc` (0x821090B0 ->
  0x82108E58) [C]: a random entry, re-drawn among the others when it equals
  the last one played; skill = player 1's health < 0.66 bad, > 1.33 good,
  else normal (0x82108C90); speed (0x82108B60) = slow below
  (slow_tempo + med_tempo) / 2 = 95 bpm, med below (med_tempo + fast_tempo)
  / 2 = 130, else fast; tempo = 60000 / `current_song_beat_time` (0x82108A38)
  [I: the beat time is the fretbar spacing at that time]. Clips play at
  speed 1.
- Stances: `Band_ChangeStance` sets `next_stance`; when the body clip ends
  the transition `anim_set.<old>.transitions.<new>` plays first [I: the
  C function's lookup]; `no_wait` does it at once.
  `Band_PlayAnim` queues `next_anim` (repeat_count, Cycle, disable_auto_arms /
  disable_auto_strum) or, with `no_wait`, restarts the loop with it.
- Song events: `<song>_performance` {time, scr, params} at their time
  (event_iterator, lead 0) [C]. On the disc: Band_ChangeStance (1027),
  Band_PlayAnim (839), Band_PlayFacialAnim (316), Band_WalkToNode (79, not
  done).
- The fast intro (quickplay) does not run `play_intro_anims`: the members
  start in their profile's stance [C data `Common_FastIntro_Transition`].

### Hands  [C scripts `guitar_hand_events.qb`]

| What | Source | Lead | Clip |
|---|---|---|---|
| strum | each gem of `<song>_song_<part>expert` (guitarist: the player's part; bassist: `rhythm_`) | `strum_anim_lead_time` 250 ms | `<strum_anims>_Strums.{Short,med,Long}` by gem length (< 150, < 600 ms), random, blend 0.1; when it ends `no_strum_anim` (0.2) |
| fret position | `<song>_anim_notes` 118..127 (guitar) / 101..110 (bass) | 250 ms | `fret_anims.track_<note>`, blend `fret_blend_time` 0.1 |
| fingers | each gem (`FretFingers_iterator_CFunc` 0x821119F8 [C]) | 250 ms | `finger_anims.<colours joined by '_'>` (at most four), press 0.07 s; released (`none`, 0.2 s) `finger_note_padding` (10 ms) after the gem's length unless another gem came |
| missed note | the player's judge | - | `Bad_Strums` once until a hit [I: the hit clears it] |

### Drummer  [C]

- Arms: `<song>_drums_notes` 37..45 left, 49..57 right, lead
  `drum_anim_lead_time` 150 ms (`Drum_iterator_CFunc` 0x821121D8):
  `Drummer_anims.track_<note>`, blend 0.1; when it ends, after
  `drummer_arm_blend_out_delay` 0.25 s, `stickdown_l/r` over 0.6 s
  (`play_drummer_*_arm_anim`).
- Cymbals: notes 44/56, 45/57, 43/55, 41-42/53-54 -> cymbal1..4,
  `cymbal_anims[]`, lead 0 (`Drum_cymbal_iterator`).
- Twist: every game frame `drummer_info.twist` moves `drummer_twist_rate`
  0.05 toward the mean of the two arms' `drummer_*_arm_twist_factors` of
  their last notes (`compute_desired_drummer_twist` 0x82107F48); it scales
  `Test_Drum_BodyTwist_D`. gd04 steps it on a fixed 60 Hz clock.

### Face  [C script `facial_anim_loop`, `play_special_facial_anim`]

`anim_set.facial_anims.idle` at random, one after another (+1 frame);
`Band_PlayFacialAnim` plays a song's lip-sync clip, then the loop resumes.

### Frame-rate independence  [gd04]

Every event (song notes, performance entries, clip ends, the twist's frame
steps) is processed at its own song time in time order; random choices use
a per-member seed (`GetRandomValue`'s generator is not reproduced [I]);
skill reads the 60 Hz performance timeline. `--chartest` compares every
bone of the band at 60 and 240 fps every 250 ms of the whole song.

## 5. Cameras  [C data; I: the tracking]

A camera with `lookat = <member>` / `lookatbone` / `screenoffset` keeps
that bone at the screen offset (x right, y down, half-screens): gd04 turns
the authored orientation by the least rotation that puts the live bone
there, every frame (`CameraDirector::aim`); `LockTo` still uses the
member's start waypoint (members do not walk). The authored quaternions
already frame the start position this way (docs/venues.md "Conventions").
GH3's smoothing of the tracking, if any, is unknown.

## 6. Drawing

CPU skinning (up to three bones a vertex, `scene::Mesh` blend data; the
tangents and binormals too) into a dynamic vertex buffer per member, in
world space. Each material draws with its GH3 template's own shaders
(docs/venues.md "Shading"; SkinShader, HairShader,
D_N_S_Character_1SeperateChnl, ...): the normal map (DXN, class 1), the
colour and specular maps in the material's sampler order, the skin
shader's edge / transition / dark colours, the hair shader's two specular
lobes along the rotated tangent (m_vsAngle) [C: the microcode]; the light
registers come from the member's groups' lights at its chest
(`band`, plus `alt_band` for the guitarist: `create_band_member` gives the
guitarist `alt_band` outside z_training / z_viewer [C]; I: that it adds to
`band`'s lights; I: gd04's light manager, docs/venues.md). Without venue
lights (the portraits of `--chartest`) a neutral key and ambient light.
Without the material library (or GD04_GH3_SHADERS=0) one approximate
program: colour map x (ambient + lights + `GD04_BAND_FILL`). Drawn after
the venue, no back-face culling (hair cards) [I].

## 7. Gaps

Band_WalkToNode (walks, turns), intro / win / lose stances in the
transitions, two-player layouts, the twist constraint bones, leg / drummer /
singer IK, the ragdoll (the accessory bones Bone_ACC_* that GH3 simulates
with Havok from the `.rag` files, decoded: formats.md "Havok packfiles"), star power and battle reactions, receiving dynamic shadows (they cast
them onto the venue: docs/venues.md "Dynamic shadows").

## 8. Test

`gd04 --chartest <disc>` (headless: `SDL_VIDEO_DRIVER=offscreen`):

1. every profile / body pair (83) and instrument (113): skin bones in the skeleton, the
   materials' textures present, bind x inverse bind = identity, 120 frames
   of slowride without a NaN, a body animation played, a sane height;
2. the band of every venue: the whole song at 60 and 240 fps (identical
   poses every 250 ms) and every checkpoint whose shot has a lookat keeps
   that member on screen;
3. `GD04_CHARTEST_GOLDEN=<file>`: writes, then compares, a checksum of each
   venue band's poses at 5, 30 and 60 s;
4. `GD04_CHARTEST_SHOTS=<dir>`: portrait grids of every body and instrument
   (`bodies_NN.bmp`, `instruments_NN.bmp`, legend on stdout) and the band in
   each venue's first six shots.

The debug-only BoneMan profiles borrow anim sets whose clips their paks
lack (reported, not failed). `GD04_CHARTEST_ONLY=<text>` limits the bodies
(and skips the venues unless `GD04_VENUE` is set); `GD04_CHARTEST_VERBOSE=1`
lists missing textures and clips; development aids: `GD04_CHARTEST_AT=<ms>,...`
(venue screenshot times), `GD04_CHARTEST_ZOOM` / `_YAW` (closer portraits);
`GD04_NO_BAND=1` plays a song without the band; `GD04_BAND_EXPOSURE`,
`GD04_BAND_DYN`, `GD04_BAND_FILL` tune the approximate shading.
