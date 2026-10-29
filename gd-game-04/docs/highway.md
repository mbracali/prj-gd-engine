# GH3 (Xbox 360) 2D highway: the spec (implemented in engine/src/render/highway2d.cpp)

Clean-room notes for reimplementing the gameplay highway. Every rule is marked
**[C]** (read in the XEX at the address given, or in disc data) or **[I]**
(inferred). Nothing here is decompiler output; behaviour is described in our own
words. Scripts: `tools/qbscript.py dis` (inline structs decoded with a scratch
wrapper); QB data: `tools/qb.py dump`; materials: `tools/scn.py dump`.

Screen space is a 1280x720 virtual screen (x centre 640) [I: every layout value
centres on 640, offscreen y = 720].

## 0. Where things come from

| What | Source |
|---|---|
| Tweaks (geometry, colours, timing) | `scripts\guitar\guitar_tweaks.qb` (`...1` = one player, `...2` = two players / end credits) [C data] |
| Copy tweaks -> working globals, build lane / row tables | script `generate_pos_table` (`guitar\highway_2d.qb`), called first by `setup_highway` [C script] |
| Build highway, sidebars, SP fx, strings, now-bar buttons | script `setup_highway` (`guitar\guitar_highway.qb`) [C script] |
| Row tables -> C | `SetRowHeightTables` 0x82278890 -> 0x8238D500 [C] |
| Lane / material / scale cache | `SetGemConstants` 0x82103088 (globals 0x829E4C84.., 0x829E4CCC..) [C] |
| Gem + sustain creation | `Create2DGem` 0x82121C58 -> worker 0x82120C90 [C asm] |
| Fretbar creation | `Create2DFretbar` 0x82121E00 [C asm]; iterator `fretbar_iterator_CFunc` 0x821111B8 [C] |
| Per-frame motion of gems / fretbars / tails | mover list: add 0x821190B8, update `UpdateGemMovers` 0x8210E750 [C] |
| Highway mesh | 0x8238DE48 (row strips) [C]; whammy strip mesh 0x8238E338 / 0x8238ECF8 [C, partly] |
| Now-bar buttons per frame | `ButtonCheckerPerFrame` 0x8211B1E0 [C] |
| Hit flame | `NoteFX` 0x8211BAE8 [C asm], script `hit_note_fx` [C] |
| Star Power look | `StarPowerOn` 0x8211DA70 [C]; script `GuitarEvent_StarPowerOff` [C] |
| Sidebar colours | scripts `GuitarEvent_Fretbar`, `set_sidebar_flash` (`guitar_events.qb`) [C] |
| Textures | `DATA/COMPRESSED/ZONES/global.pak.xen`, tex `c:/gh3/data/zones/global/global_gfx.tex.xen`, materials in `global_gfx.scn.xen` (128 materials) [C data] |
| Highway texture | per guitarist: `PAK/<char>_1.pak.xen`, scene `Guitarist_<Char>_Outfit<o>_Style<s>`, material `sys_<char>_1_highway_sys_<char>_1_highway` (e.g. Axel: key 0x89A130C2, tex `axel1c1tex:1`, 512x1024 DXT1) [C: script at the band-member setup, `AddToMaterialLibrary` + `FormatText 'sys_%a_1_highway...'`]. Global fallback material 0xCE5B3C9F / 0x1D627C5E use tex 0x2C0ED3FD (512x1024) [C data] |

## 1. Geometry

### 1.1 Tweak values [C data, guitar_tweaks.qb]

| | 1P (`..1`) | 2P (`..2`) |
|---|---|---|
| gHighwayTiling | 1.5 | 1.4 |
| highway_playline (y) | 655 | 655 |
| highway_height | 350 | 270 |
| highway_top_width | 160 | 170 |
| widthOffsetFactor | 2.2 | 1.33 |
| highway_fade | 30 | 25 |
| gem_start_scale / gem_end_scale(tweak) | 0.25 / 0.8 | 0.27 / 0.8 |
| gem_star_scale | 1.3 | 1.3 |
| gem_y_just / star_y_just | 0.83 / 0.5 | same |
| fretbar_start_scale | 0.15 | 0.16 |
| whammy_top_width / whammy_width_offset | 10 / 1.8 | 9.2 / 1.5 |
| sidebar_x_offset / x_scale / y_scale | 4 / 0.3 / 1.0 | 4.5 / 0.3 / 0.75 |
| starpower_fx_scale | 1.0 | 0.75 |
| nowbar_scale_x / y | 0.8 / 0.8 | 0.66 / 0.8 |
| string_scale_x / y | 0.65 / 0.8 | 0.65 / 0.5 |

`generate_pos_table` picks set 1 iff `current_num_players = 1 and end_credits = 0` [C].
Other: x_offset_p2 225 (2P highways at x -225 / +225), destroy_time 1.0,
neck_sprite_size 16, neck_lip_add 16, neck_lip_base 5, button_up_pixels 20,
button_sink_time 0.1, fret_offset_tweak -14 (ms), whammy_offset_tweak -33 (ms),
whammy_cutoff 1100, highway_flash_dying 0.5, highway_pulse_time 0.2 [C data].

### 1.2 Derived values (generate_pos_table) [C script]

- `topY = playline - height` (1P: 305). `bottomW = top_width * (1 + widthOffsetFactor)` (1P: 512).
- Lane centres, top: `640 - top_width/2 + top_width/5 * (k + 0.5)`, k = 0..4 (green..orange).
  1P: 576, 608, 640, 672, 704. Bottom (at the playline): `640 - bottomW/2 + bottomW/5*(k+0.5)`
  1P: 435.2, 537.6, 640, 742.4, 844.8.
- Lane angle `atan2(height, start_x - end_x)` in degrees (green 1P: atan(140.8/350) = 21.9 deg;
  yellow 0; orange -21.9) [C script; degrees I].
- `SetButtonData` 0x821059D0 stores per colour in `button_models`: start_pos (start_x, topY),
  end_pos (end_x, playline), left_start_pos / left_end_pos (x of the mirrored lane: green<->orange,
  red<->blue), angle, left_angle; in `button_up_models`: pos_2d (end_x, playline), left_pos_2d [C].
  Lefty (`lefthanded_gems = 1`) uses the left_* set [C 0x82120E8C, 0x821033DC-0x82103498].
- `gem_end_scale := gem_start_scale * (1 + widthOffsetFactor)` (1P 0.8),
  `fretbar_end_scale := fretbar_start_scale * (1 + widthOffsetFactor)` (1P 0.48) [C script].
- Fade: `ghighwayendfade = topY` (305), `ghighwaystartfade = topY + highway_fade` (335) [C script].
  Material props `m_startfade` / `m_endfade` get these on the highway, sidebars, gems and fretbars
  [C]; meaning: alpha 0 at y <= endfade, ramping to 1 at y >= startfade [I].
- Sidebar: `sidebar_angle = atan2(height, stx - sbx)` (stx = 640 - top_width/2 = 560,
  sbx = 640 - bottomW/2 = 384; 1P = 26.7 deg); left anchor
  `sidebar_x = sbx + (sbx - stx)*0.25 - sidebar_x_offset` (1P 336),
  `sidebar_y = playline + height*0.25` (1P 742.5) [C script].

### 1.3 Row (depth) tables [C script generate_pos_table + C 0x8238D500]

The highway is divided into `highway_lines` = 1152 rows; `real_highway_lines` = 1024 rows
reach the playline, the other 128 continue below it [C data].

1. `hh = clamp(height - 162, 0, 510)`; F = heightPerspFactTable[hh], E = heightPerspExpTable[hh]
   (512-entry float tables in highway_2d.qb) [C]. 1P: F = 1.000886, E = 1.001541.
2. d[0] = 1; d[i] = (d[i-1] * F) ^ E for i = 1..1151 [C].
3. Normalise: divide every d by the sum of d[0..1023] (so rows 0..1023 sum to 1) [C].
   (The script also copies d into `gem_time_table512`.) 1P: d[0] = 0.000347, d[1023] = 0.00315
   (rows 9x taller at the playline than at the top).
4. rowheight[0] = topY; rowheight[i] = rowheight[i-1] + height * d[i-1] (rowheight[1024] = playline) [C].
5. time_accum[0] = 0; time_accum[i] = time_accum[i-1] + d[i] [C].
6. C copies (0x8238D500): rowheight, distances, plus per-8-row tables (cumulative distance, and
   rowheight[8k] - topY) used by the strip meshes [C].

### 1.4 Gem position from time [C 0x821190B8, 0x8210E750]

- Travel time over 1024 rows: `T = gHighwayTiling / |highway_speed|`, and the highway speed is set
  to `highway_speed = -gHighwayTiling / (scroll_time - destroy_time)` [C script setup_highway;
  C 0x8210E814 reads gHighwayTiling cached at 0x829E4C84 by SetGemConstants], so
  **T = scroll_time - destroy_time**.
- scroll_time per difficulty (`difficulty_list_props`): easy 3.5, medium 3.0, hard 2.75,
  expert 2.5 s; x0.8 with two players / end credits; x0.88/0.83/0.78/0.73/0.68 for hyperspeed 1..5
  [C script difficulty_setup]. So T 1P = 2.5 / 2.0 / 1.75 / 1.5 s.
- Gems are spawned by the iterator `time_offset = T*1000 + gem_offset` ms ahead; gem_offset =
  xenon_gem_offset = -66 ms [C script gem_scroller]. Elapsed at creation =
  (song_ms + time_offset - note_ms) * 0.001 [C 0x82121310-0x82121330].
  Hence: **elapsed e = (song_ms - (note_ms - gem_offset)) / 1000 + T**; the gem reaches the playline
  (row 1024) when song_ms = note_ms - gem_offset = note_ms + 66 [C, follows].
- Each frame: `r = e / T * 1024` (row, fractional). `v = time_accum[floor r] + (d[floor r + 1] -
  d[floor r]) * frac(r)` [C 0x8210E8xx; note the C interpolates with the *difference* of d — a
  tiny deviation from `+ d[r+1]*frac`; < 0.002 in v]. Row index is taken modulo 1152 [C].
- Position = lerp(start_pos[lane], end_pos[lane], v); uniform scale = lerp(s0, s1, v) [C]. v is
  the normalised distance (0 top, ~1.0 playline, ~1.55 at row 1152 for 1P), so y =
  topY + height*v and lane x = start_x + (end_x - start_x)*v (straight lines towards the
  vanishing point). Sample 1P: t/T 0 -> y 305; 0.25 -> 340.6; 0.5 -> 390.8; 0.75 -> 474.7;
  1.0 -> 656; 1.125 -> 845 (offscreen).
- If v decreases (row wrapped past 1152) the element is not moved; after 10 such frames it is
  destroyed [C 0x8210E9xx].
- Gem scale: s0 = gem_start_scale, s1 = gem_end_scale (0.25 -> 0.8); star gems x gem_star_scale
  (0.325 -> 1.04) [C 0x82120F80-0x82120F98]. A 128x64 gem at 0.8 = 102.4 px = bottom lane width [C data].
- Width of the highway at depth v: `top_width * (1 + widthOffsetFactor * v)` [C 0x8238DE48].

## 2. Elements, textures, blend, z

All highway elements of player p hang under `gem_container<p>` (containerelement, z 0, pos
(x_off, 0)) [C script setup_highway]. Intro: the container starts at y = 720 and `move_highway_2d`
slides it up: speed starts at -720 px/s, multiplied each frame by a factor that starts at 1.0 and
drops by 0.00044 per frame, until y <= 0 [C script].

Materials: `d8` field (material +0xD8) is 3 for gems, nowbar, sidebar, fretbars, whammy, highway and
1 for note-hit flames, sparks, lightning [C data]; read as 3 = alpha blend, 1 = additive [I].
Flip-book shader 0x98D259F8: constsA[0] = columns, [1] = rows, [2] = frames per second [I from
values]: Star2D 4x4 @30, BattleGEM 2x4 @30, note_hit 4x4 @60, Big_Bolt 8x1 @15 [C data].

z_priority (within the container; children of sub-containers order inside their parent) [C values]:

| z | element |
|---|---|
| 0.1 | highway (`Highway_2D<p>`) |
| 1.0 | fretbars |
| 2.0 | strings |
| 3 | sidebar containers (+ SP glow/lightning inside), now-bar button containers |
| 4.0 | sustain (whammy) containers + sprites |
| 4.1 | gems |
| 8 | hit sparks (particles) |
| 10 | hit flame |

### 2.1 Highway [C script setup_highway, C 0x8238DE48]

- SpriteElement, clonematerial = `player_status.highway_material`, rgba highway_normal
  [255,255,255,255], pos (640 - top_width/2, 0), dims (top_width, 0), just [left left], z 0.1.
- Drawn as a strip mesh, one strip per row (rows shorter than 2 px are merged, DAT_8291E72C = 2.0),
  stopping below the screen; row i spans y = rowheight[i]..rowheight[i+1] (+ container y), centred
  on x = 640, width top_width*(1 + wof*D) where D = cumulative distance at that row; texture
  U = 0..1 across, **V = row * tiling / 1024** (so 1.5 texture repeats from top to playline 1P) [C].
- Scrolling: material `m_velocityV = highway_speed` (V/s, negative), `m_tiling = gHighwayTiling`
  [C script Set2DHighwaySpeed]; V offset = velocity * time [I], so the texture moves 1024 rows
  per T — locked to the gems.
- Fade at top via m_startfade/m_endfade (see 1.2).
- Star Power: `StarPowerOn` sets the highway rgba to highway_starpower [64,255,255,255] (cyan
  tint) and makes `starpower_container_left/right<p>` alpha 1 [C 0x8211DA70]; Off: alpha 0, rgba
  highway_normal [C script GuitarEvent_StarPowerOff].
- Miss pulse `highway_pulse_black`: rgba -> highway_pulse [0,0,0,255] over 0.1 s, back to normal
  over 0.1 s; spawned from GuitarEvent_MissedNote when `silent_miss = 1` [C script].
- Multiplier loss shake `highway_pulse_multiplier_loss` (on 2x/3x/4x off; not in battle / boss):
  push = (0,3)/(0,4)/(0,10)/(0,15) for mult 1/2/3/4 (x0.6 in 2P modes); five morphs of the
  container of 0.1 s (0.11 s for 4x): +push, -0.7*push, +0.4*push, -0.3*push, +0.2*push, each plus
  a random x of +-7, +-7, +-5, +-5, +-3 px, then back to the original position [C script].

### 2.2 Sidebars and Star Power fx [C script setup_highway]

- Left: container at (sidebar_x, sidebar_y), rot_angle = sidebar_angle, z 3; sprite
  `sys_sidebar2D` (tex 0x7F2DACD6, 32x512) pos (0,0), scale (sidebar_x_scale, sidebar_y_scale)
  = (0.3, 1.0), just [center bottom], z 3, gem fade props. Right: container at
  (1280 - sidebar_x, sidebar_y), rot = -angle, sprite scale (-0.3, 1.0) (mirrored).
  Rotation sign: positive = clockwise on screen [I: places the bar top at (566, 285) ~ highway
  top-left (560, 305)].
- SP fx in containers `starpower_container_left/right<p>` (same pos/rot, alpha 0 until SP):
  glow `sys_Starpower_SDGLOW` (0x72B4DF7E, 128x512) pos (-55, 55)*fx (right: (55,55)), scale
  (1, 1.1)*fx (right (-1,1.1)), just [center bottom], z 0; lightning `sys_Big_Bolt01`
  (0x9D12571C, 1024x512 = 8 frames) rot 180, just [center top]: #1 rgba [0,0,128,128] scale
  (-0.5, 0.9)*fx (right (0.5,0.9)) z 4; #2 white scale (2, 0.9)*fx z 0.02 [C].
- Colours (`set_sidebar_flash`, run on every beat fretbar as it reaches the playline, i.e.
  fretbar_iterator with time_offset gem_offset -> GuitarEvent_Fretbar) [C script]: beat_flip
  toggles each beat; state priority: SP active -> sidebar_starpower; else dying (health <=
  crowd_poor_medium 0.6666 * highway_flash_dying 0.5 = 0.333; 1P uses current_crowd) ->
  sidebar_dying; else star_power_amount >= 50 -> sidebar_starready; else sidebar_normal. beat_flip 0
  uses `X0` = [255,255,255,255], 1 uses `X1`: normal [192,255,255], starready [128,192,255],
  dying [255,80,80], starpower [192,255,255] (all alpha 255) [C data]. No interpolation (set props).

### 2.3 Strings [C script]

Per lane: `sys_String01` (0x2ED9AC77, 32x512), parent gem_container, pos = pos_2d (end_x,
playline), rot = lane angle, scale (string_scale_x, string_scale_y) = (0.65, 0.8), just [center
bottom], rgba [200,200,200,200], z 2.

### 2.4 Fretbars [C 0x821111B8, 0x82121E00]

- Iterator over the song's fretbar array (`<song>_fretbars`, beat times) with time signatures
  (`<song>_timesig`) [C]. Spawned with time_offset T*1000 + gem_offset [C script gem_scroller].
- Type: first beat of a measure -> `thick` -> `sys_fretbar_large` (0xC1832175, 1024x16); other
  beats -> `medium` -> `sys_fretbar_medium` (0x9C77551B) [C 0x82111xxx case 4]. Thin half-beat
  bars (`sys_fretbar_small` 0xBB0ED6C7) at the midpoint between two fretbars only when the
  iterator has `thin_fretbars` (the gem_scroller call passes it) AND the time signature key
  "t<num>d<den>" is in `thin_fretbar_timesigs` AND low_bpm < BPM < high_bpm, BPM = 60000 /
  current beat ms (8th-note sigs x/4: 1..180; 16th sigs x/8: 1..120) [C].
- Sprite: material by type, parent gem_container, z 1.0, just (0, 1) [C values; centre-bottom I];
  mover from yellow lane start_pos (640, topY) to end_pos (640, playline), uniform scale
  fretbar_start_scale -> fretbar_end_scale (0.15 -> 0.48: 153.6 -> 491.5 px wide ~ highway width)
  [C]. Elapsed gets fret_offset_tweak (-14 ms) added, so bars run 14 ms behind gems [C 0x821220D4].

### 2.5 Gems [C 0x82120C90]

Material choice (per lane; `hopo` = input_array word 6; `star` = note in a star phrase via
0x82102B70) [C branches 0x82120E84-0x82120FC8]:

| condition | material | scale x | y just |
|---|---|---|---|
| star_power_used (SP active), any gem | `sys_Gem2D_StarPower[_hammer]` (0x6FB9F94E / 0x5BDA8E6A) | 1 | gem_y_just |
| star gem, battle or boss battle | `sys_BattleGEM_<C>01` / `sys_BattleGEM_Hammer_<C>01` | gem_star_scale | star_y_just |
| star gem | `sys_Star2D_<C>` / `sys_Star2D_<C>_Hammer` (512x256, 4x4 frames @30) | gem_star_scale | star_y_just |
| normal | `sys_Gem2D_<C>` / `sys_Gem2D_<C>_hammer` (128x64) | 1 | gem_y_just |

(Table-to-name mapping of the hammer / non-hammer arrays inferred from SetGemConstants' load
order [I].) Sprite: parent gem_container, z 4.1, just (0, y_just) [C; just units -1..1, so 0.83
anchors near the bottom I], materialprops m_startfade/m_endfade/m_playerindex [C]. Hit gems are
removed and a flame shown (2.8) [I: GemStep / hit path not fully read].

### 2.6 Sustains (whammy tails) [C 0x82121360-0x8212167C, 0x8210E750]

- Exists when the lane's sustain length (0x82102E28) > 0 (sustain rule: FINDINGS §5) [C].
- Container (z 4.0, just (-1,-1)) + sprite: clonematerial = `sys_Whammy2D_<C>` (32x32), or
  `sys_Whammy2D_StarPower` if SP is active at creation [C]; pos = lane start, scale (0.6, 0),
  rot_angle = lane angle, just (0, 1), z 4.0, `m_glowenabled` = 0 [C].
- Mover with whammy flag: same start/end positions, scale 1; elapsed + whammy_offset_tweak
  (-33 ms); length parameter L = sustain_s / T (fraction of the travel) [C 0x82121428-0x82121444].
- Per frame: head row = e/T*1024 (as a gem); if the note is being held (`holding` = 1) the head
  start time is reset to now - whammy_cutoff x T / N (N = gem_time_table512's size, 1152), so the
  head row stays at whammy_cutoff x 1024 / 1152 = 977.8 (1P: 0.870 of the way down, above
  the playline row 1024 [I: hidden by the fret buttons]) [C 0x8210E8D4-0x8210E910; gd04 pins it at the
  playline, a follow-up]. Tail row = (e_creation/T - L)*1024, clamped at 0 (top); tail beyond 1152 -> destroy
  [C]. Sprite at the head position, y scale = (y_head - y_tail) / (texture height * cos(angle)),
  x scale 0.6 [C 0x8210EAxx: 1/f(pi/2 - rot); f = sin I].
- Rendering: a strip mesh in 8-row steps (0x8238E338 / 0x8238ECF8): half width = whammy_top_width *
  (1 + whammy_width_offset * D) / 2 (1P: 10 px at top -> 28 px at the playline), each vertex
  carrying a row index into the per-player wibble array `whammywibble0/1` (128 floats) [C: keys
  whammy_top_width, whammy_width_offset, heightperspfact/exp, whammywibble0/1 read in 0x8238ECF8;
  displacement use I]. `SetNewWhammyValue` 0x821056F8 (per player, args value, time_remaining):
  n = floor(128 * time_remaining_s / T) + 2, clamped to [whammy_wibble_speed (2), 128]; the
  array is shifted down by whammy_wibble_speed entries up to n, the first whammy_wibble_speed
  entries get the new value, entries from n to 127 are set to 1.0 [C 0x821057C4-0x821058F4;
  reading of the loops I]. So the wibble pattern scrolls with the tail.
- Dead / star / normal swaps: `MakeDeadWhammy` 0x8210E508 (sys_Whammy2D_Dead), `MakeStarWhammy`
  0x8210E5C8, `MakeNormalWhammy` 0x8210E6B8 [C names; triggers I: miss/release, SP on].

### 2.7 Now-bar strike buttons [C script setup_highway, C 0x8211B1E0]

Per lane a container `<button>_base<p>` (z 3) with four sprites at pos2d = (end_x, playline)
(lefty: left_pos_2d), scale now_scale = (nowbar_scale_x, nowbar_scale_y) = (0.8, 0.8) (lefty x
negated), just [center bottom]:

| part | material (128x64 unless noted) | pos | z (rest / raised) |
|---|---|---|---|
| lip | `sys_NowBar_Button01_<C>_Lip` | pos2d | 3.9 / 4.9 |
| mid | `sys_NowBar_Button01_<C>_Mid2` | pos2d | 3.6 / 4.6 |
| neck | `sys_NowBar_Neck01` (64x16) | pos2d - (0, neck_lip_base 5) | 3.7 / 4.7 |
| head | `sys_NowBar_Head_<C>` / `_<C>L` (lit) / `sys_NowBar_Button01_<C>_Down` | pos2d - (0, pixels) | 3.8 / 4.8 |

- pixels[lane] (`button_up_pixel_array<p>`, 0..button_up_pixels=20): head raised by `pixels`, neck
  y scale = (pixels + neck_lip_add 16) / neck_sprite_size 16; z base 4 when pixels > 0 else 3;
  each frame pixels -= 20 * dt / 0.1 (sinks in 0.1 s), clamped at 0 [C 0x8211B1E0].
- Head material: fret not held (and string not broken) -> head; held and pixels <= 0 -> down;
  held and pixels > 0 -> lit (`..L`) [C 0x8211B9xx; which table is which name I].
- pixels set to button_up_pixels when a note is hit, for the frets of the note that are held: a
  chord raises every held fret of it; a single note raises the held frets from its fret towards
  orange (the pattern masked with 0x11111 >> ...) [C 0x8211C9D0: `button_up_pixels` (0x829E39E0)
  into the player's pixel array, FUN_82226C78]; the intro ripple raises them one by one
  (`intro_buttonup_ripple`: from button_ripple_start_time -800, one button per 100 ms, green
  first) [C script]. Draw order follows z: resting buttons (3.6..3.9) under the tails (4.0) and
  gems (4.1), raised ones (4.6..4.9) over them; the lip (x.9) is drawn over the head (x.8) [C values].

### 2.8 Hit flame and sparks [C 0x8211BAE8, script hit_note_fx]

- Flame sprite: clonematerial `sys_note_hit01` (0xA38B2820, 512x1024 = 4x4 frames of 128x256 @60
  fps, additive) or `sys_note_hit_blue_01` (0x9E700D1C) when star_power_used; pos = given pos -
  (0, 20); just (0, 1); scale 1; z 10 [C 0x8211BCBC, DAT_82007A9C = 20, DAT_8200158C = 10].
- Particles (`hit_particle_params`, SP: `star_hit_particle_params`): material
  sys_Particle_Spark01, z 8, colour [255,128,0,255] -> [255,0,0,0] (SP [0,255,255,255] ->
  [0,255,255,0]), scale 1 -> 0.5, emit_rate 0.02, spread 160, velocity 10, friction (0,50),
  life 0.25 s [C data].
- `hit_note_fx`: after 6 game frames stop emitting, after 10 more destroy the flame (16 frames =
  one pass of the 16-frame flip-book at 60 fps) [C script].

## 3. Screen-element API needed [C scripts; semantics I]

- `CreateScreenElement {Type, id, parent, pos, just, scale, dims, rgba, alpha, z_priority,
  rot_angle, material | clonematerial | texture, font/text (TextElement via displayText)}`.
  Types used: containerelement (transform node), SpriteElement (textured quad), TextElement.
- pos: pair in parent space (root = 1280x720). Children inherit parent pos/scale/rot/alpha [I].
- just [h v]: -1/left/top, 0/center, 1/right/bottom (numbers allowed, e.g. [0.5 0.8]); the
  sprite's anchor point inside its rectangle [I].
- scale: float (uniform) or pair; a negative component mirrors [C use]. dims: pixel size
  (default = texture size [I]).
- rgba [r g b a] 0..255 multiplies the texture; alpha 0..1 multiplies [I].
- rot_angle degrees, clockwise-positive on screen [I].
- material = shared material key; clonematerial = per-element copy so `materialprops`
  ([{name property}], e.g. m_startfade, m_endfade, m_playerindex, m_velocityV, m_tiling,
  m_glowenabled) can differ per element [C].
- `SetScreenElementProps` (set), `DoScreenElementMorph {id pos|rgba|alpha|scale time}` (linear
  interpolation over time [I]), `DestroyScreenElement`, `ScreenElementExists` [C].

## 4. HUD (one player) [C data career_hud_2d_elements; script create_2d_hud_elements]

`g_hud_2d_struct_used = career_hud_2d_elements` (guitar_hud_2d_career.qb). Element textures are
`.img` entries in global.pak.xen (HUD_rock_body, HUD_score_body, ...). Each element: container or
SpriteElement (texture, pos_off, zoff -> z_priority, just default [left top], alpha), parent by
`element_parent` + player suffix. Containers slide from offscreen_* to the final pos (morph) [I].

- Rock meter: container `HUD2D_rock_container` final `rock_pos` (1260, 692) (offscreen (2000,610)).
  Children of HUD2D_rock_body (HUD_rock_body, z 20): BG green/yellow/red (z 16/15/14), lights_all
  (17), lights green (128,0)/red (0,0)/yellow (128,0) (z 18, alpha 0), needle (132,165) just
  (0.5,0.8) z 19, glow Char_Select_Hilite1 (-50,-100) 350x350 rgba [95,205,255] alpha 0 z -10.
- Star Power meter: 6 bulb containers at (128,128) in the rock container, rot -47.5, -33, -18.5,
  0, 21, 42; tube HUD_rock_tube 64x128 at (0,-160) (bulbs 1-3, small: dims x0.7) / (0,-170)
  (4-6, big: x1.0), plus `tube` fill (HUD_rock_tube_glow_fill / _b in SP, 64x16 at +(0,40|32),
  z 0.1) and `full` (HUD_rock_tube_glow_full / _b, z 0.2) [C data].
- Score: container `HUD2D_score_container` at score_pos (300,650) (offscreen (-500,560)); body
  HUD_score_body z 5; score text font num_a9 at (222,70), scale 1.1, just right, z 20,
  font_spacing 5; 5 multiplier progress lights at (0,200),(0,170),(0,140),(0,110),(0,80) each with
  hud_score_light_0 (z 5) / _1 (5.1) / _2 (5.2) states; multiplier nixie tubes HUD_score_nixie_1a,
  2a, 2b, 3a, 4a, 4b, 6b, 8b at (70,90) z 4 (alpha toggled; C `UpdateNixie` 0x82106F20);
  HUD_score_flash (128,128) z 20.
- Note streak counter: container at counter_pos (330,810) + offscreen_note_streak_bar_off (0,800)
  until shown; body HUD_counter_body z 9, drum HUD_counter_drum (4,40) z 8, icon (44,40) z 26;
  4 digit texts font num_a7 at (222 - 37*i, 78), i = 1..4, first rgba [15,15,70,200], others
  [230,230,230,200].
- Who drives needle / lights / tubes each frame: UpdateScoreFastPerFrame 0x821167B0 and the HUD
  scripts (agents/scratch/hud_spec.md §1.3, §1.4) [C].
- Port (render/hud2d.cpp, render/hud2p.cpp): the morphs use GH3's motions (0x822A6B28: smooth
  3t^2 - 2t^3, ease_in t^2, ease_out 2t - t^2; values start + (target - start) x f) [C], so the
  HUD's slides, settles, tube moves and pulses are exact in shape; the scripts' random pulse
  times (pulsate_star_power_bulb) come from our own generator [I: GH3's RNG not reproduced].
- Texts (ui_system drawText with a TextStyle: rot_angle about the anchor, x / y scale,
  font_spacing): the score num_a9 with font_spacing 5, squeezed past 5 digits to scale
  (score_frame_width / width, 1.0) [C 0x82116884; the width read unscaled I]; "<n> Note Streak!"
  and "Star Power Ready" with their rotation wobble, glow burst (Char_Select_Hilite1) and
  lightning (HUD_lightning_01/03/05/07, 800x100, each 0.2 s in / out) [C scripts
  hud_show_note_streak_combo, hud_glowburst_alert, show_star_power_ready, hud_lightning_alert];
  the two-player positions as the scripts give them (face-off (415|865, 170/180), co-op /
  p2_career player 1's only) [C]. The shadow offset turning with the text is [I].
- The song intro (intro_song_info): title (scale 1.23, [210 210 210 250]), artist_text ('by' /
  'as made famous by', scale (1, 0.5), [230 205 160 255]), artist (1.0, [255 190 70 255]), upper
  case, text_a10, at (255,75) / (255,135) / (255,150), shadow (1,1); fade 0.7 s in, 3 s on, 0.7 s
  out from song_title_start_time (fastintro: the scroller's first frame) [C scripts setup_sprites,
  intro_song_info; data fastintro_sequence_props].

## 5. Texture keys (global_gfx.tex.xen unless noted) [C data]

Export: `img.py export DATA/COMPRESSED/ZONES/global.pak.xen "c:/gh3/data/zones/global/global_gfx.tex.xen:<key>" out.png`.

Gem2D Green 151EE874, Red 27DD8DC4, Yellow 39E35D20, Blue C715D48D, Orange A5A7F110; hammer
G 59C6A636, R D51DD55E, Y 9E1B6D3E, B D326251A, O 0243B96C; StarPower 6FB9F94E, StarPower_hammer
5BDA8E6A. Star2D G C2C8127D, R BDB47035, Y CE93E0D6, B C76F7566, O E8D694D6; Star2D hammer
G 22E10D71, R 5C5C9114, Y 76D89EA6, B B695B723, O EA804AF4. BattleGEM G 06485C80, R AE484472,
Y D9987563, B CEA8D3F0, O 36381741; BattleGEM hammer G 4E605076, R DADBCA9A, Y AB496E94,
B D6C32766, O 486C2688. Whammy2D G B47133C1, R 6C97B6C6, Y EC463E7A, B 037E6D2C, O CA034A7A,
StarPower E78E1A35, Dead 2A589F46. Fretbar large C1832175, medium 9C77551B, small BB0ED6C7.
Sidebar2D 7F2DACD6; Starpower_SDGLOW 72B4DF7E; Big_Bolt01 9D12571C; String01 2ED9AC77.
NowBar: Neck01 90C16FBD; Head G 42B576FB, R 2A2FFBB4, Y 043BD094, B C9901DA2, O 49DED95F;
HeadL G DF92D29E, R 39991412, Y 02D920F1, B CDAE1EA5, O 0E22DEC0; Lip G CABC5F5B, R 46C4F2B5,
Y 6A87F755, B 7DCDB59B, O 6AA1B221; Mid2 G 8DA374D1, R E539F99E, Y CB2DD2BE, B 06861F88,
O 86C8DB75; Down G 31966E14, R 8EDDCCC9, Y B59DF82A, B 7AEAC67E, O B966061B.
note_hit01 A38B2820, note_hit_blue_01 9E700D1C; Particle_Spark01 44E7B154; whammy_hold01 736906DE.

## 6. Implemented (engine/src/render/highway2d.cpp)

- Intro: `move_highway_2d` (released by move_highway_camera_to_default at highway_start_time,
  fastintro -2000 ms after current_starttime): the gem container's y starts at 720; each game
  frame y += v dt, v *= k, k -= 0.00044 (v from -720 px/s, k from 1.0), snapped to 0 at y <= 0
  [C script] — at 60 fps it is up after 129 frames (~+150 ms). Run on the judge's 60 Hz step.
- Strings, sidebars, SP glow + bolts, buttons (lip / mid / neck / head with the pixel raise and
  sink), hit flames as in 2.2, 2.3, 2.7, 2.8 [C]. The lightning frame order (frame =
  song time x 15 fps mod 8) and the bolt dims being one frame (128x512) are [I].
- The highway's rgba: StarPowerOn -> highway_starpower, GuitarEvent_StarPowerOff -> highway_normal
  (set instantly); highway_pulse_black (GuitarEvent_MissedNote with silent_miss when the guitar
  is already muted, `guitar_volume = 0`; start_gem_scroller starts muted) morphs to highway_pulse
  [0 0 0 255] over highway_pulse_time / 2 and back to highway_normal — so a pulse in Star Power
  leaves the highway untinted until the next StarPowerOn, as GH3's scripts do [C scripts].
- Multiplier-loss shake (guitarevent_multiplier2x/3x/4xoff -> highway_pulse_multiplier_loss,
  not in battle / boss): pushes (0,4) / (0,10) / (0,15) for losing 2x / 3x / 4x (x0.6 in face-off,
  pro face-off, p2_career, p2_coop), 0.1 s steps (0.11 s for 4x), random x +-7 / +-7 / +-5 / +-5
  / +-3, then back [C script]. (The script's guard reads highway_pulse_p1 but sets highway_pulse,
  so it never blocks [C script].)
- Lefty flip: user_options.lefty_flip_p1 / _p2 -> lefthanded_gems and lefthanded_button_ups at
  start_gem_scroller [C script]; battle's Lefty Flip toggles on top of it.
- Gem offset: gem_offset = default (the 360's -66 ms left out on PC, D7) + the song's
  `gem_offset`; gems and fretbars reach the playline gem_offset ms later [C script gem_scroller].
- Battle star gems: sys_BattleGEM_<C>01 / _Hammer_ (2x4 flip-book, 30 fps) with a child
  `<gem>_battle_icon`: texture = the phrase card's card_texture, scale 1.2, alpha 0.5 [C
  0x821218A8..0x821219D0: CreateScreenElement with pos (130,100), scale (1.2,1.2), then alpha
  0.5]; drawn centred at the gem's (130,100)/(256,256) point [I: the parent's dims].
- Hit sparks: hit_particle_params / star_hit_particle_params per hit fret for 6 game frames
  (hit_note_fx), one spark per emit_rate 0.02 s, within emit_spread 160 deg of up, life 0.25 s,
  start -> end colour and scale, additive [C data]; the speed (velocity 10 read as px per frame)
  and friction (0,50) read as a downward pull are [I] (Create2DParticleSystem's C not read).
- P2's highway: its character's sys_<name>_1_highway (player2_status.character_id; a boss: its
  character_profile), its body pak loaded first; devil_finish puts player 2's container at x 1000
  [C scripts create_guitarist, setup_highway].

## 7. Open

- Exact flip-book frame selection (start frame / loop) and Star2D spin (star_spin_rate -2.5 unused
  here?) [I].
- Whammy wibble displacement amount and SetNewWhammyValue index formula [I].
- Hit path: where the gem is destroyed and which pos is passed to NoteFX (gem pos vs pos_2d) [I].
- Blend enum confirmation (material d8 3 / 1) and fade shader math [I].
- The 2D particle system's units (Create2DParticleSystem: velocity, friction) [I].
