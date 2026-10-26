// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

class GameData;

// Part of --venuetest: one song (the first in arena) played by the autoplay
// bot through the world, its script, the camera director and the lighting
// presets with fixed seeds; the (beat, shot) and (beat, preset) log must
// match the golden line count and hash (tests/director_golden.cpp), so a
// change in what the director or the presets pick shows up.
// GD02_GOLDEN_LOG=<file> writes the log. Returns the failures (0 or 1).
int runDirectorGolden(const GameData& data);
