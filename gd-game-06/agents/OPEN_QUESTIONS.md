# gd-game-06: open questions for the owner

1. **Song audio (owner item).** Aerosmith's song banks
   (`DATA/MUSIC/<song>.fsb.xen` + `.dat.xen`) are the same encrypted FSB3
   container as GH3's. The port does not decrypt song audio and does not
   reuse gd04's decryption (AEROSMITH_20261117.md rules). gd06 will keep one
   entry point for it and play songs without their audio until then. How do
   you want to supply it?
2. **Shared library or copy?** The recommendation is a copy of gd04's engine
   without its song decryption (DECISIONS D2). Approve, or prefer a shared
   GH3-engine library (touches gd-game-04)?
3. **Milestones and order.** M1..M6 as in PROGRESS.md "Plan"; which ones to
   run, and in one run or one at a time?
4. **Online / Xbox Live.** Out of scope as in GH3 (net_guitar_battle,
   leaderboards, sign-in natives answered as "offline")?
5. **Reference captures.** gd04 had 66 Xenia captures (GD04-027); emulator
   comparisons were dropped on 10-31, so gd06 matches the original from data
   and code only, unless you say otherwise.
6. **The chart's aux part** (`<song>_song_aux_<diff>`, new in Aerosmith):
   what it drives is not known yet (a second guitar part for co-op /
   battle, or the band's rhythm player?); M3 reads it from the scripts and
   XEX. No decision needed now; noted so it is not forgotten.
