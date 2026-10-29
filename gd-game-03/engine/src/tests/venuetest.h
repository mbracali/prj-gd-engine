// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

// `gd03 --venuetest <disc>`: the eight venues, each as one player, face-off
// (_mp) and co-op (_coop), loaded the way world_panel does (game/venue.h),
// headless. Per variant: the chain of milos and the proxies load; every
// object of a class the port decodes is decoded; the other classes are the
// expected ones (the band's characters, the sound banks); every reference
// between objects resolves (or is one of the known names the game fills at
// run time); the venue's script (WORLD_OBJECT_BASE + world/<v>/<v>.dta) runs
// through a scripted mini-song on the world runtime (game/world.h) with no
// unknown names or script errors, its unhandled / stubbed messages listed.
// Then the world milos no venue loads are listed against the expected ones.
// Exits 1 on any regression.
int runVenueTest(const std::string& dataPath);

class Venue;
// The crowd, crowd audio and two-player / encore checks of one venue variant
// (tests/venuetest_crowd.cpp): the failures; `report` gets the summary.
int crowdRun(Venue& v, std::string& report);
// The camera checks of one venue variant (tests/venuetest_camera.cpp): every
// shot framed through its length is a proper camera, not upside down, not
// under the venue, its targets at their screen offsets. The failing shots.
int cameraRun(Venue& v, std::string& report);
