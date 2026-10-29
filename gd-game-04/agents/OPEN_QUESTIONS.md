# gd-game-04: open questions for the owner

1. **Reference captures.** Many GH3 behaviours (visuals, timing) would be
   confirmed fastest against captures of the original (Xenia or real
   hardware). Do you have any, or should we plan for them?
2. **Pads in GH3.** GH3 (360) plays with the guitar; how pads play (every
   fret a strum, as gd-game-02's joypad mode?) is not yet confirmed in the
   XEX. Until then gd-game-02's input layer is used as it is.
3. **C++20.** The kickoff asks for C++17; the code is C++20 like gd-game-02
   (DECISIONS.md D2). Fine?
4. **Main-menu texts left behind.** create_main_menu_backdrop makes its text
   container with id 0x00d40671, but destroy_main_menu destroys
   main_menu_text_container (0x20d40671): one bit apart, so the texts stay on
   the next screens in the port. Either the original masks element ids, or
   something else removes them; needs the XEX's id lookup (or a capture).
