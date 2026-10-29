# gd-game-03 progress log

Guitar Hero Encore: Rocks the 80s (PS2) -> `gd03`. Kickoff:
`GD-GAME-03_KICKOFF.md` (repository root). Newest entries at the bottom.
"Verified" lines quote the command and its output; "built only" and "not
tested" are said explicitly.

## Overall estimate

Written at the start (2026-10-28 22:45): the disc is the PS2 build of Guitar
Hero II's engine (volume id `GH2`, directory version 24, GH2's script set), so
the plan is to start from gd-game-02's whole engine (GH2 Xbox 360) and swap the
platform layers for gd-game-01's PS2 ones (ISO 9660, ARK v3, the dtb cipher,
PS2 textures, VGS, SPU ADPCM, PSS video), then walk the older class versions.

| Milestone | Estimate |
|---|---|
| M0 set-up | 1 h |
| M1 data layer (dir v24, ~30 older class versions, PS2 textures) | 4 h |
| M2 songs, VGS audio, gameplay core | 2 h |
| M3 scripts, UI, asset audit | 3 h |
| M4-M6 highway, venues, band (mostly gd02's, re-verified) | 8 h |
| M7-M9 modes, extras, launcher module, packages | 6 h |
| M10 gaps | rest |

Total to a playable, packaged module: about 24 hours of agent time; 100 %
parity (every finding re-confirmed in SLUS_215.86) is longer.

## 2026-10-28 23:00, M0 set-up

Done:
- Disc ripped (`7z x` -> `extracted/iso`), ARK unpacked (1761 files, no
  compressed entry), all 187 .dtb decoded to text with the GH1 cipher.
- REA tried on SLUS_215.86: opens it, no analysis provider for MIPS
  (CONTEXT.md). Ghidra headless import + export: 11480 functions, 54180 data
  refs, 57278 strings. `ExportAnalysisElf.java`, `DumpAsm.java`, `ReadF.java`.
- dtb cipher confirmed at 0x002d95b0 (rand LCG table, indices 0 / 0x67).
- Engine: gd-game-02's at 0d04efe, renamed; Xbox layers replaced (ISO 9660,
  plain ARK header, dtb cipher, VGS streams, ADPCM samples; FFmpeg, stb_vorbis,
  XMA, XDVDFS removed; movies skipped until GD03-008).

Verified:
- `ninja -C build/linux` -> builds, 0 warnings (grep for warning/error empty).
- `build/linux/gd03 "$ISO" --scenetest` runs on the disc: `0 milo files
  parsed, 430 failed` (every one "directory version 24, expected 25"): the M1
  work.
- `python3 .github/scripts/repo_guard.py` -> `0 problem(s)`.

Not yet: the full `./build.sh` (Windows, Android) still carries gd02's FFmpeg
steps; adapted in M9.

Next: M1 (directory v24 and the older class versions). Estimate unchanged.
