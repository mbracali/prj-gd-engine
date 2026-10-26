// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The port's own screens, in GH2's script language, run after the game's
// ui.dta (UiManager::runPortScripts). (port_patches (<object> <handlers>...))
// adds handlers to the game's objects; later handlers win.

namespace ui {

extern const char* const kPortScreens;

const char* const kPortScreens = R"DTA(
{new GamepadPanel gamepads_panel (file "")}
{new
   GHScreen
   gamepads_screen
   (panels gamepads_panel helpbar)
   (focus gamepads_panel)
   (helpbar (default ((fret1 help_select) (fret2 help_back) (strum help_updown))))
}
(port_patches
   (main_screen
      (helpbar (default ((fret1 help_continue) (fret3 port_gamepads) (strum help_updown))))
      (BUTTON_DOWN_MSG
         {if_else
            {|| {== $button kPad_Circle} {== $button kPad_Square}}
            {do {synth play_sequence button_select} {ui goto_screen gamepads_screen} TRUE}
            kDataUnhandled
         }
      )
   )
   (main_not_logged_in_screen
      (enter
         {signin_changed_title.lbl set_localized_text "NO XBOX LIVE"}
         {signin_changed_msg.lbl
            set_localized_text
            "Leaderboards need Xbox Live, which this version of the game does not have. Your best scores are kept in Quick Play's high score tables."
         }
      )
   )
   (options_screen
      (SELECT_START_MSG
         {if_else
            {== $component port_gamepads.btn}
            {do {ui goto_screen gamepads_screen} TRUE}
            kDataUnhandled
         }
      )
   )
)
)DTA";

}  // namespace ui
