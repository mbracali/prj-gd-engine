// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The port's own screens (Enhanced Options and the Gamepads screen it
// opens), in GH2's script language, run after the game's
// ui.dta (UiManager::runPortScripts). (port_patches (<object> <handlers>...))
// adds handlers to the game's objects; later handlers win. Both screens have
// no transition: universal menus open on the next frame (universal_mechanisms.md).

namespace ui {

extern const char* const kPortScreens;

const char* const kPortScreens = R"DTA(
{new GamepadPanel gamepads_panel (file "")}
{new
   GHScreen
   gamepads_screen
   (animate_transition FALSE)
   (port_universal_menu TRUE)
   (panels gamepads_panel helpbar)
   (focus gamepads_panel)
   (helpbar (default ((fret1 help_select) (fret2 help_back) (strum help_updown))))
}
{new EnhancedPanel enhanced_panel (file "")}
{new
   GHScreen
   enhanced_options_screen
   (animate_transition FALSE)
   (port_universal_menu TRUE)
   (panels enhanced_panel helpbar)
   (focus enhanced_panel)
   (helpbar (default ((fret1 help_select) (fret2 help_back) (strum help_updown))))
}
(port_patches
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
            {== $component port_enhanced.btn}
            {do {ui goto_screen enhanced_options_screen} TRUE}
            kDataUnhandled
         }
      )
   )
)
)DTA";

}  // namespace ui
