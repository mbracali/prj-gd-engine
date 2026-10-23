# RndFont / RndText — glyph rects, advances, layout, mesh

## 1. RndFont (object layout and glyph table)

Fields: +0x10..+0x40f is `glyph[256]`, 4 bytes each, indexed by the **unsigned** char byte:
`{u16 x_px (left column), u8 width_px, u8 row}`. +0x410 is the kerning hash (32 buckets, key `(s8)first + (s8)second*256`, bucket `(first^second)&0x1f`).
+0x414 is the Mat, +0x41c cellW px, +0x420 cellH px, +0x424 deprecated_size, +0x428 space, +0x42c chars (String; data ptr at +0x440).
+0x444 holds `1/texW`, +0x448 holds `cellH/texH`, +0x44c holds `1/cellW`.

Load `RndFont_v8 @0x1b7600` (v4+ reads cell_size directly; only for v<4: space /= deprecated_size, and the cell is derived from the texture size).
It then calls **`FUN_001b7040` (the glyph-table builder)**. Copy `RndFont_v7 @0x1b7be0` does the same.

### Glyph-table builder `FUN_001b7040`
1. Clear `glyph[]`. If chars[0]==0xA0, it is replaced by ' '.
2. tex = Mat stage[0] tex (`mat+0x10` vector of 0x60-byte stages, tex at stage+0x54). The function locks the bitmap (vslot +0x58/0x5c), giving W,H.
3. It sets the +0x444/+0x448/+0x44c values above. Then a grid walk in **row-major order, starting at the top-left corner, with no padding**:
```
x=y=0
for i,c in enumerate(chars):
    if W < x+cellW: x=0; y+=cellH          # cells/row = floor(W/cellW)
    if H < y+cellH: truncate chars at i; break
    glyph[c] = measure(x,y); x += cellW
```
   serif/hand/impactor (25x28 on a 512x256 sheet) → 20 per row, 9 rows. impactor2/cutout (42x28) → 12 per row (pixels 504..511 unused). rokk (28x51 on 256x256) → 9 per row.
4. `measure` = `FUN_001b6e20` + `FUN_001b6908` (column scan, `FUN_001b0a90` = pixel getter):
   - L = the first column in [x, x+cellW) that has any pixel with alpha≠0 in rows [y, y+cellH).
   - R = the same scan run from x+cellW-1 down to x.
   - `w = R-L+1`, `glyph = {x=L, width=w, row=int(y/cellH)}`.
   - Empty cell (w<1, e.g. ' '): `{x=int(cellX), width=int(cellW*0.25), row}`.
   - So the **widths are tight alpha bounding boxes measured at load time**. There is no stored width table.
5. Tab: `glyph['\t'] = glyph[' ']`, with width*3 (u8 multiply, so it can wrap).
6. Chars not in `chars` have a zeroed entry: width 0, so the advance is only spacing and the quad is degenerate.

### Glyph UV rectangle (used by the mesh builder `FUN_001d8fc0`)
```
u0 = x_px/texW          u1 = (x_px+width_px)/texW
v0 = row*cellH/texH     v1 = v0 + cellH/texH          (full cell height, no half-texel offset)
```
V is measured from the first stored row of the bitmap, which is the top of the PNG that tools/tex.py writes. Verified (see §3).

### Advance (cell units), `FUN_001b6fd0(font, prev, c)`
```
pair(prev,c) = FUN_001b6f48 = 0 if prev==0 or c==0, else space + kern[(prev,c)] (0 if absent)
advance      = pair(prev,c) + width_px[c] / cellW
```
So `space` (+0x428) is **tracking between every adjacent glyph pair**, in units of cell widths. It is not added before the first glyph of a line (prev is reset to 0 at each line start).
Kerning amounts are also in cell widths. `deprecated_size` is unused at runtime for v>=4.
Measure-string `FUN_001d8d98` sums `advance*size` (optionally adding the trailing pair term).

## 2. RndText layout and mesh

Text fields: +0x100 lines vector (0x40-byte records), +0x110 laid-out glyph count, +0x114 font, +0x11c wrap_width, +0x120 align, +0x124 caps_mode, +0x128 leading.
More fields: +0x12c/+0x140 text String, +0x144 fixed_length, +0x150 size, +0x154 italics, +0x158/+0x15c unknown (copied into the style only), +0x160 color RGBA, +0x170 markup, +0x174 owner of the mesh (+0x128).
`FUN_001d7b30` (called on every setter) does layout `FUN_001d85e0`, then `FUN_002eac70` (mesh sync).

**Units**: `size` = world width of one full font cell. 1 texel = `size/cellW` world units on both axes, so the aspect ratio is preserved.
Glyph quad: width `size*width_px/cellW`, height (line height) `lineH = size*cellH/cellW`. Example: serif at size 30 gives lineH 33.6.

**Style state** (8 floats, saved per line): {size, italics, +0x158, +0x15c, r,g,b,a}. Markup (only if +0x170) is parsed by `FUN_001d83e8`:
- `<sup>`: size = text.size*0.7 (no baseline shift; the glyph stays top-aligned). `</sup>` sets size back to text.size.
- `<it>`: italics = 0.1. `</it>` sets italics = 0.
- `<color R G B A>`: 0..255, divided by 255. `</color>` restores +0x160.
- Unknown tags are skipped up to '>'.

**caps_mode** (`FUN_001d8c48`, SetText): 2 = upper-case a–z, 1 = lower-case A–Z, 0 = unchanged. Only ASCII is affected.
fixed_length≠0: the text is truncated into a buffer of that length, and the mesh is preallocated with that many quads.

### Line breaking (`FUN_001d85e0`)
```
wrap = wrap_width>0 ? wrap_width : 1e30;  per line: w=0, prev=0, lastBreak=null
for each char c (tags consume no width):
  '\n' -> end line (newline consumed, not drawn); '\0' -> end
  ' ' or '\t': remember lastBreak = this char, widthAtBreak = w (before its advance), countAtBreak
  a = advance(prev,c)*style.size
  if w==0 or w+a <= wrap: w+=a; count++; prev=c
  else if lastBreak: line ends at lastBreak (space dropped), w=widthAtBreak, resume after it
  else: line ends before c (mid-word break; c starts the next line)
line record: style@start, start ptr, end ptr, y(+0x28), x(+0x2c), width w(+0x30)
```
If the font is null, the whole string becomes one line.

### Alignment (+0x120)
High nibble: 0x10 top, 0x20 middle, 0x40 bottom. Low nibble: 1 left, 2 centre, 4 right.
Here `L = size*cellH/cellW` (this uses text.size, not the markup size), `n` = number of lines, `lead` = leading.
```
top:    y0 = 0
middle: y0 = lead*(n-1)*L*0.5 + L*0.5
bottom: y0 = lead*(n-1)*L + L
line i: y = y0 - i*lead*L ;  x = 0 (left) | -width*0.5 (centre) | -width (right)
```
y is the **top** edge of the line. Text grows toward −Z, and the origin is the box top/middle/bottom × left/centre/right.

### Quad generation (`FUN_001d8fc0`; indices `FUN_001d8ec0` = (0,1,2),(0,2,3) per quad; mat = font mat via `FUN_001c45e8`)
Each glyph of each line (spaces included) gets one quad. Position is (X, 0, Z): the text lies in the **XZ plane** with Y=0.
```
cx = line.x; prev=0
x0 = cx + pair(prev,c)*s ; gw = width_px[c]/cellW*s ; cx = x0+gw
sl = italics*s ; lh = s*cellH/cellW ; zt = line.y ; zb = zt - lh   (s = current style size)
v0:(x0+sl,    0, zt) uv(u0,v0)     v1:(x0-sl,    0, zb) uv(u0,v1)
v2:(x0-sl+gw, 0, zb) uv(u1,v1)     v3:(x0+sl+gw, 0, zt) uv(u1,v0)
```
Every vertex gets the style colour (in-memory vertex: pos@0, colour@+0x20, uv@+0x30). The colour is multiplied by the Mat colour and texture (Mat blend SrcAlpha).
Leftover preallocated vertices are zeroed.
Helpers: `FUN_001d9410` = max line width, `FUN_001d93d0` = vertical extent, `FUN_001d92e8` = caret position of char index.

## 3. Verification against real data
- `extracted/ghui/gen/resources.rnd_ps2` was decoded. serif.font → serif.mat → stage0 `serif.tex`, which is inline (512x256 4bpp). The same applies to impactor2/rokk/stars.
- Script `(scratch) fontcheck.py` re-implements `FUN_001b7040` on the tools/tex.py RGBA alpha channel. It draws each glyph rectangle `[x, row*cellH, x+w-1, row*cellH+cellH-1]` on the sheet.
- In the overlays (`(scratch) fonts/serif.font_rects.png`, `(scratch) fonts/impactor2.font_rects.png`), every box tightly encloses the intended glyph in chars order:
  - serif row 0 is ` 0123456789ABCDEFGHI`, row 1 is `JKL…`.
  - The 131 serif chars end at 'à'. The sheet holds more glyphs (up to 'ÿ') that serif doesn't use; impactor uses 162.
- Sample values:
  - serif: ' '→(0,w6,row0), 'A'→(276,15,0), 'W'→(326,21,1), 'i'→(126,7,2).
  - impactor2: 'A'→(463,31,0), 'W'→(379,40,2).
  - No glyph crosses a cell boundary, and the V orientation (row 0 = top of the PNG) matches.

## Uncertainties (inference)
- +0x158/+0x15c in the style vector are copied but not used by layout or the mesh.
- The vslot called when chars[0]==0xA0 was not identified (it is probably a dirty/notify call). Not needed.
- LabelEx (fit, jitter, etc.) may override size/position on top of RndText. That was not examined here.
- The pixel-alpha test is `alpha!=0` from `FUN_001b0a90`, so faint antialiased texels count toward the width. The overlay agrees with this.
