# gd-game-10: open questions

The owner may be away: every question here was decided (see `DECISIONS.md`)
and is listed for the owner to confirm or change.

1. Kickoff section 9 questions 1-8: defaults chosen for the proof of
   concept (kickoff "Owner's answers"); owner to confirm.
2. The FFmpeg used by the dev build is gd-game-04's local prefix copied into
   gd-game-10/engine/build (git-ignored); release builds build their own
   through build_common.sh as gd04 does (not run in this spike).
3. Should gd10 get its own "The Glitches" band in the save (gd04 style) or
   keep the original's unlock-all scripts as the stand-in (D10)?
4. gh4's HUD is the desc hud_1g (z_in_game) built by setup_band_hud
   (start_gem_scroller) with widgets attached by the native AttachHudWidget.
   setup_band_hud resolves `{hud_root child = 0}` as hud_container, the
   desc's root element: the desc's elements root is the descinterface
   element's child 0, not the element itself (as D14's reader builds it)
   [C script]. Until that and AttachHudWidget exist, GH3's HUD widgets
   draw with gh4's images. Desc side done (D14, 2026-11-11): the desc's root
   is now the descinterface element's child 0; AttachHudWidget is still to write.
5. Drums: flag 0x2000 is set only on Expert drum notes (1,012 of 2,201 in
   Master of Puppets, on kick and non-kick notes) [C data]; whether it is
   the Expert+ double kick is not confirmed in the XEX yet.
6. Save-data messages (GD10-034): gh4's memcard scripts keep "Loading /
   Saving content" up at least 3 s (memcard_wait_for_timer) and the
   "successful" box 1 s, at the boot and after every song (about 4 s each);
   the port keeps them, as the original [C script guitar_memcard.qb]. They
   are console rules (storage messages shown 3 s); cutting them would make
   the boot and the way back to the set list ~4 s shorter. Owner to decide.
