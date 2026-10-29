# gd-game-04: open questions for the owner

1. **Reference captures.** Many GH3 behaviours (visuals, timing) would be
   confirmed fastest against captures of the original (Xenia or real
   hardware). Do you have any, or should we plan for them?
2. **Pads in GH3.** GH3 (360) plays with the guitar; how pads play (every
   fret a strum, as gd-game-02's joypad mode?) is not yet confirmed in the
   XEX. Until then gd-game-02's input layer is used as it is.
3. **C++20.** The kickoff asks for C++17; the code is C++20 like gd-game-02
   (DECISIONS.md D2). Fine?
4. ~~**Main-menu texts left behind.**~~ Answered (2026-10-29): our LZSS
   filled its ring with zeros, the XEX's decoders (0x823071B0, 0x823072D0)
   with spaces, so a back-reference into the unwritten ring gave 0x00 for
   0x20: the id 0x20d40671 came out 0x00d40671 (and 1079 of 16552 scripts
   differed, mostly `begin` read as `end`). The element lookup compares all
   32 bits (0x820EC088) [C Ghidra].
5. ~~**Stars.**~~ Answered (2026-10-29): GH3's script Progression_SongWon
   gives 3, 4 at score >= 2 x base_score, 5 at >= 2.8 x (a boss song 5);
   base_score comes from CalcSongScoreInfo 0x8210AF70 (50 a fret plus the
   sustains' 25 a beat) [C Ghidra + script]. The port leaves stars to the
   script and fills base_score that way.
6. ~~**The Glitches and the store.**~~ Answered by the owner (2026-10-29):
   Vault purchases scoped to The Glitches' band only (D9 revised, GD04-026).
