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

## D3. The version 11 mogg key is a constant in the engine (2026-11-09)
- **What:** every Rock Band `.mogg` (374 on the disc) is version 11: the Ogg
  stream is AES-128 in counter mode (16-byte nonce after the seek map,
  128-bit counter incremented little-endian). The key is 16 bytes in the
  executable's `.data` at 0x828B9AB4, set up by the mogg header reader at
  0x827E02A8 [C]. `engine/src/data/mogg.cpp` holds it as a constant and
  `data/aes128.cpp` is our own AES-128 (forward cipher).
- **Why:** without it no song plays. Reading it from the player's own
  `default.xex` at run time is not simpler: the executable on the disc is
  itself encrypted (XEX2 file format: encryption 1, basic compression), so
  that path would put the Xbox 360's own executable key in the source
  instead. gd-game-02's Harmonix cipher constants are the precedent for a
  format constant in the code.
- **Undo:** replace the constant with a key read at run time (from a file
  the player provides, or the decrypted XEX); `decryptMogg` is the only user.
  OPEN_QUESTIONS.md Q3 asks the owner.

## D4. No Xbox profile sign-in; the port's own save (2026-11-09)
- **What:** `memcardmgr get_new_signin_player_num` and
  `get_autosavable_player_num` answer -1 (nobody signed in), so RB's Xbox
  save / load screens are skipped; `platform_mgr is_player_signed_in` is
  FALSE. Progress goes to the port's versioned JSON save (lesson L12, M7).
- **Why:** there is no Xbox Live profile or memory unit on PC; gd-game-02
  did the same with GH2's memory card flow.
- **Undo:** `engine/src/ui/rb_natives.cpp` (memcardmgr, platform_mgr).
