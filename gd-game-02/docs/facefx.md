# FaceFX lip-sync and facial expressions

How GH2 (Xbox 360) drives the characters' faces: the FaceFX SDK 1.5 archives on
the disc and the `FaceFxLipSyncServo` (each character's `lip.servo`). Code:
`engine/src/char/facefx.{h,cpp}`; check: `gd02 --facefxtest <disc>`.
[C] = read in the executable's code (addresses), [I] = inferred.

## Files on the disc

- 70 `.voc` (FxAnim): `songs/<song>/<song>.voc` ×68, `songs/_blinktrack/_blinktrack.voc`,
  `xboxlive/gh2xp0001/songs/rodgort/rodgort.voc`. 201,109 keys. No `.voc` for the
  instrumentals (gemini, jessica, jordan, misirlou, soybomb, yyz) or the tutorials.
- 2 `.fac` (FxFaceGraph): `char/guitarist.fac` (SDK 1500), `char/metal_singer/ng/metal_singer.fac`
  (SDK 1200, project "Karaoke Revolution Vol 4").

## Archive format [C]

Little-endian. Readers: u16 0x823858A8, u32 0x82385A30, f32 0x82385D40.

- Header: `'FACE'`, u32 SDK version (1500; 1200 in `_blinktrack.voc` and `metal_singer.fac`),
  FxString licensee ("Harmonix"), FxString project, u32 1000. Then one object.
- FxString (0x82383C80) = u16 version (1; 0 in SDK 1200) + u32 length + bytes. FxName (0x82389ED8) =
  u16 0 + FxString. FxArray = u16 0 + u32 count + items. FxObject = u16 0. FxNamedObject (0x82387D40) =
  FxObject + u16 0 + FxName.
- FxAnim (0x82385468): named; u16 version (3; 0 in `_blinktrack`); u32 absolute end offset; curves
  FxArray {named, u16 0, u32 interpolation (0 Hermite everywhere), keys FxArray {u16 0, f32 time,
  value, slope in, slope out} (0x8239B500)}; bone curves FxArray (empty); f32 0.16, f32 0.22;
  version > 1: FxArray {u16 0, FxName, f32} (bone weights, empty); version > 0: FxString audio,
  FxString text (empty); version > 2: u32 (0xFFFFFFFF). Every song `.voc` has the same 25 curves:
  15 visemes (Eat Earth If Ox Oat Wet Size Church Fave Though Told Bump New Roar Cage), head
  orientation / emphasis pitch-roll-yaw, gaze pitch / yaw, Eyebrow Raise, Blink. Times are song
  seconds. `_blinktrack` has keys only in Blink (906 keys, 3.8–1068.6 s: 0 → 1 → 0 in ~0.2 s).
- FxFaceGraph (0x8238D250): FxObject, u16 0, u16 0, u32 n, n object pointers. Pointer: i32 index
  (-1 = none); a new object: FxString class, u32 object size (0x60 / 0x50; 0x5C in SDK 1200), object.
  FxFaceGraphNode (0x82393528): named, u16 version, f32 min, f32 max, u32 input op (0 sum, 1
  multiply), links FxArray, version > 0: user properties FxArray (0x82393498, none on the disc).
  Link (0x82391788): FxObject, u16 version, FxName input, FxName function ("linear"), u16 0 +
  FxArray f32 parameters, function object pointer (-1). FxBonePoseNode (0x8238FF70): node, u16,
  bones FxArray {named, u16, pos 3 f32, rot 4 f32, scale 3 f32} (0x8238ECA8). FxCombinerNode
  (0x8238D658): node, u16.
- `guitarist.fac`: pose nodes Neutral, Good1–5, Bad1–3, EyesClosed, OwFace; combiners
  expressionGood1–5 / expressionBad1–3 (each linked 1:1 into its pose node), Blink, L/R-eyeZ,
  L/R-eyeX (min −1), EyeZCombiner = 0.5·R-eyeZ + 0.5·L-eyeZ; EyesClosed = Blink + EyeZCombiner.
  `metal_singer.fac`: pose nodes Neutral + the 15 visemes, no links.
- FxAnim+0x3C / +0x40 (0.16 / 0.22): the SDK's blend-in / blend-out times [I]. Nothing GH2 runs
  reads them (the player tick, the instance tick and the servo do not) [C, by search].

## Evaluation [C]

- Curve (FxAnimCurve::Evaluate 0x82390318): `t <= first key` → first value; `t >= last key` →
  last value; else segment [k0, k1) (cached index), dt = t1 − t0, s = (t − t0)/dt, m0 = k0.slopeOut·dt,
  m1 = k1.slopeIn·dt, v = (((2v0 − 2v1 + m1 + m0)·s + (3v1 − 3v0 − 2m0 − m1))·s + m0)·s + v0
  (interp 1: linear). A curve bound to a node writes the node's track value.
- Anim bounds (FindBounds 0x82382E50): min / max over curves of (first, last key); an empty curve
  counts as (0, 0); no curves → (0, 1).
- Node value (FxFaceGraphNode::GetValue 0x82391AB8, cached per frame): without links: the track
  value, combined with the register (op 0 add, 1 multiply, 2 replace; a missing track starts at
  0 for add, 1 otherwise); with links: Σ (or Π) of link(input value), + track, then the register
  op; corrective links would scale by (1 − x) (none on the disc). Missing value → 0; else clamped
  to [min, max].
- Linear link (0x823964C8): 0 parameters → x; 1 → m·x; 2 → m·x + b (fused multiply-add);
  other counts → x.

## FaceFxLipSyncServo

Record v5 (decoded in `char/char_objects.*`): Wt, `.fac` path, viseme CharClipSet path, entries
{object, u32 axis, node}. Types (`char/gen/char_objects.dtb`): `gh2` on 20 records (19 guitarist
characters + rockabill1_ui), `singer` on metal_singer and female_singer. The eye entries (eye-L/R.mesh axis 2 →
L/R-eyeZ, axis 0 → L/R-eyeX) are on 14 guitarists; alterna1, classic, deathmetal1, goth2, grim
and the singers have none.

Load (0x82190DB0) [C]:
1. Load the `.fac` (FilePath relative to the character milo's folder; `og` → `ng`).
2. Master bone list from the viseme clip set (0x821A8368).
3. 0x821A7DD8: for each name i of the type's `visemes` list (gh2: EXPRESSIONS_LIST = Neutral Good1–5
   Bad1–3 EyesClosed OwFace; singer: VISEMES_LIST = Neutral + 15 visemes): pose the clip set's
   `visemes` clip at frame i, find (or create) the bone pose node of that name, replace its bones
   with that pose, node+0x5C = i. So the face poses come from the clip set, never from the
   `.fac` bones.
4. 0x8218E6B8: for each entry node name, a combiner node when missing, registered as a register.

change_song (sent at the end of PreLoad; dta handlers):
- gh2: `{$this set_song _blinktrack}` → 0x821915D8 → `songs/_blinktrack/_blinktrack.voc`
  (format `%s/%s%s.voc` in `songs`).
- singer: `{handle (game set_lipsync_song $this)}` (0x822B6A18) → set_song(current song, "", 1,
  song known (0x822A33C0)): known song → `<folder of midi_file>/<midi_file base>.voc` (`%s/%s.voc`);
  else `songs/<song>/<song>.voc`. A missing file (instrumentals) plays nothing.
- 0x8218E150: load the FxAnim, add it to the actor, create the FxActorInstance on the first song
  (kept), PlayAnim (0x82387748): curves bound to nodes by name (unmatched curves drive nothing:
  for a guitarist only Blink binds; for the singer the 15 visemes; head / gaze / brow curves never
  bind), player state 2.

Which songs have which singer [C data]: `songs.dta` `(band ...)` with female_singer: crazyonyou,
tattooedloveboys, ftk, lesstalkmorerokk, yeswecan; no singer: gemini, jessica, jordan, misirlou,
soybomb, yyz; every other song: `gh2.dta (default_band metal_singer metal_bass metal_drummer)`.
Both singers use `metal_singer.fac` and the song's `.voc`.

Poll (0x8218E7E0) [C], skipped when no instance, when a global flag (0x8278291C, no writer found)
is set, when the player has stopped, or when the servo's directory is a Character that is not
showing:
1. (+0x154 == 0: pose `neutral` at weight 1 only. +0x154 is set to 1 in the constructor
   0x821911D0 and nothing writes it: dead path.)
2. Entry registers (0x821A71D0): each entry object's transform at +0x1C (the local transform [I])
   → axis 0: atan2(y.z, y.y); 1: atan2(−x.z, z.z); 2: −atan2(y.x, y.y) (rows = local axes;
   0x82269988 / 0x822699B8 / 0x822699E8), set with op replace. So an eye rotated by +θ about its
   z axis adds θ/2 per eye to EyesClosed (lids follow the gaze).
3. Expression cross-fade: f = (now − start)/(end − start), capped at 1 (no lower cap); previous
   expression register = (1 − f)·its weight at the switch; current = f. Interpolation time 0.
4. FxActorInstance tick (0x82388098) with the UI clock and the song clock (TaskMgr 0x8280EC40:
   +0x28 → +0x28 kTaskUISeconds, +0x08 kTaskSeconds = song seconds): registers (interpolated on
   the UI clock; the servo always passes 0 = immediate); node values cleared; FxAnimPlayer
   (0x82387540): anim time = song seconds when ≥ 0, else UI seconds since the first tick minus
   the anim's negative start; time ≤ anim end → all curves evaluated; past the end → stop
   (state 4: that frame still poses with no curve values, later frames do not pose at all).
5. Registers into the nodes (0x82387F50), then the pose (0x821A7978): ScaleDown the `visemes`
   clip; for each bone pose node in graph order (the master bone list's first bone's nodes,
   0x8238E1A0) with value ≠ 0: ScaleAdd (CharClip vtable +0x14) frame node+0x5C with that weight;
   residual = 1 − Σ|w|; every accumulated quaternion gets w += residual (−= when its w < 0):
   the rest of the weight goes to the identity; then the `neutral` clip at its start frame
   through vtable slot 7 (0x82161F98 → 0x8215E6A0): an additive layer (positions +, quaternions
   neutral·q, euler channels +). So the viseme frames are deltas over the `neutral` pose.
6. 0x821A51E0: the servo's bones out (CharWeightable weight), as other servos.

Messages (Handle 0x82191950) [C]: `set_register name value [op]`, `set_song song`,
`do_expression name blend` (0x8218F380: nothing if name is current; the current expression
becomes the previous one with weight min(1, f now); start = now (song seconds), end = now + blend).

Expressions (gh2 type dta) [C data]:
- `pick_expression` (character enter, intro_start_msg, intro_skip): restarts `expression_task`,
  a kTaskSeconds thread: do_pick_expression, sleep random_float 1.5 2.5, loop.
- do_pick_expression: only with a game and no override; `game get excitement`: Boot / Bad →
  expressionBad`r` (r = random_int 1 3), Okay / Great / Peak → expressionGood`r` (random_int 1 5);
  if r ≥ last_expression, r + 1 (no repeat); last_expression is shared by both lists and starts at
  −1, so the first pick is never 1. Blend 0.25 s.
- ow_face_on (MIDI text event) → `override_expression OwFace` (do_expression OwFace 1, override
  on); ow_face_off → `resume_random_expression` (override off, pick at once).
- The singer type has no expressions and no blinking (its graph has no Blink node).

## In gd02

`facefx::LipSync` is the servo runtime (setup, play, poll, doExpression, setRegister) and returns a
`FacePose` (visemes clip frames + weights + residual); `chr::Character` applies it as in step 5 at the
servo's place in the rig order (docs/band.md "Faces"), the band (`game/band.cpp`) sets it up per character;
`facefx::ExpressionScript` mirrors the gh2 type's handlers for code that does not run them through the
interpreter. `--facefxtest` parses every archive and checks the graph values against the M9 research
parser (`facefx.py graph_eval`) at 96 sample times, plus the servo semantics above.
