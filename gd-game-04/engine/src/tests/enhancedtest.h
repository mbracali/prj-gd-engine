// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

// --enhancedtest <disc> (GD04-118): one venue (z_artdeco, or GD04_VENUE) at
// one song time (GD04_ENHANCEDTEST_AT, 12000 ms: a wide shot of the band on the stage), drawn with the Enhanced
// options at their defaults, then with each picture option at each other
// value applied as the game applies them (applyEnhancedGlobals): every one
// must change the frame by more than drawing the defaults twice does (the
// resolution rows: the scene's size). Disable Shaky Camera: the camera's
// motion with it off and on. Headless: SDL_VIDEO_DRIVER=offscreen.
int runEnhancedTest(const std::string& discPath);
