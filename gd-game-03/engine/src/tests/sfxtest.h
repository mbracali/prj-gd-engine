// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

// `gd03 --sfxtest <disc>`: GH2's sound effects, headlessly. Every
// SynthSample of every milo decodes (XMA, audio/xma_decoder.h) to exactly
// its SampleData sample count with no decoder error, its loop inside the
// sample and its audio neither silent nor beyond full scale; every Sfx map
// and sequence child resolves; config/sound.dta's banks load and every
// sound GH2's scripts name (play_sfx, play_sequence, the HUD's and the
// crowd's) is found in a bank (or the venues' banks) and starts voices.
// GD03_SFX_DUMP=<dir> also writes each decoded sample as a 16-bit WAV.
// Exits 1 on any failure.
int runSfxTest(const std::string& dataPath);
