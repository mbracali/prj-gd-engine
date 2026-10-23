# Song list (`sel_song` / `ss_song.lst`) — how the original draws it

Scope: the `List` component (RndScrollList) `ss_song.lst` in `ghui/sel_song.gh`, fed by `song_provider`
(SongProvider), plus the `sel_song.view` frame link. [C] = confirmed in code or data, [I] = inferred.

## 1. Objects and data

`ghui/sel_song.gh` (world positions as stored; rotations are identity):

| object | parent | local pos | notes |
|---|---|---|---|
| `sel_song.view` | – | 0 | anims: `ss_setlist.view`, `ss_songlist.tnm` |
| `ss_setlist.view` | sel_song.view | driven by `ss_songlist.tnm` | draws `ss_paper.view`, `setlist.lbl`, `ss_songlist.view` |
| `ss_songlist.tnm` | – | – | TransAnim on `ss_setlist.view`: trans key (0,0,0) @ frame 0 → (0,0,4000) @ frame 100, i.e. **z = 40·frame** (linear) |
| `ss_songlist.view` | ss_setlist.view | (0,0,0) | draws only `ss_song.lst` |
| `setlist.lbl` | ss_setlist.view | (0,0,40) | LabelEx "Setlist" |
| `ss_song.lst` | ss_songlist.view | (34,0,40) | style `song`, **count 60, spacing 40, mode 2** |

The stored world z of ss_setlist.view (2409.6) is editor state. At runtime the tnm sets its local translation. [I: TransAnim replaces local pos]

Style `list/song` (config.dta `components`): `element_view "song_element.view"`, `back_arrow_mesh song_back.mesh`, `next_arrow_mesh song_next.mesh`, `max_step 1.0`.
Colours are resolved once at load/copy by `FUN_00124ce8` (via `FUN_0011dfb8(…, colorName, stateKey)`), and **never re-resolved on focus change**. [C]
- `text_color` with state `normal` → class default (.1,.1,.1)
- `highlight_text_color` with state **`focused`** → (0,0,1)
- `header_text_color` with state `focused` → (.66,0,0)

`highlight_mesh_color` and `song_highlight.mesh` (an empty mesh in `song_list.view`) are **not used** by the list code. [C]

### Element template `resources.rnd_ps2 :: song_element.view`

The element view is cloned `count` times (`FUN_001250c0` → `FUN_00130ef0`).
`FUN_00131138` walks the view's **draws** list. It collects every RndText in order as the columns 0..4 and shows them. The first object whose name contains `"check"` (0x305a30) becomes the checkmark, which is hidden by default. [C]
The draws order is name, score, stars, header, blurb, checkmark, so:

| col | template | local pos (x,0,z) in element | scale (x,z) | font / size | align |
|---|---|---|---|---|---|
| 0 name | `song_name.txt` | (-260, 0, 0) | 1.4 | hand.font 18 | 33 MiddleLeft |
| 1 score | `song_score.txt` | (137, 0, 0) | 2.0 | hand.font 15 | 33 MiddleLeft |
| 2 stars | `song_stars.txt` | (132, 0, 0) | 2.0 | hand.font 15 | 36 MiddleRight (ends where the score starts) |
| 3 header | `song_header.txt` | (-290, 0, **+40**) | 1.5 | song_header.font 20 | 33 MiddleLeft |
| 4 blurb/footer | `song_blurb.txt` | (-235, 0, **-35**) | 1.4 | song_blurb.font 15 | 33 MiddleLeft |
| check | `song_checkmark.mesh` | (-270, 0, 0) | (1.2, 1.4) | 32×32 quad in xz, `song_checkmark.mat` | – |

All texts have colour (1,1,1,1) and empty default text. hand.font contains `*`, so the stars are plain asterisks.

## 2. Provider columns (SongProvider, vtable 0x2f71c8) [C]

The data rows are **songs only**. Headers and the footer are extra columns of a song row, not rows of their own.

- `v1` @0x1189c8 (refresh, called by `set_provider`) does two things:
  - It fills the header table. The table has one (row, venue) pair per campaign venue plus `store`, built in the ctor `FUN_00118400`. Each pair's row is the **first data row** whose song belongs to that venue (store songs count as `store`).
  - It sets the footer row `+0x28` = (first store-song row − 1), or the last row if there is no store song.
- `v4` @0x119020: NumData = the number of songs in the list (the filters are in docs/career.md 2.3).
- `v3` @0x118bb8, Text(row, col), jump table @0x301f10:
  - 0 `SongProvider_v2`: songs.dta `name`.
  - 1 `FUN_00118c58`: career → `"%i"` of the item's best score (item+0x18). This is **unconditional**, so an unplayed song shows **"0"**. Quickplay → "".
  - 2 `FUN_00118cb8`: career → `"***"`, `"****"` or `"*****"` for stars (item+0x1c) of 3, 4 or 5, else "". Quickplay → "".
  - 3 `FUN_00118d48`: if the row is a header row, `localize("song_header_<venue>")`, else "".
  - 4 `FUN_00118dd8`: only on the footer row (+0x28), else "".
    - Career:
      - status ≥ N → "".
      - Current venue index < status → `song_old_venue`.
      - Current venue = N−1 → `song_last_venue`.
      - Otherwise `song_current_venue` with `%i` = required(curVenue).
    - Quickplay: `song_quickplay`, **unless** some difficulty 1..3 (Medium..Expert) has status ≥ numVenues−1, in which case "".
- `v5` @0x118f90 (checkmark shown): career && item beaten (item+0x14) && not a store song.
- `v6(col)` @0x119000: `col ∉ {3,4}`, i.e. the column takes the highlight colour.
- `v7(col)` @0x119010: `col == 3`, i.e. the column takes the header colour.
- `v8(row)` @0x118f50: the row has a header.

## 3. Layout (list space) [C]

The element positions are computed only in `FUN_00125518`, which runs from `FUN_00124e88` (Load/Copy and `set_provider`). `refresh` only re-sets the texts (`FUN_00125760`). `FUN_00125680(d, obj)` sets obj local pos = (x, y, **−d**) and keeps x, y (0 for elements).

```
back arrow : z = 0
element k  : z = -spacing * (1 + k + 2*Hk)      k = 0..count-1
             Hk = #rows r in {first..first+k} with v8(r)   (DAT_002edbb0 = 2 extra slots per header)
next arrow : z = -spacing * (count + 1)
```

Element k shows data row `first + k` (wrapped modulo NumData, `FUN_00126008`). In sel_song `first` is always 0 (see §4), so element k = row k.

A row with a header is pushed down 2 slots. Its header text sits at +40 (one slot above the name), so a tier change looks like this:

```
slot s    last song of previous tier (blurb, if any, at s + 0.875)
slot s+1  (empty)
slot s+2  header text  (element s+3's col 3)
slot s+3  first song of the tier
```

The first row always has a header, so it sits at slot 3 (z −120), with its header at −80. `setlist.lbl` ("Setlist") is at the same height as the list origin (both z = 40 in ss_setlist.view space), i.e. list z 0.

Text for elements k ≥ NumData is never set, so they stay empty and are effectively invisible. All `count` (60) elements are drawn every frame; there is no culling window. The paper and camera framing do the clipping.

Drawing (`RndScrollList_v5` @0x1240c0, DrawShowing) [C]:
- It draws the back arrow if `mode==0 || first>0`, and the next arrow if `mode==0 || first < max(0, N−count)`. In sel_song (N ≤ 60) **neither arrow is drawn**.
- Then for each element it calls `FUN_001314c0(elem, provider, hl, text, highlight, header)`:
  - `hl` = (k == selected display pos `[3]`). In mode 0 it is also false while scrolling.
  - For each text column c, the colour is: if `hl && v6(c)` → highlight (0,0,1); else if `v7(c)` → header (.66,0,0); else text (.1,.1,.1).
  - So for the selected row, name, score and stars are blue; the header is always red; the blurb is always .1 grey.
  - The colour is written into the **font's material** colour (`text->font(+0x114)->mat(+0x414)+0x30`), then the element view is drawn.
  - The checkmark is drawn if v5 (set in `FUN_001313c8` via SetShowing).

## 4. Selection, scroll and the `sel_song.view` frame [C]

Scroll state is at list+0x11c: [1] first, [2] target first, [3] selected display pos, [4] progress (float), [5] dir (initially 1), [6] scrolling.

- **Mode 2 input** (`FUN_001263a0`):
  - `[3] += ±1`, clamped to [0, min(count, N)−1]. There is no wrap.
  - Only an in-range move sets `scrolling=1`. Input is ignored while scrolling (`FUN_00123150`).
- **`set_selected_pos i`** (`FUN_00124a30` → `FUN_00126118`): i is wrapped mod N.
  - If i < max(0, N−count): first = i, [3] = 0.
  - Otherwise: first = N−count (clamped to ≥ 0), [3] = i − first.
  - With N ≤ 60: first = 0 and [3] = i.
- **Poll** (`FUN_00126538` via `FUN_00125b38`), per UI poll while scrolling:
  - If progress == 0, send SCROLL_START_MSG (`FUN_00123320`).
  - Then `progress += clamp(dist, ±max_step)`, where `dist = FUN_00126440(first−dir, target)` = 1 in mode 2.
  - With max_step 1.0 the progress reaches 1 **in the same poll**. Then progress = 0, scrolling = 0, and `FUN_00125ec8(list,0,1)` sends SCROLL_MSG `$done`. (Verified in the disassembly at 0x12665c–0x1266c4.)
  - So the move is effectively **instant**. `scroll_percent` (`FUN_001260c8` = min(1, progress·dir)) never shows an intermediate value between frames.
- **`spaced_selected_pos`** (`FUN_00124998`) = `sel + 2 * #{r in 0..sel : v8(r)}`. The headers counted include the selected row's own.
- The script `get_scroll_frame` returns `ss_song.lst spaced_selected_pos`. `sel_song.view set_frame` is called with it on enter and on SCROLL_MSG done. The poll lerps it with scroll_percent while scrolling, which in practice is a no-op.
- The frame drives `ss_songlist.tnm`, so ss_setlist.view z = 40·frame.

Resulting world z of element k (sel_song.view space):

```
z(k) = 40*frame + 40 - 40*(1 + k + 2*Hk) = 40*(frame - k - 2*Hk)
```

With frame = spaced_selected_pos(sel) and k = sel, this gives **z = 0**: the selected song's name line is always at sel_song world z 0. The x of the name is 34 − 260 = −226. Rows above and below sit at ±40 per slot (±120 across a header).

## 5. Differences in our `UiSystem::ComponentObj::drawList` / `ProviderObj`

1. **Header rows**
   - Ours: a header is a separate row taking **1 slot**, drawn at the row's z with only the template x, without the template's scale (1.5) or z offset.
   - Original: the header is col 3 of the song's own element, at +40 above the name, and the song is pushed **2** extra slots. Use the §3 formula.
2. **Slot origin**
   - Ours: row i at −40·(i+2), with i counting header rows.
   - Original: −40·(1 + k + 2Hk), with k counting songs only.
   - Our `spaced_selected_pos` = rowOf(sel), i.e. sel + #headers. It must be **sel + 2·#headers(0..sel)** so the frame and tnm put the selected row at z 0.
3. **Visibility**
   - Ours: draws selRow±8.
   - Original: draws all elements (≤60, i.e. every song) and lets the paper and camera clip.
4. **Footer (blurb)**
   - Ours: draws it one extra slot down (−40) and then applies the template.
   - Original: only the template offset (−35 below the row's name), on row +0x28.
   - Ours never emits the **quickplay** footer `song_quickplay` (shown unless Medium/Hard/Expert status ≥ N−1).
   - In quickplay, the footer row is the row before the first bonus song, or the last row.
5. **Score**: the original shows `"%i"` for every career row, including **"0"** for unplayed songs. Ours only shows it when > 0. [C per code; looks odd but matches FUN_00118c58]
6. **Colours**
   - The highlight is always (0,0,1). It is resolved with state `focused` once, not from the current focus state.
   - The highlight applies to **name, score and stars** (ours only to the name).
   - The header (.66,0,0) and blurb (.1) never highlight.
7. **Checkmark**: `song_checkmark.mesh` at (−270,0,0) is drawn for career && beaten && non-store rows. We don't draw it (`Row.beaten` is computed but unused).
8. **Arrows**: never drawn in sel_song. Ours draws none, which is correct.
9. **Scrolling**: instant in the original too. Our `scrolling` = 0 and `scroll_percent` = 1 are equivalent. The original does send SCROLL_START_MSG (the script does `game set_song_index`) and SCROLL_MSG `$done` on each move, which the script relies on.
10. **Bounds**: mode 2 clamps to [0, min(60, N)−1] without wrap. A list longer than 60 songs could not be scrolled past 60 (GH1 has ≤ 47).
