// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

namespace gd24 {
class Umd;
}

// gd24 --charttest <disc> [<song> | all] [--notes]: reads each song's
// BEATMATCH.STR (data/beatmatch.h): the four difficulty images and their
// difficulty words, the image base (kPad_* symbols), the four tracks (PART
// DRUMS / BASS / GUITAR / VOCALS) with their Easy / Medium / Hard / Expert
// gem counts, and checks: one tick has one time in every list (the shared
// tempo map), the Easy image gives the same gems. With
// one song: each list's range; --notes prints every gem. Exit code 1 on a
// failed check.
int chartTest(const gd24::Umd& umd, const std::string& which, bool notes);
