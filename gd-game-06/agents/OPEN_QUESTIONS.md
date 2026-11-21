# gd-game-06: open questions for the owner

1. **Song audio.** Aerosmith's song banks (`DATA/MUSIC/<song>.fsb.xen` +
   `.dat.xen`) use GH3's container, so gd06 ports gd04's audio handling as-is
   (as gd-game-10 does), reading the player's own disc at runtime. M3 confirms
   the key path on Aerosmith's banks. No decision needed unless you want it done
   differently. CONFIRMED (M3): gd04's FSB reader decodes every Aerosmith bank
   (key "5atu6w4zaw", bit-reverse per offset); the quickplay song plays to
   non-silent PCM (dreampolice: peak 0.74, RMS 0.063) and lines up with the
   chart. XMA needs a GD06_VIDEO=ON build (FFmpeg); see DECISIONS D8.
2. **Shared library or copy?** The recommendation is a copy of gd04's engine
   (DECISIONS D2). Approve, or prefer a shared GH3-engine library (touches
   gd-game-04)?
3. **Milestones and order.** M1..M6 as in PROGRESS.md "Plan"; which ones to
   run, and in one run or one at a time?
4. **Online / Xbox Live.** Out of scope as in GH3 (net_guitar_battle,
   leaderboards, sign-in natives answered as "offline")?
5. **Reference captures.** gd04 had 66 Xenia captures (GD04-027); emulator
   comparisons were dropped on 10-31, so gd06 matches the original from data
   and code only, unless you say otherwise.
6. **The chart's aux part** (`<song>_song_aux_<diff>`, new in Aerosmith):
   RESOLVED (M3, DECISIONS D6): it is NOT a playable part. No player is ever
   assigned it (menu_choose_part.qb offers only LEAD/RHYTHM or GUITAR/BASS,
   quickplay sets part=guitar, no script sets part=aux, no "_aux" in the XEX),
   so it is an auxiliary non-interactive track (likely an on-stage band-member
   animation / authoring reference). Not wired to a Part; revisit as band data
   in M4 if useful.
