# RB1 faces, eyes and drummer vs the port (GD05-106, GD05-108, research 2026-11-16)

Research only (no builds, no runs). Sources: RB's XEX in ghidra/ (addresses below), the disc's DTA
(`extracted/dta`: char/gen/char_objects.dta, config/gen/midi_parsers.dta, config/gen/macros.dta,
char/gen/char_face_exp.dta), the character milos (male_drum / male_guitar / male_vocals) and clip
sets (male_viseme_head_1, male_drum_play, male_drum_l-hand) read with `tools/milo.py unpack`.
[C] = confirmed from XEX or data, [I] = inferred. Port lines are on 9e672a3.

## How RB animates the face (summary)

- Every member's head has `CharEyes.eyes` (rev 7): two eyes = (left_eye.lookat, bone_L-lid,
  bone_L-eyelid-low) and (right_eye.lookat, bone_R-lid, bone_R-eyelid-low), no interests,
  face_servo lip.faceservo, weight owner animate_eyes.weight [C data].
- animate_eyes.weight: driver main.drv, flags kAnimateEyes (0x1000000), offset 1, scale -1: the
  eyes run unless the body clip animates them itself [C data].
- The eye look-ats (rev 3): source "", pivot bone_X-eye.mesh, dest bone_eyes.mesh (child of
  bone_head), half_time 0.03, yaw -16..20, pitch -16..5 [C data]. RB's CharLookAt Poll
  (0x82321A48) uses source if set, else the pivot, and rotates the pivot [C].
- CharEyes Poll (0x82329588) [C]:
  - Blink: the face servo's Blink weight (servo +0x10c). Below 0.3 arms a flag. A blink is the
    weight falling from above 0.8 while the flag is armed.
  - New gaze point (0x82328B48, same maths as GH2) after 3 s, or after 1 s on a blink or a head
    turn.
  - Moves the look-at's dest bone to the point, lerped by the CharEyes weight.
  - Polls each eye's look-at itself.
  - Lids: the upper lid turns by -(eye local m[1][0]) x (1 - blink), and the lower lid's local
    x moves by 0.75 x the same.
- FaceFxLipSyncServo Poll (0x82324110) [C]:
  - Base is set first.
  - Each lit FaceFX node adds its clip. The node whose clip is the set's `Blink` (servo +0x108)
    goes in at its full value: enable_blink (+0x15d) is its only gate, and the value is kept at
    +0x10c. The other nodes are scaled by the servo's weight.
  - With song_owner set (+0x154), its own song's nodes count 0. The owner's lit nodes (not its
    blink) go in x its own weight, using clips of the same names from its own set.
- Songs [C data]:
  - Non-singers: `world_configure` -> `set_blink_song` = _blinktrack.voc, loop TRUE, song_offset
    random_float 0..1000.
  - Guitar type: song_owner = player_vocals0's lip.faceservo while a game is up. Its weight owner
    is singalong.weight: VENUE notes 85 bass / 86 drums / 87 guitar set weight 1 for the note's
    span (*_singalong_parser).
  - Singer: `load_face_song` = the song's .voc, song_offset +0.05 s. Weight owner
    expression.weight = 1 - kDisableLipSync of main.drv.
- Expressions [C data + C code]:
  - Every play clip has a `viseme_group`. Examples: drum play_90_* exp_drummer_mellow / normal /
    intense, play_128_* exp_drummer_normal / intense, base_128_02 `normal`.
  - The CharClip `enter` (char_objects.dta musician / shell) sends `$clip.dude change_face_group`.
    The handler is BandCharacter 0x822E5850 -> 0x822E4C58: FindClipGroup in expression.drv's clips
    (= the visemes set, CHAR_COMMON sync_objects), pick with flag 0x80000 first (kExpressionFirst
    [I]), then the CharDriver Play.
  - BandCharacter poll (0x822E6360) picks again when that clip is done.
  - Groups: exp_drummer_normal = 5 clips (pouty, bassface_cool, teethgrit_pained, snear_mellow,
    slackjawed); intense = 7; mellow = 4; normal = Neutral_lo.

## Differences, faces and eyes (GD05-106)

| # | Prio | Port (file:line) | RB (address / data) | Fix (switch) | Objective check |
|---|---|---|---|---|---|
| A1 | P1 | char/rig.cpp:423, :440, :850: `k.source = bone("")` = -1, so `pollLookAt` returns at once. The eyes never turn for anyone | CharLookAt Poll 0x82321A48: `src = source ? source : pivot`, rotates the pivot (+0x3c); every RB eye look-at has source "" [C] | When source is empty, use the pivot as the source (and in p.writes). `GD05_EYES_LEGACY` | New bandcheck "eyes": per member, max angle of bone_L/R-eye's local rotation from bind over the song, and the number of changes > 2 deg. Legacy 0 deg; expect > 2 deg on every member, a change at least every 3 s |
| A2 | P1 | char/character.cpp:786-806: the whole face x servo weight, and returns when it is 0. Non-singers' owner singalong.weight is 0 outside notes 85..87, so they never blink and their faces are frozen | Servo Poll 0x82324110: the Blink-clip node goes in at full value, gated only by enable_blink (+0x15d); its value is kept at +0x10c [C] | Pose Base + the Blink node unscaled even when the servo weight is 0; scale only the other nodes. `GD05_BLINK_LEGACY` | Bandcheck "blink": per member, blinks/min (Blink weight crossing 0.8) and the bone_L-lid rotation range. Legacy 0 for guitar / bass / drums; expect > 0 for all four |
| A3 | P1 | No expression.drv / change_face_group anywhere (grep `expression.drv`: only a comment, rig.cpp:542). Faces sit at Base + visemes | Clip `viseme_group` -> change_face_group 0x822E5850 / 0x822E4C58 -> expression.drv plays a clip of the group; re-picks when done 0x822E6360 [C]; groups above [C data] | On each main / main_add clip start, read its `viseme_group` (default `normal`). Pick a clip of that group from the visemes set (0x80000-flagged first on a change) and pose it as an offset from Base (D94 composition) at expression.drv's weight, under the visemes / blink. Re-pick when it ends. `GD05_EXPR_LEGACY` | Log `expr <member> <clip>`. Bandcheck "expression": share of play frames with an exp_* clip lit > 0.9 for non-singers; >= 3 distinct clips per member per song; legacy 0 |
| A4 | P2 | char/rig.cpp:948-971 is GH2's CharEyes: 8 s timer; blink = right_lid m00 test + 33 % random; rig.cpp:521 `rightLid` is empty for RB, so no blink retarget | CharEyes Poll 0x82329588: 3 s; blink from servo +0x10c (< 0.3 arms, > 0.8 then falling fires); no random; dest bone = lerp(own, point, CharEyes weight = animate_eyes.weight) [C] | Port the RB Poll under the A1 switch; feed it the servo's Blink value from A2 | Retargets per minute per member: RB-like >= 20 (<= 3 s apart); legacy ~7.5 |
| A5 | P2 | None: lids only move with the visemes | 0x82329588 tail: upper lid rotated by -(eye local +0x2c) x (1 - blink) (0x8238ED60), lower lid local x += 0.75 x it [C structure; I: the rotation axis and that +0x2c is local m[1][0]] | Add the lid follow after the eyes. `GD05_LIDS_LEGACY` | Correlation of eye pitch to upper-lid angle > 0.9 when not blinking; legacy ~0 |
| A6 | P2 | game/band.cpp:771-772: one Anim from t=0 for every non-singer; no loop, no song_offset | set_blink_song: loop TRUE, song_offset random_float 0..1000 per servo [C data] | Per-member random offset (band rng) and loop for the blink track. Same switch as A2 | Blink starts of two members within 50 ms on < 20 % of blinks; blinks continue past the track's length |
| A7 | P2 | game/band.cpp:758 [I comment]: song_owner not followed; during singalong, non-singers play their own blink track x weight, so they don't mouth the lyrics | Guitar-type servo song_owner = vocals' lip.faceservo; the owner's nodes x own weight (0x82324110 +0x154 loop) [C] | Non-singers take the singer's LipSync frames x singalong weight (Blink stays their own). `GD05_SINGALONG_LEGACY` | On a song with VENUE 85/86/87: member jaw range > 5 deg inside those spans, ~0 outside; legacy ~0 inside |
| A8 | P3 | D53: singer's song_offset ignored | load_face_song: song_offset = -WORLD_OFFSET_SEC = +0.05 s [C data] | Offset the singer's .voc clock by 0.05 s (sign as RB's servo applies it [I]). `GD05_FACEPOSE_LEGACY` covers it, or a new `GD05_VOCOFFSET_LEGACY` | First jaw opening against the first lyric's time: 50 ms shift |
| A9 | P3 | rb_band.cpp:629 hooks face_accent.dmidi to *_face_accent; its clips are never set (visemes set via sync_objects) [I: probably a no-op] | face_accent.dmidi: weight 0.4 x accent.weight (main.drv kIgnoreAccent, offset 0.4 scale -0.4), bones lip.faceservo, clips = visemes (CHAR_COMMON sync_objects) [C data]. Message `accent_sustain` on held gems; what it plays [I] | Log first; fix only if RB's clip is found | Log the driver's clip per accent message |

## Differences and checks, drummer (GD05-108)

What already matches RB, read side by side [C]:
- CharIKHand Poll 0x8232EB48 against rig.cpp:684-770. The parts compared:
  - the 144 / d^2 target weights and the w x sum when under 1;
  - the finger compose;
  - the goal = lerp(hand, target, w);
  - the elbow cos clamp [-1, 1], with the forearm set to the pure z bend;
  - the shoulder rotBetween;
  - the stretch.
- measure_lengths 0x8232E150 = rig.cpp:674 (local hand / forearm lengths).
- elbow_swing is 0 on all four drum IKs, so RB's swing branch never runs [C data: flags 01 01 00 01, swing 0.0].
- CharDriverMidi midi_parser 0x82342168: Play(-length) then blend = length, which is rb_band.cpp:784.
- The hand assignment comes from the chart's animation notes. drums_left_hand plays 26 30 34 35 40 41 46 48 50; drums_right_hand plays 27 31 36..42 47 49 51. The port runs these parsers from the DTA.

What the data says the arm is:
- The L/R-hand clips (male_drum_l-hand / r-hand) animate the whole arm, the stick (bone_X-stick), the spine / pelvis / head / thighs, and the targets bone_X-tip_<pad> (children of the kit's bone_target_<pad>).
- The IK finger is bone_X-tip.mesh (child of bone_X-stick).
- A clip "calls" a pad by taking its target's local offset to ~0. That is the IK's dominant weight [C data; I that the minimum lands on the hit].

So no static IK difference remains. The next step is to measure:

| # | Prio | Port (file:line) | RB (address / data) | Fix (switch) | Objective check |
|---|---|---|---|---|---|
| B1 | P1 | game/rb_band.cpp:857+ bandcheck measures guitar reach only | Data above | Add bandcheck "drum reach" first, then fix what it shows | At each PART DRUMS animation note (pad from the note: 26/27 snare, 30/31 hihat, 34..37 L-cymbal, 38/39 R-cymbal, 42 ride, 46/47 L-tom, 48/49 R-tom, 50/51 floor tom; hand from the note's parser), log: (a) \|bone_X-tip world - bone_target_<pad> world\|; (b) the time of the min of \|bone_X-tip_<pad> local pos\| minus the note time (WORLD_OFFSET_SEC -0.05 included); (c) the IK weight. Expect a) p95 < 1 unit, b) within +-1 frame, c) 1 while playing. A wrong pad = a cross-over bug; a large (b) = timing |
| B2 | P2 | char/rig.cpp:718: target rotation = orthoY(row1, row2) per target | 0x8232EB48: the target's world matrix straight to a quat (0x82193D70 -> 0x8238E4C0), sign-aligned weighted sum (0x8232E0B8), normalised (0x8238DF08) [C; I: whether 0x82193D70 orthonormalises] | Use the world rotation as is when it is orthonormal; keep orthoY otherwise. `GD05_IKORIENT_LEGACY` | Stick angle at the hit: angle between bone_X-stick's y and RB's target y (from the clip) < 2 deg; log the targets' \|det\| and the orthogonality error |
| B3 | P2 | char/char_objects.cpp:268: the CharDriver starved clip (`ready` on left/right_hand.dmidi) is read and never used; after the last hit the arm holds the hit clip's last frame | left_hand.dmidi: apply 1 (add) into limbs.blender, starved clip `ready` (a clip of male_drum_L-hand) [C data; I: RB plays it when the driver runs dry] | When a hand dmidi's chain ends, play its starved clip. `GD05_STARVED_LEGACY` | After the drums' last note and in gaps > max_gap 2 s, the hand pose equals `ready` (bone error < 0.5 units) |
| B4 | P2 | game/rb_band.cpp:744 (main.drv plays `still` while main_add.drv plays) | left/right_hand.weight = 1 - kLeftArm / kRightArm flag of main.drv, offset 1 scale -1 base 1 beats 0 [C data] | None if B1(c) shows weight 1 | B1(c): the IK weight per hand per frame |
| B5 | P3 | rig.cpp:840 uses the data's length_scale (L 0.333, R 0.384) | CompositeCharacter enter: outside test / proxy, every CharIKScale `scale 1`, then CharForeTwist `length_scale 0.38` [C data; I: DataCond runs only the first default clause, so maybe just the IKScale one] | Leave as is unless B1 / arm stretch shows the forearm twist bones off | bandcheck stretch on fore twist bones |
| B6 | P3 | GD05-092 (IKScale capture / measure_lengths after deform) | DEFORM_COMMON (char_objects.dta:777-803): capture_before -> deform -> capture_after -> measure_lengths -> measure_length_scale; in game only where `deform` runs [C data] | Only with GD05-092's deform | B1(a) per character body type |
| B7 | P3 | Kit pad hits (snare.dmidi etc. on bone.servo, clip sets male_drum_<pad>) were done in D51; nothing new found | | | Pad target moves > 0.2 units within 100 ms of its note |

## Order for the build agent

1. Faces: A1 (eyes turn), A2 (blinks on everyone), A3 (expressions). Each is self-contained and behind its own switch.
2. Then A4 and A6, which need A2's Blink value.
3. Then A5 and A7. A8 and A9 last.
4. Drummer: add B1 first and report its numbers. Then fix B2 / B3 only if B1 shows them. Do not change the IK maths otherwise; it matches RB.
