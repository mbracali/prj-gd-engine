# GH1 PS2: what the original draws when a gem is hit

[C] means read from code (the EE decompile in analysis/decomp) or from the shipped data (extracted/track/gen/track.rnd_ps2, decoded with tools/rnd_classes). [I] means inference.

All objects named here are in `extracted/track/gen/track.rnd_ps2`. Track coordinates are the same as the engine's: x = lane (kLaneX −7.87 … 7.87), y = distance along the track (the strike line is y 0, larger y is toward the horizon), z = up from the track surface.

Related docs: render_gems_dof.md §2.3 (tails, burn, flare mat), endgame_and_hud.md §1.7 (smashers), camera_walk_crowd_lighting.md §7 (venue particles).

---------------------------------------------------------------------------------------------------
## 1. Summary

A hit plays the GemSmasher bump, which we already do. It also plays one **particle effect view** on every fret of the chord:
- `smash_normal.view` for a normal gem: orange/yellow flames and sparks.
- `smash_star.view` for a gem in an intact Star Power phrase: cyan/blue.
- `smash_bonus.view` while Star Power is active: cyan, plus embers that fall and bounce on the track.

Each of these views holds 3–4 ParticleSys emitters, a ParticleSysAnim per emitter that shapes the emit rate over the first 3–5 frames, and a spinning, scaling, colour-animated flare quad (`smash_flamelight*.mesh`).

Two more effects are added:
- **Star-phrase capture.** On the hit that completes a Star Power phrase, two more views play on each fret of that chord: `smash_combo.view`, a burst of about 80 cyan particles shot upward that then fall, and `smash_combo_lightning.view`, three flashing vertical lightning bolts fanned outward by 12° per lane.
- **Sustains.** While a sustain is held, `burn_tail.view` plays on the held frets. It is a continuous fire particle stream plus a pulsing glow quad, coloured orange, cyan while whammying, or blue during Star Power.

**We implement none of the particles.** We draw only the flare quad, with a linear tint fade: no spin, no scale, no colour keys, and no star variant. We also lack the combo burst and lightning, and the whole burn effect. The engine has no ParticleSys class at all. §5 gives the full simulation and draw rules needed to add one.

---------------------------------------------------------------------------------------------------
## 2. Triggers and lifetime (code path) [C]

### 2.1 Objects built per track
- The smasher set is built by **FUN_001643b0** and stored at gem manager +0x180. Its fields:
  - `[0..4]`: five GemSmashers (FUN_001669d0, see endgame_and_hud §1.7).
  - `[5]`: the **smash effect holder**, built by FUN_00166208. It contains:
    - `+4`: a new empty RndView, the **container**.
    - `+8`: a vector of 3 view instances, built by FUN_00164f70 from track_graphics.dta `particles smash (normal smash_normal.view) (star smash_star.view) (bonus smash_bonus.view)`.
    - `+0x18`: the burn holder (FUN_00166758). This is a `burn_tail.view` instance plus `burn_tail_state` and the state frames `normal 0 / whammy 10 / bonus 20` from `particles burn`.
  - `[6]`: an instance of `smash_combo.view` (`particles combo1`).
  - `[7]`: `smash_combo_lightning.view` (`particles combo2`). It is used directly, not instanced.
  - `[8]`, `[9]`, `[10]`: three **per-lane proxy sets** (FUN_001659c8). Each set has 5 new RndViews, one per lane, at `x = FUN_00163828(lane, lefty)` and rotated about the track's **Y axis** by `(lane−2)·angle`. The angle is negated for lefty. Set [10] uses `angle = combo2_angle = 12°`; sets [8] and [9] use 0. The 12° fan applies only when `GameConfig+0x8c == 0`, i.e. single player [I: the flag's meaning]. Each set also keeps a start-time array (+0x10).
- **View instance** (FUN_00164f70):
  - Finds the view by name. In multiplayer, for players after the first, it deep-copies it (FUN_001ecf60).
  - Sets frame 0, sets showing = TRUE, and detaches it.
  - For every ParticleSys in its draw list it calls **FUN_001cf918(1.0)**, which forces `relative_motion = 1`. The files store 0 for most smash parts. With 1, the particles live in the emitter's local space and move with it (§5.4).
- **Proxy views** use RndView +0x130. The file field is mis-named `env` in geometry.decode_view; it is really an "instance source" view.
  - RndView draw is RndView_t1_v5 @0x1ef8f0. If `+0x130 != self`, it pushes its own frame to each of the source's anims (SetFrame), re-parents the source's transform children to its own world matrix, and draws the source's draw list.
  - `FUN_001efa90(view, src)` sets the source. `src = self` (FUN_00166160) means "draw nothing".
  - So one shared set of particle systems is drawn at each lane that currently points at it.

### 2.2 Hit
`PlayerMatcher_v3` @0x108a90 (`hit_gem`) calls Track FUN_00159a88, then FUN_0015e200 (gem record), then **FUN_00164988(now, smashers, gem, bonus = gm+0x188, sp_state = player+0xcc)**.

`player+0xcc` is the StarPower object (+0x88) field +0x44, set by FUN_0011c390 `star_gem_hit` / `phrase_captured`:

| value | meaning |
|---|---|
| 0 | gem not in a phrase |
| 1 | phrase missed |
| 2 | in an intact phrase |
| 3 | this hit completed the phrase (`phrase_captured`) |

FUN_00116580 updates it before the track call.

FUN_00164988 does the following:
1. Calls **FUN_00166628(holder, bonus, star = sp_state ∈ {2,3}, burn = !(gem+0xc bit 9))**. Bit 9 is the no-sustain flag; FUN_00116580 uses it the same way.
   - It **stops everything first** (FUN_001666b8): it detaches all 3 smash views and the burn view from the container.
   - It attaches one smash view to the container: `bonus ? smash_bonus : star ? smash_star : smash_normal`.
   - If the gem has a sustain, it also attaches `burn_tail.view` and sets the burn's bonus flag.
2. For each lane 0..4:
   - **In the gem's fret mask:**
     - Set [8] proxy for that lane gets source = container and `start[lane] = now`.
     - GemSmasher FUN_00167530 plays `gem_smasher_hit.tnm`.
     - If `sp_state == 3`: set [9] proxy gets source = the smash_combo.view instance and set [10] proxy gets source = smash_combo_lightning.view, both with start = now.
   - **Not in the mask:** set [8] proxy is cleared, so an effect still playing on another fret **vanishes instantly**. If `sp_state == 3`, sets [9] and [10] are cleared too. Otherwise the old combo proxies keep playing.
3. Sets `set[4] = gem index`, the gem whose sustain owns the burn.

### 2.3 Per frame
Track poll FUN_0015de78 path → **FUN_00164870(now, set, whammy = track state[3] = player+0xc0)**:
- Smasher tnm frames (FUN_001676c0).
- `burn.whammy = state[3]` (FUN_00166738 → FUN_00166940, which freezes `burn_tail_state.view` at frame 0, 10 or 20; whammy wins over bonus).
- FUN_00166080 for sets [8], [9], [10]: `proxy[lane].SetFrame(0.03 · (now − start[lane]))`.

`now` is song time in ms, so these are **30 fps frames from the hit**. Nothing loops at this level, but some child anims loop through their own filters.

### 2.4 Stop
- **Sustain end** (FUN_0015e020, when now ≥ tail end) or **early release** (FUN_0015e388) calls FUN_00164c50(set, gem). If `gem == set[4]` it runs FUN_001666b8. That detaches the burn **and the smash view**, so any hit sparks still flying disappear too.
- **Miss with a fret** (FUN_0015e2f8 → FUN_00164ae8): pops the smasher (big 0.71 bump), stops everything, and sets `set[4] = −1`.
- `track set_bonus_gems B` → FUN_00164d00 → burn bonus flag (cyan burn).
- Seek or reset FUN_0015e490 → FUN_00164910: resets every smasher and clears every proxy.
- Restarting an effect (a new hit) sets the frame back to 0. The particle systems see `dt < 0`, and the kill rule `frame ≤ birth` removes all of their old particles (§5.3). **There is only ever one smash effect per track, replayed on every fret of the latest hit.**

Scripts (player.dta `star_power_handlers`) add no track visuals for these events:
- `phrase_captured` → sp_awarded sequence after 200 ms (HUD/sfx) and a callback.
- `star_gem_hit` → callback only.
- `start_using` → camera shake, `track set_bonus_gems TRUE`.

---------------------------------------------------------------------------------------------------
## 3. The hit effect views [C data]

The view transform is relative to the lane proxy (x = lane, y = 0, z = 0). "Keys" are the ParticleSysAnim/MatAnim/TransAnim keys **as resolved through keys_owner / frames_owner**: the anim reads the keys of its owner, see §5.5.

### 3.1 `smash_normal.view` (normal gem)
View at z **1.156**. Draw order: `_0.part, _1.part, _3.part, _2.part, smash_flamelight.mesh`.

| object | pos (local) | what | material |
|---|---|---|---|
| smash_normal_0.part | z +0.5 | 3–4 big flame blobs. Life 5 f, speed 0.1 any direction, size 2.5 grows to 7.5, colour (1,.96,.65,.5)…(1,.8,0,.5) → (1,.41,0,0)…(1,0,0,0). **Force (0,1,0)/f²**, so they shoot up the track about 12 units in 5 frames. Collide plane z=0. Max 12. | flare_up (flame_part.tex, SrcAlphaAdd, z off) |
| smash_normal_1.part | z +0.5 | 3–4 flame blobs, size 3.0–3.5 (constant), same colours, no force | flare_up1 (flame_part2.tex) |
| smash_normal_2.part | z +2.5 | 3–4 sparks, speed **1.0/f** any direction, size 0.3–0.5 shrinking to 0, white/yellow → orange/red, force (0,1,0), collide z=0, max 12 | fuse_burn (startime.tex, SrcAlphaAdd) |
| smash_normal_3.part | z −0.5 | **showing = FALSE, so it is simulated but never drawn**. Burst 10–20 at f0, spark2.tex, bubble motion. | smash_ignition |
| smash_flamelight.mesh | (0,−1,−0.9) → world z 0.256 | 7×7 quad (the mesh's own xfm is 45° × 0.8·√2) | smash_flamelight.mat: smash_firelight.tex, blend Add, colour (1,.2,.1), zmode Transparent, use_environ 1 |

Emit keys:
- parts _0, _1 and _2 all use **smash_normal_0.panim**: `emit_rate (low,high)` = f1 (0,0) → f2 (3,4) → f3 (0,0). That is about 3.5 particles each over frames 1–3 (33–100 ms).
- smash_normal_3 uses its own keys: f0 (10,20) → f1 (0,0).

Flare anims:
- **smash_flamelight_normal.mnm** (base colour): f0 (0,0,0) → f1 (1,1,.1) → f3 (1,.5,.1) → f5 (1,.2,.1) → f8 (0,0,0). Yellow flash fading to red and off over 267 ms.
- **smash_flamelight_normal.tnm**:
  - rot: one full turn about Z over 8 frames. Quats (0,0,0,−1) f0 → (0,0,−.707,−.707) f2 → (0,0,−1,0) f4 → (0,0,−.707,.707) f6 → (0,0,0,1) f8, linear (no spline).
  - scale (xy): 0.3 f0 → 1.0 f1 → 1.2 f4 → 1.0 f7 → 0.303 f8.
  - trans: constant (0,−1,−0.9).

### 3.2 `smash_star.view` (gem of an intact phrase, sp_state 2 or 3)
z 1.156. Same layout as normal, with colours in blue/cyan:

| object | differences from normal |
|---|---|
| smash_star_0.part | colour (.5,.5,.15,1)…(.05,.64,.98,.5) → (.07,.46,.86,0)…(0,.35,.69,0); max 5 |
| smash_star_1.part | colour (.05,.64,.5,1)…(.05,.64,.98,.5) → blue, alpha 0; max 4 |
| smash_star_2.part | sparks (.09,.85,.89)…(.05,.64,.98) → (.07,.46,.86,0)…(1,1,1,1); max 5 |
| smash_star_3.part | hidden (showing FALSE) |
| smash_flamelight_starcollect.mesh | same geometry (geom_owner smash_flamelight.mesh), mat **smash_flamelight_starcollect.mat** (colour 0, Add). Its tnm has no keys and frames_owner = smash_flamelight_normal.tnm, so the spin and scale are identical. |

- The star_0/1/2 panims use keys_owner **smash_normal_0.panim**, so the timing is identical. smash_star_0.panim's own keys are ignored.
- **smash_flamelight_starcollect.mnm**: f0 (0,0,0) → f1 (.15,.85,1) → f3 (.15,.85,1) → f5 (0,.15,.25) → f8 0. A cyan flash.

### 3.3 `smash_bonus.view` (Star Power active)
z 1.15. Draw order: `_4, _3, _2, _1, smash_flamelight_starcollect.mesh`.

| object | data |
|---|---|
| smash_bonus_1.part (z −0.5) | **drawn** here. Own keys f0 (10,20) → f1 0, so a burst of about 15 at the hit. Speed 0, bubble on (period 10–25 f, radius 0.5–0.75 in XZ), size 0.5–2 → +0..1, colour (.5,0,.5)…(0,1,1) → 0, force (0,.5,0), max 8. Mat smash_ignition (spark2.tex, SrcAlphaAdd). |
| smash_bonus_2.part (z +0.5) | flare_up blob, own keys f1 0 → f2 (3,4) → f3 0. Size 2.5/1.5 → +5, colour (0,.84,1,1)…(.6,.9,1,1) → alpha 0, force (0,1,0). |
| smash_bonus_3.part (z +0.5) | flare_up1, same keys, size 2–3.5, light cyan |
| smash_bonus_4.part (z 0) | **embers**. Own keys f3 0 → f4 (4,5) → f5 0, about 4.5 embers. Box x ±1, y 0, z 1..2; speed .02–.1, horizontal, any yaw; **life 20–50 f (0.67–1.67 s)**; size .5–1.2 → 0; colour (.1,.89,1)…(.48,.81,1) → alpha 0; **force (0,0,−.02)/f²** (falls); collide z=0 (bounces). Mat starfire_3 (starfire_one.tex, Add). Max 7. |
| smash_flamelight_starcollect.mesh | as in §3.2 |

### 3.4 Star-phrase capture: `smash_combo.view` + `smash_combo_lightning.view` (sp_state == 3)
**smash_combo.view** (z 0) → **smash_combo_burst.part**:
- Local rotation rows X (1,0,0), Y (0,0,1), Z (0,−1,0): local +Y points **up** (+Z world).
- Emit keys (own): f0 0 → f1 50 → f3 0, about 75 attempted, capped by **max 80**.
- life keys (5,50) f0 → (0,0) f20; speed keys (.4,.8) f1 → (0,0) f20. Both shrink for particles born later.
- Box x ±1; yaw ±15°, pitch 0. So it is an **upward fan** at 12–24 units/s.
- Force (0,0,−.04). After the code's rotation by the particle-space matrix (§5.3) this acts as about 0.04/f² of gravity [I: net effect].
- Size 0.5 → +0.2..1.5; colour (.3,1,1)…(1,1,.83) → (0,1,.57,0)…(0,.95,1,0) (cyan/green fade).
- Mat smash_combo.mat (startime.tex, SrcAlphaAdd).

**smash_combo_lightning.view**:
- Three quads `smash_combo_lightning01..03.mesh`. Each is 2.7 × 10.8 in the mesh, stood up by a 90° X rotation and scaled 2.355, so each is a **~25-unit-tall bolt** from z 0.86.
- Mats SP_biglightning01..03.tex, colour (.5,1,.8), SrcAlphaAdd, base alpha 0.
- Alpha keys (MatAnim):

| bolt | keys |
|---|---|
| L01 | f−1 0, f0 1, f1 1, f2 0 |
| L02 | f1 0, f2 1, f3 0, f4 1, f5 0 |
| L03 | f2 0, f3 1, f4 0 |

  So the bolts flicker for about 5 frames (170 ms).
- tnm: a sway of ±3.5° about the bolt's axis. 01 and 02 use filter ScaleOffset ×2 + MinMaxLoop 0..10 (loops every 5 f). 03 is unlooped over 7 f, and its x scale goes −2 → −1 over 15 f.
- The code's set [10] proxies rotate each lane's copy about Y by (lane−2)·12°, so the bolts **fan outward**: slot 4 leans to +x. The file's `smash_combo_L2..L5.view` (rotations ±12°/±24°, empty draw lists) are the authoring copies of the same fan and are unused.

### 3.5 Sustain: `burn_tail.view` (shown from the hit until the sustain ends or is released)
Its frame runs at 0.03·(now − hit), so it is continuous.

| object | data |
|---|---|
| burn_tail_base.part | at (0,−.06,.66). **Continuous emit 5–10/f**, life 8–11 f, box ±.5/±.2/±.2, speed .3–.4 any direction, size .35–.6 → +(0..0.2), force (0,.002,−.05), collide z=0, max 80. Mat fuse_burn_new (fuse.tex, Add). |
| burn_tail_big.part | at z .9. Type **1**. Emit 5/f, life 2–5 f, box x ±.95, y 0..1, z −.2..0.85, speed .3–.4 toward +y, size .2–3.5. shrink_ratio .3: holds size for 30% of life, then shrinks linearly to 0. Mid colour white at ratio 0 (§5.3). Max 20. Mat fuse_burn_big (fuse_big.tex, Add). |
| burn_castlight.mesh | smash_flamelight geometry, ×0.8, at y −1. burn_castlight.tnm scale .8 → 1 (f1) → .6 (f4) → 1.2 (f7) → .8 (f10), MinMaxLoop 0..10 (pulses every 333 ms). Mat burn_castlight.mat (smash_firelight.tex, Add). |
| burn_tail_state.view | frozen at frame 0 / 10 / 20 = normal / whammy / bonus. It drives the keys below. |

State keys driven by burn_tail_state.view:

| track | f0 normal | f10 whammy | f20 bonus |
|---|---|---|---|
| base start colour | (1,.7,.1,.5) | (.4,1,1,1) | (.4,.9,1,1) |
| base end colour | (1,0,.1,0) | (.2,1,1,1) | (.2,1,1,1) |
| base speed | .3–.4 | .3–.4 | .3–1.0 |
| big start colour | (1,.7,.15,.5) | (.4,1,1,.6) | (.4,1,1,1) |
| big end colour | (1,0,.15,0) | (.2,1,.5,1) | (.2,1,1,1) |
| big speed | .3–.4 | .002–.03 | .002–.03 |
| castlight mat colour | (1,.6,.15) | (.15,.4,.7) | (.15,.6,1) |

Colour keys preserve the original high−low spread (§5.5).

### 3.6 Other hit-related visuals (already documented elsewhere)
- Gem removal on hit and the tail glow line and width wobble: render_gems_dof.md §2.3.
- Star-tail whammy particles **tail_bonus.part** (whammyparticle.tex, emit ×2g along the tail, killed past the tail end): render_gems_dof.md §2.3. It is a ParticleSys too.
- SP deploy sparkle burst (gem_bonus_spark1/2) and song-start sparkle: render_gems_dof.md §2.2. These are TransAnim/MatAnim, not particles.
- Smasher bump `gem_smasher_hit.tnm` (z 0.35 peak at f1, back at f5) and the pressed material: endgame_and_hud.md §1.7.
- Unused in the track scene: `finishline.part` [I: no code or script references it]. `track_explode_*` particles belong to the win explosion.

---------------------------------------------------------------------------------------------------
## 4. What the engine does vs. what is missing

| original | engine (track_renderer.cpp) | status |
|---|---|---|
| smasher hit bump gem_smasher_hit.tnm, pop, pressed mat, rim | yes | OK |
| flare quad smash_flamelight.mesh: spin 360°/8 f, scale .3→1.2→.3, MatAnim colour keys | drawn at (lane, −1, 0.26) with a linear `hitFlash` tint fade (5/s); no spin, no scale, no colour keys | **wrong** |
| flare mat: starcollect for star-phrase gems (sp_state 2/3) and during SP | starcollect only during SP | **wrong** |
| smash particles normal/star/bonus (3–4 drawn emitters each) | none | **missing** |
| effect shared per track: a new hit restarts it and removes it from frets not in the new chord | the flare is per fret | differs |
| phrase-capture burst + lightning fan | none | **missing** |
| sustain burn: 2 particle systems + castlight pulse, 3 colour states, ends on release or end | none | **missing** |
| release/miss cuts the current smash effect | n/a | missing |
| star-tail whammy particles tail_bonus.part | none | missing (see render_gems_dof) |

How to drive it from the engine:
- `app.cpp:828` sets `flashes_[f] = 1` on a hit. It needs to pass the hit time, the gem's fret mask, `bonus`, the star-phrase state (0/1/2/3, as in StarPower +0x44) and `noSustain` to the TrackRenderer instead. Sustain end, release and miss must also be signalled.
- **Simplest faithful plan:**
  1. Load ParticleSys and ParticleSysAnim into Scene. The decoders exist in tools/rnd_classes/animation.py.
  2. Implement §5.
  3. Treat a "view instance" as the view's anims + draws.
  4. Per track, keep `effect = {normal|star|bonus}`, `burnOn`, `t0[5]` and `mask`.
  5. Each frame, for each lane in the mask, SetFrame((now − t0)·0.03) and draw the effect's draw list with model = translate(laneX, 0, 0).
  6. Draw the combo and lightning the same way with their own t0, the lightning rotated about Y by (lane−2)·12° (single player).
  - AnimPlayer already applies keys owners and ScaleOffset/MinMaxLoop filters. It can run the flare mnm/tnm and the lightning anims as soon as the views are set to a frame.

---------------------------------------------------------------------------------------------------
## 5. ParticleSys: file format, simulation, draw [C unless marked]

Vtables: RndParticleSys 0x2fa560/5a0/5c0/5f0, PsParticleSys 0x2f9a40…, RndParticleSysAnim 0x2fafe8.

| routine | address |
|---|---|
| Load | RndParticleSys_t3_v8 @0x1cedc0 (then PsParticleSys_v8 @0x1a2c20 → FUN_001a2a20 builds a static VU buffer if preserve_particles) |
| Print | PsParticleSys_v3 @0x1cde90 (field names) |
| SetFrame | PsParticleSys_v1 @0x1cffc0 |
| update | FUN_001d0a60 |
| emit | FUN_001d0060 (+ FUN_001d0990 start colour/size) |
| world-transform hook | PsParticleSys_t2_v1 @0x1cfae8 |
| draw | PsParticleSys_t1_v5 @0x1a2fd8 → FUN_001a2df0 |
| pool alloc/free | FUN_001cf870 / FUN_001cf8c0 |
| set relative motion | FUN_001cf918 |
| ParticleSysAnim Load | RndParticleSysAnim_v8 @0x1fe528 |
| ParticleSysAnim SetFrame | RndParticleSysAnim_v1 @0x1fedf8 |

### 5.1 Record (version 22, every particle record in the game) — `decode_particlesys`
After `u32 version`, the Animatable, Transformable (+0x30) and Drawable (+0xf0) blocks come first. Then (Vec2 = (low, high); each particle draws uniformly between them):

| field | type | obj offset |
|---|---|---|
| life | vec2 | +0x158 |
| pos_low | vec3 | +0x160 |
| pos_high | vec3 | +0x170 |
| speed | vec2 | +0x180 |
| pitch | vec2 (rad) | +0x188 |
| yaw | vec2 (rad) | +0x190 |
| emit_rate | vec2 (per frame) | +0x198 |
| start_size | vec2 | +0x1a0 |
| end_size | vec2: a **delta** added over life (see emit) | +0x1a8 |
| start_color_low | rgba | +0x1b0 |
| start_color_high | rgba | +0x1c0 |
| end_color_low | rgba | +0x1d0 |
| end_color_high | rgba | +0x1e0 |
| collide | bool8 | +0x250 |
| collide_plane | a,b,c,d | +0x260 |
| force | vec3 (per frame²) | +0x270 |
| mat | ref | +0x1f8 |
| type | u32 (0 normal, 1 grow/shrink/mid-colour) | +0x144 |
| grow_ratio | f32 | +0x284 |
| shrink_ratio | f32 | +0x288 |
| midcol_ratio | f32 | +0x28c |
| midcol_low | rgba | +0x290 |
| midcol_high | rgba | +0x2a0 |
| max_particles | u32 (pool) | |
| bubble_period | vec2 | +0x148 |
| bubble_size | vec2 | +0x150 |
| bubble | bool8 | +0x280 |
| relative_motion | f32 | +0x204 |
| mesh | ref: emitter mesh | +0x1f0 |
| preserve_particles | bool8 | +0x200 |
| particles (if preserve_particles) | `{pos3, rgba, size}` × n | |

Runtime fields:
- +0x130: live list head.
- +0x134: last frame (init −9999999).
- +0x138: explicit-emit clock (−9999999 = off).
- +0x13c: emit accumulator.
- +0x210..0x24c: **particle-space matrix** P.

### 5.2 Particle struct (floats, index × 4 = byte offset)
| index | field |
|---|---|
| [0..3] | colour rgba |
| [4..7] | colour delta per frame |
| [8..10] | pos |
| [0xc..0xe] | velocity |
| [0x10..0x12] | bubble axis × radius |
| [0x14] | bubble angular frequency |
| [0x15] | bubble phase |
| [0x16] | death frame |
| [0x17] | birth frame |
| [0x18] | size |
| [0x19] | size delta per frame |
| [0x1c] | grow end |
| [0x1d] | grow rate |
| [0x1e] | shrink start |
| [0x1f] | shrink rate |
| [0x20..0x23] | type 1: mid → colour slope before the mid time |
| [0x24] | mid time |
| +0x68 / +0x6c | list links |

### 5.3 Per-frame rules
`rand(a,b) = a + u·(b−a)`, u = 16-bit LFSR / 65536 (FUN_0024a0c8). `sin` is a 256-entry table (FUN_0024cc68).

**SetFrame(frame)** (PsParticleSys_v1):
```
if last != -9999999:
    dt = frame - last
    update(dt)
    if explicitClock == -9999999:
        emit(dt)
last = frame
```
The frame also sits at anim+8 and is used as "now" below. The units are those of the driving view: here 0.03·ms, so **one frame = 1/30 s**.

**update(dt)** (skipped when dt == 0). Let `f = force·dt`. For each particle:
- **Kill** if `now ≥ death || now ≤ birth`. The second test means rewinding the frame (dt < 0) kills everything born after the new frame, which is how a restarted effect is cleared.
- `pos += vel·dt`.
- If bubble: `pos += axis · sin(now·freq + phase + π/2) · freq · dt`. This is the derivative of `axis·sin(freq·t + phase)`.
- If collide and `dot(n, pos) + d < 0` and `dot(vel, n) < 0`: `vel −= 2·dot(vel, n)·n` (lossless reflection). n is the plane's abc, unnormalised. The plane is in particle space.
- `vel += P3x3 · f` (rows +0x210, +0x220, +0x230).
- **type 0:** `colour += Δcolour·dt` (no clamp); `size += Δsize·dt`.
- **type 1:**
  - `colour += (now < midTime ? midSlope : Δcolour)·dt`, clamped to [0,1].
  - `size += (now < growEnd ? growRate : now < shrinkStart ? Δsize : shrinkRate)·dt`.

**emit(dt)** (only when dt > 0):
1. `accum += rand(emit_rate)·dt`.
2. Spawn matrix M:
   - relative_motion == 1: M = identity, so particles are stored in emitter-local space.
   - Otherwise: M = emitterWorld · P⁻¹. With P = identity (rm 0) particles are stored in world space and stay where they were spawned.
3. `while accum ≥ 1`: take a particle from the pool. If the pool is empty, set accum = 0 and stop. Otherwise:
   - `birth = now`; `death = birth + rand(life)`.
   - Without an emitter mesh: `pos = rand per axis(pos_low, pos_high)`; `p = rand(pitch)`, `y = rand(yaw)`; `dir = (−cos p·sin y, cos p·cos y, sin p)`. pitch 0, yaw 0 = +Y.
   - With an emitter mesh (+0x1f0, with faces): pick a random face and a uniform point on it (barycentric u, v, reflected when u + v > 1). `dir` = the normalised face normal. pitch and yaw are not used.
   - `vel = M3x3·(dir · rand(speed))`; `pos = M·pos`.
   - If bubble: `freq = 2π / rand(bubble_period)`; `phase = rand(0, 2π)`; `a = rand(0, 2π)`; `axis = M3x3·((sin a, 0, cos a)·rand(bubble_size))`; `pos += axis·sin(phase)`; `phase −= now·freq`.
   - `colour = rand per channel(start_color_low, start_color_high)`; `size = rand(start_size)` (FUN_001d0990).
   - `Δsize = rand(end_size)`, clamped to ≥ −size. `endColour = rand per channel(end_color_low, end_color_high)`. `inv = 1/(death − birth)`.
   - **type 0:** `Δcolour = (endColour − colour)·inv`.
   - **type 1:**
     - `growEnd = grow_ratio ? birth + life·grow_ratio : 0`; `growRate = size/(growEnd − birth)`. If grow_ratio ≠ 0, the size starts at 0.
     - `shrinkStart = shrink_ratio == 1 ? 1 : birth + life·shrink_ratio`; `shrinkRate = (size + Δsize)/(shrinkStart − death)`. That rate is ≤ 0, so the size reaches −Δsize (normally 0) at death.
     - `midTime = birth + life·midcol_ratio`; `mid = rand(midcol_low, midcol_high)`.
     - `Δcolour = (endColour − mid)/(death − midTime)`. If midTime ≠ birth, `midSlope = (mid − colour)/(midTime − birth)`.
   - `Δsize *= inv`.
   - `accum −= 1`.

**World-transform hook** (PsParticleSys_t2_v1):
- relative_motion == 1: P = emitter world, so the particles follow the emitter.
- rm == 0: P is untouched (identity).
- 0 < rm < 1: P is interpolated with weight rm.

### 5.4 Draw
FUN_001a2df0 / PsParticleSys_t1_v5:
- Uploads P (4×4) as the model matrix.
- Sends each live particle as `{rgba[0..3], pos[8..10], size·0.5}` to VU1 program 0x5cc, in batches of 127, once per material pass (FUN_002ebcb0, the mat's blend/zmode/texture; DAT_002edd78 is the default mat).
- The VU expands each particle to a **camera-facing quad of side `size`** centred on pos, with the full texture and colour = particle rgba [I: the VU1 code is not decompiled; half-size per side is implied by the ·0.5].
- **Colour scale [I]:** treat rgba 1.0 as the full texture (PS2 0x80). Values outside [0,1] (type 0 does not clamp) saturate.
- The Drawable `showing` flag is honoured by the draw walk (FUN_002ec850), so `showing = FALSE` parts (smash_*_3) simulate but do not draw.
- preserve_particles systems draw a static list built at load time (FUN_001a2a20).

### 5.5 ParticleSysAnim (RndParticleSysAnim_v1 @0x1fedf8) [C]
- Every key track is read from **keys_owner** (+0x8c), not from the anim itself.
- For each **non-empty** track: find the keys around the frame (clamped to the first/last key), lerp linearly, then:
  - start colour: `start_low = k`, `start_high = k + (old_high − old_low)` (the spread is kept). End colour is done the same way.
  - emit_rate, speed, life, start_size: set (low, high) = k directly.
- Blend weight w ≠ 1 lerps from the current values.
- The anim's own filters apply to the frame first.
- Anim-list order matters: in smash_normal.view the parts come before their panims, so a part emits with last frame's rate (a one-frame lag) [C order, I effect: negligible].

### 5.6 Smash-specific consequences
- relative_motion is forced to 1 (§2.1). Particles therefore live in the part's local space and are drawn at whichever lane proxy draws them. A chord shows identical copies on each fret.
- The collide plane (0,0,1,0) is then the part's own origin height, not the track. That is [I]: probably authored for world space; we reproduce it as coded.
- The proxies of a chord share one frame, so the 2nd and later SetFrame calls in the same tick have dt = 0 and change nothing.

---------------------------------------------------------------------------------------------------
## 6. Address index
- Smasher set: FUN_001643b0 (ctor), FUN_00164768 (dtor), FUN_00164870 (poll), FUN_00164988 (hit), FUN_00164ae8 (miss), FUN_00164c50 (sustain end/release), FUN_00164910 (reset), FUN_00164d00 (bonus burn), FUN_00164b30 / FUN_00164bc0 (pop / pressed mat), FUN_00164c78 (toggle_nowbar).
- Smash holder: FUN_00166208 (ctor), FUN_00166628 (select + start), FUN_001666b8 (stop all), FUN_00166718 / FUN_00166738 (bonus / whammy burn flag).
- Burn holder: FUN_00166758 (ctor), FUN_001668c0 / FUN_001668e0 (attach / detach), FUN_00166940 (freeze state frame).
- Lane proxies: FUN_001659c8 (build, fan angle), FUN_00166108 (set source + t0), FUN_00166160 (clear), FUN_00166080 (frame = 0.03·(now − t0)), FUN_00166190 (showing).
- View instance: FUN_00164f70 (find/copy, frame 0, show, force rm = 1), FUN_00165218 (attach to parent: draws + anims + transform child), FUN_001653e8 (detach).
- RndView proxy draw: RndView_t1_v5 @0x1ef8f0; set source FUN_001efa90.
- Star phrase state: FUN_0011c390 (`star_gem_hit` / `phrase_captured`, StarPower+0x44 = player+0xcc), FUN_0011c700, FUN_0011c7c8 (`phrase_miss` → 1).
- Hit entry: PlayerMatcher_v3 @0x108a90 → FUN_00159a88 → FUN_0015e200 → FUN_00164988.
