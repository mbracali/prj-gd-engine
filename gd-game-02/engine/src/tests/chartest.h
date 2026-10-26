// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

class GameData;

// `gd02 --chartest <disc>`: GH2's character classes (char/char_objects.h).
// Every .milo_xbox on the disc parses; every object of a character class and
// every Character / BandCharacter / CharClipSet directory record decodes
// exactly to its end, in the counts on the disc (docs/band.md); every clip
// quaternion is a unit one; each CharClipSet's per-clip entries agree with
// its clips' flags; golden poses (the reference parser's values at a few
// (clip, frame)) match to 1e-4. Then the band checks (chartestBandChecks).
// Exits 1 on any failure.
int runCharTest(const std::string& dataPath);

// The band's runtime checks (tests/chartest_band.cpp): the failures.
int chartestBandChecks(const GameData& data);
// The crowd's runtime checks (tests/chartest_crowd.cpp): the failures.
int chartestCrowdChecks(const GameData& data);
