// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

// --venuetest <disc>: every venue (or GD06_VENUE=<zone>) with a song's
// cameras and light show (GD06_VENUETEST_SONG, default slowride): the whole
// song's camera cuts, camera and lights simulated at 60 and at 240 fps must
// agree at the same song times; then `frames` frames drawn at each rate
// (GD06_VENUETEST_FRAMES, default 240; GD06_VENUETEST_SHOTS=<dir> saves
// screenshots). Headless: SDL_VIDEO_DRIVER=offscreen.
int runVenueTest(const std::string& discPath);
