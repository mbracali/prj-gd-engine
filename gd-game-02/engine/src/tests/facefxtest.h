// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

// `gd02 --facefxtest <disc>`: GH2's FaceFX lip-sync data, headlessly. Every
// .voc (FxAnim) and .fac (FxFaceGraph) in the archive parses to its exact
// end with the expected counts; the face graph evaluated at sample song
// times matches the reference parser's values (facefx.py graph_eval, numbers
// kept in facefxtest_golden.inc); the lip servo runtime (registers, eye
// entries, expression cross-fade, player stop, the gh2 expression script)
// and the path / dta helpers behave as GH2's code does (char/facefx.h).
// Exits 1 on any failure.
int runFaceFxTest(const std::string& dataPath);
