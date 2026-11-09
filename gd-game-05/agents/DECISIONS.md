# gd-game-05 decisions

Choices the owner would normally make, taken while the owner was away
(kickoff section 2: the original's behaviour first, else gd-game-02's
pattern). Each says what, why and how to undo it.

## D1. Start from gd-game-02's engine at the branch's base (2026-11-09)
- **What:** every tracked file of `gd-game-02/engine`, `tools`,
  `build_commands` and `local_build.sh` copied into `gd-game-05/` and renamed
  (gd02 -> gd05, GD02_ -> GD05_, gd-game-02 -> gd-game-05). gd02 backlog IDs
  in comments (`GD02-nnn`) are kept: they point at gd02's history.
- **Why:** the kickoff's plan (the gd-game-03 path); the first look showed
  GH2's ARK v3 and cipher read as is.
- **Addresses:** every `0x82xxxxxx` address in a comment inherited from gd02
  is a **GH2** address until re-confirmed. Rock Band confirmations are
  written `RB 0x82xxxxxx` in code and `[C] 0x82xxxxxx` in
  `gd-game-05/docs/` (whose addresses are always RB's).
- **Undo:** none needed; this is the base.

## D2. FFmpeg decoders: Bink in, WMV out (2026-11-09)
- **What:** `engine/build_common.sh` builds FFmpeg 7.1.5 with the `bink`
  demuxer and the `bink`, `binkaudio_rdft`, `binkaudio_dct`, `xma1`, `xma2`
  decoders; GH2's ASF / WMV3 / WMA2 are dropped (no WMV on RB's disc).
- **Why:** RB's movies are `videos/*.bik`; the kickoff asks for the Bink
  decoders in our own FFmpeg build (LGPL, pinned).
- **Undo:** the `FFMPEG_CONFIGURE` line.
