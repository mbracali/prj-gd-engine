// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

// --scripttest <disc>: tokenizes every script on the disc, then runs the
// boot script (guitar_startup) and what it spawns for a few seconds of
// game time against the natives there are, and reports the calls nothing
// handles yet.
int runScriptTest(const std::string& discPath);
