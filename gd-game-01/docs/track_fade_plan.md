# Track horizon fade: how the original fades notes in at the far end

[C] = read from the decompile, the ELF, or the data. [I] = inferred.

Sources: `analysis/decomp/00150000.c` (Track ctor FUN_00157b50, FUN_00158838, Track draw FUN_00159590,
TrackPanel_v11), `analysis/decomp/00190000.c` (PsRnd_v8 DrawRect, FUN_00199578, FUN_0019a4f0),
`analysis/decomp/001a0000.c` FUN_001a0b80, `extracted/track/gen/track.rnd_ps2` (decoded with
`tools/rnd.py` + `tools/rnd_classes`), `extracted_dta/config/gen/track_graphics.dta`.

---------------------------------------------------------------------------------------------------
## 1. Answer in one paragraph

The original has **no fog, no alpha ramp by track y, no per-vertex distance alpha, and no fade mesh**.
The fade is a **screen-space "background re-paint" trick**. Before it draws the highway, the Track copies a
**128 x 32 pixel strip of the frame buffer** into `track_fade.tex`. At that point the strip holds only the
venue. The strip's top-left corner is at **(fade.x - 64, fade.y) = (192, 216)** on the 512 x 448 PS2 frame.
The Track then draws the whole highway: surface, rails, lane lines, beat lines, gems and tails.
After that it draws the saved strip back at the same place as a 2D sprite with `track_fade.mat` (SrcAlpha blend).
The sprite's vertex colour is **alpha 1 on the top edge and alpha 0 on the bottom edge**.
So, over those 32 rows, the highway and everything on it blends linearly in screen y back into the venue
behind it. The far edge of the surface (track y 119.2) is fully replaced by the venue. By row 248
(track y ~ 79.5) the highway is fully visible. Last, an additive `track_add.mat` rect
(colour 0.02) goes over the same 128 x 32 box. The smashers, the hit effects and the Star Power lightning
are drawn **after** the sprite, so they never fade.

---------------------------------------------------------------------------------------------------
## 2. Evidence

### 2.1 Config and setup — FUN_00158838 (called from the Track ctor FUN_00157b50 at 0x157fd8) [C]
- `track_graphics` → Track+0x10. These are read by the ctor and **never read again in any Track function**
  (the only accesses in 0x157b50..0x15a4b8 are the stores at 00150000.c:5181/5187) [C]:
  - `alpha_dist 40` → +0x9c. No reader was found, so it is dead config here and is **not** the fade [I].
  - `horizon_y 110` → +0xa0.
- FUN_00158838 [C]:
  - +0xa4 = `track_fade.tex` (class 0x2c46a8 = Tex), +0xa8 = `track_fade.mat`, +0xac = `track_add.mat`.
  - `cfg = track_graphics.fade`. Then `cfg = cfg.normal` when single player (`GameConfig+0x8c == 0`).
    Otherwise `cfg = cfg.multi[player + 1]`, i.e. the (player)th `(x y)` pair.
  - `+0xb0 = cfg.x - track_fade.tex.width / 2`, so x is the **centre**. `+0xb4 = cfg.y`, so y is the **top edge** and is not halved.
  - Values: single (256,216) → **rect (192, 216, 128, 32)**. Multi P1 (128,236) → (64, 236, 128, 32).
    Multi P2 (384,236) → (320, 236, 128, 32).
  - The function also sets `+0x80 = 1` and `+0x64 |= 2` on `smash_flamelight.mat`, `star_base.mat` and
    `smash_flamelight_starcollect.mat`. That is a mat flag tweak and has nothing to do with the fade [I].

### 2.2 Scene data (track.rnd_ps2) [C]
| object | fields |
|---|---|
| `track_fade.tex` | type 0x22 **RenderedNoZ**, **128 x 32**, 16 bpp, no bitmap (a render target) |
| `track_fade.mat` | blend 3 **SrcAlpha**, zmode 0 **Disable**, **vert_amb 1** (use the vertex colours), colour (1,1,1,1), stage 0 = `track_fade.tex`, stage blend 2 (modulate), Clamp, identity xfm |
| `track_add.mat` | blend 2 **Add**, zmode 0 Disable, vert_amb 0 (use the mat colour), colour **(0.02, 0.02, 0.02, 1)**, no stages |
| `track.env` | `fog_enable FALSE` (start 100 / end 160 are unused). **There is no fog on the track.** |
| `track_surface.mesh` | y -9.94 .. **119.24**, x ±9.74 |
| `track_side_rails.mesh` | y -10 .. 119.19, x ±10.16 |
| `track_lane_lines.mesh` | world y 39.84 ± 49.71, so it ends at y ≈ 89.6 (inside the fade) |
| `track.cam` | fov 0.541 (horizontal, 4:3), pos (0.197, -63.15, 17.93), ~6° pitch down, near 50, far 200 |

### 2.3 The draw — FUN_00159590 (one call per Track from TrackPanel_v11 @0x15bba8) [C]
```
if (root(+0x28).showing) {
    if (exploding(+0x74)) FUN_0015cc88(surface +0x20, track_surface.mat);
    if (trackGfx(+0x2c).showing)
        FUN_001a0b80(track_fade.tex, x=+0xb0, y=+0xb4);          // 1. grab frame buffer strip
    draw(root +0x28);                                            // 2. highway (contains +0x2c)
    if (trackGfx(+0x2c).showing) {
        rect = { +0xb0, +0xb4, tex.w(128), tex.h(32) };
        Rnd.DrawRect(rect, top=(1,1,1,1), track_fade.mat, 0, bottom=(1,1,1,0));  // 3. fade sprite
        Rnd.DrawRect(rect, (1,1,1,1),    track_add.mat,  0, 0);                  // 4. +0.02 add
        draw(overlay group +0x38);                                // 5. not faded
    }
}
```
- **FUN_001a0b80** is a GS local→local copy [C]: BITBLTBUF (0x50) source = the current FRAME register
  (FBP, FBW, PSM), destination = the tex's buffer and PSM. TRXREG (0x52) = tex w x h. TRXPOS (0x51) =
  source (x, y). TRXDIR (0x53) = 2. It copies the 128 x 32 frame-buffer pixels at (x, y) into the texture, 1:1.
- **DrawRect = PsRnd vtable slot 8, `PsRnd_v8` @0x19b598** [C] (call through vptr+0x4c = 0x0c + 8·8):
  - `rect` is in frame-buffer pixels with the origin at the top-left: `X = (x + 2048 - W/2)·16`, `Y = (y + 2048 - H/2)·16`.
  - It is drawn as a 4-vertex strip:

    | vertex | position | uv | colour |
    |---|---|---|---|
    | v0 | (x0, y0) | (0, 0) | top |
    | v1 | (x0, y1) | (0, 1) | bottom |
    | v2 | (x1, y0) | (1, 0) | top |
    | v3 | (x1, y1) | (1, 1) | bottom |

  - Colour args `(c, mat, cRight, cBottom)`:
    - If the mat has vert_amb (+0x54) or there is no mat:
      - `cRight` set → horizontal gradient.
      - Else `cBottom` set → **vertical gradient**: top = `c`, bottom = `cBottom`.
      - Else all four vertices = `c`.
    - If the mat lacks vert_amb → all four vertices = mat colour (+0x30).
  - So the fade sprite is **(1,1,1,1) at the top and (1,1,1,0) at the bottom**. The add rect is the
    track_add mat colour (0.02, 0.02, 0.02, 1).
  - The UVs map the 128 x 32 texture 1:1 onto the rect, so the venue pixels go back exactly where they came from.
  - Colours go through FUN_00199578 (rgb x255 / a x128 with no stage, or the pass's per-channel scales).
    With a modulate stage the texture comes out at x1.0 [I: pass scale 128, as for other blend-2 stages].
- The GS interpolates the vertex colour across the strip (Gouraud). So **fade alpha is linear in screen
  y**: `a(row) = 1 - (row - y) / 32` for rows y..y+32. Blend SrcAlpha:
  `out = venue·a + highway·(1 - a)`.
- The fade only runs when the track-graphics view is showing. On `explode` (FUN_001598c8) that view is hidden
  and `track_explode.view` goes into the root, so the explosion is never faded [C].

### 2.4 Which objects fade (group membership, Track ctor) [C unless noted]
- Root +0x28 draw list = { +0x2c track-graphics view, and later `track_explode.view` }.
- +0x2c draw list = `track.env`, beat-line view +0x30, `ui::track_side_rails.mesh` +0x24, gem view +0x34. The
  surface object +0x20 (`track_surface.mesh` + `track_lane_lines.mesh`) is added by FUN_0015ca00.
  +0x38 is only a *transform* child of +0x2c (FUN_001da100 = add to the transform-children list) and is **not** in its draw list.
- Gem view +0x34 holds the gem/tail pool objects (FUN_001630d0(pool, +0x34)): gems, star gems,
  tails, tail glow lines, sparkles and bonus sparks [C for the pool; I for each sub-object].
- Overlay +0x38 holds:
  - The five GemSmashers and their smash particle sets: FUN_0015d208 passes +0x38 to FUN_001643b0 → FUN_001669d0 `gem_smasher.*`.
  - The Star Power lightning: FUN_00158b90 inserts the `lightning_track.view` copy into +0x38's draw list.
  - The hit effects [I: hit-fx proxies are made by the smasher set].
- **So these FADE:**
  - track surface (its far edge too)
  - side rails
  - lane lines
  - beat lines
  - gems, star gems, HOPO tops
  - sustain tails and tail glow lines
  - bonus/deploy sparks and tail_bonus particles [I]
- **These do NOT fade:**
  - smashers / now ring
  - smash / combo / burn hit effects
  - lightning_track.view (bolts and track glow)

### 2.5 Screen geometry check [I, strong]
- The frame is 512 x 448 [I]:
  - The fade x values centre the full screen (256) and the two halves (128 / 384).
  - The Rnd mode setter FUN_0019a4f0 treats a height of 448 (0x1c0) as frame mode.
  - The movie player temporarily switches width to 640 (FUN_0024f190), which means the game width is not 640.
- Projecting with track.cam (horizontal fov 0.541 at 4:3 → vertical tan = 0.75·tan(0.2705)) onto 512 x 448:

  | track y | screen row | fade alpha a | highway visible (1 - a) |
  |---|---|---|---|
  | 120 (surface far edge 119.2) | 216.4 | 0.99 | 1 % |
  | 110 (`horizon_y`, gems appear) | 222.4 | 0.80 | 20 % |
  | 100 | 229.2 | 0.59 | 41 % |
  | 90 | 236.8 | 0.35 | 65 % |
  | 80 | 245.5 | 0.08 | 92 % |
  | ≤ 79.5 | ≥ 248 | 0 | 100 % |

- The track half-width at row 248 (x = ±10.16) is ≈ 63-64 px, so the 128 px width covers exactly the highway.
- The fade top (216) sits on the surface's far edge (row 216.4).
- Multi: dual perspective scales the track view by 0.76 about the track origin, so the horizon (110 → 83.6) lands at
  row ≈ 242. Rect top 236 is again ≈ 6 px above it, the same as in single player.
- **So a gem appears at y=110 already 80 % covered by the venue, and reaches full opacity at y ≈ 80.**
  At 1.0 track speed that is 30 units ≈ 500 ms (0.06 u/ms); at 1.4 it is ≈ 357 ms.

---------------------------------------------------------------------------------------------------
## 3. Engine today
- `render/track_renderer.cpp` draws everything with hard cut-offs (`y > kHorizonY` culls; the surface simply ends).
  It has no grab, no fade sprite and no add rect. The shader fog is not used for the track, which is
  correct since `track.env` has fog off.
- `App` (app.cpp ~1310-1330) draws the venue (with `depthBlur`) in the wide viewport. Then it calls `endWide()`,
  `clearDepth()`, and `track_->draw(...)` (then `track2_` in MP) in the 16:9 viewport.
- `Renderer` already does a frame-buffer grab for the DOF (`depthBlur`: `glCopyTexSubImage2D` of the
  viewport) and has 2D drawing in virtual 1280 x 720 (`draw(v, n, tex, blend)` with `mvp = nullptr`, y down).

---------------------------------------------------------------------------------------------------
## 4. Implementation steps

### 4.1 Constants (from data, not hard-coded where possible)
- Read `track_graphics.fade.normal (x 256)(y 216)` and `fade.multi` pairs `(128,236)`, `(384,236)`.
- Size = `track_fade.tex` width/height (128, 32) [C].
- `track_add.mat` colour 0.02 → add `round(0.02·255) = 5` / 255 to rgb [I: could be 2/255 if the pass
  scale is 128; the effect is ~1-2 %].

### 4.2 Map the PS2 rect to engine virtual 1280 x 720 (single player)
- The track camera keeps the PS2 vertical framing (fovY derived from the 4:3 horizontal fov, SceneRenderer::camera).
  So a PS2 pixel maps to the 4:3 region centred in the 16:9 virtual screen:
  - The 4:3 region of a 720-line screen is 960 wide, and the 512 PS2 pixels span that width, so
    **x scale = 960/512 = 1.875, ox = 160; y scale = 720/448 = 1.607143, oy = 0**.
  - `vx = 160 + px·1.875`, `vy = py·1.607143`.
  - Single player: **(192..320, 216..248) → vx 520..760, vy 347.14..398.57 (240 x 51.43)**.
  - In NDC (useful with `screen_`): `ndcX = (px - 256)/256 · (4/3)/(16/9)`, `ndcY = (224 - py)/224`.
    That gives x ±0.1875, y +0.035714 (top) .. -0.107143 (bottom).
- Face-off as the engine does it now (`setFaceOff`: `screen_` = scale 0.76 about the NDC centre, ±0.5 x):
  - Transform the **single-player** NDC rect corners by `screen_`, so the rect stays tied to our horizon.
  - The dta `multi` (128/384, 236) values only match the original dual framing: 0.76 model scale about the
    track origin, plus a half viewport with fov·0.5 (docs/multiplayer_plan.md §6).
  - When that framing is implemented, convert `multi` directly. Each half-screen is 256 PS2 px wide =
    480 virtual (4:3 region width/2): `vx = 160 + px·1.875` still holds, `vy = py·1.607`. [I]

### 4.3 Renderer additions (`render/renderer.{h,cpp}`)
1. `int grabRect(float vx, float vy, float vw, float vh)`:
   - Convert the virtual rect to window pixels using `viewport_` (x0 = vp.x + vx/1280·vp.w, GL y from the bottom
     = vp.y + (720 - vy - vh)/720·vp.h). Round outward to integers and store the integer rect.
   - Then `glCopyTexSubImage2D` from the current draw buffer into a persistent RGBA8 texture.
     Use the same pattern as `depthBlur`'s `blurTex_` with GL_NEAREST and CLAMP_TO_EDGE, and re-allocate when the size changes.
2. `void drawGrab(int handle, uint8_t aTop, uint8_t aBottom)`:
   - Draw a quad exactly over the stored integer pixel rect (convert back to virtual units) with the grab texture.
   - **UV v = 1 on the top edge and v = 0 on the bottom** (GL copies are bottom-up).
   - Vertex rgb 255. Vertex alpha: top 255, bottom 0.
   - `Blend::Alpha` (SRC_ALPHA, ONE_MINUS_SRC_ALPHA). No depth test or write, no cull, no fog, colorScale 1.
   - Alpha must be **linear in screen y**. Two triangles with top/bottom vertex alphas give exactly that.
3. `void addRect(..., rgb = 5/255)`: the same pixel rect, untextured, `Blend::Add` (ONE, ONE), no depth.
   - Or use `fillRect` with `Blend::Add` if you add a blend parameter.

### 4.4 TrackRenderer::draw order (`render/track_renderer.cpp`)
```
if (exploding) { ...explode...; return; }                  // no fade (trackGfx hidden) [C]
fadeRect = rect for this track (4.2)
grab = renderer.grabRect(fadeRect)                          // BEFORE any track draw
// --- +0x2c group (faded) ---
surface, side rails, lane lines, beat lines,
tails + glow lines, gems / star gems, gem_bonus sparks, tail_bonus.part
renderer.drawGrab(grab, 255, 0)                             // fade sprite
renderer.addRect(fadeRect, 5/255)                           // track_add
// --- +0x38 group (not faded) ---
smashers (shadow, gem_smasher, rim), hit effects (smash/burn/combo), lightning_track.view
```
- Move the smasher block, which today sits before the notes, to after the sprite. The hit-fx and lightning blocks are already last.
- Keep the culling as it is: `y > kHorizonY` and remove_y, which the original also uses (FUN_0015a4b8 / gem manager).
  The fade hides the cut.
- MP: each TrackRenderer grabs its own rect at the start of its own draw. P2 grabs after P1 has drawn,
  as the original does (TrackPanel_v11 loops the tracks in order). The rects do not overlap the other track.
- The grab must happen after the venue's DOF composite (`depthBlur`), which is already true in App.
  - Note [I]: on the PS2 the DOF composite runs at end of frame (FUN_0019afa8) with ZTST GEQUAL. The track
    pixels are near (cam z_end 0.1), so the original's grabbed strip was the *unblurred* venue and stays
    sharp after the composite. The difference is a few px of venue sharpness inside a 128 x 32 box.
  - Keeping our order (blurred grab) matches the surrounding pixels and is the recommended choice.
- Ultrawide: the venue uses `beginWide`, but the track and the grab are in the 16:9 viewport. The grabbed rect
  is inside the centre 4:3 region, so this needs no change.

### 4.5 Shader
- No new uniforms are needed if the vertex-alpha quad path is used.
- For a single shader pass instead, the equivalent is:
  - `uniform vec4 uFadeRect` (window px x0, y0top, w, h), `uniform sampler2D uGrab`.
  - `a = clamp(1 - (yTop - gl_FragCoord.y)/h, 0, 1)`.
  - `out = mix(highway, grab, a)`, applied only inside the rect, then +5/255.
- The quad path is closer to the original and simpler.

### 4.6 Verification
- `--selftest` screenshot at a fixed song time:
  - Gems at y = 110 should show at ~20 % over the venue, reaching full opacity around y ≈ 80 (rows 222 → 248 on the 448 frame,
    i.e. virtual y ≈ 357 → 399).
  - The surface's far edge should dissolve into the venue with no hard line.
  - The smashers and lightning should be unaffected.
- With a bright venue, check there is no visible 240 x 51 box edge: the left/right edges sit just outside the track
  (±64 px vs ±63 px half-width at the bottom row). The +5/255 add is the only change outside the highway.
