# Guitar Hero: Aerosmith (Xbox 360): disc and file formats

Aerosmith is built on Guitar Hero III's engine (the XEX's debug paths name the
build `GH3_xp1`, "GH3 expansion pack 1"). This file lists only what differs
from GH3; for everything else read `gd-game-04/docs/formats.md`, which applies
unchanged. **[C]** = confirmed over the whole disc (or at an XEX address),
**[I]** = inferred. Checked with gd-game-04's own decoders (`gd-game-04/tools/*.py`)
run over our rip, 2026-11-17 (GD06-001).

## The disc

Same XDVDFS layout as GH3: game partition at 0xFD90000 (XGD2) [C], 806 files
(GH3: 660), 4.67 GB. Same folders and file kinds:

| Path | Aerosmith | GH3 |
| --- | --- | --- |
| `DATA/COMPRESSED/PAK/*.pak.xen` (+ `.pab.xen`) | 552 files, 1.78 GB | 300 files, 0.94 GB |
| `DATA/COMPRESSED/SONGS/<song>_song.pak.xen` | 60 | 90 |
| `DATA/COMPRESSED/ZONES/*.pak.xen` | 13 (6 venues + credits, soundcheck, viewer, test, global, global_sfx) | 15 |
| `DATA/COMPRESSED/IMAGES`, `FONTS`, `FXFILES` | 14, 2, 1 (adds the `boot_legal_eu*` loading screens) | 9, 2, 1 |
| `DATA/MUSIC/<song>.fsb.xen` + `.dat.xen` | 60 banks | 91 banks |
| `DATA/STREAMS/streamall.fsb.xen` + `.dat.xen` | 87 MB | 72 MB |
| `DATA/MOVIES/BIK/*.bik.xen` | 32 (Bink 1, `BIKi`) | 49 |
| `DATA/ANIMS/standardkey{Q,T}.bin.xen` | byte-identical to GH3's [C] | |
| `DATA/FXFILES/MaterialLibrary.bin.xen` | different content, same format [C: 4,804 shaders, 0 problems] | |

Localisation: the same `qb`, `qb_f`, `qb_g`, `qb_i`, `qb_s` paks as GH3.

## Compressed files and the TOC

Unchanged: each file under `DATA/COMPRESSED/` is one raw deflate stream;
`compress.toc.xen` is `TOC1` with 16-byte entries (0x282 = 642 here) [C: every
pak inflates and parses].

## QB keys and names

Unchanged (CRC-32 without the final XOR). `dbg.pak` gives 41,234 names
(GH3: 41,012) with `qbkey.py --table`.

## PAK

Unchanged [C: 620 paks, 39,372 entries, 0 problems]. The same 16 entry types,
no new ones: `.ska` 33,213, `.qb` 2,525, `.dbg` 1,267, `.img` 768, `.tex` 603,
`.skin` 432, `.wav` 243, `.scn` 159, `.ske` 36, `.rag` 29, `.hkc` 20, `.nqb`
20, `.pfx` 20, `.fnt` 16, `.mdl` 11, `.table` 10.

## QB and QScript

Unchanged [C: 2,545 QB files, 46,883 sections, 17,912 scripts, every byte
accounted for; every script tokenizes with GH3's opcode table, no new opcode].
qb.pak: 472 QB files (GH3: 448), 372 of them in both games; 3,417 scripts
(GH3: 3,192), 3,141 of them with the same names.

## Charts (`songs\<song>.mid.qb`)

Same sections as GH3 plus one more part: `<song>_song_aux_{Easy..Expert}`,
`<song>_aux_<diff>_Star` and `<song>_aux_<diff>_StarBattleMode` [C: one song
compared section by section; to check on every song in M1]. What the
aux part is used for is open (OPEN_QUESTIONS).

## Textures, scenes, Havok, fonts, sounds

Unchanged: `img.py check --layout-only` 1,385 files, 8,202 textures (DXT1/3/5,
DXN, 8_8_8_8, cubes, one volume), 0 problems; `scn.py check` 159 `.scn`, 11
`.mdl`, 432 `.skin`, 20 `.hkc`, 0 problems; `havok.py check` 981 rigid
bodies, 949 constraints, 0 problems; `.fnt`, `.wav`, `.table`, `.nqb`, `.pfx`,
`.rag` parse with GH3's layouts.

## Animations (`.ska`): the one decoder gap

`anim.py check` (GH3's) rejects 30,333 of Aerosmith's 33,213 `.ska` (GH3: 0):
compressed rotation keys decode to quaternion parts up to 2.0 long (GH3's never
exceed 1.0). The header, flags and section layout are the same (the flag
values seen are GH3's, plus a few new combinations: 0x06834000, 0x06835100,
0x16011040, ...). The XEX's rotation-key decoder is unchanged
(Aerosmith 0x82426528 and GH3 0x824213B8 have the same 1,160-byte body [C]),
so the data uses a path of the decoder or its caller that GH3's anims never
took and our parser does not model yet [I]. To be read in M1 (GD06-002).

## Audio

- Song banks `DATA/MUSIC/<song>.fsb.xen`: the same container as GH3 (FSB3,
  encrypted; the first bytes match GH3's encrypted banks [I]) with a `.dat.xen`
  index next to each. Song-audio decryption is an owner item and is not part
  of this port's code (OPEN_QUESTIONS).
- `DATA/STREAMS/streamall.fsb.xen`: plain FSB3 (`FSB3` magic) [C], the menu
  and effect sounds; GH3's `fsb.py list` layout.
- In-pak `.wav` (XMA) sound effects: GH3's layout [C].

## Movies

Bink 1 as GH3. New: 14 `AO_long_N` / `AO_short_N` movies (the career's
between-tier videos, `initial_movie` / `completion_movie` in
`GH3_Career_Songs`) with subtitles (`scripts\game\misc\movie_subtitle.qb`,
`scripts\game\movie_text.qb`); the eight `z_*_encore_audio.bik` files keep
GH3's venue names.

## The executable

| | Aerosmith | GH3 |
| --- | --- | --- |
| Format | XEX2 | XEX2 |
| Title ID | 41560819 | 415607F7 |
| Media ID / version | 37A41C2D / 1 | 43048167 / 4 |
| Base / entry | 0x82000000 / 0x8213A0B0 | 0x82000000 / 0x82136AC8 |
| Functions (after FixXexFunctions) | 33,178 | 32,998 |
| Strings / data refs | 8,282 / 85,940 | 8,705 / 85,859 |
| QScript C functions (names) | 1,334 | 1,325 |

Similarity [C, from the two `analysis/` exports]:

- Every one of GH3's 1,325 C-function names is in Aerosmith; 9 are new:
  `ClearXUserSigninInfo`, `RefreshXUserSigninInfo`, `DeleteAllSongDataFromFile`,
  `WriteSongDataToFile`, `DeleteAllTeslaEffectObjects`, `IsStarEntry`,
  `IsUserMusicPlaying`, `ResetScoreUpdateReady`, `SetLightMapOverlayParams`.
- Of the 1,010 C functions whose bodies Ghidra resolved in both, 972 (96%)
  have the same size and 19 more are within 5%.
- Over the whole program, 32,194 of 33,178 functions (97%) pair with a GH3
  function of the same size and the same callee count.
- 8,120 of Aerosmith's 8,238 distinct strings are also in GH3.

So the native code is GH3's with small changes; addresses moved (most
functions shift by a few KiB), so every GH3 **[C] address** must be re-found
in Aerosmith's export (by size / callees / strings) before it is cited.
