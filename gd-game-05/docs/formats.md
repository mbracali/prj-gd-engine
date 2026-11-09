# Rock Band (Xbox 360): file formats

Every format on the disc, how it is read, and the tool that decodes it
byte-exact (lesson L21). [C] = confirmed (in the data with the whole-disc
check named, or in `default.xex` at the address given); [I] = inferred.

## Disc and archive [C]
- XDVDFS game partition at 0xFD90000 (`tools/xdvdfs.py`, gd-game-02's,
  unchanged): `default.xex`, `charnames.zbm`, `gen/main.hdr`,
  `gen/main_0.ark`, `gen/main_1.ark`, `videos/*.bik` (14), `$SystemUpdate/`.
- `gen/main.hdr`: ARK version 3 encrypted with Harmonix's Park-Miller
  cipher, exactly GH2's layout (`tools/ark.py`): 4,588 files in 2 parts,
  none compressed (every extract matches its size).
- `.dtb`: GH2's cipher and array layout (`tools/dtb.py dump`): all 291 parse
  exactly to their end.

## Milo (`.milo_xbox`)
- Container: GH2's (`0xCBBEDEAF`, raw deflate blocks), header little-endian.
- **The directory stream and every record are big-endian** (GH2's were
  little-endian): the directory version 25 is stored `00 00 00 19`, the
  0xADDEADDE marker keeps its bytes `AD DE AD DE`. [C data: every file on
  the disc, `tools/milo.py scan`.] Bitmaps (`.png_xbox`, `.bmp_xbox` and the
  Tex records' images) keep GH2's little-endian header.
- `tools/milo.py` and the engine (`data/bytes.h` `milo_order`) read the
  payload in the order the file's first u32 says, so GH2-style files would
  still load.
- Class inventory and versions: see the scan in `PROGRESS.md` (M1).
