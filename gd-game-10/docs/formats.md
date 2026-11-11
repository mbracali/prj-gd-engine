# Guitar Hero: Metallica (Xbox 360): file formats

The base is GH3's (gd-game-04 `docs/formats.md`): the same QB key hash,
`.pak.xen` entry table, QB sections and compiled QScript. This file lists
what changed in gh4 (Metallica), decoded byte-exact by `tools/` over the
whole disc (L21). [C] = confirmed on the disc data over every file (or at an
XEX address); [I] = inferred.

## Files under DATA/COMPRESSED

GH3 deflated every such file as one raw-deflate stream. Metallica stores each
one either **as is** (206 paks, 3 images, the TOC files) or as a chain of
**CHNK** blocks (807 paks, 3 pabs, 10 images, `MaterialLibrary.bin.xen`)
[C: every file on the disc]. No file is a whole raw-deflate stream any more.

### CHNK

Blocks follow each other; each is a 32-byte big-endian header, padding up to
the header size, then a raw-deflate stream (RFC 1951, no zlib header):

| Offset | Type | Field |
|---|---|---|
| 0 | char[4] | `CHNK` |
| 4 | u32 | header size (0x80: the stream starts here) |
| 8 | u32 | deflated size |
| 12 | u32 | block size (header + stream, padded; `0xffffffff` on the last block) |
| 16 | u32 | the next block's size (0 on the last block) |
| 20 | u32 | inflated size (0x80000 except on the last block) |
| 24 | u32 | inflated offset (where these bytes go in the file) |
| 28 | u32 | 0 |

The file is the concatenation of the inflated blocks [C: `pak.py check`, all
1,060 paks]. Readers: `tools/pak.py` `read_chnk`, `engine/src/data/disc.cpp`
`inflateChunks`.

### compress.toc.xen / compress.toc.lst.xen

`compress.toc.lst.xen` is text, one line per file: `Name: <path> <size>
<hex key>`. `compress.toc.xen` starts `TOC1`, then 16-byte entries whose first
u32 is the line's key and whose third is its size [I: not decoded further;
nothing reads it yet].

## Paks with a pab

Three paks have a `.pab.xen` (`qb`, `dbgq`, `zones/global`). As in GH3 the
entry offsets count through pak + pab as one buffer, but the `.pak.xen` is
padded on the disc to 32 KiB: the pab's bytes start at the header's real
size, which is the lowest data offset of its entries (4 KiB for `dbgq`, 8 KiB
for `global`, 32 KiB for `qb`; `compress.toc.lst` gives the same sizes)
[C: the three files; the XEX's own rule still to find, I].

## QB

- **Struct items**: GH3 wrote the item type as `0x00TT0000` with bit
  `0x800000`; Metallica writes `0x0001TT00`, the same shape as an array
  header [C: every .qb on the disc].
- **Type 0x1C**: a key into the localized string tables (`.qs.<lang>`),
  stored inline like a key (`lyrics` texts in the song charts) [C: data;
  name I].
- Result: `qb.py check` decodes all 1,544 .qb files to their last byte
  (38,466 sections, 8,853 scripts) [C].

## Not decoded yet (C++ `--formattest`, 2026-11-10)

`.ska` (8,255), `.img` (2,548 "image version" + 259 short), `.wav` (2,466),
`.skin` (2,166), `.tex` (1,891), `.ske` (251), `.rag` (200), `.hkc` (45),
`.scn` (46), `.table` (46), `.fnt` (15), `.mdl` (14); the FSB used by
`streamall.fsb.xen` (not FSB3). Each needs its gh4 layout.
